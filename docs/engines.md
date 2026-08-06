# 引擎检测与 Hook 现状

## 检测表（进程内模块扫描 / UI 静态检测）

| 引擎 | 判定依据 | Hook 策略 | 状态 |
| --- | --- | --- | --- |
| TestTarget（自建验收目标） | exe 导出 `GTI_ProduceLine` | MinHook 直接替换返回值 | ✅ 已验收（x86/x64 集成测试） |
| Unity IL2CPP | `GameAssembly.dll` + `UnityPlayer.dll` | `il2cpp_string_new` 系列导出 Hook；混淆构建签名扫描 | ✅ FAPNAF v0.2.8（x64）真实文本捕获已验证 |
| Unity Mono | `UnityPlayer.dll` + `mono-2.0-bdwgc.dll`/`mono.dll` | `mono_string_new` 系列导出 Hook | 🟡 代码就绪，官方 Mono 测试台待建 |
| RenPy | `renpy/` 目录或 `python*.dll`（含 `libpython3.12.dll`） | Python C API 包装 `translate_string`/`Text` | ✅ RenPy 8.5.3 SDK 官方引擎 Python 补丁应用成功 |
| RPGMaker MV/MZ | `nw.dll` + `www/js/rpg_core.js` / `rmmz_core.js` | `v8::Script::Run` 签名 + JS bootstrap | 🟡 NW.js v0.95 测试台引擎识别/IPC 已验证；V8 签名表待填充 |

## 已勘察的真实样本（本机）

- FAPNAF ARCADE v0.2.7XX：x86 IL2CPP，导出完整（`il2cpp_string_new` 等可用），metadata 魔数 `FAB11BAF`。
- FAPNAF ARCADE v0.2.8 / SUMMER：x64 IL2CPP，导出完整。
- FAPNAF STORYMODE 0.3.0：x64 IL2CPP 混淆版（导出名混淆、metadata 非标准魔数），需签名扫描路径。

## 验收基准

- Phase 0（已完成）：TestTarget x86/x64 注入→握手→词典翻译回显→卸载恢复→进程存活。
- Phase 1（冒烟已过）：FAPNAF ARCADE v0.2.8（x64 IL2CPP）真实 UI 文本（“Text INGLES / GAME MODE / PRESS F1...”等）被捕获并流入翻译管线，进程存活；游戏内中文显示待人工目验。
- Phase 2（冒烟已过）：RenPy 8.5.3 官方 SDK（`libpython3.12.dll`）引擎识别 + Python 层补丁应用成功；教程对话中文显示待人工目验。
- Phase 3（冒烟已过）：NW.js v0.95 + 自建 RPGMaker 外形测试台，引擎识别与 IPC 握手通过；V8 JS bootstrap 需真实样本签名分析后启用。

## 验收资产（下载于 `C:\Users\h'k'y\Desktop\gti-acceptance`）

- `renpy-8.5.3-sdk`（RenPy 官方 SDK，含 tutorial/launcher）
- `nwjs-v0.95.0-win-x64`（NW.js 运行时）
- `nwjs-harness`（自建 RPGMaker 外形测试台，源码在 `tools/nwjs-harness`）
