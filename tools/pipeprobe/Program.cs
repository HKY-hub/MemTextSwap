using System.Buffers.Binary;
using System.IO.Pipes;
using System.Text;
using MemTextSwap.UI.Services;

// Protocol probe: acts as the UI-side pipe server and talks to a REAL injected
// NativeCore client (TestTarget + Injector). Prints every received frame and
// responds to HELLO with a valid ACK. Usage: pipeprobe <pipeName>

string pipeName = args.Length > 0 ? args[0] : "GTI_probe_test";
bool asyncMode = args.Length > 1 && args[1] == "--async";
bool threadPoolMode = args.Length > 2 && args[2] == "--threadpool";
int delayMs = 0;
for (int i = 1; i < args.Length - 1; i++)
{
    if (args[i] == "--delay" && int.TryParse(args[i + 1], out int d))
    {
        delayMs = d;
    }
}

async Task RunServer()
{
    using var server = new NamedPipeServerStream(pipeName, PipeDirection.InOut, 1,
                                                 PipeTransmissionMode.Byte,
                                                 asyncMode ? PipeOptions.Asynchronous : PipeOptions.None,
                                                 8192, 8192);
    Console.WriteLine($"probe: waiting on {pipeName}");
    if (asyncMode)
    {
        await server.WaitForConnectionAsync();
    }
    else
    {
        server.WaitForConnection();
    }
    Console.WriteLine("probe: connected");
    Console.Out.Flush();

    while (true)
    {
        var header = new byte[16];
        if (asyncMode)
        {
            await server.ReadExactlyAsync(header);
        }
        else
        {
            server.ReadExactly(header);
        }
        ushort type = BinaryPrimitives.ReadUInt16LittleEndian(header.AsSpan(4, 2));
        int length = (int)BinaryPrimitives.ReadUInt32LittleEndian(header.AsSpan(8, 4));
        var payload = new byte[length];
        if (length > 0)
        {
            if (asyncMode)
            {
                await server.ReadExactlyAsync(payload);
            }
            else
            {
                server.ReadExactly(payload);
            }
        }
        Console.WriteLine($"probe: frame type={type} len={length}");
        Console.Out.Flush();

        if (type == (ushort)IpcProtocol.MsgType.Hello)
        {
            Console.WriteLine($"probe: hello text={Encoding.UTF8.GetString(payload, 20, payload.Length - 20)}");
            var ack = new byte[20];
            uint corr = BinaryPrimitives.ReadUInt32LittleEndian(payload);
            BinaryPrimitives.WriteUInt32LittleEndian(ack, corr);
            BinaryPrimitives.WriteUInt32LittleEndian(ack.AsSpan(4), 1);
            BinaryPrimitives.WriteUInt32LittleEndian(ack.AsSpan(8), 3000);
            BinaryPrimitives.WriteUInt32LittleEndian(ack.AsSpan(12), 1);
            byte[] frame = IpcProtocol.BuildFrame(IpcProtocol.MsgType.HelloAck, 0, ack);
            Console.WriteLine("probe: writing ACK...");
            Console.Out.Flush();
            if (asyncMode)
            {
                await server.WriteAsync(frame);
            }
            else
            {
                server.Write(frame);
            }
            Console.WriteLine("probe: ACK written");
            Console.Out.Flush();
        }
        else if (type == (ushort)IpcProtocol.MsgType.TextRequest)
        {
            Console.WriteLine($"probe: TEXT_REQUEST={Encoding.UTF8.GetString(payload, 16, payload.Length - 16)}");
            uint corr = BinaryPrimitives.ReadUInt32LittleEndian(payload);
            ulong hash = BinaryPrimitives.ReadUInt64LittleEndian(payload.AsSpan(4));
            byte[] target = Encoding.UTF8.GetBytes("探针译文");
            var response = new byte[20 + target.Length];
            BinaryPrimitives.WriteUInt32LittleEndian(response, corr);
            BinaryPrimitives.WriteUInt64LittleEndian(response.AsSpan(4), hash);
            BinaryPrimitives.WriteUInt32LittleEndian(response.AsSpan(12), 1);
            response[16] = 2;
            target.CopyTo(response, 20);
            byte[] resultFrame = IpcProtocol.BuildFrame(IpcProtocol.MsgType.TextResult, 0, response);
            if (delayMs > 0)
            {
                await Task.Delay(delayMs);
            }
            if (asyncMode)
            {
                await server.WriteAsync(resultFrame);
            }
            else
            {
                server.Write(resultFrame);
            }
            Console.WriteLine("probe: TEXT_RESULT sent");
            Console.Out.Flush();
        }
        else
        {
            Console.WriteLine($"probe: other frame type={type}");
        }
    }
}

if (threadPoolMode)
{
    await Task.Run(RunServer);
}
else
{
    await RunServer();
}
