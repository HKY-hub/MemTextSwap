using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using MemTextSwap.UI.Models;
using MemTextSwap.UI.Services;

namespace MemTextSwap.UI.ViewModels;

public partial class MainWindowViewModel : ObservableObject
{
    private readonly ProcessService _processes;
    private readonly InjectorService _injector;
    private readonly PipeServer _pipeServer;
    private readonly DictionaryService _dictionary;
    private readonly CacheService _cache;
    private readonly LogService _log;
    private readonly SettingsService _settings;

    public MainWindowViewModel(ProcessService processes, InjectorService injector,
                               PipeServer pipeServer, DictionaryService dictionary,
                               CacheService cache, LogService log, SettingsService settings)
    {
        _processes = processes;
        _injector = injector;
        _pipeServer = pipeServer;
        _dictionary = dictionary;
        _cache = cache;
        _log = log;
        _settings = settings;
        Settings = settings.Current;
    }

    public ObservableCollection<ProcessInfo> Processes { get; } = new();
    public ObservableCollection<LogEntry> Logs => _log.Entries;
    public ObservableCollection<CacheEntry> CacheEntries { get; } = new();
    public List<string> TranslationModes { get; } = new() { "Local", "Ai" };

    [ObservableProperty]
    private ProcessInfo? selectedProcess;

    [ObservableProperty]
    private AppSettings settings;

    [ObservableProperty]
    private string statusText = "就绪";

    [ObservableProperty]
    private string dictionarySummary = "词典未加载";

    [ObservableProperty]
    private string sessionsSummary = "无活动会话";

    [RelayCommand]
    private async Task RefreshAsync()
    {
        try
        {
            Processes.Clear();
            var list = _processes.ListGames();
            foreach (var item in list)
            {
                item.Injected = _pipeServer.GetSession(item.Pid) is not null;
                Processes.Add(item);
            }
            StatusText = $"共发现 {Processes.Count} 个进程";
            UpdateSessions();
        }
        catch (Exception ex)
        {
            HandleCommandError("刷新进程", ex);
        }
    }

    [RelayCommand]
    private async Task InjectAsync()
    {
        try
        {
            var target = SelectedProcess;
            if (target is null)
            {
                StatusText = "请先选择一个进程";
                return;
            }
            if (_pipeServer.GetSession(target.Pid) is not null)
            {
                StatusText = $"pid={target.Pid} 已注入，无需重复操作";
                return;
            }
            var session = _pipeServer.CreateSession(target.Pid);
            InjectResult result = await _injector.InjectAsync(target.Pid, target.Is64Bit,
                                                              session.PipeName);
            StatusText = result.Ok
                ? $"注入成功: pid={target.Pid}（{target.Name}）"
                : $"注入失败: pid={target.Pid} code={result.Code} {result.Error}";
            if (!result.Ok)
            {
                await session.DisposeAsync();
            }
            target.Injected = result.Ok;
            UpdateSessions();
        }
        catch (Exception ex)
        {
            HandleCommandError("注入", ex);
        }
    }

    [RelayCommand]
    private async Task UnloadAsync()
    {
        try
        {
            var target = SelectedProcess;
            if (target is null)
            {
                StatusText = "请先选择一个进程";
                return;
            }
            await _pipeServer.UnloadAsync(target.Pid);
            StatusText = $"卸载指令已发送: pid={target.Pid}";
            await Task.Delay(800);
            target.Injected = _pipeServer.GetSession(target.Pid) is not null;
            UpdateSessions();
        }
        catch (Exception ex)
        {
            HandleCommandError("卸载", ex);
        }
    }

    [RelayCommand]
    private void SaveSettings()
    {
        try
        {
            _settings.Save();
            ReloadDictionary();
            StatusText = "设置与词典已保存/加载";
        }
        catch (Exception ex)
        {
            HandleCommandError("保存设置", ex);
        }
    }

    [RelayCommand]
    public void ReloadDictionary()
    {
        try
        {
            _dictionary.LoadAll(_settings.Current.DictionaryPaths);
            DictionarySummary = $"已加载词典条目: {_dictionary.Count}";
            StatusText = DictionarySummary;
        }
        catch (Exception ex)
        {
            HandleCommandError("加载词典", ex);
        }
    }

    [RelayCommand]
    private void ExportDictionary()
    {
        try
        {
            string path = Path.Combine(_settings.ConfigDir, "dictionary-export.txt");
            _dictionary.ExportFile(path);
            StatusText = $"词典已导出: {path}";
        }
        catch (Exception ex)
        {
            HandleCommandError("导出词典", ex);
        }
    }

    [RelayCommand]
    private async Task ClearCacheAsync()
    {
        try
        {
            await _cache.ClearAsync();
            await RefreshCacheAsync();
        }
        catch (Exception ex)
        {
            HandleCommandError("清空缓存", ex);
        }
    }

    [RelayCommand]
    private async Task RefreshCacheAsync()
    {
        try
        {
            CacheEntries.Clear();
            foreach (var item in await _cache.ListAsync(500))
            {
                CacheEntries.Add(new CacheEntry(item.Source, item.Target, item.Origin,
                                                item.Updated));
            }
            StatusText = $"缓存条目: {CacheEntries.Count}";
        }
        catch (Exception ex)
        {
            HandleCommandError("刷新缓存", ex);
        }
    }

    private void HandleCommandError(string action, Exception ex)
    {
        _log.Error($"{action}失败: {ex}");
        StatusText = $"{action}失败: {ex.Message}";
    }

    private void UpdateSessions()
    {
        var sessions = _pipeServer.Sessions;
        SessionsSummary = sessions.Count == 0
            ? "无活动会话"
            : string.Join(" | ", sessions.Select(s => $"pid={s.Pid} {s.EngineName}"));
    }
}

public sealed record CacheEntry(string Source, string Target, string Origin, DateTime Updated);
