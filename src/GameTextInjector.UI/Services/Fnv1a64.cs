using System.Text;

namespace GameTextInjector.UI.Services;

public static class Fnv1a64
{
    public static ulong Compute(string text)
    {
        ulong hash = 14695981039346656037UL;
        ReadOnlySpan<byte> bytes = Encoding.UTF8.GetBytes(text);
        foreach (byte b in bytes)
        {
            hash ^= b;
            hash *= 1099511628211UL;
        }
        return hash;
    }
}
