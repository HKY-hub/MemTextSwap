using GameTextInjector.UI.Services;
using Xunit;

namespace GameTextInjector.Tests;

public class EngineDetectorTests : IDisposable
{
    private readonly string _dir = Path.Combine(Path.GetTempPath(),
        $"gti-engine-{Guid.NewGuid():N}");

    public EngineDetectorTests()
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

    private void Touch(params string[] relativePaths)
    {
        foreach (string relative in relativePaths)
        {
            string path = Path.Combine(_dir, relative);
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            File.WriteAllText(path, "");
        }
    }

    [Fact]
    public void UnityIl2Cpp()
    {
        Touch("GameAssembly.dll", "UnityPlayer.dll", "game.exe");
        Assert.Equal("Unity IL2CPP",
            EngineDetector.DetectFromExe(Path.Combine(_dir, "game.exe")));
    }

    [Fact]
    public void UnityMono()
    {
        Touch("UnityPlayer.dll", "mono-2.0-bdwgc.dll", "game.exe");
        Assert.Equal("Unity Mono",
            EngineDetector.DetectFromExe(Path.Combine(_dir, "game.exe")));
    }

    [Fact]
    public void RenPy()
    {
        Directory.CreateDirectory(Path.Combine(_dir, "renpy"));
        Touch("game.exe");
        Assert.Equal("RenPy", EngineDetector.DetectFromExe(Path.Combine(_dir, "game.exe")));
    }

    [Fact]
    public void RpgMakerMvAndMz()
    {
        Touch("nw.dll", "www/js/rpg_core.js", "game.exe");
        Assert.Equal("RPGMaker MV",
            EngineDetector.DetectFromExe(Path.Combine(_dir, "game.exe")));

        string mz = Path.Combine(_dir, "mz");
        Directory.CreateDirectory(Path.Combine(mz, "www", "js"));
        File.WriteAllText(Path.Combine(mz, "nw.dll"), "");
        File.WriteAllText(Path.Combine(mz, "www", "js", "rmmz_core.js"), "");
        Assert.Equal("RPGMaker MZ",
            EngineDetector.DetectFromExe(Path.Combine(mz, "game.exe")));
    }

    [Fact]
    public void TestTarget()
    {
        Touch("TestTarget64.exe");
        Assert.Equal("TestTarget",
            EngineDetector.DetectFromExe(Path.Combine(_dir, "TestTarget64.exe")));
    }
}
