using System.Text.Json;
using MemTextSwap.UI.Models;

namespace MemTextSwap.UI.Services;

public sealed class SettingsService
{
    private readonly LogService _log;
    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        WriteIndented = true,
    };

    public AppSettings Current { get; private set; } = new();
    public string ConfigDir { get; }
    public string ConfigPath { get; }

    public SettingsService(LogService log)
    {
        _log = log;
        ConfigDir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
                                 "MemTextSwap");
        ConfigPath = Path.Combine(ConfigDir, "config.json");
    }

    public void Load()
    {
        try
        {
            if (File.Exists(ConfigPath))
            {
                var loaded = JsonSerializer.Deserialize<AppSettings>(File.ReadAllText(ConfigPath), JsonOptions);
                if (loaded is not null)
                {
                    Current = loaded;
                }
            }
            if (string.IsNullOrEmpty(Current.CachePath))
            {
                Current.CachePath = Path.Combine(ConfigDir, "cache.db");
            }
        }
        catch (Exception ex)
        {
            _log.Warn($"配置文件读取失败，使用默认配置: {ex.Message}");
        }
    }

    public void Save()
    {
        try
        {
            Directory.CreateDirectory(ConfigDir);
            File.WriteAllText(ConfigPath, JsonSerializer.Serialize(Current, JsonOptions));
            _log.Info("配置已保存");
        }
        catch (Exception ex)
        {
            _log.Error($"配置保存失败: {ex.Message}");
        }
    }
}
