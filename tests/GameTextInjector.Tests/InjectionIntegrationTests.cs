using System.Collections.Concurrent;
using System.Diagnostics;
using GameTextInjector.UI.Services;
using Xunit;

namespace GameTextInjector.Tests;

public class InjectionIntegrationTests : IDisposable
{
    private readonly string _tempDir = Path.Combine(Path.GetTempPath(),
        $"gti-it-{Guid.NewGuid():N}");

    public InjectionIntegrationTests()
    {
        Directory.CreateDirectory(_tempDir);
    }

    public void Dispose()
    {
        if (Environment.GetEnvironmentVariable("GTI_KEEP_TEMP") == "1")
        {
            return;
        }
        try
        {
            Directory.Delete(_tempDir, true);
        }
        catch
        {
        }
    }

    [Theory]
    [InlineData("64")]
    [InlineData("32")]
    [Trait("Category", "Integration")]
    public async Task EndToEnd_InjectTranslateUnload(string arch)
    {
        if (Environment.GetEnvironmentVariable("GTI_TEST_ARCH") is string onlyArch &&
            onlyArch != arch)
        {
            return;
        }
        string artifacts = FindArtifactsDir(arch);
        string targetExe = Path.Combine(artifacts, $"TestTarget{arch}.exe");
        string injectorExe = Path.Combine(artifacts, $"Injector{arch}.exe");
        string nativeDll = Path.Combine(artifacts, $"NativeCore{arch}.dll");
        Assert.True(File.Exists(targetExe), $"missing {targetExe}");
        Assert.True(File.Exists(injectorExe), $"missing {injectorExe}");
        Assert.True(File.Exists(nativeDll), $"missing {nativeDll}");

        string dictPath = Path.Combine(_tempDir, "dict.txt");
        await File.WriteAllLinesAsync(dictPath,
            Enumerable.Range(1, 40).Select(i => $"Hello World #{i}=你好世界 #{i}"));

        var log = new LogService();
        var settings = new SettingsService(log);
        settings.Load();
        settings.Current.CachePath = Path.Combine(_tempDir, "cache.db");
        var cache = new CacheService(settings, log);
        var dictionary = new DictionaryService(log);
        dictionary.ImportFile(dictPath);
        var ai = new AIService(settings, log);
        var translation = new TranslationService(cache, dictionary, ai, settings, log);
        var pipeServer = new PipeServer(translation, log);

        var lines = new ConcurrentQueue<string>();
        var targetInfo = new ProcessStartInfo(targetExe)
        {
            RedirectStandardOutput = true,
            UseShellExecute = false,
            CreateNoWindow = true,
            StandardOutputEncoding = new System.Text.UTF8Encoding(false),
        };
        targetInfo.Environment["GTI_LOG_FILE"] = Path.Combine(_tempDir, "native.log");
        targetInfo.ArgumentList.Add("--lines");
        targetInfo.ArgumentList.Add("80");
        using var target = Process.Start(targetInfo);
        Assert.NotNull(target);
        target.OutputDataReceived += (_, e) =>
        {
            if (!string.IsNullOrEmpty(e.Data))
            {
                lines.Enqueue(e.Data);
            }
        };
        target.BeginOutputReadLine();

        int pid = await WaitForReadyAsync(lines, TimeSpan.FromSeconds(15));
        Assert.True(pid > 0, "test target did not report readiness");

        var session = pipeServer.CreateSession(pid);
        var injectorInfo = new ProcessStartInfo(injectorExe)
        {
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false,
            CreateNoWindow = true,
        };
        injectorInfo.ArgumentList.Add("--pid");
        injectorInfo.ArgumentList.Add(pid.ToString());
        injectorInfo.ArgumentList.Add("--dll");
        injectorInfo.ArgumentList.Add(nativeDll);
        injectorInfo.ArgumentList.Add("--pipe");
        injectorInfo.ArgumentList.Add(session.PipeName);

        using var injector = Process.Start(injectorInfo);
        Assert.NotNull(injector);
        string injectorOut = await injector.StandardOutput.ReadToEndAsync();
        string injectorErr = await injector.StandardError.ReadToEndAsync();
        await injector.WaitForExitAsync();
        Assert.Contains("\"ok\":true", injectorOut);
        Assert.True(string.IsNullOrEmpty(injectorErr), injectorErr);

        try
        {
            await WaitForLineAsync(lines, "你好世界 #", TimeSpan.FromSeconds(20));
        }
        catch (Exception)
        {
            string logs = string.Join("\n", log.Entries.Select(e => $"{e.Level}: {e.Message}"));
            string tail = string.Join("\n", lines.TakeLast(10));
            string nativeLog = File.Exists(Path.Combine(_tempDir, "native.log"))
                ? string.Join("\n", File.ReadAllLines(Path.Combine(_tempDir, "native.log")).TakeLast(30))
                : "(无)";
            Assert.Fail($"翻译回显超时。\n注入器输出: {injectorOut}\nUI日志:\n{logs}\nNative日志:\n{nativeLog}\n目标输出尾部:\n{tail}");
        }

        // Unload: subsequent lines must revert to English and the process must survive.
        int snapshot = lines.Count;
        session.SendUnload();
        await WaitForNewLineAsync(lines, snapshot, "Hello World #", TimeSpan.FromSeconds(15));
        Assert.False(target.HasExited, "target process must stay alive after unload");
        await Task.Delay(1000);
        Assert.False(target.HasExited);

        try
        {
            target.Kill(true);
        }
        catch
        {
        }
    }

    private static async Task<int> WaitForReadyAsync(ConcurrentQueue<string> lines,
                                                     TimeSpan timeout)
    {
        var deadline = DateTime.UtcNow + timeout;
        while (DateTime.UtcNow < deadline)
        {
            foreach (string line in lines)
            {
                if (line.StartsWith("[GTI-READY] pid=", StringComparison.Ordinal))
                {
                    string pidText = line["[GTI-READY] pid=".Length..]
                        .Split(' ', StringSplitOptions.RemoveEmptyEntries)[0];
                    if (int.TryParse(pidText, out int pid))
                    {
                        return pid;
                    }
                }
            }
            await Task.Delay(100);
        }
        return -1;
    }

    private static async Task WaitForLineAsync(ConcurrentQueue<string> lines, string contains,
                                               TimeSpan timeout)
    {
        var deadline = DateTime.UtcNow + timeout;
        while (DateTime.UtcNow < deadline)
        {
            if (lines.Any(l => l.Contains(contains, StringComparison.Ordinal)))
            {
                return;
            }
            await Task.Delay(100);
        }
        Assert.Fail($"timed out waiting for line containing '{contains}'");
    }

    private static async Task WaitForNewLineAsync(ConcurrentQueue<string> lines, int snapshot,
                                                  string contains, TimeSpan timeout)
    {
        var deadline = DateTime.UtcNow + timeout;
        while (DateTime.UtcNow < deadline)
        {
            if (lines.Skip(snapshot).Any(l => l.Contains(contains, StringComparison.Ordinal)))
            {
                return;
            }
            await Task.Delay(100);
        }
        Assert.Fail($"timed out waiting for new line containing '{contains}'");
    }

    private static string FindArtifactsDir(string arch)
    {
        DirectoryInfo? dir = new DirectoryInfo(AppContext.BaseDirectory);
        for (int i = 0; i < 12 && dir is not null; i++)
        {
            string candidate = Path.Combine(dir.FullName, "build", arch == "64" ? "x64" : "x86",
                "src", "NativeCore", "Release");
            if (Directory.Exists(candidate))
            {
                return candidate;
            }
            dir = dir.Parent;
        }
        throw new InvalidOperationException("native artifacts not found; run build.ps1 first");
    }
}
