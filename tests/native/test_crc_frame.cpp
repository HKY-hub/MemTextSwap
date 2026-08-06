#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "gti/ipc_protocol.h"

using namespace gti;

TEST(Crc32, KnownVector) {
    const uint8_t data[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    EXPECT_EQ(Crc32(data, sizeof(data)), 0xCBF43926u);
}

TEST(Crc32, ChainMatchesSinglePass) {
    std::vector<uint8_t> a = {1, 2, 3};
    std::vector<uint8_t> b = {4, 5, 6, 7};
    std::vector<uint8_t> ab = {1, 2, 3, 4, 5, 6, 7};
    EXPECT_EQ(Crc32(b.data(), b.size(), Crc32(a.data(), a.size(), 0)),
              Crc32(ab.data(), ab.size(), 0));
}

TEST(IpcFrame, HeaderIsSixteenBytes) {
    EXPECT_EQ(sizeof(GtiHeader), 16u);
}

TEST(IpcFrame, BuildParseRoundTrip) {
    std::vector<uint8_t> payload;
    wire::PutU32(payload, 42);
    wire::PutU64(payload, 0x1122334455667788ULL);
    wire::PutString(payload, "hello world");

    std::vector<uint8_t> frame = BuildFrame(MsgType::TextRequest, 0, payload.data(),
                                            static_cast<uint32_t>(payload.size()));
    GtiFrame parsed;
    uint32_t error = 0;
    ASSERT_TRUE(FrameFromBytes(frame.data(), frame.size(), &parsed, &error));
    EXPECT_EQ(static_cast<uint16_t>(parsed.header.type),
              static_cast<uint16_t>(MsgType::TextRequest));
    EXPECT_EQ(parsed.payload.size(), payload.size());
    EXPECT_EQ(wire::GetU32(parsed.payload.data()), 42u);
    EXPECT_EQ(wire::GetU64(parsed.payload.data() + 4), 0x1122334455667788ULL);
    std::string text(reinterpret_cast<const char*>(parsed.payload.data() + 12),
                     parsed.payload.size() - 12);
    EXPECT_EQ(text, "hello world");
}

TEST(IpcFrame, DetectsCorruptCrc) {
    std::vector<uint8_t> payload = {1, 2, 3};
    std::vector<uint8_t> frame = BuildFrame(MsgType::Ping, 0, payload.data(),
                                            static_cast<uint32_t>(payload.size()));
    frame[13] ^= 0xFF;
    GtiFrame parsed;
    uint32_t error = 0;
    EXPECT_FALSE(FrameFromBytes(frame.data(), frame.size(), &parsed, &error));
    EXPECT_EQ(error, 5u);
}

TEST(IpcFrame, DetectsBadMagic) {
    std::vector<uint8_t> frame = BuildFrame(MsgType::Ping, 0, nullptr, 0);
    frame[0] = 'X';
    GtiFrame parsed;
    uint32_t error = 0;
    EXPECT_FALSE(FrameFromBytes(frame.data(), frame.size(), &parsed, &error));
    EXPECT_EQ(error, 2u);
}

TEST(IpcFrame, RejectsTruncatedPayload) {
    std::vector<uint8_t> payload(16, 7);
    std::vector<uint8_t> frame = BuildFrame(MsgType::Log, 0, payload.data(),
                                            static_cast<uint32_t>(payload.size()));
    GtiFrame parsed;
    uint32_t error = 0;
    EXPECT_FALSE(FrameFromBytes(frame.data(), frame.size() - 4, &parsed, &error));
    EXPECT_EQ(error, 4u);
}
