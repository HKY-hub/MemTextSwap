namespace MemTextSwap.UI.Models;

public sealed record TranslationResult(string Target, string Origin)
{
    public bool IsSuccess => !string.IsNullOrEmpty(Target);
}
