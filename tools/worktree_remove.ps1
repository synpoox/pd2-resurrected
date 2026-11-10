# Removes a git worktree without ever deleting through a junction (docs/agents/game-check.md, "Worktree hygiene").
#   powershell -ExecutionPolicy Bypass -File tools\worktree_remove.ps1 -Path <worktree> [-DeleteBranch] [-Force]
# Refuses the main checkout, a path that is not a worktree of this repo, and a worktree with uncommitted or untracked
# changes (-Force overrides that one). Unlinks every reparse point under the worktree (never recursing into one),
# checks the shared caches (C:\pd2r-cache, F:\pd2r-cache, $env:PD2R_CACHE) are unchanged, then runs
# `git worktree remove --force` (plain remove refuses because of the PluginSDK submodule) and `git worktree prune`.
# -DeleteBranch deletes the local branch only if it is merged into origin/main, or its remote branch is gone (it had
# an upstream that was deleted), or `gh pr list --head <branch> --state merged` finds its squash-merged PR.
param(
    [Parameter(Mandatory)] [string]$Path,
    [switch]$DeleteBranch,
    [switch]$Force
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent

function Norm([string]$p) { [IO.Path]::GetFullPath($p).TrimEnd('\', '/').Replace('/', '\').ToLowerInvariant() }
function Say([string]$m) { Write-Host $m }
function GitC { $out = & git -C $repo @args 2>&1; if ($LASTEXITCODE -ne 0) { throw "git $args failed: $out" }; $out }

# Worktree list: the first entry is the main checkout.
$trees = @(); $cur = $null
foreach ($line in (GitC worktree list --porcelain)) {
    if ($line -like 'worktree *') { $cur = @{ Path = $line.Substring(9); Branch = $null }; $trees += $cur }
    elseif ($line -like 'branch *') { $cur.Branch = $line.Substring(7) -replace '^refs/heads/', '' }
}
if (-not (Test-Path -LiteralPath $Path)) { throw "Refusing: $Path does not exist" }
$target = Norm $Path
$wt = $trees | Where-Object { (Norm $_.Path) -eq $target } | Select-Object -First 1
if (-not $wt) { throw "Refusing: $Path is not a worktree of this repo" }
if ((Norm $trees[0].Path) -eq $target) { throw "Refusing: $Path is the main checkout" }
if ((Norm $PWD.Path) -eq $target -or (Norm $PWD.Path).StartsWith($target + '\')) {
    throw "Refusing: the current directory is inside $Path"
}
$root = $wt.Path
$branch = $wt.Branch

if (-not $Force) {
    $dirty = @(& git -C $root status --porcelain 2>&1)
    if ($dirty.Count -gt 0) {
        $dirty | ForEach-Object { Write-Host "  $_" }
        throw "Refusing: $root has uncommitted or untracked changes (listed above); commit them or pass -Force"
    }
}

# Shared-cache fingerprint: top-level count of extract\ plus the first entry's existence.
$caches = @('C:\pd2r-cache', 'F:\pd2r-cache', $env:PD2R_CACHE) | Where-Object { $_ -and (Test-Path -LiteralPath $_) } |
    ForEach-Object { (Resolve-Path -LiteralPath $_).Path } | Select-Object -Unique
function Fingerprint {
    $fp = @{}
    foreach ($c in $caches) {
        $ex = Join-Path $c 'extract'
        $items = @(if (Test-Path -LiteralPath $ex) { Get-ChildItem -LiteralPath $ex -Force | Sort-Object Name })
        $fp[$c] = @{ Count = $items.Count; Sample = if ($items.Count) { $items[0].FullName } else { $null } }
    }
    $fp
}
$before = Fingerprint
foreach ($c in $caches) { Say "cache $c\extract: $($before[$c].Count) top-level entries" }

# Find every reparse point without descending into any of them.
$links = @(); $stack = New-Object System.Collections.Stack; $stack.Push($root)
while ($stack.Count) {
    $d = $stack.Pop()
    foreach ($e in (Get-ChildItem -LiteralPath $d -Force)) {
        if ($e.Attributes -band [IO.FileAttributes]::ReparsePoint) { $links += $e }
        elseif ($e.PSIsContainer) { $stack.Push($e.FullName) }
    }
}
foreach ($l in $links) {
    if ($l.PSIsContainer) {
        # rmdir on a junction or directory symlink removes the link only.
        & cmd /c rmdir "`"$($l.FullName)`"" | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "could not unlink $($l.FullName)" }
        if (Test-Path -LiteralPath $l.FullName) { throw "link still present after unlink: $($l.FullName)" }
    } else {
        [IO.File]::Delete($l.FullName)
    }
    Say "unlinked $($l.FullName)"
}
if (-not $links) { Say "no reparse points under $root" }

function CheckCache([string]$when) {
    $after = Fingerprint
    foreach ($c in $caches) {
        $b = $before[$c]; $a = $after[$c]
        if ($a.Count -ne $b.Count -or ($b.Sample -and -not (Test-Path -LiteralPath $b.Sample))) {
            throw "ABORT: cache $c\extract changed $when (entries $($b.Count) -> $($a.Count)); stop and restore it"
        }
    }
}
CheckCache 'after unlinking'

GitC worktree remove --force $root | Out-Null
Say "removed worktree $root"
GitC worktree prune | Out-Null
Say "pruned worktree list"
CheckCache 'after removal'
Say "cache unchanged"

if ($DeleteBranch) {
    if (-not $branch) { Say "no branch to delete (detached worktree)" }
    else {
        $reason = $null
        & git -C $repo fetch origin --prune -q 2>$null
        & git -C $repo merge-base --is-ancestor $branch origin/main 2>$null
        if ($LASTEXITCODE -eq 0) { $reason = 'merged into origin/main' }
        if (-not $reason) {
            # Remote gone only counts when the branch had an upstream (a never-pushed branch is not "gone").
            $up = & git -C $repo config --get "branch.$branch.merge" 2>$null
            & git -C $repo rev-parse --verify -q "refs/remotes/origin/$branch" 2>$null | Out-Null
            if ($up -and $LASTEXITCODE -ne 0) { $reason = 'its remote branch is gone' }
        }
        if (-not $reason) {
            $n = & gh pr list --head $branch --state merged --json number --jq 'length' 2>$null
            if ($n -and [int]$n -gt 0) { $reason = 'its PR is merged' }
        }
        if ($reason) {
            & git -C $repo branch -D $branch 2>&1 | Out-Null
            if ($LASTEXITCODE -ne 0) { throw "could not delete branch $branch" }
            Say "deleted local branch $branch ($reason)"
        } else { Say "kept branch $branch (not merged and its remote branch still exists or never existed)" }
    }
}
