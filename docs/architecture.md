# GameTextInjector 架构

## 总览

GameTextInjector 是纯内存 REPLACE 汉化工具：捕获游戏引擎产生的原文，经本地缓存/词典/AI 得到译文后，在游戏进程内新建引擎合法字符串对象并替换指针。不修改游戏磁盘文件，无 Overlay/OCR/弹窗。

```
┌──────────────────────────────┐       命名管道        ┌──────────────────────────────┐
│ GameTextInjector.UI (C#)     │ ◄──────────────────► │ NativeCore32/64.dll (C++)     │
│ 进程列表 / 注入 / 设置 / 日志 │    GTI_{pid}_{guid}  │ 引擎检测 / MinHook / 文本过滤  │
│ 翻译管线：缓存→词典→AI→写缓存 │                      │ 请求串行化 / 卸载恢复          │
└──────────────────────────────┘                      └──────────────────────────────┘
```

注入由 UI 按目标位数拉起 `Injector32/64.exe`，注入器校验 PE 位数后执行 `CreateRemoteThread + LoadLibraryW`，随后远程线程调用 `GTI_ThreadEntry`。

## 关键设计

### 位数隔离
- 注入器与 DLL 均双架构构建；注入前读取目标 PE 机器类型（0x14C/0x8664），跨位直接拒绝。
- x64 注入基址通过 `EnumProcessModulesEx` 获取（`GetExitCodeThread` 只有 32 位，不能承载 x64 HMODULE）。

### IPC 串行化
- 单连接、单锁：TEXT_REQUEST 的写+读在同一把 `writeMutex_` 下完成，控制线程的读也走同一把锁。
- 服务端显式设置 8KB 输入/输出管道缓冲区。经验教训：.NET `NamedPipeServerStream` 默认小缓冲会让写操作呈现“对端读走才完成”的会合语义，并发读写会互相卡死；显式缓冲 + 串行化后稳定。
- 卸载：UI 发 UNLOAD → DLL 停止 IPC、卸载全部 Hook、等待在途请求退出（≤3.5s）→ 工作线程结束。模块故意保留加载（防游戏线程正执行 detour 代码时被卸载），游戏功能完全恢复，进程不终止。

### Hook 策略
- 通用防护：`thread_local` 防递归 + `__try/__except` SEH + 文本过滤（长度/中文/路径/URL/去重/限流）+ 失败回原文。
- Unity IL2CPP：优先按导出名解析 `il2cpp_string_new` 系列；标准构建可用；混淆构建走签名扫描框架（模式表待真实样本分析后填充）。
- Unity Mono：`mono_string_new` 系列导出 Hook。
- RenPy：Python C API 注入 `translate_string`/`Text` 包装，经 ctypes 回调 DLL 翻译。
- RPGMaker MV/MZ：`v8::Script::Run` 签名扫描 + JS bootstrap（模式表待真实样本分析）。

### 翻译管线
捕获原文 → SQLite 缓存 → 词典 → AI（DeepSeek/OpenAI 兼容）→ 术语修正（清洗器）→ 写缓存 → IPC 回传 → 指针替换。

## 与计划的偏差（有意为之）

- 心跳：PING/PONG 协议保留，默认不发送（写会合死锁风险），UI 以连接与请求流量判定存活。
- LOG 帧：协议保留，默认停用；DLL 日志写 `GTI_LOG_FILE` 文件与 OutputDebugString，UI 日志面板显示 UI 侧事件。
- 卸载：不执行 `FreeLibrary`，模块保留（安全原因，见上）。
- RenPy/RPGMaker 的 V8/签名模式表为空：需要真实样本（RenPy 官方 Demo、NW.js 测试台）分析后填充，代码框架已就位。
