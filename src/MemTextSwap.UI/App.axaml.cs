using Avalonia;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Markup.Xaml;
using MemTextSwap.UI.Services;
using MemTextSwap.UI.ViewModels;
using MemTextSwap.UI.Views;
using Microsoft.Extensions.DependencyInjection;

namespace MemTextSwap.UI;

public partial class App : Application
{
    public static ServiceProvider Services { get; private set; } = null!;

    public override void Initialize()
    {
        AvaloniaXamlLoader.Load(this);
    }

    public override void OnFrameworkInitializationCompleted()
    {
        Services = ConfigureServices();
        var settings = Services.GetRequiredService<SettingsService>();
        settings.Load();
        var dictionary = Services.GetRequiredService<DictionaryService>();
        dictionary.LoadAll(settings.Current.DictionaryPaths);

        var viewModel = Services.GetRequiredService<MainWindowViewModel>();
        viewModel.ReloadDictionary();
        var window = new MainWindow { DataContext = viewModel };

        if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
        {
            desktop.MainWindow = window;
        }
        base.OnFrameworkInitializationCompleted();
    }

    private static ServiceProvider ConfigureServices()
    {
        var services = new ServiceCollection();
        services.AddSingleton<LogService>();
        services.AddSingleton<SettingsService>();
        services.AddSingleton<ProcessService>();
        services.AddSingleton<DictionaryService>();
        services.AddSingleton<CacheService>();
        services.AddSingleton<AIService>();
        services.AddSingleton<TranslationService>();
        services.AddSingleton<PipeServer>();
        services.AddSingleton<InjectorService>();
        services.AddSingleton<MainWindowViewModel>();
        return services.BuildServiceProvider();
    }
}
