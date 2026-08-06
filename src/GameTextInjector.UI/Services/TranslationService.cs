using GameTextInjector.UI.Models;

namespace GameTextInjector.UI.Services;

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

        string? cached = await _cache.GetAsync(source);
        if (!string.IsNullOrEmpty(cached))
        {
            return new TranslationResult(cached, "cache");
        }

        string? dict = _dictionary.Lookup(source);
        if (!string.IsNullOrEmpty(dict))
        {
            await _cache.PutAsync(source, dict, engine, "dict");
            return new TranslationResult(dict, "dict");
        }

        if (_settings.Current.TranslationMode.Equals("Ai", StringComparison.OrdinalIgnoreCase))
        {
            await _aiGate.WaitAsync(ct);
            try
            {
                string? target = await _ai.TranslateAsync(source, ct);
                if (!string.IsNullOrEmpty(target))
                {
                    await _cache.PutAsync(source, target, engine, "ai");
                    return new TranslationResult(target, "ai");
                }
            }
            finally
            {
                _aiGate.Release();
            }
        }
        return new TranslationResult("", "none");
    }
}
