using System.Buffers.Binary;
using System.IO.Pipes;
using System.Text;
using MemTextSwap.UI.Models;

namespace MemTextSwap.UI.Services;

public sealed class TranslationSession : IAsyncDisposable
{
    private readonly TranslationService _translation;
    private readonly LogService _log;
    private CancellationTokenSource? _cts;
    private Task? _runTask;
    private NamedPipeServerStream? _activePipe;
    private NamedPipeServerStream? _acceptPipe;

    public int Pid { get; }
    public string PipeName { get; }
    public uint Engine { get; private set; }
    public string EngineName { get; private set; } = "未知";
    public DateTime StartedAt { get; } = DateTime.Now;
    public bool Running => _runTask is { IsCompleted: false };

    public event Action<TranslationSession, string>? Ended;

    public TranslationSession(int pid, string pipeName, TranslationService translation,
                              LogService log)
    {
        Pid = pid;
        PipeName = pipeName;
        _translation = translation;
        _log = log;
    }

    public void Start()
    {
        _cts = new CancellationTokenSource();
        _runTask = Task.Run(() => AcceptLoopAsync(_cts.Token));
        _log.Info($"会话已启动: pid={Pid} pipe={PipeName}");
    }

    public void SendUnload()
    {
        _log.Info($"正在向 pid={Pid} 发送卸载指令");
        if (_activePipe is not null)
        {
            SendFrame(_activePipe, IpcProtocol.MsgType.Unload, new byte[4]);
            _log.Info("UNLOAD 已发送");
        }
        else
        {
            _log.Warn("DLL 未连接，无法发送 UNLOAD（请确认已注入）");
        }
    }

    private void AcceptLoopAsync(CancellationToken ct)
    {
        while (!ct.IsCancellationRequested)
        {
            NamedPipeServerStream? pipe = null;
            try
            {
                pipe = new NamedPipeServerStream(PipeName, PipeDirection.InOut, 1,
                                                 PipeTransmissionMode.Byte,
                                                 PipeOptions.None, 8192, 8192);
                _acceptPipe = pipe;
                pipe.WaitForConnection();
                _acceptPipe = null;
                _log.Info($"DLL 已连接: pid={Pid} pipe={PipeName}");
                _activePipe = pipe;
                ReadLoop(pipe);
                _log.Warn($"DLL 连接断开: pid={Pid}");
                _activePipe = null;
                pipe.Dispose();
                pipe = null;
                Thread.Sleep(200);
            }
            catch (OperationCanceledException)
            {
                _log.Warn($"会话取消: pid={Pid}");
                break;
            }
            catch (ObjectDisposedException)
            {
                _log.Warn($"会话管道已释放: pid={Pid}");
                break;
            }
            catch (Exception ex)
            {
                _log.Warn($"管道监听异常: {ex}");
                _acceptPipe = null;
                _activePipe = null;
                pipe?.Dispose();
                Thread.Sleep(500);
            }
        }
        Ended?.Invoke(this, "stopped");
    }

    private void ReadLoop(NamedPipeServerStream pipe)
    {
        var header = new byte[IpcProtocol.HeaderSize];
        while (true)
        {
            pipe.ReadExactly(header);
            uint length = BinaryPrimitives.ReadUInt32LittleEndian(header.AsSpan(8, 4));
            if (length > IpcProtocol.MaxPayload)
            {
                _log.Warn($"非法消息长度 {length}，断开连接");
                return;
            }
            var payload = new byte[length];
            if (length > 0)
            {
                pipe.ReadExactly(payload);
            }
            var full = new byte[IpcProtocol.HeaderSize + length];
            header.CopyTo(full, 0);
            payload.CopyTo(full, IpcProtocol.HeaderSize);
            if (!IpcProtocol.TryParse(full, out ushort type, out ushort flags,
                                      out byte[] payloadBytes, out int error))
            {
                _log.Warn($"协议帧校验失败 error={error}");
                continue;
            }
            try
            {
                Dispatch(pipe, (IpcProtocol.MsgType)type, payloadBytes);
            }
            catch (Exception ex)
            {
                _log.Warn($"消息处理异常 type={type}: {ex}");
                throw;
            }
        }
    }

    private void Dispatch(NamedPipeServerStream pipe, IpcProtocol.MsgType type,
                          byte[] payload)
    {
        switch (type)
        {
            case IpcProtocol.MsgType.Hello:
                HandleHello(pipe, payload);
                break;
            case IpcProtocol.MsgType.TextRequest:
                HandleTextRequest(pipe, payload);
                break;
            case IpcProtocol.MsgType.Log:
                HandleLog(payload);
                break;
            case IpcProtocol.MsgType.Unload:
                HandleUnload(pipe, payload);
                break;
            case IpcProtocol.MsgType.Ping:
                SendFrame(pipe, IpcProtocol.MsgType.Pong, payload);
                break;
            default:
                break;
        }
    }

    private void HandleHello(NamedPipeServerStream pipe, byte[] payload)
    {
        if (payload.Length < 20)
        {
            return;
        }
        uint corr = BinaryPrimitives.ReadUInt32LittleEndian(payload);
        uint pid = BinaryPrimitives.ReadUInt32LittleEndian(payload.AsSpan(4));
        uint arch = BinaryPrimitives.ReadUInt32LittleEndian(payload.AsSpan(8));
        uint engine = BinaryPrimitives.ReadUInt32LittleEndian(payload.AsSpan(12));
        string name = Encoding.UTF8.GetString(payload, 20, payload.Length - 20);
        Engine = engine;
        EngineName = name;
        _log.Info($"HELLO: pid={pid} arch={(arch == 2 ? "x64" : "x86")} engine={name}");

        var response = new byte[20];
        BinaryPrimitives.WriteUInt32LittleEndian(response, corr);
        BinaryPrimitives.WriteUInt32LittleEndian(response.AsSpan(4), 1);  // ok
        BinaryPrimitives.WriteUInt32LittleEndian(response.AsSpan(8), 3000);  // blockTimeoutMs
        BinaryPrimitives.WriteUInt32LittleEndian(response.AsSpan(12), 1);  // filtersEnabled
        SendFrame(pipe, IpcProtocol.MsgType.HelloAck, response);
        _log.Info($"HELLO_ACK 已写入: pid={Pid}");
    }

    private void HandleTextRequest(NamedPipeServerStream pipe, byte[] payload)
    {
        if (payload.Length < 16)
        {
            return;
        }
        uint corr = BinaryPrimitives.ReadUInt32LittleEndian(payload);
        ulong hash = BinaryPrimitives.ReadUInt64LittleEndian(payload.AsSpan(4));
        uint engine = BinaryPrimitives.ReadUInt32LittleEndian(payload.AsSpan(12));
        string source = Encoding.UTF8.GetString(payload, 16, payload.Length - 16);

        _log.Info($"TEXT_REQUEST pid={Pid}: {source}");
        TranslationResult result = _translation.TranslateAsync(source, engine)
            .GetAwaiter().GetResult();
        _log.Info($"TEXT_RESULT pid={Pid}: {result.Origin} => {result.Target}");
        byte[] targetBytes = Encoding.UTF8.GetBytes(result.Target);
        var response = new byte[20 + targetBytes.Length];
        BinaryPrimitives.WriteUInt32LittleEndian(response, corr);
        BinaryPrimitives.WriteUInt64LittleEndian(response.AsSpan(4), hash);
        BinaryPrimitives.WriteUInt32LittleEndian(response.AsSpan(12), result.IsSuccess ? 1u : 0u);
        response[16] = result.Origin switch
        {
            "cache" => 1,
            "dict" => 2,
            "ai" => 3,
            "manual" => 4,
            _ => 0,
        };
        targetBytes.CopyTo(response, 20);
        SendFrame(pipe, IpcProtocol.MsgType.TextResult, response);
        _log.Info($"TEXT_RESULT 已写入: pid={Pid} corr={corr}");
    }

    private void HandleLog(byte[] payload)
    {
        if (payload.Length < 5)
        {
            return;
        }
        byte level = payload[4];
        string message = Encoding.UTF8.GetString(payload, 5, payload.Length - 5);
        switch (level)
        {
            case 3:
                _log.Warn(message);
                break;
            case 4:
                _log.Error(message);
                break;
            default:
                _log.Info(message);
                break;
        }
    }

    private void HandleUnload(NamedPipeServerStream pipe, byte[] payload)
    {
        uint corr = payload.Length >= 4
            ? BinaryPrimitives.ReadUInt32LittleEndian(payload)
            : 0;
        var response = new byte[8];
        BinaryPrimitives.WriteUInt32LittleEndian(response, corr);
        BinaryPrimitives.WriteUInt32LittleEndian(response.AsSpan(4), 1);
        SendFrame(pipe, IpcProtocol.MsgType.UnloadAck, response);
        _log.Info($"pid={Pid} 已确认卸载");
        Ended?.Invoke(this, "unload-requested");
    }

    private static void SendFrame(NamedPipeServerStream pipe, IpcProtocol.MsgType type,
                                  byte[] payload)
    {
        byte[] frame = IpcProtocol.BuildFrame(type, 0, payload);
        pipe.Write(frame);
    }

    public async ValueTask DisposeAsync()
    {
        _cts?.Cancel();
        _acceptPipe?.Dispose();
        _activePipe?.Dispose();
        if (_runTask is not null)
        {
            await Task.WhenAny(_runTask, Task.Delay(2000));
        }
        _cts?.Dispose();
        _log.Info($"会话已停止: pid={Pid}");
    }
}
