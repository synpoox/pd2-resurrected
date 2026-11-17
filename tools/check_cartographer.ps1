# Throws unless -Dll is a cartographer.dll that knows all 25 tilesets (d2r-tools f4964090 or later; the pin, tools\cartographer.pin, #1300).
# The DLL carries no build stamp, so the check is its content: tileset names the last builds add ("KurastSewer",
# "Catacombs", "Tombs", "MazeTemple"; older DLLs lack them and skip those maps). The names prove all 25 tilesets, not the exact commit. With -Commit (release.yml: CARTOGRAPHER_COMMIT) it also
# checks the pin is an ancestor of that commit, when a d2r-tools checkout (-Repo) can answer.
param([Parameter(Mandatory)][string]$Dll, [string]$Commit = "", [string]$Repo = "")
$ErrorActionPreference = 'Stop'
$pin = (Get-Content (Join-Path $PSScriptRoot "cartographer.pin") | Where-Object { $_ -match '^[0-9a-f]{40}$' } | Select-Object -First 1)
if (-not $pin) { throw "tools\cartographer.pin holds no 40-digit commit" }
$rebuild = "rebuild it from d2r-tools at or after $($pin.Substring(0,8)): git -C <d2r-tools> pull; powershell tools\build.ps1 -Plugin cartographer (dist\cartographer\cartographer.dll)"
if (-not (Test-Path $Dll)) { throw "$Dll missing: $rebuild" }
$text = [Text.Encoding]::GetEncoding(28591).GetString([IO.File]::ReadAllBytes($Dll))
foreach ($m in "KurastSewer", "Catacombs", "Tombs", "MazeTemple") {
    if (-not $text.Contains($m)) { throw "$Dll is older than d2r-tools $($pin.Substring(0,8)) (no '$m' tileset, 12 tilesets missing): $rebuild" }
}
if ($Commit) {
    if ($Commit -notmatch '^[0-9a-f]{7,40}$') { throw "CARTOGRAPHER_COMMIT '$Commit' is not a commit id" }
    if ($Repo -and (Test-Path (Join-Path $Repo ".git"))) {
        git -C $Repo merge-base --is-ancestor $pin $Commit 2>$null
        if ($LASTEXITCODE -ne 0) { throw "CARTOGRAPHER_COMMIT $Commit is older than the pin $pin (or unknown in $Repo)" }
    } else { Write-Warning "no d2r-tools checkout at '$Repo': commit $Commit not compared to the pin, DLL content checked only" }
}
Write-Output "cartographer.dll ok (pin $($pin.Substring(0,8))): $Dll"
