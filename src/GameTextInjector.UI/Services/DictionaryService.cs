using System.Text;
using System.Text.Json;
using GameTextInjector.UI.Models;

namespace GameTextInjector.UI.Services;

public sealed class DictionaryService
{
    private readonly LogService _log;
    private readonly Dictionary<string, string> _entries = new();
    private readonly object _lock = new();

    public DictionaryService(LogService log)
    {
        _log = log;
    }

    public int Count
    {
        get
        {
            lock (_lock)
            {
                return _entries.Count;
            }
        }
    }

    public void LoadAll(IEnumerable<string> paths)
    {
        lock (_lock)
        {
            _entries.Clear();
            foreach (string path in paths)
            {
                if (!File.Exists(path))
                {
                    continue;
                }
                try
                {
                    if (path.EndsWith(".json", StringComparison.OrdinalIgnoreCase))
                    {
                        LoadJson(path);
                    }
                    else
                    {
                        LoadFlat(path);
                    }
                    _log.Info($"词典已加载: {path}");
                }
                catch (Exception ex)
                {
                    _log.Warn($"词典加载失败 {path}: {ex.Message}");
                }
            }
        }
    }

    public string? Lookup(string source)
    {
        lock (_lock)
        {
            return _entries.TryGetValue(source, out string? value) ? value : null;
        }
    }

    public IReadOnlyDictionary<string, string> Snapshot()
    {
        lock (_lock)
        {
            return new Dictionary<string, string>(_entries);
        }
    }

    public void ImportFile(string path)
    {
        lock (_lock)
        {
            if (path.EndsWith(".json", StringComparison.OrdinalIgnoreCase))
            {
                LoadJson(path);
            }
            else
            {
                LoadFlat(path);
            }
        }
        _log.Info($"词典已导入: {path}");
    }

    public void ExportFile(string path)
    {
        var snapshot = Snapshot();
        var builder = new StringBuilder();
        foreach (var pair in snapshot.OrderBy(p => p.Key, StringComparer.Ordinal))
        {
            builder.Append(pair.Key).Append('=').AppendLine(pair.Value);
        }
        File.WriteAllText(path, builder.ToString(), new UTF8Encoding(false));
        _log.Info($"词典已导出: {path}（{snapshot.Count} 条）");
    }

    private void LoadJson(string path)
    {
        using var document = JsonDocument.Parse(File.ReadAllText(path));
        if (document.RootElement.ValueKind == JsonValueKind.Object)
        {
            foreach (var property in document.RootElement.EnumerateObject())
            {
                if (property.Value.ValueKind == JsonValueKind.String)
                {
                    _entries[property.Name] = property.Value.GetString() ?? "";
                }
            }
        }
        else if (document.RootElement.ValueKind == JsonValueKind.Array)
        {
            foreach (var item in document.RootElement.EnumerateArray())
            {
                if (item.ValueKind == JsonValueKind.Object &&
                    item.TryGetProperty("source", out var source) &&
                    item.TryGetProperty("target", out var target) &&
                    source.ValueKind == JsonValueKind.String &&
                    target.ValueKind == JsonValueKind.String)
                {
                    _entries[source.GetString() ?? ""] = target.GetString() ?? "";
                }
            }
        }
    }

    private void LoadFlat(string path)
    {
        foreach (string raw in File.ReadAllLines(path, new UTF8Encoding(false)))
        {
            string line = raw.Trim();
            if (line.Length == 0 || line.StartsWith('#') || line.StartsWith(';'))
            {
                continue;
            }
            if (line.StartsWith('[') && line.EndsWith(']'))
            {
                continue;  // AutoTranslator section headers are ignored
            }
            int separator = line.IndexOf('=');
            if (separator <= 0)
            {
                continue;
            }
            string key = line[..separator].Trim();
            string value = line[(separator + 1)..].Trim();
            if (key.Length > 0 && value.Length > 0)
            {
                _entries[key] = value;
            }
        }
    }
}
