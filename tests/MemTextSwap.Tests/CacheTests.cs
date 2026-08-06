using MemTextSwap.UI.Models;
using MemTextSwap.UI.Services;
using Xunit;

namespace MemTextSwap.Tests;

public class CacheTests : IDisposable
{
    private readonly string _dir = Path.Combine(Path.GetTempPath(),
        $"gti-cache-{Guid.NewGuid():N}");

    public CacheTests()
    {
        Directory.CreateDirectory(_dir);
    }

    public void Dispose()
    {
        try
        {
            Directory.Delete(_dir, true);
        }
        catch
        {
        }
    }

    private (CacheService, SettingsService) Create()
    {
        var log = new LogService();
        var settings = new SettingsService(log);
        settings.Current.CachePath = Path.Combine(_dir, "cache.db");
        return (new CacheService(settings, log), settings);
    }

    [Fact]
    public async Task PutAndGet_RoundTrip()
    {
        (var cache, _) = Create();
        Assert.Null(await cache.GetAsync("Hello"));
        await cache.PutAsync("Hello", "你好", 2, "dict");
        Assert.Equal("你好", await cache.GetAsync("Hello"));
    }

    [Fact]
    public async Task Put_UpdatesExisting()
    {
        (var cache, _) = Create();
        await cache.PutAsync("Hello", "你好", 2, "dict");
        await cache.PutAsync("Hello", "您好", 2, "ai");
        Assert.Equal("您好", await cache.GetAsync("Hello"));
    }

    [Fact]
    public async Task ListAndClear()
    {
        (var cache, _) = Create();
        await cache.PutAsync("A", "甲", 2, "dict");
        await cache.PutAsync("B", "乙", 4, "ai");
        var list = await cache.ListAsync();
        Assert.Equal(2, list.Count);
        int deleted = await cache.ClearAsync();
        Assert.Equal(2, deleted);
        Assert.Empty(await cache.ListAsync());
    }
}
