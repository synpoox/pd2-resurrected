# CI: puts the d2rAI-toolkit commit that toolkit.version pins in its own worktree on the runner and points the rest of
# the job at it (D2R_TOOLKIT, PYTHONPATH, strict pin check). The runner's shared checkout (the D2R_TOOLKIT variable) is only
# fetched, never moved, so a pull or a checked-out branch there cannot change what a job builds against.
#   powershell -ExecutionPolicy Bypass -File tools\ci_toolkit.ps1 -Main <the runner's toolkit checkout>
param([Parameter(Mandatory)] [string]$Main, [string]$Base = "C:\pd2r-cache\toolkit")
$ErrorActionPreference = 'Continue'  # native stderr is progress here; every git call's exit code is checked
$pin = @(Get-Content (Join-Path $PSScriptRoot "..\toolkit.version") | ForEach-Object { "$_".Trim() } | Where-Object { $_ -and -not $_.StartsWith('#') })[0]
if ($pin -notmatch '^[0-9a-fA-F]{7,40}$') { throw "toolkit.version: the first non-comment line must be a commit SHA" }
# The runner's jobs run at once on one machine: one at a time may fetch in the shared checkout and make the worktree.
$mutex = [System.Threading.Mutex]::new($false, 'Global\pd2r-ci-toolkit')
[void]$mutex.WaitOne()
try {
    git -C $Main cat-file -e "$pin^{commit}" 2>$null
    if ($LASTEXITCODE -ne 0) {
        git -C $Main fetch -q origin
        if ($LASTEXITCODE -ne 0) { throw "git fetch in $Main failed ($LASTEXITCODE)" }
    }
    $sha = (git -C $Main rev-parse "$pin^{commit}").Trim()
    if ($LASTEXITCODE -ne 0 -or $sha -notmatch '^[0-9a-f]{40}$') { throw "toolkit commit $pin is not in $Main (pushed to origin?)" }
    $dir = Join-Path $Base $sha
    if (-not (Test-Path (Join-Path $dir "modkit"))) {
        # a half-made worktree (a cancelled job) or a folder deleted by hand: drop its registration and folder, then add it again
        if (Test-Path $dir) { git -C $Main worktree remove --force $dir 2>$null }
        if (Test-Path $dir) { Remove-Item -Recurse -Force $dir }
        git -C $Main worktree prune  # forgets worktrees whose folder is gone, which `worktree add` would refuse
        git -C $Main worktree add --detach $dir $sha
        if ($LASTEXITCODE -ne 0) { throw "git worktree add $dir failed ($LASTEXITCODE)" }
    }
    git -C $dir submodule update --init third_party/PluginSDK
    if ($LASTEXITCODE -ne 0) { throw "PluginSDK submodule update in $dir failed ($LASTEXITCODE)" }
} finally {
    $mutex.ReleaseMutex()
}
# build/ is untracked: only the shared checkout has the CascLib the toolkit's modkit loads
$casc = Join-Path $Main "build\CascLib\Release\CascLib.dll"
if (-not (Test-Path $casc)) { throw "CascLib is missing in $Main ($casc): run tools/build_casclib.ps1 there" }
Write-Output "toolkit $sha at $dir"
if ($env:GITHUB_ENV) {
    Add-Content -Encoding utf8 $env:GITHUB_ENV "D2R_TOOLKIT=$dir"
    Add-Content -Encoding utf8 $env:GITHUB_ENV "PYTHONPATH=$dir"
    Add-Content -Encoding utf8 $env:GITHUB_ENV "D2R_TOOLKIT_STRICT=1"
    Add-Content -Encoding utf8 $env:GITHUB_ENV "MODKIT_CASCLIB=$casc"
}
