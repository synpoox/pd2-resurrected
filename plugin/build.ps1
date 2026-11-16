# Builds every plugin (Visual Studio generator, x64, Release) into plugin\build\Release\ with the toolkit's template
# build script (plugin-template\build.ps1) over this folder's CMakeLists.txt, which lists the plugins (modkit_add_plugin).
#   powershell -ExecutionPolicy Bypass -File plugin\build.ps1 [-Tests]
# The toolkit is found as docs/toolkit-finder.md (in the toolkit) says: D2R_TOOLKIT, else the nearest d2rAI-toolkit up the tree.
param([switch]$Tests)
$ErrorActionPreference = 'Stop'
$boot = if ($env:D2R_TOOLKIT) { $env:D2R_TOOLKIT } else {
    $dir = Split-Path $PSScriptRoot -Parent
    while ($dir -and -not (Test-Path (Join-Path $dir "d2rAI-toolkit\tools\ToolkitPath.ps1"))) { $dir = Split-Path $dir -Parent }
    if ($dir) { Join-Path $dir "d2rAI-toolkit" }
}
if (-not $boot) { throw "d2rAI-toolkit not found beside any parent folder: set D2R_TOOLKIT" }
. (Join-Path $boot "tools\ToolkitPath.ps1")
$toolkit = Find-D2rToolkit -From $PSScriptRoot  # warns when toolkit.version differs from the toolkit's HEAD
& (Join-Path $toolkit "plugin-template\build.ps1") -Source $PSScriptRoot -Name "core-rules", "core-events", "core-ui" -Toolkit $toolkit -Tests:$Tests
if ($LASTEXITCODE -ne 0) { throw "plugin build failed ($LASTEXITCODE)" }
