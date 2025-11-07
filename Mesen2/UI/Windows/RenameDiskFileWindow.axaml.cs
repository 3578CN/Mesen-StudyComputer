using Avalonia;
using Avalonia.Controls;
using Avalonia.Interactivity;
using Avalonia.Markup.Xaml;
using Mesen.Utilities;
using System;
using System.ComponentModel;
using System.Threading.Tasks;

namespace Mesen.Windows
{
    public partial class RenameDiskFileWindow : MesenWindow
    {
        private readonly string _originalName;
        private TextBox? _nameBox;

        [Obsolete("Designer only")]
        public RenameDiskFileWindow() : this(string.Empty)
        {
        }

        public RenameDiskFileWindow(string currentName)
        {
            _originalName = currentName;
            InitializeComponent();
#if DEBUG
            this.AttachDevTools();
#endif
        }

        private void InitializeComponent()
        {
            AvaloniaXamlLoader.Load(this);
            _nameBox = this.FindControl<TextBox>("txtFileName");
            if(_nameBox != null) {
                _nameBox.Text = _originalName;
            }
        }

        protected override void OnOpened(EventArgs e)
        {
            base.OnOpened(e);
            _nameBox?.Focus();
            _nameBox?.SelectAll();
        }

        private void OnOkClick(object? sender, RoutedEventArgs e)
        {
            string text = _nameBox?.Text?.Trim() ?? string.Empty;
            if(string.IsNullOrWhiteSpace(text)) {
                DisplayMessageHelper.DisplayMessage("Error", "文件名不能为空。");
                return;
            }

            Close(text);
        }

        private void OnCancelClick(object? sender, RoutedEventArgs e)
        {
            Close(null);
        }

        public static Task<string?> ShowDialog(Window owner, string currentName)
        {
            RenameDiskFileWindow wnd = new RenameDiskFileWindow(currentName);
            return wnd.ShowCenteredDialog<string?>(owner);
        }
    }
}
