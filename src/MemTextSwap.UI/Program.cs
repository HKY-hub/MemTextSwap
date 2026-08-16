using Avalonia;
using System;
using System.IO;

namespace MemTextSwap.UI;

internal static class Program
{
    private static readonly string CrashLogPath = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
        "MemTextSwap", "logs", "crash.log");

    [STAThread]
    public static void Main(string[] args)
    {
        try
        {
            // 只保留最新崩溃日志
            string dir = Path.GetDirectoryName(CrashLogPath)!;
            Directory.CreateDirectory(dir);
            if (File.Exists(CrashLogPath))
            {
                File.Delete(CrashLogPath);
            }
        }
        catch
        {
        }
        AppDomain.CurrentDomain.UnhandledException += (_, e) =>
            WriteCrash("UnhandledException", e.ExceptionObject as Exception);
        TaskScheduler.UnobservedTaskException += (_, e) =>
        {
            WriteCrash("UnobservedTaskException", e.Exception);
            e.SetObserved();
        };
        try
        {
            BuildAvaloniaApp().StartWithClassicDesktopLifetime(args);
        }
        catch (Exception ex)
        {
            WriteCrash("Startup", ex);
            throw;
        }
    }

    public static AppBuilder BuildAvaloniaApp()
    {
        return AppBuilder.Configure<App>()
            .UsePlatformDetect()
            .LogToTrace();
    }

    private static void WriteCrash(string stage, Exception? ex)
    {
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(CrashLogPath)!);
            File.AppendAllText(CrashLogPath,
                $"[{DateTime.Now:yyyy-MM-dd HH:mm:ss}] {stage}: {ex}\n\n");
        }
        catch
        {
            // Crash logging must never mask the original failure.
        }
    }
}
