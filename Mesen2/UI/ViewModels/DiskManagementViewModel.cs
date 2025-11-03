using System;
using System.Collections.Generic;
using System.Linq;
using System.Reactive;
using System.Text.Json;
using Avalonia;
using Avalonia.Controls;
using Mesen.Interop;
using ReactiveUI;

namespace Mesen.ViewModels
{
	/// <summary>
	/// 表示软盘目录树中的单个节点。
	/// </summary>
	public class DiskDirectoryNode
	{
		public DiskDirectoryNode(string name, bool isDirectory, bool isDisk, long size, IReadOnlyList<DiskDirectoryNode> children)
		{
			Name = name;
			IsDirectory = isDirectory;
			IsDisk = isDisk;
			Size = size;
			Children = children;
		}

		/// <summary>
		/// 节点名称。
		/// </summary>
		public string Name { get; }

		/// <summary>
		/// 指示当前节点是否为目录。
		/// </summary>
		public bool IsDirectory { get; }

		/// <summary>
		/// 指示当前节点是否为磁盘根节点。
		/// </summary>
		public bool IsDisk { get; }

		/// <summary>
		/// 节点大小（字节）。
		/// </summary>
		public long Size { get; }

		/// <summary>
		/// 子节点集合。
		/// </summary>
		public IReadOnlyList<DiskDirectoryNode> Children { get; }

		/// <summary>
		/// 是否存在子节点。
		/// </summary>
		public bool HasChildren => Children.Count > 0;

		/// <summary>
		/// 用于 UI 显示的大小描述。
		/// </summary>
		public string SizeText
		{
			get
			{
				if(IsDisk) {
					return Size > 0 ? $"{Size} 字节" : "无数据";
				}
				if(IsDirectory) {
					return "目录";
				}
				return $"{Size} 字节";
			}
		}
	}

	/// <summary>
	/// 提供软盘目录树数据并支持刷新命令。
	/// </summary>
	public class DiskManagementViewModel : ViewModelBase
	{
		private IReadOnlyList<DiskDirectoryNode> _items = Array.Empty<DiskDirectoryNode>();
		private string _statusText = "未加载磁盘";

		/// <summary>
		/// 树视图使用的根节点集合。
		/// </summary>
		public IReadOnlyList<DiskDirectoryNode> Items
		{
			get => _items;
			private set => this.RaiseAndSetIfChanged(ref _items, value);
		}

		/// <summary>
		/// 状态字符串，用于提示当前软盘状态。
		/// </summary>
		public string StatusText
		{
			get => _statusText;
			private set => this.RaiseAndSetIfChanged(ref _statusText, value);
		}

		/// <summary>
		/// 刷新目录树的命令。
		/// </summary>
		public ReactiveCommand<Unit, Unit> RefreshCommand { get; }

		/// <summary>
		/// 构造函数，初始化命令并在非设计模式下立即刷新。
		/// </summary>
		public DiskManagementViewModel()
		{
			RefreshCommand = ReactiveCommand.Create(Refresh);
			if(!Design.IsDesignMode) {
				Refresh();
			}
		}

		/// <summary>
		/// 调用互操作层获取目录树并更新绑定。
		/// </summary>
		private void Refresh()
		{
			try {
				string json = EmuApi.FloppyGetDirectoryTree();
				if(string.IsNullOrWhiteSpace(json)) {
					Items = Array.Empty<DiskDirectoryNode>();
					StatusText = "未加载磁盘";
					return;
				}

				DiskDirectoryNode? root = ParseRoot(json);
				if(root == null) {
					Items = Array.Empty<DiskDirectoryNode>();
					StatusText = "未加载磁盘";
					return;
				}

				Items = new List<DiskDirectoryNode> { root };
				StatusText = BuildStatus(root);
			} catch {
				Items = Array.Empty<DiskDirectoryNode>();
				StatusText = "读取磁盘目录失败";
			}
		}

		private static DiskDirectoryNode? ParseRoot(string json)
		{
			try {
				using JsonDocument doc = JsonDocument.Parse(json);
				switch(doc.RootElement.ValueKind) {
					case JsonValueKind.Object:
						return ParseNode(doc.RootElement);
					case JsonValueKind.Array:
						return doc.RootElement.EnumerateArray().Select(ParseNode).FirstOrDefault();
					default:
						return null;
				}
			} catch(JsonException) {
				return null;
			}
		}

		private static DiskDirectoryNode ParseNode(JsonElement element)
		{
			string name = element.TryGetProperty("name", out JsonElement nameProp) ? nameProp.GetString() ?? string.Empty : string.Empty;
			string type = element.TryGetProperty("type", out JsonElement typeProp) ? typeProp.GetString() ?? "file" : "file";
			bool isDisk = string.Equals(type, "disk", StringComparison.OrdinalIgnoreCase);
			bool isDirectory = isDisk || string.Equals(type, "dir", StringComparison.OrdinalIgnoreCase);
			long size = 0;
			if(element.TryGetProperty("size", out JsonElement sizeProp)) {
				size = sizeProp.TryGetInt64(out long value) ? value : 0;
			}

			List<DiskDirectoryNode> children = new();
			if(element.TryGetProperty("children", out JsonElement childrenProp) && childrenProp.ValueKind == JsonValueKind.Array) {
				foreach(JsonElement child in childrenProp.EnumerateArray()) {
					DiskDirectoryNode childNode = ParseNode(child);
					children.Add(childNode);
				}
			}

			return new DiskDirectoryNode(name, isDirectory, isDisk, size, children);
		}

		private static string BuildStatus(DiskDirectoryNode root)
		{
			int fileCount = 0;
			int directoryCount = 0;
			CountEntries(root, ref fileCount, ref directoryCount);

			if(fileCount == 0 && directoryCount == 0) {
				return "磁盘为空";
			}

			return $"目录 {directoryCount} 个，文件 {fileCount} 个";
		}

		private static void CountEntries(DiskDirectoryNode node, ref int files, ref int directories)
		{
			foreach(DiskDirectoryNode child in node.Children) {
				if(child.IsDirectory && !child.IsDisk) {
					directories++;
				}
				if(!child.IsDirectory) {
					files++;
				}
				CountEntries(child, ref files, ref directories);
			}
		}
	}
}
