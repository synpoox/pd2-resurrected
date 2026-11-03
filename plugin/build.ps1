# Builds every plugin (Visual Studio generator, x64, Release) into plugin\build\Release\.
#   powershell -ExecutionPolicy Bypass -File plugin\build.ps1
$ErrorActionPreference = 'Stop'
$build = Join-Path $PSScriptRoot "build"
cmake -S $PSScriptRoot -B $build -A x64
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed ($LASTEXITCODE)" }
cmake --build $build --config Release
if ($LASTEXITCODE -ne 0) { throw "cmake build failed ($LASTEXITCODE)" }
Write-Output "built: $(Join-Path $build 'Release\core-rules.dll')"
