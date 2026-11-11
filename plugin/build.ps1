# Builds every plugin (Visual Studio generator, x64, Release) into plugin\build\Release\.
#   powershell -ExecutionPolicy Bypass -File plugin\build.ps1
$ErrorActionPreference = 'Stop'
$build = Join-Path $PSScriptRoot "build"
# A fresh worktree has an empty PluginSDK submodule folder: initialise it here instead of failing in cmake.
$sdk = Join-Path $PSScriptRoot "../third_party/PluginSDK"
if (-not (Test-Path $sdk) -or -not (Get-ChildItem -Force $sdk -ErrorAction SilentlyContinue | Select-Object -First 1)) {
    Write-Output "PluginSDK submodule is empty: running git submodule update --init third_party/PluginSDK"
    git -C (Join-Path $PSScriptRoot "..") submodule update --init third_party/PluginSDK
    if ($LASTEXITCODE -ne 0) { throw "git submodule update --init third_party/PluginSDK failed ($LASTEXITCODE)" }
}
cmake -S $PSScriptRoot -B $build -A x64
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed ($LASTEXITCODE)" }
cmake --build $build --config Release
if ($LASTEXITCODE -ne 0) { throw "cmake build failed ($LASTEXITCODE)" }
Write-Output "built: core-rules.dll, core-maps.dll, core-ui.dll in $(Join-Path $build 'Release')"
