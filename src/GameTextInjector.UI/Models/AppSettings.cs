namespace GameTextInjector.UI.Models;

public sealed class AiSettings
{
    public string BaseUrl { get; set; } = "https://api.deepseek.com/v1";
    public string Model { get; set; } = "deepseek-chat";
    public string ApiKey { get; set; } = "";
    public int TimeoutMs { get; set; } = 15000;
    public int Concurrency { get; set; } = 2;
    public int Retries { get; set; } = 2;
}

public sealed class FilterSettings
{
    public int MinLen { get; set; } = 1;
    public int MaxLen { get; set; } = 1024;
    public bool SkipCjk { get; set; } = true;
    public bool SkipPaths { get; set; } = true;
    public int RateLimitPerSec { get; set; } = 200;
}

public sealed class AppSettings
{
    public string TranslationMode { get; set; } = "Local"; // Local | Ai
    public int BlockTimeoutMs { get; set; } = 3000;
    public AiSettings Ai { get; set; } = new();
    public FilterSettings Filters { get; set; } = new();
    public List<string> DictionaryPaths { get; set; } = new();
    public string CachePath { get; set; } = "";
}
