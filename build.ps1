param(
    [switch]$Push
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot

Write-Host "== 1/5 配置并编译 NativeCore (x64) =="
cmake -S $root -B "$root\build\x64" -A x64 -DGTI_BUILD_TESTS=ON
cmake --build "$root\build\x64" --config Release

Write-Host "== 2/5 配置并编译 NativeCore (x86) =="
cmake -S $root -B "$root\build\x86" -A Win32 -DGTI_BUILD_TESTS=ON
cmake --build "$root\build\x86" --config Release

Write-Host "== 3/5 原生单元测试 =="
& "$root\build\x64\tests\native\Release\gti_native_tests.exe"
if ($LASTEXITCODE -ne 0) { throw "x64 native tests failed" }
& "$root\build\x86\tests\native\Release\gti_native_tests.exe"
if ($LASTEXITCODE -ne 0) { throw "x86 native tests failed" }

Write-Host "== 4/5 C# UI 编译 + 单元/集成测试 =="
dotnet build "$root\src\MemTextSwap.UI" -c Release -t:Rebuild
dotnet test "$root\tests\MemTextSwap.Tests" -c Release
if ($LASTEXITCODE -ne 0) { throw "C# tests failed" }

Write-Host "== 5/6 打包原生产物到 UI 输出目录 =="
$uiOut = "$root\src\MemTextSwap.UI\bin\Release\net8.0-windows"
Copy-Item "$root\build\x64\src\NativeCore\Release\NativeCore64.dll" $uiOut -Force
Copy-Item "$root\build\x64\src\NativeCore\Release\Injector64.exe" $uiOut -Force
Copy-Item "$root\build\x86\src\NativeCore\Release\NativeCore32.dll" $uiOut -Force
Copy-Item "$root\build\x86\src\NativeCore\Release\Injector32.exe" $uiOut -Force
Write-Host "已复制 NativeCore32/64.dll 与 Injector32/64.exe 到 $uiOut"

Write-Host "== 6/6 密钥扫描（推送门禁）=="
$patterns = 'sk-[A-Za-z0-9]{20,}|ghp_[A-Za-z0-9]{20,}|AKID[A-Za-z0-9]{10,}|BEGIN (RSA|OPENSSH|EC) PRIVATE KEY'
$hits = & rg --hidden -g '!build/**' -g '!third_party/**' -g '!.git/**' -n $patterns $root 2>$null
if ($LASTEXITCODE -eq 0 -and $hits) {
    Write-Error "检测到疑似密钥，禁止推送：`n$hits"
    exit 1
}
Write-Host "密钥扫描通过，未发现敏感信息。"

if ($Push) {
    Write-Host "== 推送门禁通过：提交并创建公开仓库 HKY-hub/MemTextSwap =="
    git -C $root add -A
    git -C $root commit -m "feat: MemTextSwap Phase 0 基础链路（双架构注入/IPC/翻译管线）" | Out-Null
    gh repo create MemTextSwap --public --source $root --remote origin --push
} else {
    Write-Host "未指定 -Push，跳过建仓推送。"
}

Write-Host "构建完成。产物位于 build\x64\src\NativeCore\Release 与 build\x86\src\NativeCore\Release。"
