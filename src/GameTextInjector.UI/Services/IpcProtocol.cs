using System.Buffers.Binary;
using System.Text;

namespace GameTextInjector.UI.Services;

public static class IpcProtocol
{
    public const int HeaderSize = 16;
    public const uint MaxPayload = 1024 * 1024;
    public const ushort FlagResponse = 0x0001;

    public enum MsgType : ushort
    {
        Hello = 1,
        HelloAck = 2,
        TextRequest = 16,
        TextResult = 17,
        Log = 32,
        Unload = 48,
        UnloadAck = 49,
        Ping = 64,
        Pong = 65,
        Stats = 80,
    }

    public static byte[] BuildFrame(MsgType type, ushort flags, ReadOnlySpan<byte> payload)
    {
        if (payload.Length > MaxPayload)
        {
            throw new ArgumentOutOfRangeException(nameof(payload), "payload too large");
        }
        var frame = new byte[HeaderSize + payload.Length];
        frame[0] = (byte)'G';
        frame[1] = (byte)'T';
        frame[2] = (byte)'I';
        frame[3] = (byte)'1';
        BinaryPrimitives.WriteUInt16LittleEndian(frame.AsSpan(4), (ushort)type);
        BinaryPrimitives.WriteUInt16LittleEndian(frame.AsSpan(6), flags);
        BinaryPrimitives.WriteUInt32LittleEndian(frame.AsSpan(8), (uint)payload.Length);
        uint crc = Crc32.Compute(frame.AsSpan(0, 12), 0);
        crc = Crc32.Compute(payload, crc);
        BinaryPrimitives.WriteUInt32LittleEndian(frame.AsSpan(12), crc);
        payload.CopyTo(frame.AsSpan(HeaderSize));
        return frame;
    }

    public static bool TryParse(ReadOnlySpan<byte> data, out ushort type, out ushort flags,
                                out byte[] payload, out int error)
    {
        type = 0;
        flags = 0;
        payload = Array.Empty<byte>();
        error = 0;
        if (data.Length < HeaderSize)
        {
            error = 1;
            return false;
        }
        if (data[0] != 'G' || data[1] != 'T' || data[2] != 'I' || data[3] != '1')
        {
            error = 2;
            return false;
        }
        type = BinaryPrimitives.ReadUInt16LittleEndian(data.Slice(4, 2));
        flags = BinaryPrimitives.ReadUInt16LittleEndian(data.Slice(6, 2));
        uint length = BinaryPrimitives.ReadUInt32LittleEndian(data.Slice(8, 4));
        if (length > MaxPayload)
        {
            error = 3;
            return false;
        }
        if (data.Length < HeaderSize + length)
        {
            error = 4;
            return false;
        }
        uint expected = Crc32.Compute(data.Slice(0, 12), 0);
        expected = Crc32.Compute(data.Slice(HeaderSize, (int)length), expected);
        uint actual = BinaryPrimitives.ReadUInt32LittleEndian(data.Slice(12, 4));
        if (actual != expected)
        {
            error = 5;
            return false;
        }
        payload = data.Slice(HeaderSize, (int)length).ToArray();
        return true;
    }

    public static byte[] EncodeTextRequest(uint corr, ulong hash, uint engine, string source)
    {
        var payload = new byte[16 + Encoding.UTF8.GetByteCount(source)];
        BinaryPrimitives.WriteUInt32LittleEndian(payload, corr);
        BinaryPrimitives.WriteUInt64LittleEndian(payload.AsSpan(4), hash);
        BinaryPrimitives.WriteUInt32LittleEndian(payload.AsSpan(12), engine);
        Encoding.UTF8.GetBytes(source, payload.AsSpan(16));
        return payload;
    }
}
