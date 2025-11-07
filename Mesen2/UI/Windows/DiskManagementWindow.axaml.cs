using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Markup.Xaml;
using Avalonia.Threading;
using Mesen.ViewModels;
using Mesen.Interop;
using Mesen.Utilities;
using Mesen.Localization;
using System;
using System.ComponentModel;
using System.IO;
using System.Linq;
using System.Threading.Tasks;

namespace Mesen.Windows
{
    public class DiskManagementWindow : MesenWindow, INotifyPropertyChanged
    {
        // 明确使用 new 隐藏基类的 PropertyChanged 事件，表示这是 INotifyPropertyChanged 的实现
        public new event PropertyChangedEventHandler? PropertyChanged;

        private void OnPropertyChanged(string name)
        {
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
        }
    private DiskManagementViewModel? _model;
    private NotificationListener? _listener;
    // 去抖定时器：在收到多次磁盘 I/O 通知时，合并 200ms 内的刷新请求
    private DispatcherTimer? _debounceTimer;
    // 拖放相关临时状态
    private Avalonia.Point? _dragStartPos;
    private Mesen.ViewModels.DiskDirectoryNode? _dragNode;
    private Avalonia.Controls.Control? _dragSourceControl;
    // 标记当前是否在执行由窗口内部发起的系统拖放（临时文件在 %TEMP%\\Mesen_Drag）
    // 用于在 Drop 事件中区分外部文件与内部拖放，避免内部拖放导致把临时文件写回镜像（自覆盖）。
    private bool _internalDragInProgress = false;

        // 窗口级代理属性：将列宽的读写代理到 ViewModel，供 XAML 绑定使用
        public double ColumnNameWidth {
            get => _model?.ColumnNameWidth ?? 240.0;
            set {
                if(_model != null) _model.ColumnNameWidth = value;
                OnPropertyChanged(nameof(ColumnNameWidth));
                OnPropertyChanged(nameof(ColumnTotalWidth));
            }
        }

        public double ColumnModifiedWidth {
            get => _model?.ColumnModifiedWidth ?? 140.0;
            set {
                if(_model != null) _model.ColumnModifiedWidth = value;
                OnPropertyChanged(nameof(ColumnModifiedWidth));
                OnPropertyChanged(nameof(ColumnTotalWidth));
            }
        }

        public double ColumnSizeWidth {
            get => _model?.ColumnSizeWidth ?? 100.0;
            set {
                if(_model != null) _model.ColumnSizeWidth = value;
                OnPropertyChanged(nameof(ColumnSizeWidth));
                OnPropertyChanged(nameof(ColumnTotalWidth));
            }
        }

        public double ColumnTotalWidth {
            get {
                if(_model != null) return _model.ColumnTotalWidth;
                return 240.0 + 140.0 + 100.0 + 12.0;
            }
        }

        public DiskManagementWindow()
        {
            InitializeComponent();
            if(!Design.IsDesignMode) {
                _model = new DiskManagementViewModel();
                DataContext = _model;
            }
            // 订阅列表区域的点击事件：点击空白处可以取消选中
            try {
                var listBorder = this.FindControl<Border>("ListBorder");
                if(listBorder != null) {
                    listBorder.PointerPressed += ListBorder_PointerPressed;
                }
            } catch { }
            // 支持拖放文件到窗口以写入镜像
            AddHandler(DragDrop.DropEvent, OnDrop);
            // 支持从列表拖出文件（仅在 Windows 平台启用）
            AddHandler(InputElement.PointerPressedEvent, OnPointerPressed, Avalonia.Interactivity.RoutingStrategies.Tunnel);
            AddHandler(InputElement.PointerMovedEvent, OnPointerMoved, Avalonia.Interactivity.RoutingStrategies.Tunnel);
            AddHandler(InputElement.PointerReleasedEvent, OnPointerReleased, Avalonia.Interactivity.RoutingStrategies.Tunnel);
            // 支持通过键盘 Delete 键删除所选文件（不弹确认框）
            AddHandler(InputElement.KeyDownEvent, OnKeyDown, Avalonia.Interactivity.RoutingStrategies.Tunnel);
        }

        private void InitializeComponent()
        {
            AvaloniaXamlLoader.Load(this);
        }

        protected override void OnOpened(EventArgs e)
        {
            base.OnOpened(e);

            if(Design.IsDesignMode) {
                return;
            }

            _listener = new NotificationListener();
            _listener.OnNotification += OnNotification;
        }

        protected override void OnClosing(WindowClosingEventArgs e)
        {
            base.OnClosing(e);

            if(Design.IsDesignMode) {
                return;
            }

            // 停止并释放去抖定时器
            try {
                _debounceTimer?.Stop();
            } catch { }
            _debounceTimer = null;

            _listener?.Dispose();
            _listener = null;
        }

        private void OnNotification(NotificationEventArgs e)
        {
            switch(e.NotificationType) {
                case ConsoleNotificationType.FloppyIoStarted:
                case ConsoleNotificationType.FloppyIoStopped:
                case ConsoleNotificationType.FloppyLoaded:
                case ConsoleNotificationType.FloppyEjected:
                        // 在 UI 线程上重启去抖定时器：1000ms 内合并多次通知为一次刷新
                        Dispatcher.UIThread.Post(() => {
                            try {
                                if(_debounceTimer == null) {
                                    _debounceTimer = new DispatcherTimer(TimeSpan.FromMilliseconds(1000), DispatcherPriority.Normal, (s, args) => {
                                        try {
                                            _debounceTimer?.Stop();
                                            _model?.Refresh();
                                        } catch {
                                            // 忽略刷新错误
                                        }
                                    });
                                }

                                // 重启定时器以延后刷新
                                _debounceTimer.Stop();
                                _debounceTimer.Start();
                            } catch {
                                // 忽略定时器创建/启动异常
                            }
                        });
                    break;
            }
        }

        private void OnDrop(object? sender, DragEventArgs e)
        {
            var files = e.Data.GetFiles();
            if(files == null || !files.Any()) return;
            try {
                var paths = files.Select(f => f.Path.LocalPath).Where(p => !string.IsNullOrEmpty(p)).ToArray();
                if(paths.Length == 0) return;

                // 若当前正在进行由窗口内部发起的系统拖放（我们在 StartDragForNode 中创建的临时文件），
                // 并且所有被放下的文件都位于我们的临时拖放目录下，则视为内部拖放并忽略，
                // 以避免把临时拖出文件再次写回镜像（即“自己覆盖自己”）。
                try {
                    string tempRoot = Path.Combine(Path.GetTempPath(), "Mesen_Drag");
                    bool allInternal = _internalDragInProgress && paths.All(p => {
                        try {
                            var fp = Path.GetFullPath(p);
                            return fp.StartsWith(tempRoot, StringComparison.OrdinalIgnoreCase);
                        } catch { return false; }
                    });
                    if(allInternal) {
                        // 忽略内部拖放造成的 Drop
                        return;
                    }
                } catch { }

                foreach(var path in paths) {
                    if(!File.Exists(path)) {
                        DisplayMessageHelper.DisplayMessage("Error", ResourceHelper.GetMessage("FileNotFound", path));
                        continue;
                    }

                    string fileName = Path.GetFileName(path);
                    byte[] buffer = File.ReadAllBytes(path);

                    bool ok = EmuApi.FloppyWriteFile(fileName, buffer);
                    if(!ok) {
                        DisplayMessageHelper.DisplayMessage("Error", "写入镜像失败: " + fileName);
                    } else {
                        // 刷新视图
                        Dispatcher.UIThread.Post(() => {
                            try { _model?.Refresh(); } catch { }
                        });
                    }
                }
            } catch(Exception ex) {
                DisplayMessageHelper.DisplayMessage("Error", ex.Message);
            }
    }

        private void OnPointerPressed(object? sender, PointerPressedEventArgs e)
        {
            try {
                // 记录可能的拖动起点（不区分鼠标键），实际开始拖动由移动距离触发
                if(e.Source is Control ctrl && ctrl.DataContext is Mesen.ViewModels.DiskDirectoryNode node && !node.IsDirectory) {
                    _dragStartPos = e.GetPosition(this);
                    _dragNode = node;
                    _dragSourceControl = ctrl;
                    // 直接同步选中项，避免拖动前还保留旧文件导致 UI 状态与拖动的节点不一致
                    if(_model != null) {
                        _model.SelectedNode = node;
                    }
                }
            } catch { }
        }

        private void OnPointerMoved(object? sender, PointerEventArgs e)
        {
            try {
                if(_dragNode == null || !_dragStartPos.HasValue) return;

                var pos = e.GetPosition(this);
                double dx = Math.Abs(pos.X - _dragStartPos.Value.X);
                double dy = Math.Abs(pos.Y - _dragStartPos.Value.Y);
                if(dx >= 4 || dy >= 4) {
                    // 达到拖动阈值，开始拖放（仅限 Windows）
                    var node = _dragNode;
                    var src = _dragSourceControl;
                    // 释放可能存在的指针捕获，避免拖放后指针事件仍然锁定在旧的按钮上
                    try {
                        if(e.Pointer.Captured != null) {
                            e.Pointer.Capture(null);
                        }
                    } catch { }
                    _dragStartPos = null;
                    _dragNode = null;
                    _dragSourceControl = null;
                    StartDragForNode(node, src);
                }
            } catch { }
        }

        private void OnPointerReleased(object? sender, PointerReleasedEventArgs e)
        {
            // 取消拖动候选状态
            _dragStartPos = null;
            _dragNode = null;
            _dragSourceControl = null;
            // 确保释放指针捕获，避免释放失败导致后续 Hover/Press 事件异常
            try { e.Pointer.Capture(null); } catch { }
        }

        /// <summary>
        /// 列表区域被点击时触发：如果点击目标不是列表项（DiskDirectoryNode），则取消当前选中项。
        /// 仅响应左键点击。
        /// </summary>
        private void ListBorder_PointerPressed(object? sender, PointerPressedEventArgs e)
        {
            try {
                // 仅处理左键点击
                var pt = e.GetCurrentPoint(this);
                if(!pt.Properties.IsLeftButtonPressed) return;

                // 如果点击在某个列项上（数据上下文为 DiskDirectoryNode），则不清除选中
                if(e.Source is Control ctrl && ctrl.DataContext is Mesen.ViewModels.DiskDirectoryNode) {
                    return;
                }

                // 否则取消选中
                if(_model != null) _model.SelectedNode = null;
            } catch { }
        }

        /// <summary>
        /// 处理键盘按下事件，按 Delete 键删除选中的文件（不弹确认框）。
        /// </summary>
        private void OnKeyDown(object? sender, KeyEventArgs e)
        {
            try {
                if(e.Key == Key.Delete) {
                    var node = _model?.SelectedNode;
                    if(node != null && !node.IsDirectory) {
                        DeleteNodeAndRefresh(node);
                    }
                }
            } catch { }
        }

        private async void OnMenuSaveAsClick(object? sender, RoutedEventArgs e)
        {
            try {
                var node = GetNodeFromSender(sender);
                if(node == null || node.IsDirectory) {
                    return;
                }
                if(_model != null) {
                    _model.SelectedNode = node;
                }
                await SaveFileAsync(node);
            } catch { }
            if(e != null) {
                e.Handled = true;
            }
        }

        private async void OnMenuRenameClick(object? sender, RoutedEventArgs e)
        {
            try {
                var node = GetNodeFromSender(sender);
                if(node == null || node.IsDirectory) {
                    return;
                }
                if(_model != null) {
                    _model.SelectedNode = node;
                }
                await RenameFileAsync(node);
            } catch { }
            if(e != null) {
                e.Handled = true;
            }
        }

        private void OnMenuDeleteClick(object? sender, RoutedEventArgs e)
        {
            try {
                var node = GetNodeFromSender(sender);
                if(node == null || node.IsDirectory) {
                    return;
                }
                if(_model != null) {
                    _model.SelectedNode = node;
                }
                DeleteNodeAndRefresh(node);
            } catch { }
            if(e != null) {
                e.Handled = true;
            }
        }

        /// <summary>
        /// 根据菜单事件源解析当前节点。
        /// </summary>
        private static Mesen.ViewModels.DiskDirectoryNode? GetNodeFromSender(object? sender)
        {
            return (sender as MenuItem)?.DataContext as Mesen.ViewModels.DiskDirectoryNode;
        }

        /// <summary>
        /// 将软盘中的文件另存为主机端文件。
        /// </summary>
        private async Task SaveFileAsync(Mesen.ViewModels.DiskDirectoryNode node)
        {
            byte[]? data = EmuApi.FloppyReadFile(node.Name);
            if(data == null) {
                DisplayMessageHelper.DisplayMessage("Error", "读取文件失败: " + node.Name);
                return;
            }

            string initialName = Path.GetFileName(node.Name);
            if(string.IsNullOrWhiteSpace(initialName)) {
                initialName = node.Name;
            }

            string extension = "bin";
            string ext = Path.GetExtension(initialName);
            if(!string.IsNullOrEmpty(ext)) {
                string trimmed = ext.TrimStart('.');
                if(!string.IsNullOrEmpty(trimmed)) {
                    extension = trimmed;
                }
            }

            string? targetPath = await FileDialogHelper.SaveFile(null, initialName, this, extension);
            if(string.IsNullOrEmpty(targetPath)) {
                return;
            }

            try {
                File.WriteAllBytes(targetPath, data);
            } catch(Exception ex) {
                DisplayMessageHelper.DisplayMessage("Error", "写入文件失败: " + ex.Message);
            }
        }

        /// <summary>
        /// 在软盘镜像中对文件执行重命名操作。
        /// </summary>
        private async Task RenameFileAsync(Mesen.ViewModels.DiskDirectoryNode node)
        {
            string? newName = await RenameDiskFileWindow.ShowDialog(this, node.Name);
            if(string.IsNullOrWhiteSpace(newName)) {
                return;
            }
            newName = newName.Trim();
            if(string.Equals(newName, node.Name, StringComparison.OrdinalIgnoreCase)) {
                return;
            }
            if(newName.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0 || newName.Contains('/') || newName.Contains('\\')) {
                DisplayMessageHelper.DisplayMessage("Error", "文件名无效: " + newName);
                return;
            }

            string oldShort = GetShortNameCandidate(node.Name);
            bool oldIsShort = string.Equals(node.Name, oldShort, StringComparison.OrdinalIgnoreCase);
            string newShort = GetShortNameCandidate(newName);
            if(oldIsShort && string.Equals(oldShort, newShort, StringComparison.OrdinalIgnoreCase)) {
                DisplayMessageHelper.DisplayMessage("Error", "重命名失败: 新旧文件名在 8.3 格式下相同。");
                return;
            }

            byte[]? data = EmuApi.FloppyReadFile(node.Name);
            if(data == null) {
                DisplayMessageHelper.DisplayMessage("Error", "读取文件失败: " + node.Name);
                return;
            }

            if(!EmuApi.FloppyDeleteFile(node.Name)) {
                DisplayMessageHelper.DisplayMessage("Error", "无法删除旧文件: " + node.Name);
                return;
            }

            if(!EmuApi.FloppyWriteFile(newName, data)) {
                bool restored = EmuApi.FloppyWriteFile(node.Name, data);
                if(restored) {
                    DisplayMessageHelper.DisplayMessage("Error", "重命名失败，新文件写入失败，已恢复原文件。");
                } else {
                    DisplayMessageHelper.DisplayMessage("Error", "重命名失败且原文件无法恢复，文件可能已丢失。");
                }
                Dispatcher.UIThread.Post(() => {
                    try { _model?.Refresh(); } catch { }
                });
                return;
            }

            Dispatcher.UIThread.Post(() => {
                try { _model?.Refresh(); } catch { }
            });
        }

        /// <summary>
        /// 删除软盘镜像中的文件并刷新列表。
        /// </summary>
        private void DeleteNodeAndRefresh(Mesen.ViewModels.DiskDirectoryNode node)
        {
            bool ok = EmuApi.FloppyDeleteFile(node.Name);
            if(ok) {
                Dispatcher.UIThread.Post(() => {
                    try { _model?.Refresh(); } catch { }
                });
            } else {
                DisplayMessageHelper.DisplayMessage("Error", "删除失败: " + node.Name);
            }
        }

        /// <summary>
        /// 生成与原生实现一致的 8.3 短文件名表示，用于判定重命名冲突。
        /// </summary>
        private static string GetShortNameCandidate(string name)
        {
            if(string.IsNullOrEmpty(name)) {
                return string.Empty;
            }

            string justName = name;
            int slash = justName.LastIndexOfAny(new[] { '/', '\\' });
            if(slash >= 0 && slash < justName.Length - 1) {
                justName = justName[(slash + 1)..];
            }

            justName = justName.ToUpperInvariant();

            string basePart = justName;
            string extPart = string.Empty;
            int dot = justName.LastIndexOf('.');
            if(dot > 0 && dot < justName.Length - 1) {
                basePart = justName.Substring(0, dot);
                extPart = justName.Substring(dot + 1);
            }

            static string Sanitize(string input, int maxLen)
            {
                if(maxLen <= 0) {
                    return string.Empty;
                }
                char[] buf = new char[Math.Min(maxLen, input.Length)];
                int c = 0;
                foreach(char ch in input) {
                    if(c >= maxLen) {
                        break;
                    }
                    char v = ch;
                    if(!((v >= 'A' && v <= 'Z') || (v >= '0' && v <= '9'))) {
                        v = '_';
                    }
                    buf[c++] = v;
                }
                return c > 0 ? new string(buf, 0, c) : string.Empty;
            }

            string shortBase = Sanitize(basePart, 8);
            string shortExt = Sanitize(extPart, 3);

            if(string.IsNullOrEmpty(shortExt)) {
                return shortBase;
            }
            if(string.IsNullOrEmpty(shortBase)) {
                return shortExt;
            }
            return shortBase + "." + shortExt;
        }

    private void StartDragForNode(Mesen.ViewModels.DiskDirectoryNode node, Control? source)
        {
            try {
                // 仅支持 Windows 平台的外部拖放
                if(!System.Runtime.InteropServices.RuntimeInformation.IsOSPlatform(System.Runtime.InteropServices.OSPlatform.Windows)) {
                    return;
                }

                byte[]? data = EmuApi.FloppyReadFile(node.Name);
                if(data == null) {
                    DisplayMessageHelper.DisplayMessage("Error", "无法从镜像读取文件：" + node.Name);
                    return;
                }

                // 使用原始文件名写入临时目录，避免临时文件名中包含 GUID 前缀导致目标处显示为随机名。
                string safeName = Path.GetFileName(node.Name);
                if(string.IsNullOrEmpty(safeName)) safeName = "file.bin";
                string tmpDir = Path.Combine(Path.GetTempPath(), "Mesen_Drag", Guid.NewGuid().ToString());
                try {
                    Directory.CreateDirectory(tmpDir);
                    string tmpPath = Path.Combine(tmpDir, safeName);
                    File.WriteAllBytes(tmpPath, data);

                    // 使用 Windows OLE 启动系统级拖放，使得外部目标（如资源管理器）能够接收文件并在鼠标释放时完成复制。
                    // DoDragDropFiles 将在拖放结束（放下或取消）后返回，因此可在返回后删除整个临时目录，保证临时文件在拖放期间可用并且目标文件名为原始名。
                    // 标记这是由窗口内部发起的系统拖放，OnDrop 中将根据该标记和临时目录判断是否忽略以避免“自覆盖”。
                    _internalDragInProgress = true;
                    try {
                        bool started = Win32DragDrop.DoDragDropFiles(new[] { tmpPath });
                        if(!started) {
                            DisplayMessageHelper.DisplayMessage("Info", "拖放未成功启动或被取消。");
                        }
                    } finally {
                        _internalDragInProgress = false;
                    }
                } finally {
                    // 尝试删除临时目录（可能因目标移动文件或锁定而失败），忽略异常
                    try { Directory.Delete(tmpDir, true); } catch { }
                }
            } catch(Exception ex) {
                DisplayMessageHelper.DisplayMessage("Error", ex.Message);
            }
        }
    }
}
