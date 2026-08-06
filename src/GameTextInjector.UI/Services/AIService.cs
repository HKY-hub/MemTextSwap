using System.Net.Http.Headers;
using System.Text;
using System.Text.Json;
using GameTextInjector.UI.Models;

namespace GameTextInjector.UI.Services;

public sealed class AIService
{
    public const string SystemPrompt =
        "你是游戏汉化翻译引擎。只输出最终简体中文译文。" +
        "禁止任何解释、禁止markdown、禁止符号、禁止换行、禁止注释、禁止举例。" +
        "只返回纯翻译文本，不返回原文、不返回分析、不返回多余内容。";

    private readonly SettingsService _settings;
    private readonly LogService _log;
    private readonly HttpClient _http;

    public AIService(SettingsService settings, LogService log)
    {
        _settings = settings;
        _log = log;
        _http = new HttpClient { Timeout = TimeSpan.FromMilliseconds(settings.Current.Ai.TimeoutMs) };
    }

    public bool IsConfigured =>
        !string.IsNullOrWhiteSpace(ApiKey) && !string.IsNullOrWhiteSpace(_settings.Current.Ai.BaseUrl);

    private string ApiKey
    {
        get
        {
            string? env = Environment.GetEnvironmentVariable("GTI_DEEPSEEK_API_KEY");
            return string.IsNullOrWhiteSpace(env) ? _settings.Current.Ai.ApiKey : env;
        }
    }

    public async Task<string?> TranslateAsync(string source, CancellationToken ct = default)
    {
        if (!IsConfigured)
        {
            return null;
        }
        var ai = _settings.Current.Ai;
        for (int attempt = 0; attempt <= ai.Retries; attempt++)
        {
            try
            {
                string content = await CallAsync(source, ct);
                string cleaned = Sanitize(content);
                if (!string.IsNullOrWhiteSpace(cleaned) && cleaned != source)
                {
                    return cleaned;
                }
            }
            catch (Exception ex)
            {
                _log.Warn($"AI 翻译请求失败（第 {attempt + 1} 次）: {ex.Message}");
            }
        }
        return null;
    }

    private async Task<string> CallAsync(string source, CancellationToken ct)
    {
        var ai = _settings.Current.Ai;
        var request = new
        {
            model = ai.Model,
            temperature = 0.2,
            messages = new[]
            {
                new { role = "system", content = SystemPrompt },
                new { role = "user", content = source },
            },
        };
        using var message = new HttpRequestMessage(HttpMethod.Post,
            $"{ai.BaseUrl.TrimEnd('/')}/chat/completions");
        message.Headers.Authorization = new AuthenticationHeaderValue("Bearer", ApiKey);
        message.Content = new StringContent(JsonSerializer.Serialize(request), Encoding.UTF8,
            "application/json");
        using var response = await _http.SendAsync(message, ct);
        response.EnsureSuccessStatusCode();
        using var document = JsonDocument.Parse(await response.Content.ReadAsStringAsync(ct));
        return document.RootElement
            .GetProperty("choices")[0]
            .GetProperty("message")
            .GetProperty("content")
            .GetString() ?? "";
    }

    public static string Sanitize(string raw)
    {
        if (string.IsNullOrEmpty(raw))
        {
            return "";
        }
        string text = raw;
        // Remove markdown fenced blocks while keeping their inner content.
        while (true)
        {
            int start = text.IndexOf("```", StringComparison.Ordinal);
            if (start < 0)
            {
                break;
            }
            int end = text.IndexOf("```", start + 3, StringComparison.Ordinal);
            if (end < 0)
            {
                text = text[..start];
                break;
            }
            int contentStart = start + 3;
            int newline = text.IndexOf('\n', contentStart);
            if (newline >= 0 && newline < end)
            {
                contentStart = newline + 1;
            }
            text = text[..start] + text[contentStart..end] + text[(end + 3)..];
        }
        text = text.Replace("\r", " ").Replace("\n", " ").Replace("\t", " ");
        text = string.Join(" ", text.Split(' ', StringSplitOptions.RemoveEmptyEntries));
        text = text.Trim().Trim('"', '\'', '`', '，', '。', '！', '？', ';', ':', '.', ',', '!', '?');
        return text;
    }
}
