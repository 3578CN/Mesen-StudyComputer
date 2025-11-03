using System;
using System.Collections.Generic;
using System.Linq;
using System.Text.Json;
using Avalonia;
using Avalonia.Controls;
using Mesen.Interop;
using ReactiveUI;
using System.Windows.Input;

namespace Mesen.ViewModels
{
	/// <summary>
	/// 表示软盘目录树中的单个节点。
	/// </summary>
	public class DiskDirectoryNode
	{
		public DiskDirectoryNode(string name, bool isDirectory, bool isDisk, long size, IReadOnlyList<DiskDirectoryNode> children, long capacity = 0, long freeBytes = 0)
		{
			Name = name;
			IsDirectory = isDirectory;
			IsDisk = isDisk;
			Size = size;
			Children = children;
			Capacity = capacity;
			FreeBytes = freeBytes;
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

		/// <summary>
		/// 磁盘总容量（字节），仅在 IsDisk 时有意义。
		/// </summary>
		public long Capacity { get; }

		/// <summary>
		/// 磁盘可用空间（字节），仅在 IsDisk 时有意义。
		/// </summary>
		public long FreeBytes { get; }

		/// <summary>
		/// 当节点为文件时，可在 UI 上绑定的选择命令。
		/// </summary>
		public System.Windows.Input.ICommand? SelectCommand { get; set; }

		/// <summary>
		/// 节点深度，用于计算缩进（由 ViewModel 在解析时设置）。
		/// </summary>
		public int Depth { get; set; }

		/// <summary>
		/// 用于 UI 显示的类型文本（文件/文件夹/磁盘）。
		/// </summary>
		public string TypeText => IsDisk ? "磁盘" : (IsDirectory ? "文件夹" : "文件");

		/// <summary>
		/// 修改时间文本（如果可用）。
		/// </summary>
		public string ModifiedText { get; set; } = string.Empty;
	}

	/// <summary>
	/// 提供软盘目录树数据并支持刷新命令。
	/// </summary>
	public class DiskManagementViewModel : ViewModelBase
	{
		private IReadOnlyList<DiskDirectoryNode> _items = Array.Empty<DiskDirectoryNode>();
		private string _statusText = "未加载磁盘";

		private DiskDirectoryNode? _selectedNode;

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
		/// 当前选中的文件节点（通过列表中每项的 SelectCommand 设置）。
		/// </summary>
		public DiskDirectoryNode? SelectedNode
		{
			get => _selectedNode;
			set => this.RaiseAndSetIfChanged(ref _selectedNode, value);
		}

		/// <summary>
		/// 构造函数，在非设计模式下立即刷新目录数据。
		/// </summary>
		public DiskManagementViewModel()
		{
			if(!Design.IsDesignMode) {
				Refresh();
			}
		}

		/// <summary>
		/// 调用互操作层获取目录树并更新绑定。
		/// 外部可调用（例如通过通知触发自动刷新）。
		/// </summary>
		public void Refresh()
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

				// 展示根节点的子项
				Items = root.Children ?? Array.Empty<DiskDirectoryNode>();
				// 为文件节点分配选择命令，使其可被单独点击选择
				foreach(var child in Items) {
					AssignSelectCommands(child);
				}
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
						// 从 depth=-1 开始，使根的直接子项 depth=0
						return ParseNode(doc.RootElement, -1);
					case JsonValueKind.Array:
						return doc.RootElement.EnumerateArray().Select(e => ParseNode(e, -1)).FirstOrDefault();
					default:
						return null;
				}
			} catch(JsonException) {
				return null;
			}
		}

		private static DiskDirectoryNode ParseNode(JsonElement element, int depth)
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
					DiskDirectoryNode childNode = ParseNode(child, depth + 1);
					children.Add(childNode);
				}
			}


			long capacity = 0;
			long freeBytes = 0;
			if(element.TryGetProperty("capacity", out JsonElement capProp) && capProp.TryGetInt64(out long capVal)) {
				capacity = capVal;
			}
			if(element.TryGetProperty("free", out JsonElement freeProp) && freeProp.TryGetInt64(out long freeVal)) {
				freeBytes = freeVal;
			}

			var node = new DiskDirectoryNode(name, isDirectory, isDisk, size, children, capacity, freeBytes);
			// 设置深度（根的子项为 0）
			node.Depth = Math.Max(0, depth);
			// 如果 JSON 中包含 modified 字段则解析（native 层可扩展以提供该字段）
			if(element.TryGetProperty("modified", out JsonElement modProp) && modProp.ValueKind == JsonValueKind.String) {
				node.ModifiedText = modProp.GetString() ?? string.Empty;
			}

			return node;
		}

		private static string BuildStatus(DiskDirectoryNode root)
		{
			int fileCount = 0;
			int directoryCount = 0;
			CountEntries(root, ref fileCount, ref directoryCount);

			if(fileCount == 0 && directoryCount == 0) {
				return "磁盘为空";
			}

			string baseStatus = $"目录 {directoryCount} 个，文件 {fileCount} 个";

			// 如果是磁盘根节点且提供了容量/可用空间信息，则附加显示
			if(root.IsDisk && root.Capacity > 0) {
				string capText = FormatBytes(root.Capacity);
				string freeText = root.FreeBytes > 0 ? FormatBytes(root.FreeBytes) : "未知";
				return baseStatus + $"，总大小 {capText}，可用 {freeText}";
			}

			return baseStatus;
		}

		private static string FormatBytes(long bytes)
		{
			double v = bytes;
			string[] units = new[] { "B", "KB", "MB", "GB", "TB" };
			int unit = 0;
			while(v >= 1024 && unit < units.Length - 1) { v /= 1024.0; unit++; }
			if(unit == 0) return $"{(long)v} {units[unit]}";
			return $"{v:0.##} {units[unit]}";
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

		/// <summary>
		/// 递归为文件节点分配 SelectCommand，使其可被单独点击选中。
		/// </summary>
		private void AssignSelectCommands(DiskDirectoryNode node)
		{
			if(node == null) return;
			if(!node.IsDirectory) {
				// 文件节点：设置命令以在触发时更新 SelectedNode
				node.SelectCommand = new RelayCommand(_ => {
					SelectedNode = node;
				});
			} else {
				foreach(var child in node.Children) {
					AssignSelectCommands(child);
				}
			}
		}

		/// <summary>
		/// 简单的 ICommand 实现，用于在节点上绑定执行动作。
		/// </summary>
		private class RelayCommand : ICommand
		{
			private readonly Action<object?> _execute;
			public RelayCommand(Action<object?> execute) { _execute = execute ?? throw new ArgumentNullException(nameof(execute)); }
			public bool CanExecute(object? parameter) => true;
			public event EventHandler? CanExecuteChanged { add { } remove { } }
			public void Execute(object? parameter) => _execute(parameter);
		}
	}
}
