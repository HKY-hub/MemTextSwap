namespace GameTextInjector.UI.Models;

public sealed record TranslationResult(string Target, string Origin)
{
    public bool IsSuccess => !string.IsNullOrEmpty(Target);
}
