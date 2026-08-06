# MemTextSwap

通用 PC 单机 GAL/RPG 游戏 · **纯内存 REPLACE 内嵌汉化工具**（对标 RenpyThief 6.0 核心功能）。

100% 开源免费（GPL-3.0）。支持 Unity Mono / Unity IL2CPP（含混淆版）/ RenPy / RPGMaker MV·MZ。只替换内存指针、不修改游戏磁盘文件、无 Overlay/OCR/弹窗。

## 功能

- 进程列表（PID/名称/位数/引擎自动识别），一键注入/停止/卸载
- 翻译后端双模式：本地词典（AutoTranslator 兼容 flat/JSON）+ SQLite 永久缓存；AI 在线（OpenAI 兼容协议，默认 DeepSeek）
- 强制 AI 清洗规则：只输出简体中文译文，去 markdown/符号/换行
- 严格 32/64 位隔离；TLS 防递归；SEH 防崩；卸载自动恢复 Hook、不杀游戏进程
- 16 字节包头 + CRC32 + 心跳协议（PING/PONG 保留）

## 快速开始

```powershell
.\build.ps1
```

产物：

```
build/x64/src/NativeCore/Release/NativeCore64.dll + Injector64.exe + TestTarget64.exe
build/x86/src/NativeCore/Release/NativeCore32.dll + Injector32.exe + TestTarget32.exe
src/MemTextSwap.UI/bin/Release/net8.0-windows/MemTextSwap.exe
```

启动 UI（管理员权限）→ 刷新进程 → 选中目标 → 注入。翻译设置中配置词典路径或 AI（密钥建议用环境变量 `GTI_DEEPSEEK_API_KEY`）。

## 技术栈

- NativeCore：C++17 / MSVC2022 / CMake / MinHook / Win32 / 命名管道
- UI：.NET 8 / Avalonia 11 / MVVM / DI / SQLite

## 目录

```
src/NativeCore/           C++ 底层 DLL（含 Injector/TestTarget/管道探针工具）
src/MemTextSwap.UI/  Avalonia 前端
third_party/minhook/      MinHook v1.3.4（MIT，固定 commit）
tests/                    原生 GoogleTest + C# xUnit（含端到端注入测试）
docs/                     架构、协议、引擎、配置说明
```

## 许可

GPL-3.0，详见 [LICENSE](./LICENSE) 与 [LICENSES-THIRD-PARTY.md](./LICENSES-THIRD-PARTY.md)。

## 当前状态

- ✅ Phase 0 基础链路：双架构构建、位数校验、注入/卸载、IPC 串行协议、翻译管线（缓存/词典/AI）、端到端集成测试（x86/x64）
- 🟡 Phase 1-3：IL2CPP/Mono/RenPy/RPGMaker Hook 代码就绪，待真实游戏/官方 Demo 验收（见 `docs/engines.md`）
- 🟡 Phase 4-5：词典/AI 服务已实现，UI 美化待完善
