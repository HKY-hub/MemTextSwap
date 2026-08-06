using System.Diagnostics;
using System.Text.Json;

namespace MemTextSwap.UI.Services;

public sealed record InjectResult(bool Ok, int Code, string Error);

public sealed class InjectorService
{
    private readonly LogService _log;

    public InjectorService(LogService log)
    {
        _log = log;
    }

    public async Task<InjectResult> InjectAsync(int pid, bool is64Bit, string pipeName)
    {
        string arch = is64Bit ? "64" : "32";
        string? dll = FindArtifact($"NativeCore{arch}.dll");
        string? injector = FindArtifact($"Injector{arch}.exe");
        if (dll is null || injector is null)
        {
            return new InjectResult(false, 20, "native artifacts not found");
        }

        var startInfo = new ProcessStartInfo
        {
            FileName = injector,
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = true,
        };
        startInfo.ArgumentList.Add("--pid");
        startInfo.ArgumentList.Add(pid.ToString());
        startInfo.ArgumentList.Add("--dll");
        startInfo.ArgumentList.Add(dll);
        startInfo.ArgumentList.Add("--pipe");
        startInfo.ArgumentList.Add(pipeName);

        _log.Info($"注入: pid={pid} arch={arch} dll={dll}");
        try
        {
            using var process = Process.Start(startInfo);
            if (process is null)
            {
                return new InjectResult(false, 21, "injector start failed");
            }
            string output = await process.StandardOutput.ReadToEndAsync();
            string error = await process.StandardError.ReadToEndAsync();
            await process.WaitForExitAsync();
            if (!string.IsNullOrEmpty(error))
            {
                _log.Warn($"注入器 stderr: {error.Trim()}");
            }
            foreach (string line in output.Split('\n', StringSplitOptions.RemoveEmptyEntries))
            {
                _log.Info($"注入器: {line.Trim()}");
            }
            return ParseResult(output);
        }
        catch (Exception ex)
        {
            _log.Error($"注入失败: {ex.Message}");
            return new InjectResult(false, 22, ex.Message);
        }
    }

    private static InjectResult ParseResult(string output)
    {
        string? line = output.Split('\n', StringSplitOptions.RemoveEmptyEntries)
            .Select(l => l.Trim())
            .LastOrDefault(l => l.StartsWith('{'));
        if (line is null)
        {
            return new InjectResult(false, 23, "injector produced no result");
        }
        try
        {
            using var document = JsonDocument.Parse(line);
            bool ok = document.RootElement.TryGetProperty("ok", out var okValue) &&
                      okValue.GetBoolean();
            int code = document.RootElement.TryGetProperty("code", out var codeValue)
                ? codeValue.GetInt32()
                : 0;
            string error = document.RootElement.TryGetProperty("error", out var errorValue)
                ? errorValue.GetString() ?? ""
                : "";
            return new InjectResult(ok, code, error);
        }
        catch (Exception ex)
        {
            return new InjectResult(false, 24, $"result parse failed: {ex.Message}");
        }
    }

    private static string? FindArtifact(string fileName)
    {
        string local = Path.Combine(AppContext.BaseDirectory, fileName);
        if (File.Exists(local))
        {
            return local;
        }
        // Dev fallback: search upward for build/<arch>/src/NativeCore/Release/<fileName>.
        DirectoryInfo? dir = new DirectoryInfo(AppContext.BaseDirectory);
        for (int i = 0; i < 10 && dir is not null; i++)
        {
            string candidate = Path.Combine(dir.FullName, "build", "x64", "src", "NativeCore",
                                            "Release", fileName);
            if (File.Exists(candidate))
            {
                return candidate;
            }
            candidate = Path.Combine(dir.FullName, "build", "x86", "src", "NativeCore",
                                     "Release", fileName);
            if (File.Exists(candidate))
            {
                return candidate;
            }
            dir = dir.Parent;
        }
        return null;
    }
}
