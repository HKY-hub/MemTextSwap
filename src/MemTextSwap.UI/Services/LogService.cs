using System.Collections.ObjectModel;
using System.Diagnostics;
using MemTextSwap.UI.Models;

namespace MemTextSwap.UI.Services;

public sealed class LogService
{
    private const int MaxEntries = 5000;
    private const long MaxFileBytes = 4 * 1024 * 1024;
    private readonly object _lock = new();
    private readonly string _filePath;

    public ObservableCollection<LogEntry> Entries { get; } = new();
    public string LogFilePath { get; }
    public string LogsDirectory { get; }

    public event Action<LogEntry>? EntryAdded;

    public LogService()
    {
        LogsDirectory = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
            "MemTextSwap", "logs");
        Directory.CreateDirectory(LogsDirectory);
        // 只保留最新日志：每次启动清空上一轮的日志文件。
        RotateToLatestOnly();
        _filePath = Path.Combine(LogsDirectory, "app.log");
        LogFilePath = _filePath;
        Info("========== MemTextSwap 启动 ==========");
    }

    public void Debug(string message) => Add("DEBUG", message);
    public void Info(string message) => Add("INFO", message);
    public void Warn(string message) => Add("WARN", message);
    public void Error(string message) => Add("ERROR", message);
    public void Error(string context, Exception ex) => Add("ERROR", $"{context}: {ex}");

    public void Add(string level, string message)
    {
        var entry = new LogEntry { Level = level, Message = message };
        lock (_lock)
        {
            if (Entries.Count >= MaxEntries)
            {
                Entries.RemoveAt(0);
            }
            Entries.Add(entry);
        }
        EntryAdded?.Invoke(entry);
        System.Diagnostics.Debug.WriteLine($"[{entry.Time:HH:mm:ss.fff}] {level} {message}");
        try
        {
            if (File.Exists(_filePath) && new FileInfo(_filePath).Length > MaxFileBytes)
            {
                File.WriteAllText(_filePath, "");  // 超限清空，只保留最新内容
            }
            File.AppendAllText(_filePath,
                $"[{entry.Time:yyyy-MM-dd HH:mm:ss.fff}] [{Environment.CurrentManagedThreadId:D3}] {level} {message}{Environment.NewLine}");
        }
        catch
        {
            // 日志写入失败不应影响主流程
        }
    }

    public void ExportTo(string path)
    {
        List<LogEntry> snapshot;
        lock (_lock)
        {
            snapshot = Entries.ToList();
        }
        var lines = snapshot.Select(e =>
            $"[{e.Time:yyyy-MM-dd HH:mm:ss.fff}] {e.Level} {e.Message}");
        File.WriteAllLines(path, lines);
    }

    private void RotateToLatestOnly()
    {
        try
        {
            foreach (string pattern in new[] { "app*.log", "crash*.log", "native*.log" })
            {
                foreach (string file in Directory.GetFiles(LogsDirectory, pattern))
                {
                    File.Delete(file);
                }
            }
        }
        catch
        {
        }
    }
}
