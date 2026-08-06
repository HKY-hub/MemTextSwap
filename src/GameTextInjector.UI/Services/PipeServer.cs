using System.Collections.Concurrent;
using GameTextInjector.UI.Models;

namespace GameTextInjector.UI.Services;

public sealed class PipeServer
{
    private readonly TranslationService _translation;
    private readonly LogService _log;
    private readonly ConcurrentDictionary<int, TranslationSession> _sessions = new();

    public PipeServer(TranslationService translation, LogService log)
    {
        _translation = translation;
        _log = log;
    }

    public IReadOnlyCollection<TranslationSession> Sessions => _sessions.Values.ToList();

    public TranslationSession CreateSession(int pid)
    {
        string pipeName = $"GTI_{pid}_{Guid.NewGuid():N}";
        var session = new TranslationSession(pid, pipeName, _translation, _log);
        session.Ended += async (s, reason) =>
        {
            if (_sessions.TryRemove(pid, out _))
            {
                _log.Info($"会话结束: pid={pid} reason={reason}");
            }
            await s.DisposeAsync();
        };
        _sessions[pid] = session;
        session.Start();
        return session;
    }

    public TranslationSession? GetSession(int pid)
    {
        return _sessions.TryGetValue(pid, out var session) ? session : null;
    }

    public async Task UnloadAsync(int pid)
    {
        if (_sessions.TryGetValue(pid, out var session))
        {
            session.SendUnload();
        }
        await Task.CompletedTask;
    }

    public async Task StopAllAsync()
    {
        foreach (var session in _sessions.Values.ToList())
        {
            await session.DisposeAsync();
        }
        _sessions.Clear();
    }
}
