using Microsoft.Data.Sqlite;

namespace GameTextInjector.UI.Services;

public sealed class CacheService
{
    private readonly LogService _log;
    private readonly string _dbPath;
    private readonly SemaphoreSlim _gate = new(1, 1);

    public CacheService(SettingsService settings, LogService log)
    {
        _log = log;
        _dbPath = string.IsNullOrEmpty(settings.Current.CachePath)
            ? Path.Combine(settings.ConfigDir, "cache.db")
            : settings.Current.CachePath;
        Directory.CreateDirectory(Path.GetDirectoryName(_dbPath) ?? settings.ConfigDir);
        Initialize();
    }

    private void Initialize()
    {
        try
        {
            using var connection = Open();
            using var command = connection.CreateCommand();
            command.CommandText = """
                CREATE TABLE IF NOT EXISTS translations (
                    source_hash TEXT PRIMARY KEY,
                    source TEXT NOT NULL,
                    target TEXT NOT NULL,
                    engine INTEGER NOT NULL DEFAULT 0,
                    origin TEXT NOT NULL DEFAULT 'cache'
                        CHECK(origin IN ('cache','dict','ai','manual')),
                    created_at INTEGER NOT NULL,
                    updated_at INTEGER NOT NULL
                );
                """;
            command.ExecuteNonQuery();
        }
        catch (Exception ex)
        {
            _log.Error($"缓存数据库初始化失败: {ex.Message}");
        }
    }

    private SqliteConnection Open()
    {
        var connection = new SqliteConnection($"Data Source={_dbPath}");
        connection.Open();
        return connection;
    }

    public async Task<string?> GetAsync(string source)
    {
        string hash = Fnv1a64.Compute(source).ToString("X16");
        await _gate.WaitAsync();
        try
        {
            using var connection = Open();
            using var command = connection.CreateCommand();
            command.CommandText =
                "SELECT target FROM translations WHERE source_hash = $hash LIMIT 1";
            command.Parameters.AddWithValue("$hash", hash);
            return await command.ExecuteScalarAsync() as string;
        }
        catch (Exception ex)
        {
            _log.Warn($"缓存读取失败: {ex.Message}");
            return null;
        }
        finally
        {
            _gate.Release();
        }
    }

    public async Task PutAsync(string source, string target, uint engine, string origin)
    {
        string hash = Fnv1a64.Compute(source).ToString("X16");
        long now = DateTimeOffset.UtcNow.ToUnixTimeSeconds();
        await _gate.WaitAsync();
        try
        {
            using var connection = Open();
            using var command = connection.CreateCommand();
            command.CommandText = """
                INSERT INTO translations (source_hash, source, target, engine, origin, created_at, updated_at)
                VALUES ($hash, $source, $target, $engine, $origin, $now, $now)
                ON CONFLICT(source_hash) DO UPDATE SET
                    target = excluded.target,
                    origin = excluded.origin,
                    updated_at = excluded.updated_at
                """;
            command.Parameters.AddWithValue("$hash", hash);
            command.Parameters.AddWithValue("$source", source);
            command.Parameters.AddWithValue("$target", target);
            command.Parameters.AddWithValue("$engine", engine);
            command.Parameters.AddWithValue("$origin", origin);
            command.Parameters.AddWithValue("$now", now);
            await command.ExecuteNonQueryAsync();
        }
        catch (Exception ex)
        {
            _log.Warn($"缓存写入失败: {ex.Message}");
        }
        finally
        {
            _gate.Release();
        }
    }

    public async Task<List<(string Source, string Target, string Origin, DateTime Updated)>> ListAsync(
        int limit = 500)
    {
        var result = new List<(string, string, string, DateTime)>();
        await _gate.WaitAsync();
        try
        {
            using var connection = Open();
            using var command = connection.CreateCommand();
            command.CommandText =
                "SELECT source, target, origin, updated_at FROM translations ORDER BY updated_at DESC LIMIT $limit";
            command.Parameters.AddWithValue("$limit", limit);
            using var reader = await command.ExecuteReaderAsync();
            while (await reader.ReadAsync())
            {
                long unix = reader.GetInt64(3);
                result.Add((reader.GetString(0), reader.GetString(1), reader.GetString(2),
                            DateTimeOffset.FromUnixTimeSeconds(unix).LocalDateTime));
            }
        }
        catch (Exception ex)
        {
            _log.Warn($"缓存列表读取失败: {ex.Message}");
        }
        finally
        {
            _gate.Release();
        }
        return result;
    }

    public async Task<int> ClearAsync()
    {
        await _gate.WaitAsync();
        try
        {
            using var connection = Open();
            using var command = connection.CreateCommand();
            command.CommandText = "DELETE FROM translations";
            int deleted = await command.ExecuteNonQueryAsync();
            _log.Info($"缓存已清空（{deleted} 条）");
            return deleted;
        }
        catch (Exception ex)
        {
            _log.Warn($"缓存清空失败: {ex.Message}");
            return 0;
        }
        finally
        {
            _gate.Release();
        }
    }
}
