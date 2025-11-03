using Avalonia;
using Avalonia.Controls;
using Avalonia.Markup.Xaml;
using Avalonia.Threading;
using Mesen.ViewModels;
using Mesen.Interop;
using System;

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
    }
}
