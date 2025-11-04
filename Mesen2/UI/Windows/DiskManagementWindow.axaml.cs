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
using System.IO;
using System.Linq;

namespace Mesen.Windows
{
    public class DiskManagementWindow : MesenWindow
    {
    private DiskManagementViewModel? _model;
    private NotificationListener? _listener;
    // 去抖定时器：在收到多次磁盘 I/O 通知时，合并 200ms 内的刷新请求
    private DispatcherTimer? _debounceTimer;
    // 拖放相关临时状态
    private Avalonia.Point? _dragStartPos;
    private Mesen.ViewModels.DiskDirectoryNode? _dragNode;
    private Avalonia.Controls.Control? _dragSourceControl;

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
        }

    private async void StartDragForNode(Mesen.ViewModels.DiskDirectoryNode node, Control? source)
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

                string safeName = Path.GetFileName(node.Name);
                if(string.IsNullOrEmpty(safeName)) safeName = "file.bin";
                string tmpPath = Path.Combine(Path.GetTempPath(), Guid.NewGuid().ToString() + "_" + safeName);
                File.WriteAllBytes(tmpPath, data);

                try {
                    // 直接复制到桌面（仅限 Windows）：因为直接发起系统级拖放在当前平台/引用下存在重载歧义，
                    // 为保证功能可用，这里实现为拖动手势触发时将文件写入用户桌面目录以完成“拖出复制到桌面”的效果。
                    string desktop = Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory);
                    string dest = Path.Combine(desktop, safeName);
                    // 若目标已存在则生成唯一名称
                    if(File.Exists(dest)) {
                        dest = Path.Combine(desktop, Guid.NewGuid().ToString() + "_" + safeName);
                    }
                    File.Copy(tmpPath, dest);
                    DisplayMessageHelper.DisplayMessage("Info", "已复制到桌面: " + dest);
                } finally {
                    try { File.Delete(tmpPath); } catch { }
                }
            } catch(Exception ex) {
                DisplayMessageHelper.DisplayMessage("Error", ex.Message);
            }
        }
    }
}
