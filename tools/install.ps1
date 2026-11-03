# Installs the built plugins into a D2R folder (a test slot or a frozen install copy), and optionally a modkit
# project's data into its mods folder. The game must be closed.
#   powershell -ExecutionPolicy Bypass -File tools\install.ps1 -Game C:\D2RTestSlots\Slot4
#   powershell -ExecutionPolicy Bypass -File tools\install.ps1 -Game C:\D2RTestSlots\Slot4 -Data tests\game\fixtures\split-throw
#
# Plugins are game-wide (ADR 0004): <Game>\d2rloader\plugins\. -Data runs `modkit build` and `modkit install` and then
# puts back each modinfo.json savepath, because modkit install resets it (a slot would otherwise play other saves).
param(
    [Parameter(Mandatory)] [string]$Game,
    [string]$Data = "",
    [string]$Toolkit = ""  # d2rAI-toolkit checkout (default: next to this repository)
)
$ErrorActionPreference = 'Stop'
if (-not $Toolkit) { $Toolkit = Join-Path $PSScriptRoot "..\..\d2rAI-toolkit" }
$Game = (Resolve-Path $Game).Path
if (-not (Test-Path (Join-Path $Game "D2R.exe"))) { throw "$Game is not a D2R folder" }
if (Get-Process D2R, D2RLoader -ErrorAction SilentlyContinue | Where-Object { $_.Path -and $_.Path.StartsWith($Game, 'OrdinalIgnoreCase') }) {
    throw "the game is running from ${Game}: close it first"
}

$plugins = Join-Path $Game "d2rloader\plugins"
New-Item -ItemType Directory -Force $plugins | Out-Null
foreach ($name in @("core-rules")) {
    $dll = Join-Path $PSScriptRoot "..\plugin\build\Release\$name.dll"
    if (-not (Test-Path $dll)) { throw "$dll missing: run plugin\build.ps1 first" }
    Copy-Item $dll $plugins -Force
    Write-Output "installed $name.dll -> $plugins"
}

if ($Data) {
    $Data = (Resolve-Path $Data).Path
    $mod = (Get-Content (Join-Path $Data "mod.json") -Raw | ConvertFrom-Json).name
    $root = Join-Path $Game "mods\$mod"
    $modinfos = @((Join-Path $root "modinfo.json"), (Join-Path $root "$mod.mpq\modinfo.json"))
    $saved = @{}
    foreach ($f in $modinfos) { if (Test-Path $f) { $saved[$f] = (Get-Content $f -Raw | ConvertFrom-Json).savepath } }
    Push-Location $Toolkit
    try {
        py -3 -m modkit build $Data
        if ($LASTEXITCODE -ne 0) { throw "modkit build failed ($LASTEXITCODE)" }
        py -3 -m modkit --game $Game install $Data
        if ($LASTEXITCODE -ne 0) { throw "modkit install failed ($LASTEXITCODE)" }
    } finally { Pop-Location }
    foreach ($f in $saved.Keys) {
        $info = Get-Content $f -Raw | ConvertFrom-Json
        $info.savepath = $saved[$f]
        [IO.File]::WriteAllText($f, ($info | ConvertTo-Json))  # UTF-8 without a BOM, as modkit writes it
        Write-Output "savepath kept: $($saved[$f]) in $f"
    }
}
