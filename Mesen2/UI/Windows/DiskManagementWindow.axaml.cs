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
                        // 在 UI 线程上重启去抖定时器：200ms 内合并多次通知为一次刷新
                        Dispatcher.UIThread.Post(() => {
                            try {
                                if(_debounceTimer == null) {
                                    _debounceTimer = new DispatcherTimer(TimeSpan.FromMilliseconds(200), DispatcherPriority.Normal, (s, args) => {
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
        /// 处理键盘按下事件，按 Delete 键删除选中的文件（不弹确认框）。
        /// </summary>
        private void OnKeyDown(object? sender, KeyEventArgs e)
        {
            try {
                if(e.Key == Key.Delete) {
                    var node = _model?.SelectedNode;
                    if(node != null && !node.IsDirectory) {
                        bool ok = EmuApi.FloppyDeleteFile(node.Name);
                        if(ok) {
                            // 删除成功后刷新视图
                            Dispatcher.UIThread.Post(() => {
                                try { _model?.Refresh(); } catch { }
                            });
                        } else {
                            DisplayMessageHelper.DisplayMessage("Error", "删除失败: " + node.Name);
                        }
                    }
                }
            } catch { }
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
                    bool started = Win32DragDrop.DoDragDropFiles(new[] { tmpPath });
                    if(!started) {
                        DisplayMessageHelper.DisplayMessage("Info", "拖放未成功启动或被取消。");
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
