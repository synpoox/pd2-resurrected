# Removes a git worktree without ever deleting through a junction (docs/agents/game-check.md, "Worktree hygiene").
#   powershell -ExecutionPolicy Bypass -File tools\worktree_remove.ps1 -Path <worktree> [-DeleteBranch] [-Force]
# Every check runs before anything is removed. Refuses: the main checkout, a path that is not a worktree of this repo,
# a locked worktree (a live agent harness owns it; never unlocked here), a target that contains another worktree, a
# run from inside the target, and a dirty or untracked worktree (-Force overrides only that one).
# Then: record the file count and a sample file inside every junction's target and the shared caches' extract\ top
# level, unlink every reparse point under the worktree (never descending into one), re-scan and abort if any remain,
# `git worktree remove --force` (plain remove refuses because of the PluginSDK submodule), `git worktree prune`, and
# check the fingerprints are unchanged.
# -DeleteBranch deletes the local branch only if its tip is an ancestor of origin/main, or `gh pr list --head <branch>
# --state merged` returns a PR whose head SHA equals the tip; otherwise the branch is kept. Decided before removal.
param(
    [Parameter(Mandatory)] [string]$Path,
    [switch]$DeleteBranch,
    [switch]$Force
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent

function Norm([string]$p) { [IO.Path]::GetFullPath($p).TrimEnd('\', '/').Replace('/', '\').ToLowerInvariant() }
function Say([string]$m) { Write-Host $m }
function Inside([string]$child, [string]$parent) { $c = Norm $child; $p = Norm $parent; $c -eq $p -or $c.StartsWith($p + '\') }
# Native call: stderr never aborts on 5.1; callers read .Code.
function Native([string]$exe, [string[]]$a) {
    $ErrorActionPreference = 'Continue'
    $out = @(& $exe @a 2>&1 | ForEach-Object { "$_" })
    [pscustomobject]@{ Code = $LASTEXITCODE; Out = $out }
}
function GitC([string[]]$a) {
    $r = Native 'git' (@('-C', $repo) + $a)
    if ($r.Code -ne 0) { throw "git $a failed: $($r.Out -join ' ')" }
    $r.Out
}

# Worktree list: the first entry is the main checkout.
$trees = @(); $cur = $null
foreach ($line in (GitC @('worktree', 'list', '--porcelain'))) {
    if ($line -like 'worktree *') { $cur = @{ Path = $line.Substring(9); Branch = $null; Locked = $false }; $trees += $cur }
    elseif ($line -like 'branch *') { $cur.Branch = $line.Substring(7) -replace '^refs/heads/', '' }
    elseif ($line -eq 'locked' -or $line -like 'locked *') { $cur.Locked = $true; $cur.LockReason = $line.Substring(6).Trim() }
}
if (-not (Test-Path -LiteralPath $Path)) { throw "Refusing: $Path does not exist" }
$target = Norm $Path
$wt = $trees | Where-Object { (Norm $_.Path) -eq $target } | Select-Object -First 1
if (-not $wt) { throw "Refusing: $Path is not a worktree of this repo" }
if ((Norm $trees[0].Path) -eq $target) { throw "Refusing: $Path is the main checkout" }
if ($wt.Locked) { throw "Refusing: $Path is locked ($($wt.LockReason)); a live agent may own it. Not unlocking." }
if ((Inside $PWD.Path $Path) -or (Inside $PSScriptRoot $Path)) {
    throw "Refusing: the current directory or this script is inside $Path"
}
$nested = $trees | Where-Object { (Norm $_.Path) -ne $target -and (Inside $_.Path $Path) }
if ($nested) { throw "Refusing: $Path contains another worktree: $(($nested | ForEach-Object { $_.Path }) -join ', ')" }
$root = $wt.Path
$branch = $wt.Branch

if (-not $Force) {
    $dirty = @((Native 'git' @('-C', $root, 'status', '--porcelain')).Out)
    if ($dirty.Count -gt 0) {
        $dirty | ForEach-Object { Write-Host "  $_" }
        throw "Refusing: $root has uncommitted or untracked changes (listed above); commit them or pass -Force"
    }
}

# Decide the branch's fate now, while the tip is known and before anything is removed.
$branchVerdict = $null; $branchDelete = $false
if ($DeleteBranch) {
    if (-not $branch) { $branchVerdict = 'no branch to delete (detached worktree)' }
    else {
        $tip = (GitC @('rev-parse', '--verify', "refs/heads/$branch")) -join ''
        [void](Native 'git' @('-C', $repo, 'fetch', 'origin', '--prune', '-q'))
        if ((Native 'git' @('-C', $repo, 'merge-base', '--is-ancestor', $tip, 'origin/main')).Code -eq 0) {
            $branchDelete = $true; $branchVerdict = "deleted local branch $branch (tip $($tip.Substring(0, 9)) is in origin/main)"
        } else {
            # The remote name comes from the real upstream, which may differ from the local name.
            $up = ((Native 'git' @('-C', $repo, 'for-each-ref', '--format=%(upstream:short)', "refs/heads/$branch")).Out -join '').Trim()
            $head = if ($up -match '^[^/]+/(.+)$') { $Matches[1] } else { $branch }
            $g = Native 'gh' @('pr', 'list', '--head', $head, '--state', 'merged', '--json', 'headRefOid', '--jq', '.[].headRefOid')
            if ($g.Code -eq 0 -and ($g.Out -contains $tip)) {
                $branchDelete = $true; $branchVerdict = "deleted local branch $branch (a merged PR's head is its tip $($tip.Substring(0, 9)))"
            } else {
                $branchVerdict = "kept branch $branch (tip $($tip.Substring(0, 9)) is not in origin/main and no merged PR has it as head)"
            }
        }
    }
}

# Find every reparse point without descending into any of them.
function FindLinks {
    $found = @(); $stack = New-Object System.Collections.Stack; $stack.Push($root)
    while ($stack.Count) {
        $d = $stack.Pop()
        foreach ($e in (Get-ChildItem -LiteralPath $d -Force)) {
            if ($e.Attributes -band [IO.FileAttributes]::ReparsePoint) { $found += $e }
            elseif ($e.PSIsContainer) { $stack.Push($e.FullName) }
        }
    }
    $found
}
# File count and a sample file for a folder, followed through the link's target.
function Snapshot([string]$dir) {
    $n = 0; $sample = $null
    if (Test-Path -LiteralPath $dir -PathType Container) {
        foreach ($f in [IO.Directory]::EnumerateFiles($dir, '*', [IO.SearchOption]::AllDirectories)) { if (-not $sample) { $sample = $f }; $n++ }
    }
    @{ Count = $n; Sample = $sample }
}
$links = @(FindLinks)
$guards = @{}   # folder -> snapshot before
foreach ($l in $links) {
    foreach ($t in @($l.Target)) {
        if ($t -and -not $guards.ContainsKey($t) -and (Test-Path -LiteralPath $t -PathType Container)) { $guards[$t] = Snapshot $t }
    }
}
$caches = @('C:\pd2r-cache', 'F:\pd2r-cache', $env:PD2R_CACHE) | Where-Object { $_ -and (Test-Path -LiteralPath $_) } |
    ForEach-Object { (Resolve-Path -LiteralPath $_).Path } | Select-Object -Unique
foreach ($c in $caches) {
    $ex = Join-Path $c 'extract'
    if ((Test-Path -LiteralPath $ex) -and -not $guards.ContainsKey($ex)) {
        $top = @(Get-ChildItem -LiteralPath $ex -Force)
        $guards[$ex] = @{ Count = $top.Count; Sample = if ($top.Count) { $top[0].FullName } else { $null }; Top = $true }
    }
}
foreach ($g in $guards.Keys) { Say "guard $g`: $($guards[$g].Count) $(if ($guards[$g].Top) { 'top-level entries' } else { 'files' })" }

function CheckGuards([string]$when) {
    foreach ($g in $guards.Keys) {
        $b = $guards[$g]
        $a = if ($b.Top) { $top2 = @(Get-ChildItem -LiteralPath $g -Force); @{ Count = $top2.Count } } else { Snapshot $g }
        if ($a.Count -ne $b.Count -or ($b.Sample -and -not (Test-Path -LiteralPath $b.Sample))) {
            throw "ABORT: $g changed $when ($($b.Count) -> $($a.Count)); stop and restore it"
        }
    }
}

foreach ($l in $links) {
    if ($l.PSIsContainer) {
        # rmdir on a junction or directory symlink removes the link only.
        [void](Native 'cmd' @('/c', 'rmdir', "`"$($l.FullName)`""))
        if (Test-Path -LiteralPath $l.FullName) { throw "could not unlink $($l.FullName)" }
    } else {
        [IO.File]::Delete($l.FullName)
    }
    Say "unlinked $($l.FullName)"
}
if (-not $links) { Say "no reparse points under $root" }
CheckGuards 'after unlinking'

# Last check before the destructive call: nothing may still point elsewhere.
$left = @(FindLinks)
if ($left.Count) { throw "ABORT: reparse points remain, not removing: $(($left | ForEach-Object { $_.FullName }) -join ', ')" }

[void](GitC @('worktree', 'remove', '--force', $root))
Say "removed worktree $root"
[void](GitC @('worktree', 'prune'))
Say "pruned worktree list"
CheckGuards 'after removal'
Say "junction targets and caches unchanged"

if ($branchVerdict) {
    if ($branchDelete) {
        $r = Native 'git' @('-C', $repo, 'branch', '-D', $branch)
        if ($r.Code -ne 0) { throw "could not delete branch $branch`: $($r.Out -join ' ')" }
    }
    Say $branchVerdict
}
