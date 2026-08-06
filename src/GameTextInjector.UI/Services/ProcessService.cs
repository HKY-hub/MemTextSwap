using System.Diagnostics;
using System.Runtime.InteropServices;
using GameTextInjector.UI.Models;

namespace GameTextInjector.UI.Services;

public sealed class ProcessService
{
    private readonly LogService _log;

    public ProcessService(LogService log)
    {
        _log = log;
    }

    public IReadOnlyList<ProcessInfo> ListGames()
    {
        var result = new List<ProcessInfo>();
        foreach (Process process in Process.GetProcesses())
        {
            try
            {
                if (process.HasExited)
                {
                    continue;
                }
                string exePath = process.MainModule?.FileName ?? "";
                if (string.IsNullOrEmpty(exePath))
                {
                    continue;
                }
                bool is64 = Is64Bit(process);
                result.Add(new ProcessInfo
                {
                    Pid = process.Id,
                    Name = process.ProcessName,
                    ExePath = exePath,
                    Is64Bit = is64,
                    Engine = EngineDetector.DetectFromExe(exePath),
                });
            }
            catch
            {
                // Access denied for protected processes; skip them.
            }
        }
        return result.OrderBy(p => p.Name).ThenBy(p => p.Pid).ToList();
    }

    public static bool Is64Bit(Process process)
    {
        try
        {
            if (IsWow64Process2(process.Handle, out ushort processMachine, out _))
            {
                return processMachine == 0;
            }
            string path = process.MainModule?.FileName ?? "";
            return PeMachine(path) == 0x8664;
        }
        catch
        {
            return IntPtr.Size == 8;
        }
    }

    public static ushort PeMachine(string path)
    {
        try
        {
            using var stream = File.OpenRead(path);
            Span<byte> header = stackalloc byte[0x40];
            int read = stream.Read(header);
            if (read < 0x40 || header[0] != 'M' || header[1] != 'Z')
            {
                return 0;
            }
            int peOffset = header[0x3C] | (header[0x3D] << 8) | (header[0x3E] << 16) |
                           (header[0x3F] << 24);
            stream.Position = peOffset;
            Span<byte> pe = stackalloc byte[6];
            read = stream.Read(pe);
            if (read < 6 || pe[0] != 'P' || pe[1] != 'E')
            {
                return 0;
            }
            return (ushort)(pe[4] | (pe[5] << 8));
        }
        catch
        {
            return 0;
        }
    }

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool IsWow64Process2(IntPtr process, out ushort processMachine,
                                               out ushort nativeMachine);
}
