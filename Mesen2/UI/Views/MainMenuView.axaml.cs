using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Markup.Xaml;
using Mesen.Utilities;
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

			// Ensure hover visual even if XAML style is overridden by theme: subscribe to IsPointerOver
			var hoverBrush = new SolidColorBrush(Color.Parse("#B0A0C8FF"));
			var normalBrush = Brushes.Transparent;
			_hoverSub = Avalonia.AvaloniaObjectExtensions.GetObservable<bool>((Avalonia.AvaloniaObject)floppyPanel, Control.IsPointerOverProperty)
				.Subscribe(isOver => floppyContent.Background = isOver ? hoverBrush : normalBrush);

			this.DetachedFromVisualTree += (s, e) => {
				_hoverSub?.Dispose();
				_hoverSub = null;
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
	}
}
