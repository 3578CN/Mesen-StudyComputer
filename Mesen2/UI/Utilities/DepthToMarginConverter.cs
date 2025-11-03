using Avalonia;
using Avalonia.Data.Converters;
using System;

namespace Mesen.Utilities
{
	/// <summary>
	/// 将节点深度转换为左侧缩进（Thickness）。
	/// </summary>
	public class DepthToMarginConverter : IValueConverter
	{
		public object Convert(object? value, Type targetType, object? parameter, System.Globalization.CultureInfo culture)
		{
			int depth = 0;
			if(value is int d) depth = d;
			else if(value is long l) depth = (int)l;
			else if(value is string s && int.TryParse(s, out int parsed)) depth = parsed;

			// 基础缩进 + 每级深度 14 像素
			int indent = Math.Max(0, depth) * 14 + 4;
			return new Thickness(indent, 0, 0, 0);
		}

		public object ConvertBack(object? value, Type targetType, object? parameter, System.Globalization.CultureInfo culture)
		{
			throw new NotImplementedException();
		}
	}
}
