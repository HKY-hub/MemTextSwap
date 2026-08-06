#include <cstdio>
#include <string>

#include <windows.h>

#include "gti/ipc_protocol.h"

// Minimal native pipe client used to reproduce/validate the IPC exchange
// against the .NET pipeprobe server: connect, send HELLO, read ACK.
int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: nativepipeprobe <pipeName>\n");
        return 1;
    }
    std::wstring name = std::wstring(L"\\\\.\\pipe\\") + argv[1];
    HANDLE pipe = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) {
        std::printf("connect failed err=%lu\n", GetLastError());
        return 2;
    }
    std::printf("connected\n");

    std::vector<uint8_t> payload;
    gti::wire::PutU32(payload, 1);
    gti::wire::PutU32(payload, GetCurrentProcessId());
    gti::wire::PutU32(payload, sizeof(void*) == 8 ? 2 : 1);
    gti::wire::PutU32(payload, 1);
    gti::wire::PutU32(payload, 1);
    std::string helloName = "nativepipeprobe";
    gti::wire::PutString(payload, helloName);
    std::vector<uint8_t> frame = gti::BuildFrame(gti::MsgType::Hello, 0, payload.data(),
                                                 static_cast<uint32_t>(payload.size()));
    DWORD written = 0;
    BOOL ok = WriteFile(pipe, frame.data(), static_cast<DWORD>(frame.size()), &written,
                        nullptr);
    std::printf("hello write ok=%d written=%lu err=%lu\n", ok ? 1 : 0, written,
                GetLastError());

    uint8_t header[16] = {};
    DWORD read = 0;
    ok = ReadFile(pipe, header, sizeof(header), &read, nullptr);
    std::printf("ack header read ok=%d read=%lu err=%lu\n", ok ? 1 : 0, read,
                GetLastError());
    if (ok && read == sizeof(header)) {
        uint8_t payloadBuf[64] = {};
        DWORD payloadLen = gti::wire::GetU32(header + 8);
        read = 0;
        ok = ReadFile(pipe, payloadBuf, payloadLen, &read, nullptr);
        std::printf("ack payload read ok=%d read=%lu err=%lu\n", ok ? 1 : 0, read,
                    GetLastError());
    }

    // Second exchange: TEXT_REQUEST -> TEXT_RESULT (validates sustained duplex).
    std::vector<uint8_t> reqPayload;
    gti::wire::PutU32(reqPayload, 7);
    gti::wire::PutU64(reqPayload, 0x1122334455667788ULL);
    gti::wire::PutU32(reqPayload, 1);
    std::string text = "Hello World #1";
    gti::wire::PutString(reqPayload, text);
    std::vector<uint8_t> reqFrame = gti::BuildFrame(gti::MsgType::TextRequest, 0,
                                                    reqPayload.data(),
                                                    static_cast<uint32_t>(reqPayload.size()));
    written = 0;
    ok = WriteFile(pipe, reqFrame.data(), static_cast<DWORD>(reqFrame.size()), &written,
                   nullptr);
    std::printf("text request write ok=%d written=%lu err=%lu\n", ok ? 1 : 0, written,
                GetLastError());
    if (ok) {
        read = 0;
        ok = ReadFile(pipe, header, sizeof(header), &read, nullptr);
        std::printf("result header read ok=%d read=%lu err=%lu\n", ok ? 1 : 0, read,
                    GetLastError());
    }
    CloseHandle(pipe);
    std::printf("done\n");
    return 0;
}
