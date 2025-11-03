using Avalonia;
using Avalonia.Controls;
using Avalonia.Markup.Xaml;
using Mesen.ViewModels;

namespace Mesen.Windows
{
    public class DiskManagementWindow : MesenWindow
    {
        public DiskManagementWindow()
        {
            InitializeComponent();
            if(!Design.IsDesignMode) {
                DataContext = new DiskManagementViewModel();
            }
        }

        private void InitializeComponent()
        {
            AvaloniaXamlLoader.Load(this);
        }
    }
}
