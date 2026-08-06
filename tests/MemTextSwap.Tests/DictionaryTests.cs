using MemTextSwap.UI.Services;
using Xunit;

namespace MemTextSwap.Tests;

public class DictionaryTests : IDisposable
{
    private readonly string _dir = Path.Combine(Path.GetTempPath(),
        $"gti-dict-{Guid.NewGuid():N}");

    public DictionaryTests()
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

    [Fact]
    public void FlatFile_ParsesAutoTranslatorStyle()
    {
        string path = Path.Combine(_dir, "dict.txt");
        File.WriteAllText(path, """
            # comment
            ; another comment
            [SomeSection]
            Hello World=你好世界
            key with space = value with space
            """);
        var service = new DictionaryService(new LogService());
        service.ImportFile(path);
        Assert.Equal(2, service.Count);
        Assert.Equal("你好世界", service.Lookup("Hello World"));
        Assert.Equal("value with space", service.Lookup("key with space"));
        Assert.Null(service.Lookup("missing"));
    }

    [Fact]
    public void JsonFile_ParsesObjectAndArray()
    {
        string objectPath = Path.Combine(_dir, "dict.json");
        File.WriteAllText(objectPath, """{"Hello":"你好","World":"世界"}""");
        var service = new DictionaryService(new LogService());
        service.ImportFile(objectPath);
        Assert.Equal("你好", service.Lookup("Hello"));
        Assert.Equal("世界", service.Lookup("World"));

        string arrayPath = Path.Combine(_dir, "array.json");
        File.WriteAllText(arrayPath,
            """[{"source":"A","target":"甲"},{"source":"B","target":"乙"}]""");
        service.ImportFile(arrayPath);
        Assert.Equal("甲", service.Lookup("A"));
        Assert.Equal("乙", service.Lookup("B"));
    }

    [Fact]
    public void Export_RoundTrips()
    {
        string path = Path.Combine(_dir, "dict.txt");
        File.WriteAllText(path, "Hello World=你好世界\n");
        var service = new DictionaryService(new LogService());
        service.ImportFile(path);
        string export = Path.Combine(_dir, "export.txt");
        service.ExportFile(export);
        var reloaded = new DictionaryService(new LogService());
        reloaded.ImportFile(export);
        Assert.Equal("你好世界", reloaded.Lookup("Hello World"));
    }
}
