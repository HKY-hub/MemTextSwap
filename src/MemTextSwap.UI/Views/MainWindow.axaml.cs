using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Platform.Storage;
using MemTextSwap.UI.ViewModels;

namespace MemTextSwap.UI.Views;

public partial class MainWindow : Window
{
    public MainWindow()
    {
        InitializeComponent();
    }

    private void OnDragOver(object? sender, DragEventArgs e)
    {
        e.DragEffects = e.Data.Contains(DataFormats.FileNames)
            ? DragDropEffects.Copy
            : DragDropEffects.None;
    }

    private async void OnDrop(object? sender, DragEventArgs e)
    {
        if (DataContext is not MainWindowViewModel viewModel ||
            !e.Data.Contains(DataFormats.FileNames))
        {
            return;
        }
        var files = e.Data.GetFileNames()?.ToList() ?? new List<string>();
        string? exe = files.FirstOrDefault(f =>
            f.EndsWith(".exe", StringComparison.OrdinalIgnoreCase));
        if (exe is null)
        {
            viewModel.SetStatus("请拖入 .exe 游戏文件");
            return;
        }
        await viewModel.SelectGameByExeAsync(exe);
    }

    private async void OnPickGameFile(object? sender, RoutedEventArgs e)
    {
        if (DataContext is not MainWindowViewModel viewModel)
        {
            return;
        }
        var topLevel = TopLevel.GetTopLevel(this);
        if (topLevel is null)
        {
            return;
        }
        var files = await topLevel.StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
        {
            Title = "选择游戏主程序",
            AllowMultiple = false,
            FileTypeFilter = new[]
            {
                new FilePickerFileType("可执行文件") { Patterns = new[] { "*.exe" } },
            },
        });
        if (files.Count > 0)
        {
            await viewModel.SelectGameByExeAsync(files[0].TryGetLocalPath() ?? files[0].Path.LocalPath);
        }
    }

    private void OnGameDoubleTapped(object? sender, TappedEventArgs e)
    {
        if (DataContext is MainWindowViewModel viewModel)
        {
            viewModel.InjectCommand.Execute(null);
        }
    }
}
