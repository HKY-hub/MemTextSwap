using System.Diagnostics;
using MemTextSwap.UI.Services;

// 真机冒烟器：用 MemTextSwap 的真实配置/词典/缓存/AI/注入器，自动对指定游戏
// 完成 启动->注入->捕获->翻译 全链路，并把结果写入同一份 app.log。
// 用法: Realsmoke <游戏exe> [架构: 32|64] [等待秒数]

if (args.Length < 1)
{
    Console.WriteLine("用法: Realsmoke <游戏exe> [32|64] [等待秒]");
    return 1;
}

string exe = Path.GetFullPath(args[0]);
string arch = args.Length > 1 ? args[1] : "64";
int waitSeconds = args.Length > 2 ? int.Parse(args[2]) : 40;

var log = new LogService();
log.Info($"真机冒烟启动: exe={exe} arch={arch} wait={waitSeconds}s");

var settings = new SettingsService(log);
settings.Load();
var cache = new CacheService(settings, log);
var dictionary = new DictionaryService(log);
dictionary.LoadAll(settings.Current.DictionaryPaths);
var ai = new AIService(settings, log);
var translation = new TranslationService(cache, dictionary, ai, settings, log);
var pipeServer = new PipeServer(translation, log);

log.Info($"启动游戏: {exe}");
var game = Process.Start(new ProcessStartInfo(exe)
{
    WorkingDirectory = Path.GetDirectoryName(exe) ?? "",
    UseShellExecute = true,
});
if (game is null)
{
    log.Error("游戏进程启动失败");
    return 2;
}

await Task.Delay(15000);
var session = pipeServer.CreateSession(game.Id);
log.Info($"注入目标 pid={game.Id} pipe={session.PipeName}");

string root = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "..", "..", "..", ".."));
string injector = Path.Combine(root, "build", arch == "32" ? "x86" : "x64",
    "src", "NativeCore", "Release", $"Injector{arch}.exe");
string dll = Path.Combine(root, "build", arch == "32" ? "x86" : "x64",
    "src", "NativeCore", "Release", $"NativeCore{arch}.dll");
log.Info($"注入器={injector} dll={dll}");

var psi = new ProcessStartInfo(injector)
{
    RedirectStandardOutput = true,
    RedirectStandardError = true,
    UseShellExecute = false,
    CreateNoWindow = true,
};
psi.ArgumentList.Add("--pid");
psi.ArgumentList.Add(game.Id.ToString());
psi.ArgumentList.Add("--dll");
psi.ArgumentList.Add(dll);
psi.ArgumentList.Add("--pipe");
psi.ArgumentList.Add(session.PipeName);

var injectorProc = Process.Start(psi);
string output = injectorProc is null ? "" : await injectorProc.StandardOutput.ReadToEndAsync();
if (injectorProc is not null)
{
    await injectorProc.WaitForExitAsync();
}
log.Info($"注入器输出: {output.Trim()}");

log.Info($"等待 {waitSeconds}s 观察捕获/翻译…");
await Task.Delay(TimeSpan.FromSeconds(waitSeconds));

int requests = log.Entries.Count(e => e.Message.Contains("TEXT_REQUEST"));
int hits = log.Entries.Count(e => e.Message.Contains("TEXT_RESULT") &&
                                  !e.Message.Contains(": none =>"));
int misses = log.Entries.Count(e => e.Message.Contains("翻译未命中"));
log.Info($"冒烟结果: TEXT_REQUEST={requests} 未命中={misses} 游戏存活={!game.HasExited}");

try
{
    game.Kill(true);
}
catch
{
}
return 0;
