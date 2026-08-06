using System.Collections.ObjectModel;
using MemTextSwap.UI.Models;

namespace MemTextSwap.UI.Services;

public sealed class LogService
{
    private const int MaxEntries = 2000;
    private readonly object _lock = new();
    private readonly string _filePath;

    public ObservableCollection<LogEntry> Entries { get; } = new();

    public event Action<LogEntry>? EntryAdded;

    public LogService()
    {
        string dir = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
            "MemTextSwap", "logs");
        _filePath = Path.Combine(dir, "app.log");
        try
        {
            Directory.CreateDirectory(dir);
        }
        catch
        {
        }
    }

    public void Info(string message) => Add("INFO", message);
    public void Warn(string message) => Add("WARN", message);
    public void Error(string message) => Add("ERROR", message);

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
        System.Diagnostics.Debug.WriteLine($"[{entry.Time:HH:mm:ss}] {level} {message}");
        try
        {
            File.AppendAllText(_filePath,
                $"[{entry.Time:yyyy-MM-dd HH:mm:ss}] {level} {message}{Environment.NewLine}");
        }
        catch
        {
        }
    }
}
