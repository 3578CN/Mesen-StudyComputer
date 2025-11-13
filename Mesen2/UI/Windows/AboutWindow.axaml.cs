using Avalonia;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Markup.Xaml;
using Avalonia.Threading;
using System;
using System.ComponentModel;
using Avalonia.Data;
using Mesen.Interop;
using System.Collections.Generic;
using Avalonia.Input;
using Mesen.Utilities;
using System.Runtime.CompilerServices;

namespace Mesen.Windows
{
	public class AboutWindow : MesenWindow
	{
		public string Version { get; }
		public string BuildDate { get; }
		public string RuntimeVersion { get; }
		public string BuildSha { get; }
		public string BuildShortSha { get; }
		public List<AboutListEntry> AcknowledgeList { get; }

		public AboutWindow()
		{
			Version = EmuApi.GetMesenVersion().ToString();
			BuildDate = EmuApi.GetMesenBuildDate();
			RuntimeVersion = ".NET " + Environment.Version;
			RuntimeVersion += RuntimeFeature.IsDynamicCodeSupported ? " (JIT)" : " (AOT)";

			string? commitHash = UpdateHelper.GetCommitHash();
			BuildSha = commitHash ?? "";
			BuildShortSha = commitHash?.Substring(0, 7) ?? "";

			AcknowledgeList = new List<AboutListEntry>() {
				new("轻舞飘揚", "QQ：123223194", ""),
				new("钳工", "QQ：87430545", ""),
				new("惊风", "QQ：39237780", ""),
			};
			AcknowledgeList.Sort((a, b) => a.Name.CompareTo(b.Name));

			InitializeComponent();

			this.GetControl<TextBlock>("lblCopyright").Text = $"Copyright 2025-{DateTime.Now.Year} Sour";
		}

		private void InitializeComponent()
		{
			AvaloniaXamlLoader.Load(this);
		}

		private void btnOk_OnClick(object? sender, RoutedEventArgs e)
		{
			Close();
		}

		private void OnLinkPressed(object? sender, PointerPressedEventArgs e)
		{
			if(sender is TextBlock text && text.DataContext is AboutListEntry entry) {
				ApplicationHelper.OpenBrowser(entry.Url);
			}
		}

		private void OnMesenLinkTapped(object? sender, TappedEventArgs e)
		{
			ApplicationHelper.OpenBrowser("https://Mesen.Plus");
		}

		private void OnCommitLinkTapped(object? sender, TappedEventArgs e)
		{
			ApplicationHelper.OpenBrowser("https://github.com/sengbin/Mesen-StudyComputer/commit/" + BuildSha);
		}
	}

	public class AboutListEntry(string name, string note, string url)
	{
		public string Name { get; set; } = name;
		public string Note { get; set; } = note;
		public string Url { get; set; } = url;
	}
}