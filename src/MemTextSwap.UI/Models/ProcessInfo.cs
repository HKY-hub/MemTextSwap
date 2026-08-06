namespace MemTextSwap.UI.Models;

public sealed class ProcessInfo
{
    public int Pid { get; init; }
    public string Name { get; init; } = "";
    public string ExePath { get; init; } = "";
    public bool Is64Bit { get; init; }
    public string Engine { get; init; } = "未知";
    public bool Injected { get; set; }

    public string Bitness => Is64Bit ? "x64" : "x86";
}
