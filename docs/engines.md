# 引擎检测与 Hook 现状

## 检测表（进程内模块扫描 / UI 静态检测）

| 引擎 | 判定依据 | Hook 策略 | 状态 |
| --- | --- | --- | --- |
| TestTarget（自建验收目标） | exe 导出 `GTI_ProduceLine` | MinHook 直接替换返回值 | ✅ 已验收（x86/x64 集成测试） |
| Unity IL2CPP | `GameAssembly.dll` + `UnityPlayer.dll` | `il2cpp_string_new` 系列导出 Hook；混淆构建签名扫描 | 🟡 代码就绪，待真实游戏验收（本机 FAPNAF 样本可用） |
| Unity Mono | `UnityPlayer.dll` + `mono-2.0-bdwgc.dll`/`mono.dll` | `mono_string_new` 系列导出 Hook | 🟡 代码就绪，官方 Mono 测试台待建 |
| RenPy | `renpy/` 目录或 `python*.dll` | Python C API 包装 `translate_string`/`Text` | 🟡 代码就绪，官方 Demo 待下载验收 |
| RPGMaker MV/MZ | `nw.dll` + `www/js/rpg_core.js` / `rmmz_core.js` | `v8::Script::Run` 签名 + JS bootstrap | 🟡 框架就绪，NW.js 测试台待建 |

## 已勘察的真实样本（本机）

- FAPNAF ARCADE v0.2.7XX：x86 IL2CPP，导出完整（`il2cpp_string_new` 等可用），metadata 魔数 `FAB11BAF`。
- FAPNAF ARCADE v0.2.8 / SUMMER：x64 IL2CPP，导出完整。
- FAPNAF STORYMODE 0.3.0：x64 IL2CPP 混淆版（导出名混淆、metadata 非标准魔数），需签名扫描路径。

## 验收基准

- Phase 0（已完成）：TestTarget x86/x64 注入→握手→词典翻译回显→卸载恢复→进程存活。
- Phase 1-3：FAPNAF（x86/x64/混淆）、RenPy 官方 Demo、自建 NW.js 测试台。
