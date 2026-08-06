using MemTextSwap.UI.Services;
using Xunit;

namespace MemTextSwap.Tests;

public class IpcProtocolTests
{
    [Fact]
    public void Crc32_KnownVector()
    {
        byte[] data = "123456789"u8.ToArray();
        Assert.Equal(0xCBF43926u, Crc32.Compute(data));
    }

    [Fact]
    public void Frame_MatchesNativeWireVector()
    {
        // Expected bytes produced by the C++ BuildFrame implementation:
        // header: "GTI1" | type=0x0040(Ping) | flags=0 | len=3 | crc=0x40478853
        // payload: 01 02 03
        byte[] expected =
        {
            0x47, 0x54, 0x49, 0x31, 0x40, 0x00, 0x00, 0x00,
            0x03, 0x00, 0x00, 0x00, 0x53, 0x88, 0x47, 0x40,
            0x01, 0x02, 0x03,
        };
        byte[] frame = IpcProtocol.BuildFrame(IpcProtocol.MsgType.Ping, 0, new byte[] { 1, 2, 3 });
        Assert.Equal(expected, frame);
    }

    [Fact]
    public void Frame_RoundTrip()
    {
        byte[] payload = IpcProtocol.EncodeTextRequest(42, 0x1122334455667788UL, 2, "hello");
        byte[] frame = IpcProtocol.BuildFrame(IpcProtocol.MsgType.TextRequest, 0, payload);
        Assert.True(IpcProtocol.TryParse(frame, out ushort type, out _, out byte[] parsed,
                                         out int error));
        Assert.Equal((ushort)IpcProtocol.MsgType.TextRequest, type);
        Assert.Equal(payload, parsed);
        Assert.Equal(0, error);
    }

    [Fact]
    public void Frame_DetectsCorruptCrc()
    {
        byte[] frame = IpcProtocol.BuildFrame(IpcProtocol.MsgType.Ping, 0, new byte[] { 1 });
        frame[13] ^= 0xFF;
        Assert.False(IpcProtocol.TryParse(frame, out _, out _, out _, out int error));
        Assert.Equal(5, error);
    }

    [Fact]
    public void Frame_DetectsBadMagic()
    {
        byte[] frame = IpcProtocol.BuildFrame(IpcProtocol.MsgType.Ping, 0, Array.Empty<byte>());
        frame[0] = (byte)'X';
        Assert.False(IpcProtocol.TryParse(frame, out _, out _, out _, out int error));
        Assert.Equal(2, error);
    }

    [Fact]
    public void Frame_DetectsTruncation()
    {
        byte[] frame = IpcProtocol.BuildFrame(IpcProtocol.MsgType.Log, 0, new byte[16]);
        Assert.False(IpcProtocol.TryParse(frame.AsSpan(0, frame.Length - 4), out _, out _, out _,
                                          out int error));
        Assert.Equal(4, error);
    }
}
