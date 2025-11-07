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
							var startPosition = new Avalonia.PixelPoint(controlPosition.X - dlgWidthPx + (int)(12 * scale), controlPosition.Y);
							dlg.WindowStartupLocation = Avalonia.Controls.WindowStartupLocation.Manual;
							dlg.Position = startPosition;
							EventHandler? openedHandler = null;
							openedHandler = (s2, e2) => {
								try {
									double actualDlgWidthDip = dlg.FrameSize?.Width ?? dlg.Width;
									if(double.IsNaN(actualDlgWidthDip) || actualDlgWidthDip <= 0) actualDlgWidthDip = dlgWidthDip;
									int actualDlgWidthPx = (int)(actualDlgWidthDip * scale);
									// 基准偏移：用于在距离计算中加上额外的 12px*scale
									int baseOffsetPx = (int)(12 * scale);
									int extraPx = Math.Max(1, (int)Math.Round(12 * scale));
									bool isAdjustingPosition = false;
									bool isSnapped = false;
									Avalonia.PixelPoint snapPosition = new Avalonia.PixelPoint();

									Action syncWithMainPosition = () => {
										if(mainWnd == null) {
											return;
										}
										double currentScale = 1.0;
										try {
											var parentWndBase2 = mainWnd as Avalonia.Controls.WindowBase;
											var scr2 = parentWndBase2?.Screens.ScreenFromVisual(parentWndBase2);
											if(scr2 != null) {
												currentScale = scr2.Scaling;
											}
										} catch { currentScale = scale; }
										double widthDip = dlg.FrameSize?.Width ?? actualDlgWidthDip;
										if(double.IsNaN(widthDip) || widthDip <= 0) widthDip = actualDlgWidthDip;
										int widthPx = (int)Math.Round(widthDip * currentScale);
										int magnetThreshold = Math.Max(1, (int)Math.Round(12 * currentScale));
										int magnetThreshold2 = magnetThreshold * 2; // 二级阈值：24px*scale，用于更宽容的吸附区间

										// 计算当前磁盘窗口右侧坐标（包含基准偏移），用于与主窗口左侧比较
										int currentDiskRight = dlg.Position.X + widthPx + baseOffsetPx;
										int gap = mainWnd.Position.X - currentDiskRight; // >=0 表示磁盘窗口在主窗口左侧，gap 为间隙像素数

										// 若主窗口靠近磁盘窗口（gap <= 二级阈值），则触发吸附并对齐到主窗口左侧
										if(Math.Abs(gap) <= magnetThreshold2) {
											snapPosition = new Avalonia.PixelPoint(mainWnd.Position.X - widthPx + baseOffsetPx, mainWnd.Position.Y);
											isSnapped = true;
											if(dlg.Position != snapPosition) {
												isAdjustingPosition = true;
												try {
													dlg.Position = snapPosition;
												} finally {
													isAdjustingPosition = false;
												}
											}
											return;
										}

										// 若之前处于吸附状态但主窗口移动远离（超过二级阈值），则解除吸附
										if(isSnapped && Math.Abs(gap) > magnetThreshold2) {
											isSnapped = false;
										}
									};

									// 初始吸附位置（基于 controlPosition 计算，并加上基准偏移，作为起始对齐点）
									snapPosition = new Avalonia.PixelPoint(controlPosition.X - actualDlgWidthPx + baseOffsetPx, controlPosition.Y);
									isSnapped = true;
									isAdjustingPosition = true;
									try {
										dlg.Position = snapPosition;
									} finally {
										isAdjustingPosition = false;
									}

									// 直接订阅 PositionChanged 事件（内联 lambda），使用 dlg.Position 获取当前位置
									dlg.PositionChanged += (s3, e3) => {
										if(isAdjustingPosition) {
											return;
										}
										var attemptedPosition = dlg.Position;
										double currentScale = 1.0;
										try {
											var parentWndBase2 = mainWnd as Avalonia.Controls.WindowBase;
											var scr2 = parentWndBase2?.Screens.ScreenFromVisual(parentWndBase2);
											if(scr2 != null) {
												currentScale = scr2.Scaling;
											}
										} catch { currentScale = scale; }
										int magnetThreshold = Math.Max(1, (int)Math.Round(12 * currentScale));
										int magnetThreshold2 = magnetThreshold * 2;
										double widthDip = dlg.FrameSize?.Width ?? actualDlgWidthDip;
										if(double.IsNaN(widthDip) || widthDip <= 0) widthDip = actualDlgWidthDip;
										int widthPx = (int)Math.Round(widthDip * currentScale);

										// 如果尝试将磁盘窗口拖入主窗口区域（产生重叠），根据拖入深度决定是否允许移动：
										// - 若拖入深度 <= 阈值（magnetThreshold），视为未用力拉出，吸附回左侧；
										// - 若拖入深度 > 阈值，则允许移动（解除吸附），从而可以把窗口移入到主窗口右侧。
										if(mainWnd != null) {
											int mainLeftX = mainWnd.Position.X;
											int attemptedDiskRight = attemptedPosition.X + widthPx + baseOffsetPx;
											int overlap = attemptedDiskRight - mainLeftX; // 正值表示进入主窗口区域的像素数
											if(overlap > 0) {
												// 如果进入范围在一级或二级阈值内，都应当吸住（即 <=24px也吸住）
												if(overlap <= magnetThreshold2) {
													// 拖动不足以完全突破吸附（包括 12-24 区间），吸回左侧
													snapPosition = new Avalonia.PixelPoint(mainLeftX - widthPx + baseOffsetPx, mainWnd.Position.Y);
													isSnapped = true;
													isAdjustingPosition = true;
													try {
														dlg.Position = snapPosition;
													} finally {
														isAdjustingPosition = false;
													}
													return;
												} else {
													// 拖动力度超过二级阈值，解除吸附并允许移动（可以进入重叠区域）
													isSnapped = false;
													// 继续执行，允许用户移动窗口到重叠区域
												}
											}
										}

										if(isSnapped) {
											// 在比较移动距离时也以基准偏移为参考，避免初始缝隙或缩放误差
											// 释放吸附需要更大的水平位移（超过二级阈值），垂直位移继续使用一级阈值判断
											int diffX = Math.Abs((attemptedPosition.X + baseOffsetPx) - snapPosition.X);
											int diffY = Math.Abs((attemptedPosition.Y) - snapPosition.Y);
											if(diffX > magnetThreshold2 || diffY > magnetThreshold) {
												isSnapped = false;
												return;
											}
											isAdjustingPosition = true;
											try {
												dlg.Position = snapPosition;
											} finally {
												isAdjustingPosition = false;
											}
											return;
										}

										if(mainWnd == null) {
											return;
										}

										int mainLeft = mainWnd.Position.X;
										// 计算磁盘窗口右边（包含基准偏移），用于与主窗口左边比较距离
										int diskRight = attemptedPosition.X + widthPx + baseOffsetPx;
										int horizontalGap = mainLeft - diskRight;
										// 在判断是否要吸附时，基于加上基准偏移后的距离与二级阈值比较（12-24 区间也吸住）
										if(attemptedPosition.X <= mainLeft && Math.Abs(horizontalGap) <= magnetThreshold2) {
											// 触发吸附时：将吸附位置也应用基准偏移
											snapPosition = new Avalonia.PixelPoint(mainLeft - widthPx + baseOffsetPx, mainWnd.Position.Y);
											isSnapped = true;
											isAdjustingPosition = true;
											try {
												dlg.Position = snapPosition;
											} finally {
												isAdjustingPosition = false;
											}
										}
									};

									if(mainWnd != null) {
										mainWnd.PositionChanged += (s3, e3) => {
											syncWithMainPosition();
										};
									}

									// 不在此处移除事件（磁盘管理窗口以唯一窗口方式创建，重复打开不会重复注册）
									syncWithMainPosition();
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
