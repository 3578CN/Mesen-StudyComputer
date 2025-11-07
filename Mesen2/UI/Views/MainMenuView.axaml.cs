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

		// 磁吸（磁贴）行为相关字段：用于使磁盘管理窗口靠近并跟随主窗口移动
		private Mesen.Windows.DiskManagementWindow? _diskManagementWindow;
		private Avalonia.Controls.Window? _magnetMainWindow;
		private bool _isDiskMagnetized = false;
		private Avalonia.PixelPoint _magnetOffset = new Avalonia.PixelPoint(0, 0);
		private Avalonia.PixelPoint _lastMainWindowPosition = new Avalonia.PixelPoint(0, 0);
		private DispatcherTimer? _magnetTimer;
		private int _diskWidthPx = 0;
		private double _magnetThresholdBase = 12.0; // 基础像素阈值（会乘以缩放）
		private double _magnetScale = 1.0;

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

				// 停止并释放磁吸计时器并重置引用
				if(_magnetTimer != null) {
					_magnetTimer.Stop();
					_magnetTimer.Tick -= MagnetTimer_Tick;
					_magnetTimer = null;
				}
				_diskManagementWindow = null;
				_magnetMainWindow = null;
				_isDiskMagnetized = false;
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

		/// <summary>
		/// 为弹出的磁盘管理窗口设置磁吸（磁贴）行为：当靠近主窗口左侧时吸附，并在主窗口移动时跟随移动。
		/// 使用 Avalonia API 实现，无 Win32 回退；实现尽量保守以兼容不同运行环境。
		/// </summary>
		/// <param name="mainWnd">主窗口引用（用于获取位置）</param>
		/// <param name="dlg">磁盘管理窗口实例</param>
		/// <param name="controlPosition">主窗口位置（像素）</param>
		/// <param name="scale">屏幕缩放</param>
		/// <param name="actualDlgWidthPx">磁盘窗口宽度（像素）</param>
		/// <param name="extraPx">额外偏移（像素）——与主窗口之间的可视间距</param>
		private void SetupDiskMagnetization(Avalonia.Controls.Window? mainWnd, Mesen.Windows.DiskManagementWindow dlg, Avalonia.PixelPoint controlPosition, double scale, int actualDlgWidthPx, int extraPx)
		{
			try {
				if(mainWnd == null || dlg == null) return;
				// 记录引用与参数
				_diskManagementWindow = dlg;
				_magnetMainWindow = mainWnd;
				_diskWidthPx = actualDlgWidthPx;
				_magnetScale = scale;
				int thresholdPx = (int)(_magnetThresholdBase * _magnetScale);

				// 期望位置（通常在 openedHandler 已设置过一次，这里再做一次计算以便检测）
				var desiredPos = new Avalonia.PixelPoint(controlPosition.X - actualDlgWidthPx + extraPx, controlPosition.Y);
				// 计算主窗口左边与磁盘窗口右边之间的像素差（正值表示主窗口在右侧）
				int gap = Math.Abs((desiredPos.X + actualDlgWidthPx) - mainWnd.Position.X);
				if(gap <= thresholdPx) {
					// 吸附：记录偏移并开始监听
					_isDiskMagnetized = true;
					_magnetOffset = new Avalonia.PixelPoint(desiredPos.X - mainWnd.Position.X, desiredPos.Y - mainWnd.Position.Y);
					_lastMainWindowPosition = mainWnd.Position;
				} else {
					_isDiskMagnetized = false;
				}

				// 创建或启动磁吸轮询器（用于检测主窗口/磁盘窗口位置变化）
				if(_magnetTimer == null) {
					_magnetTimer = new DispatcherTimer();
					_magnetTimer.Interval = TimeSpan.FromMilliseconds(50);
					_magnetTimer.Tick += MagnetTimer_Tick;
					_magnetTimer.Start();
				}

				// 当磁盘窗口关闭时清理资源
				dlg.Closed += (s, e) => {
					CleanupDiskMagnetization();
				};
			} catch { }
		}

		/// <summary>
		/// 磁吸轮询回调：同步主窗口移动、检测用户拖动以解除吸附/重新吸附。
		/// </summary>
		private void MagnetTimer_Tick(object? sender, EventArgs e)
		{
			try {
				if(_diskManagementWindow == null || _magnetMainWindow == null) return;
				var mainPos = _magnetMainWindow.Position;

				// 主窗口移动时，如果已吸附则同步移动磁盘窗口
				if(!_lastMainWindowPosition.Equals(mainPos)) {
					if(_isDiskMagnetized) {
						var newPos = new Avalonia.PixelPoint(mainPos.X + _magnetOffset.X, mainPos.Y + _magnetOffset.Y);
						try { _diskManagementWindow.Position = newPos; } catch { }
					}
					_lastMainWindowPosition = mainPos;
				}

				// 检测用户是否手动移动了磁盘窗口：若当前位置与期望位置偏离较大则解除吸附
				if(_isDiskMagnetized) {
					var expectedPos = new Avalonia.PixelPoint(mainPos.X + _magnetOffset.X, mainPos.Y + _magnetOffset.Y);
					var cur = _diskManagementWindow.Position;
					int dx = Math.Abs(cur.X - expectedPos.X);
					int dy = Math.Abs(cur.Y - expectedPos.Y);
					if(dx > 4 || dy > 4) {
						_isDiskMagnetized = false; // 用户主动移动，解除吸附
					}
				} else {
					// 未吸附时检测是否靠近主窗口足够近以重新吸附
					var cur = _diskManagementWindow.Position;
					int dist = Math.Abs((cur.X + _diskWidthPx) - mainPos.X);
					int threshold = (int)(_magnetThresholdBase * _magnetScale);
					if(dist <= threshold) {
						// 重新吸附并记录偏移
						_isDiskMagnetized = true;
						_magnetOffset = new Avalonia.PixelPoint(cur.X - mainPos.X, cur.Y - mainPos.Y);
						_lastMainWindowPosition = mainPos;
					}
				}
			} catch { }
		}

		/// <summary>
		/// 清理磁吸相关资源
		/// </summary>
		private void CleanupDiskMagnetization()
		{
			try {
				if(_magnetTimer != null) {
					_magnetTimer.Stop();
					_magnetTimer.Tick -= MagnetTimer_Tick;
					_magnetTimer = null;
				}
				_diskManagementWindow = null;
				_magnetMainWindow = null;
				_isDiskMagnetized = false;
			} catch { }
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

		private async void InsertFloppy_Click(object? sender, RoutedEventArgs e)
		{
			try {
				// 打开文件对话框以选择 .ima/.img 镜像并加载为软盘
				string? filePath = await FileDialogHelper.OpenFile(null, ApplicationHelper.GetMainWindow(), "ima", "img");
				if(!string.IsNullOrEmpty(filePath)) {
					if(File.Exists(filePath)) {
						try {
							int state = EmuApi.FloppyLoadDiskImage(filePath);
							if(DataContext is MainMenuViewModel model) { model.SetFloppyStatus(Path.GetFileName(filePath)); }
						} catch { }
					}
				}
			} catch { }
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
								// 直接使用 Avalonia API 获取屏幕缩放，替换原先的反射实现
								// 在 AOT 环境下反射可能被裁剪或不可用，直接访问更可靠
								var parentWndBase = mainWnd as Avalonia.Controls.WindowBase;
								var scr = parentWndBase?.Screens.ScreenFromVisual(parentWndBase);
								if(scr != null) {
									// 直接使用 Screen 的 Scaling 属性（多数 Avalonia 版本支持）
									scale = scr.Scaling;
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
									// 尝试为该磁盘管理窗口启用磁吸行为（靠近时吸附并跟随主窗口移动）
									try { SetupDiskMagnetization(mainWnd as Avalonia.Controls.Window, dlg, controlPosition, scale, actualDlgWidthPx, extraPx); } catch { }
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
