# GameTextInjector IPC 协议 v1

## 传输

- 传输方式：Windows 命名管道（byte mode）。
- 管道名：`\\.\pipe\GTI_{targetPid}_{sessionGuid}`。
- 角色：UI 为服务端，注入的 NativeCore DLL 为客户端。
- 每目标一个会话，同一会话内请求串行（一次一个 TEXT_REQUEST 交换）。

## 帧格式（16 字节包头 + 载荷）

| 偏移 | 大小 | 字段 | 说明 |
| --- | --- | --- | --- |
| 0 | 4 | magic | `"GTI1"` |
| 4 | 2 | type | 消息类型（见下） |
| 6 | 2 | flags | bit0 = 响应帧 |
| 8 | 4 | payloadLen | 载荷字节数（上限 1 MiB） |
| 12 | 4 | crc32 | CRC-32（前 12 字节 + 载荷，IEEE 802.3） |

载荷均以小端序编码；除特殊说明外，载荷首 4 字节为 `correlationId`。

## 消息类型

| type | 名称 | 方向 | 载荷 |
| --- | --- | --- | --- |
| 1 | HELLO | DLL→UI | corr u32, pid u32, arch u32, engine u32, version u32, engineName UTF-8 |
| 2 | HELLO_ACK | UI→DLL | corr u32, ok u32, blockTimeoutMs u32, filtersEnabled u32 |
| 16 | TEXT_REQUEST | DLL→UI | corr u32, hash u64, engine u32, source UTF-8 |
| 17 | TEXT_RESULT | UI→DLL | corr u32, hash u64, ok u32, origin u8, target UTF-8（origin: 0=none 1=cache 2=dict 3=ai 4=manual） |
| 32 | LOG | DLL→UI | corr u32, level u8, message UTF-8（当前默认停用，见下） |
| 48 | UNLOAD | UI→DLL | corr u32 |
| 49 | UNLOAD_ACK | DLL→UI | corr u32, ok u32 |
| 64 | PING | DLL→UI | corr u32, tick u64 |
| 65 | PONG | UI→DLL | 同 PING 载荷 |
| 80 | STATS | 双向 | corr u32, hooked u32, translated u32, skipped u32 |

## 行为说明

- 连接后 DLL 先发 HELLO，UI 回 HELLO_ACK（携带阻塞超时与过滤开关）。
- TEXT_REQUEST/TEXT_RESULT 在同一连接上串行完成；DLL 等待超时后回原文。
- UNLOAD 由 UI 在任意时刻发送；DLL 回 UNLOAD_ACK 后进入卸载流程。
- PING/PONG 为协议保留；当前实现不主动发送 PING，存活由连接与请求流量判定（避免管道写会合死锁，见 architecture.md）。
- LOG 帧协议保留；当前实现停用，DLL 日志走 `GTI_LOG_FILE` 文件与 OutputDebugString。
- C++ 与 C# 共享同一组测试向量（tests/native/test_crc_frame.cpp 与 tests/GameTextInjector.Tests/IpcProtocolTests.cs）。
