namespace GameTextInjector.UI.Services;

public static class EngineDetector
{
    public static string DetectFromExe(string exePath)
    {
        try
        {
            if (string.IsNullOrWhiteSpace(exePath))
            {
                return "未知";
            }
            string dir = Path.GetDirectoryName(exePath) ?? "";
            string exe = Path.GetFileName(exePath).ToLowerInvariant();

            if (File.Exists(Path.Combine(dir, "GameAssembly.dll")) &&
                File.Exists(Path.Combine(dir, "UnityPlayer.dll")))
            {
                return "Unity IL2CPP";
            }
            if (File.Exists(Path.Combine(dir, "UnityPlayer.dll")) &&
                (File.Exists(Path.Combine(dir, "mono-2.0-bdwgc.dll")) ||
                 File.Exists(Path.Combine(dir, "mono.dll"))))
            {
                return "Unity Mono";
            }
            if (Directory.Exists(Path.Combine(dir, "renpy")) || exe.Contains("renpy"))
            {
                return "RenPy";
            }
            if (File.Exists(Path.Combine(dir, "nw.dll")) ||
                File.Exists(Path.Combine(dir, "nwjs.dll")))
            {
                string www = Path.Combine(dir, "www", "js");
                if (File.Exists(Path.Combine(www, "rpg_core.js")))
                {
                    return "RPGMaker MV";
                }
                if (File.Exists(Path.Combine(www, "rmmz_core.js")))
                {
                    return "RPGMaker MZ";
                }
                return "NW.js (RPGMaker?)";
            }
            if (exe.StartsWith("testtarget"))
            {
                return "TestTarget";
            }
        }
        catch
        {
            // Engine detection must never crash the process list.
        }
        return "未知";
    }
}
