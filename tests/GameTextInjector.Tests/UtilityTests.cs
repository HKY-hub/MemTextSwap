using GameTextInjector.UI.Services;
using Xunit;

namespace GameTextInjector.Tests;

public class UtilityTests
{
    [Fact]
    public void Fnv1a64_KnownVectors()
    {
        Assert.Equal(14695981039346656037UL, Fnv1a64.Compute(""));
        Assert.Equal(10291341955925813465UL, Fnv1a64.Compute("Hello World #1"));
    }

    [Theory]
    [InlineData("```\n你好世界\n```", "你好世界")]
    [InlineData("你好世界\r\n第二行", "你好世界 第二行")]
    [InlineData("  \"你好世界\"  ", "你好世界")]
    [InlineData("```json\n{\"a\":1}\n```", "{\"a\":1}")]
    public void Sanitize_StripsMarkdownAndNoise(string raw, string expected)
    {
        Assert.Equal(expected, AIService.Sanitize(raw));
    }

    [Fact]
    public void Sanitize_Empty()
    {
        Assert.Equal("", AIService.Sanitize(""));
        Assert.Equal("", AIService.Sanitize("``` ```"));
    }
}
