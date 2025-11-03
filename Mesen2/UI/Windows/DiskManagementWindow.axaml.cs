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

        public DiskManagementWindow()
        {
            InitializeComponent();
            if(!Design.IsDesignMode) {
                _model = new DiskManagementViewModel();
                DataContext = _model;
            }
            // 支持拖放文件到窗口以写入镜像
            AddHandler(DragDrop.DropEvent, OnDrop);
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
    }
}
