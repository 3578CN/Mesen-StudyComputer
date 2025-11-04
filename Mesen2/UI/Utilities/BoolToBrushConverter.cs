using Avalonia;
using Avalonia.Data.Converters;
using Avalonia.Media;
using Avalonia.Styling;
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
        /// <summary>
        /// 将布尔值转换为选中行背景画刷：true -> 根据当前主题返回合适的高亮背景（深色/亮色），false -> Transparent。
        /// 支持主题感知，避免亮/暗主题下高亮不可见。
        /// </summary>
        public object Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
        {
            if(targetType != typeof(IBrush) && targetType != typeof(object)) return AvaloniaProperty.UnsetValue;
            if(value is bool b && b) {
                // 根据当前应用的 RequestedThemeVariant 返回不同的颜色，确保亮/暗主题都有合适对比度
                var variant = Application.Current?.RequestedThemeVariant ?? ThemeVariant.Light;
                if(variant == ThemeVariant.Dark) {
                    // 暗色主题：使用深蓝灰以便在深色背景上可见但不刺眼
                    return new SolidColorBrush(0xFF334A76u);
                } else {
                    // 亮色主题：使用较浅的蓝色高亮，类似于 TextBox 的选中颜色
                    return new SolidColorBrush(0xFFCCE5FFu);
                }
            }
            return Brushes.Transparent;
        }

        public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture)
        {
            return AvaloniaProperty.UnsetValue;
        }
    }
}
