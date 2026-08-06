using System.Collections.ObjectModel;
using MemTextSwap.UI.Models;

namespace MemTextSwap.UI.Services;

public sealed class LogService
{
    private const int MaxEntries = 2000;
    private readonly object _lock = new();

    public ObservableCollection<LogEntry> Entries { get; } = new();

    public event Action<LogEntry>? EntryAdded;

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
    }
}
