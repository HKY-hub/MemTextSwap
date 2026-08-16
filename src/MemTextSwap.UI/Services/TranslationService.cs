using MemTextSwap.UI.Models;

namespace MemTextSwap.UI.Services;

public sealed class TranslationService
{
    private readonly CacheService _cache;
    private readonly DictionaryService _dictionary;
    private readonly AIService _ai;
    private readonly SettingsService _settings;
    private readonly LogService _log;
    private readonly SemaphoreSlim _aiGate;

    public TranslationService(CacheService cache, DictionaryService dictionary, AIService ai,
                              SettingsService settings, LogService log)
    {
        _cache = cache;
        _dictionary = dictionary;
        _ai = ai;
        _settings = settings;
        _log = log;
        int concurrency = Math.Max(1, settings.Current.Ai.Concurrency);
        _aiGate = new SemaphoreSlim(concurrency, concurrency);
    }

    // Pipeline: cache -> dictionary -> AI -> write cache.
    public async Task<TranslationResult> TranslateAsync(string source, uint engine,
                                                        CancellationToken ct = default)
    {
        if (string.IsNullOrEmpty(source))
        {
            return new TranslationResult("", "none");
        }
        var started = DateTime.UtcNow;
        string preview = source.Length > 200 ? source[..200] + "…" : source;
        _log.Debug($"翻译管线开始 engine={engine} source={preview}");

        string? cached = await _cache.GetAsync(source);
        if (!string.IsNullOrEmpty(cached))
        {
            _log.Debug($"翻译管线命中缓存 source={preview} -> {cached}");
            return new TranslationResult(cached, "cache");
        }

        string? dict = _dictionary.Lookup(source);
        if (!string.IsNullOrEmpty(dict))
        {
            await _cache.PutAsync(source, dict, engine, "dict");
            _log.Debug($"翻译管线命中词典 source={preview} -> {dict}");
            return new TranslationResult(dict, "dict");
        }

        if (_settings.Current.TranslationMode.Equals("Ai", StringComparison.OrdinalIgnoreCase))
        {
            _log.Debug($"翻译管线进入 AI source={preview}");
            await _aiGate.WaitAsync(ct);
            try
            {
                string? target = await _ai.TranslateAsync(source, ct);
                if (!string.IsNullOrEmpty(target))
                {
                    await _cache.PutAsync(source, target, engine, "ai");
                    _log.Debug($"翻译管线 AI 完成 source={preview} -> {target}");
                    return new TranslationResult(target, "ai");
                }
                _log.Warn($"翻译管线 AI 未返回有效译文 source={preview}");
            }
            finally
            {
                _aiGate.Release();
            }
        }
        var elapsed = DateTime.UtcNow - started;
        _log.Warn($"翻译管线未命中任何来源（cache/dict/ai） source={preview} elapsed={elapsed.TotalMilliseconds:F0}ms mode={_settings.Current.TranslationMode}");
        return new TranslationResult("", "none");
    }
}
