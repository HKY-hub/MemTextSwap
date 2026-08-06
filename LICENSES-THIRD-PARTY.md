# 第三方许可与版权声明

本项目的整体许可证为 GPL-3.0（见 [LICENSE](./LICENSE)）。以下为本项目使用/参考的第三方组件与项目的许可证及版权归属。本项目实现代码均为原创；对下列项目仅借鉴架构与接口思路，未复制其实现（如确需复用片段，将在对应文件头部保留版权声明）。

| 组件/项目 | 许可证 | 用途 | 版权声明 |
| --- | --- | --- | --- |
| MinHook（vendored 于 `third_party/minhook`，commit `c3fcafd`，tag v1.3.4） | MIT | 内存 Hook 库 | Copyright (C) 2009-2017 Tsuda Kageyu，见 `third_party/minhook/LICENSE.txt` |
| Avalonia / Avalonia.Desktop / Avalonia.Themes.Fluent / Avalonia.Controls.DataGrid | MIT | C# UI 框架 | Copyright (c) AvaloniaUI Contributors |
| CommunityToolkit.Mvvm | MIT | MVVM | Copyright (c) .NET Foundation and Contributors |
| Microsoft.Extensions.DependencyInjection | MIT | DI | Copyright (c) .NET Foundation and Contributors |
| Microsoft.Data.Sqlite | MIT | SQLite 缓存 | Copyright (c) .NET Foundation and Contributors |
| xUnit / xunit.runner.visualstudio / Microsoft.NET.Test.Sdk | Apache-2.0 / MIT | C# 测试 | 见各包许可证 |
| GoogleTest | BSD-3-Clause | C++ 测试 | Copyright 2008, Google Inc. |

## 架构参考项目（未复制代码）

- **LunaTranslator**（GPL-3.0）：Hook/IPC/引擎检测/翻译调度思路参考。
- **XUnity.AutoTranslator**（MIT）：Unity 文本替换思路参考。
- **Renpy-Translator**（MIT）：RenPy Python 层字符串替换思路参考。

这些项目仅作为设计参考；本仓库不包含其源代码。
