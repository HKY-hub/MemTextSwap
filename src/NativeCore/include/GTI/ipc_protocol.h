#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "gti/crc32.h"
#include "gti/gti_version.h"

namespace gti {

constexpr uint32_t kMaxPayload = 1024u * 1024u;  // 1 MiB
constexpr uint16_t kFlagResponse = 0x0001u;

enum class MsgType : uint16_t {
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
};

// 16-byte fixed header: magic[4] | type u16 | flags u16 | payloadLen u32 | crc32 u32.
#pragma pack(push, 1)
struct GtiHeader {
    uint8_t magic[4];
    uint16_t type;
    uint16_t flags;
    uint32_t payloadLen;
    uint32_t crc;
};
#pragma pack(pop)

static_assert(sizeof(GtiHeader) == 16, "GtiHeader must be exactly 16 bytes");

struct GtiFrame {
    GtiHeader header{};
    std::vector<uint8_t> payload;
};

inline bool IsValidMagic(const GtiHeader& h) {
    return h.magic[0] == 'G' && h.magic[1] == 'T' && h.magic[2] == 'I' && h.magic[3] == '1';
}

inline std::vector<uint8_t> BuildFrame(MsgType type, uint16_t flags,
                                       const void* payload, uint32_t payloadLen) {
    std::vector<uint8_t> out(sizeof(GtiHeader) + payloadLen);
    GtiHeader h{};
    h.magic[0] = 'G';
    h.magic[1] = 'T';
    h.magic[2] = 'I';
    h.magic[3] = '1';
    h.type = static_cast<uint16_t>(type);
    h.flags = flags;
    h.payloadLen = payloadLen;
    std::memcpy(out.data(), &h, sizeof(GtiHeader));
    if (payload && payloadLen > 0) {
        std::memcpy(out.data() + sizeof(GtiHeader), payload, payloadLen);
    }
    // CRC over the first 12 header bytes (excluding the crc field) chained with payload.
    uint32_t crc = Crc32(out.data() + sizeof(GtiHeader), out.size() - sizeof(GtiHeader),
                         Crc32(out.data(), 12, 0));
    std::memcpy(out.data() + 12, &crc, sizeof(crc));
    return out;
}

inline bool FrameFromBytes(const uint8_t* data, size_t len, GtiFrame* out, uint32_t* error) {
    if (!data || !out || !error) {
        return false;
    }
    if (len < sizeof(GtiHeader)) {
        *error = 1;  // truncated header
        return false;
    }
    std::memcpy(&out->header, data, sizeof(GtiHeader));
    if (!IsValidMagic(out->header)) {
        *error = 2;  // bad magic
        return false;
    }
    if (out->header.payloadLen > kMaxPayload) {
        *error = 3;  // oversized payload
        return false;
    }
    if (len < sizeof(GtiHeader) + out->header.payloadLen) {
        *error = 4;  // truncated payload
        return false;
    }
    const uint8_t* payload = data + sizeof(GtiHeader);
    uint32_t expected = Crc32(payload, out->header.payloadLen, Crc32(data, 12, 0));
    if (expected != out->header.crc) {
        *error = 5;  // crc mismatch
        return false;
    }
    out->payload.assign(payload, payload + out->header.payloadLen);
    return true;
}

namespace wire {

inline void PutU16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back(static_cast<uint8_t>(x & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
}

inline void PutU32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(static_cast<uint8_t>(x & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 16) & 0xFF));
    v.push_back(static_cast<uint8_t>((x >> 24) & 0xFF));
}

inline void PutU64(std::vector<uint8_t>& v, uint64_t x) {
    for (int i = 0; i < 8; ++i) {
        v.push_back(static_cast<uint8_t>((x >> (i * 8)) & 0xFF));
    }
}

inline void PutBytes(std::vector<uint8_t>& v, const uint8_t* p, size_t n) {
    v.insert(v.end(), p, p + n);
}

inline void PutString(std::vector<uint8_t>& v, const std::string& s) {
    v.insert(v.end(), s.begin(), s.end());
}

inline uint16_t GetU16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

inline uint32_t GetU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

inline uint64_t GetU64(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) {
        v |= (static_cast<uint64_t>(p[i]) << (i * 8));
    }
    return v;
}

}  // namespace wire
}  // namespace gti
