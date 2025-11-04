using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Markup.Xaml;
using Avalonia.Threading;
using Mesen.Utilities;
using Mesen.Config;
using Mesen.ViewModels;
using System.Collections;
using System.IO;
using Mesen.Interop;
using Avalonia.Input;
using System.Linq;
using System.Collections.Generic;
using Avalonia.Media;
using System;

namespace Mesen.Views
{
	public class MainMenuView : UserControl
	{
		public Menu MainMenu { get; }
		private IDisposable? _hoverSub;
		// 订阅核心通知以更新软驱指示灯
		private NotificationListener? _floppyNotificationListener;
		// 软驱灯延时关闭定时器（在收到停止通知后延迟 100ms 关闭）
		private DispatcherTimer? _floppyOffTimer;
		// 当前软驱是否处于活动状态（读/写）
		private bool _isFloppyActive = false;

		public MainMenuView()
		{
			InitializeComponent();
			MainMenu = this.GetControl<Menu>("ActionMenu");

			MainMenu.Closed += (s, e) => {
				//When an option is selected in the menu (e.g with enter or mouse click)
				//steal focus away from the menu to ensure pressing e.g left/right goes to the
				//game only and doesn't re-activate the main menu
				ApplicationHelper.GetMainWindow()?.Focus();
			};

			Panel panel = this.GetControl<Panel>("MenuPanel");
			panel.PointerPressed += (s, e) => {
				if(s == panel) {
					//Close the menu when the blank space on the right is clicked
					MainMenu.Close();
					ApplicationHelper.GetMainWindow()?.Focus();
				}
			};

			var floppyPanel = this.GetControl<Border>("FloppyPanel");
			var floppyInner = this.GetControl<StackPanel>("FloppyInner");
			var floppyContent = this.GetControl<Border>("FloppyContent");

			// 将 FloppyPanel 的 ContextMenu 注册到主窗口的 MouseManager，
			// 以便在原生渲染区点击时也能关闭该右键菜单（兼容 NativeRenderer 的原生 HWND 场景）。
			try {
				var cm = floppyPanel.ContextMenu;
				if(cm != null) {
					var wnd = ApplicationHelper.GetMainWindow() as Mesen.Windows.MainWindow;
					wnd?.RegisterRendererContextMenu(cm);
				}
			} catch { }

			// Ensure hover visual even if XAML style is overridden by theme: subscribe to IsPointerOver
			// 悬停颜色动态计算：亮色主题使用浅蓝（#FFCCE8FF），暗色主题使用浅灰半透明（#66CCCCCC）
			var normalBrush = Brushes.Transparent;
			_hoverSub = Avalonia.AvaloniaObjectExtensions.GetObservable<bool>((Avalonia.AvaloniaObject)floppyPanel, Control.IsPointerOverProperty)
				.Subscribe(isOver => {
					// 在 UI 线程上设置背景
					Dispatcher.UIThread.Post(() => {
						if(isOver) {
							var hb = (ConfigManager.ActiveTheme == MesenTheme.Dark)
								? new SolidColorBrush(Color.Parse("#33CCCCCC")) // 暗色主题：浅灰半透明（降低不透明度，避免看起来发白）
								: new SolidColorBrush(Color.Parse("#FFCCE8FF")); // 亮色主题：浅蓝
							floppyContent.Background = hb;
						} else {
							floppyContent.Background = normalBrush;
						}
					});
				});

			// 监听主题变化，若当前处于悬停状态则立即更新悬停色（解决运行时切换主题后颜色不变的问题）
			try {
				ColorHelper.InvalidateControlOnThemeChange(floppyPanel, () => {
					Dispatcher.UIThread.Post(() => {
						// 若当前仍悬停则重新计算并应用悬停色
						if(floppyPanel.IsPointerOver) {
							var hb = (ConfigManager.ActiveTheme == MesenTheme.Dark)
								? new SolidColorBrush(Color.Parse("#33CCCCCC"))
								: new SolidColorBrush(Color.Parse("#FFCCE8FF"));
							floppyContent.Background = hb;
						} else {
							floppyContent.Background = normalBrush;
						}
					});
				});
			} catch {
				// 若在设计器或环境不支持时忽略
			}

			this.DetachedFromVisualTree += (s, e) => {
				_hoverSub?.Dispose();
				_hoverSub = null;
				// 取消订阅通知监听器
				if(_floppyNotificationListener != null) {
					_floppyNotificationListener.Dispose();
					_floppyNotificationListener = null;
				}
				// 停止并释放任何延时关闭定时器
				if(_floppyOffTimer != null) {
					_floppyOffTimer.Stop();
					_floppyOffTimer = null;
				}
				// 从主窗口的 MouseManager 注销 FloppyPanel 的 ContextMenu（若已注册）
				try {
					var cm = floppyPanel.ContextMenu;
					if(cm != null) {
						var wnd = ApplicationHelper.GetMainWindow() as Mesen.Windows.MainWindow;
						wnd?.UnregisterRendererContextMenu(cm);
					}
				} catch { }
			};

			floppyPanel.PointerReleased += async (s, e) => {
				if(e.InitialPressMouseButton == MouseButton.Left) {
					// 支持 .ima 和 .img 镜像格式
					string? filePath = await FileDialogHelper.OpenFile(null, ApplicationHelper.GetMainWindow(), "ima", "img");
					if(!string.IsNullOrEmpty(filePath)) {
						if(File.Exists(filePath)) {
							// 弹出对话框选择进来的装载软盘。
							int state = EmuApi.FloppyLoadDiskImage(filePath);
							if(DataContext is MainMenuViewModel model) { model.SetFloppyStatus(Path.GetFileName(filePath)); }
						}
					}
				}
			};

			// 软驱活动灯：通过通知驱动（核心发送 FloppyIoStarted/FloppyIoStopped）
			try {
				var floppyLed = this.GetControl<Border>("FloppyLed");
				// 定义根据主题生成刷子的函数（便于在主题变化时重新计算）
				Func<SolidColorBrush> makeActiveBrush = () => new SolidColorBrush(Color.Parse("#FF0B8B50"));
				Func<SolidColorBrush> makeIdleBrush = () => new SolidColorBrush(Color.Parse("#FFDDDDDD"));
				Func<SolidColorBrush> makeActiveBorderBrush = () => new SolidColorBrush(Color.Parse("#FF07583F"));
				Func<SolidColorBrush> makeIdleBorderBrush = () => (ConfigManager.ActiveTheme == MesenTheme.Dark)
					? new SolidColorBrush(Color.Parse("#FF1F1F1F")) // 暗色主题：更深的边框
					: new SolidColorBrush(Color.Parse("#FFC0C0C0")); // 亮色主题：更浅的边框

				// 初始刷子
				var activeBrush = makeActiveBrush();
				var idleBrush = makeIdleBrush();
				var activeBorderBrush = makeActiveBorderBrush();
				var idleBorderBrush = makeIdleBorderBrush();
				// 初始化为空闲颜色（包括边框）
				floppyLed.Background = idleBrush;
				floppyLed.BorderBrush = idleBorderBrush;

				if(!Design.IsDesignMode) {
					_floppyNotificationListener = new NotificationListener();
					_floppyNotificationListener.OnNotification += (NotificationEventArgs e) => {
						if(e.NotificationType == ConsoleNotificationType.FloppyIoStarted) {
							// 读/写开始：取消任何待关闭定时器并立即点亮
							Dispatcher.UIThread.Post(() => {
								if(_floppyOffTimer != null) {
									_floppyOffTimer.Stop();
									_floppyOffTimer = null;
								}
								// 标记为活动并更新外观
								_isFloppyActive = true;
								floppyLed.Background = activeBrush;
								floppyLed.BorderBrush = activeBorderBrush;
							});
						} else if(e.NotificationType == ConsoleNotificationType.FloppyIoStopped) {
							// 读/写结束：启动一次性 100ms 定时器延迟关闭（若期间再次开始则会取消）
							Dispatcher.UIThread.Post(() => {
								if(_floppyOffTimer != null) {
									_floppyOffTimer.Stop();
									_floppyOffTimer = null;
								}
								_floppyOffTimer = new DispatcherTimer();
								_floppyOffTimer.Interval = TimeSpan.FromMilliseconds(50);
								_floppyOffTimer.Tick += (s2, t2) => {
									// 关闭并释放定时器
									_floppyOffTimer.Stop();
									_floppyOffTimer = null;
									// 标记为非活动并恢复空闲外观
									_isFloppyActive = false;
									floppyLed.Background = idleBrush;
									floppyLed.BorderBrush = idleBorderBrush;
								};
								_floppyOffTimer.Start();
							});
						}
					};
				}

				// 注册主题变化回调：在切换主题时重新计算并应用刷子
				try {
					ColorHelper.InvalidateControlOnThemeChange(floppyPanel, () => {
						Dispatcher.UIThread.Post(() => {
							// 重新生成刷子
							activeBrush = makeActiveBrush();
							idleBrush = makeIdleBrush();
							activeBorderBrush = makeActiveBorderBrush();
							idleBorderBrush = makeIdleBorderBrush();
							// 根据当前活动状态应用外观
							if(_isFloppyActive) {
								floppyLed.Background = activeBrush;
								floppyLed.BorderBrush = activeBorderBrush;
							} else {
								floppyLed.Background = idleBrush;
								floppyLed.BorderBrush = idleBorderBrush;
							}
						});
					});
				} catch {
					// 在设计模式或环境不支持时忽略
				}

				// 一次性同步当前状态（若可用），避免短暂不同步
				try {
					bool isActive = EmuApi.FloppyIsActive() != 0;
					_isFloppyActive = isActive;
					if(isActive) {
						floppyLed.Background = activeBrush;
						floppyLed.BorderBrush = activeBorderBrush;
					} else {
						floppyLed.Background = idleBrush;
						floppyLed.BorderBrush = idleBorderBrush;
					}
				} catch {
					// 在设计模式或 DLL 未加载时忽略
				}
			} catch {
				// 在设计模式或控件不可用时忽略
			}
		}

		private void InitializeComponent()
		{
			AvaloniaXamlLoader.Load(this);
		}

		private void mnuTools_Opened(object sender, RoutedEventArgs e)
		{
			if(DataContext is MainMenuViewModel model) {
				if(model.UpdateNetplayMenu() && e.Source is MenuItem item) {
					//Force a refresh of the tools menu to ensure
					//the "Select controller" submenu gets updated
					IEnumerable? items = item.ItemsSource;
					item.ItemsSource = null;
					item.ItemsSource = items;
				}
			}
		}

		private void RightButton_Click(object? sender, RoutedEventArgs e)
		{
			// Close the menu and refocus main window when right-side button is clicked
			MainMenu.Close();
			ApplicationHelper.GetMainWindow()?.Focus();
		}

		private void EjectFloppy_Click(object? sender, RoutedEventArgs e)
		{
			// Clear saved floppy path in native core and reset UI status
			try {
				// 弹出软盘。
				EmuApi.FloppyEject();
			} catch { }

			if(DataContext is MainMenuViewModel model) {
				model.SetFloppyStatus("软盘已弹出！");
			}
		}

		private void OpenDiskManagement_Click(object? sender, RoutedEventArgs e)
		{
			// 打开磁盘管理窗口：如果已存在则激活，否则创建并按主窗口定位展示
			try {
				var mainWnd = ApplicationHelper.GetActiveOrMainWindow();
				var existing = ApplicationHelper.GetExistingWindow<Mesen.Windows.DiskManagementWindow>();
				if(existing != null) {
					existing.BringToFront();
					return;
				}
				if(mainWnd != null) {
					try {
						var dlg = new Mesen.Windows.DiskManagementWindow();
						// 保持与主窗口相同高度
						dlg.Height = mainWnd.Height;
						if(double.IsNaN(dlg.Width) || dlg.Width <= 0) dlg.Width = 360;
						try {
							var controlPosition = mainWnd.Position;
							double scale = 1.0;
							try {
								var parentWndBase = mainWnd as Avalonia.Controls.WindowBase;
								var scr = parentWndBase?.Screens.ScreenFromVisual(parentWndBase);
								if(scr != null) {
									var scalingProp = scr.GetType().GetProperty("Scaling");
									if(scalingProp != null) {
										var val = scalingProp.GetValue(scr);
										if(val is double d) scale = d;
										else if(val is float f) scale = f;
									}
								}
							} catch { scale = 1.0; }
							double dlgWidthDip = dlg.FrameSize?.Width ?? dlg.Width;
							if(double.IsNaN(dlgWidthDip) || dlgWidthDip <= 0) dlgWidthDip = 300;
							int dlgWidthPx = (int)(dlgWidthDip * scale);
							var startPosition = new Avalonia.PixelPoint(controlPosition.X - dlgWidthPx, controlPosition.Y);
							dlg.WindowStartupLocation = Avalonia.Controls.WindowStartupLocation.Manual;
							EventHandler? openedHandler = null;
							openedHandler = (s2, e2) => {
								try {
									double actualDlgWidthDip = dlg.FrameSize?.Width ?? dlg.Width;
									if(double.IsNaN(actualDlgWidthDip) || actualDlgWidthDip <= 0) actualDlgWidthDip = dlgWidthDip;
									int actualDlgWidthPx = (int)(actualDlgWidthDip * scale);
									int extraPx = (int)(12 * scale);
									dlg.Position = new Avalonia.PixelPoint(controlPosition.X - actualDlgWidthPx + extraPx, controlPosition.Y);
								} catch { }
								dlg.Opened -= openedHandler;
							};
							dlg.Opened += openedHandler;
							dlg.Show(mainWnd);
							return;
						} catch { }
					} catch { }
				}
				// 回退：使用单例创建器
				ApplicationHelper.GetOrCreateUniqueWindow(null, () => new Mesen.Windows.DiskManagementWindow());
			} catch { }
		}
	}
}
