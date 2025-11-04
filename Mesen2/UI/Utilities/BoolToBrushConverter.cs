using Avalonia;
using Avalonia.Data.Converters;
using Avalonia.Media;
using System;
using System.Globalization;

namespace Mesen.Utilities
{
    /// <summary>
    /// 将 bool 转换为 Brush：true -> 高亮背景，false -> Transparent。
    /// 用于 DiskManagement 的选中行高亮。
    /// </summary>
    public class BoolToBrushConverter : IValueConverter
    {
        public object Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
        {
            if(targetType != typeof(IBrush) && targetType != typeof(object)) return AvaloniaProperty.UnsetValue;
            if(value is bool b && b) {
                // 使用浅蓝色作为选中背景（不覆盖主题色）
                return new SolidColorBrush(0xFFCCE5FFu);
            }
            return Brushes.Transparent;
        }

        public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture)
        {
            return AvaloniaProperty.UnsetValue;
        }
    }
}
