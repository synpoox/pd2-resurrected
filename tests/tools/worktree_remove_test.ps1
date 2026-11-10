# Tests tools\worktree_remove.ps1 on scratch worktrees and scratch junction targets (never the real cache).
#   powershell -ExecutionPolicy Bypass -File tests\tools\worktree_remove_test.ps1
$ErrorActionPreference = 'Stop'
$repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$script = Join-Path $repo 'tools\worktree_remove.ps1'
$scratch = Join-Path ([IO.Path]::GetTempPath()) ("wtr-test-" + [guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory $scratch | Out-Null
$fail = 0
function Check([string]$name, [bool]$ok) { if ($ok) { Write-Host "PASS $name" } else { Write-Host "FAIL $name"; $script:fail++ } }
function Run([string[]]$a) {
    $ErrorActionPreference = 'Continue'
    $out = & powershell -NoProfile -ExecutionPolicy Bypass -File $script @a 2>&1 | Out-String
    [pscustomobject]@{ Code = $LASTEXITCODE; Out = $out }
}
function NewWt([string]$name, [string]$start = 'origin/main') {
    $p = Join-Path $scratch $name
    & git -C $repo worktree add -q -b "scratch/$name" $p $start
    $p
}

try {
    # Refusals
    $main = ((& git -C $repo worktree list --porcelain | Select-Object -First 1) -replace '^worktree ', '')
    $r = Run @('-Path', $main); Check 'refuses main checkout' ($r.Code -ne 0 -and $r.Out -match 'main checkout')
    $r = Run @('-Path', $scratch); Check 'refuses non-worktree' ($r.Code -ne 0 -and $r.Out -match 'not a worktree')
    $wt = NewWt 'dirty'
    Set-Content (Join-Path $wt 'untracked.txt') 'x'
    $r = Run @('-Path', $wt)
    Check 'refuses dirty worktree and lists it' ($r.Code -ne 0 -and $r.Out -match 'untracked.txt' -and (Test-Path $wt))
    $r = Run @('-Path', $wt, '-Force'); Check '-Force removes dirty worktree' ($r.Code -eq 0 -and -not (Test-Path $wt))
    & git -C $repo branch -D scratch/dirty | Out-Null

    # Junctions: top-level and nested, targets must survive.
    $target = Join-Path $scratch 'target'; New-Item -ItemType Directory $target | Out-Null
    Set-Content (Join-Path $target 'keep.txt') 'keep'
    $target2 = Join-Path $scratch 'target2'; New-Item -ItemType Directory $target2 | Out-Null
    Set-Content (Join-Path $target2 'keep2.txt') 'keep2'
    $wt = NewWt 'links'
    & git -C $wt commit -q --allow-empty -m scratch
    New-Item -ItemType Junction -Path (Join-Path $wt 'pd2-data') -Target $target | Out-Null
    New-Item -ItemType Directory (Join-Path $wt 'sub\deep') -Force | Out-Null
    New-Item -ItemType Junction -Path (Join-Path $wt 'sub\deep\nested') -Target $target2 | Out-Null
    $r = Run @('-Path', $wt, '-DeleteBranch', '-Force')  # -Force: the nested junction shows as untracked
    Write-Host $r.Out
    Check 'removal succeeds' ($r.Code -eq 0)
    Check 'worktree is gone' (-not (Test-Path $wt))
    Check 'junction target survives' (Test-Path (Join-Path $target 'keep.txt'))
    Check 'nested junction target survives' (Test-Path (Join-Path $target2 'keep2.txt'))
    Check 'nested junction reported' ($r.Out -match 'unlinked .*nested')
    Check 'worktree no longer listed' (-not ((& git -C $repo worktree list) -match 'wtr-test'))
    Check 'unmerged branch kept' ($r.Out -match 'kept branch' -and (& git -C $repo branch --list 'scratch/links'))

    # Locked: refused before anything is touched, and not unlocked.
    $wt = NewWt 'locked'
    New-Item -ItemType Junction -Path (Join-Path $wt 'pd2-data') -Target $target | Out-Null
    & git -C $repo worktree lock --reason 'live agent' $wt
    $r = Run @('-Path', $wt)
    Check 'refuses locked worktree' ($r.Code -ne 0 -and $r.Out -match 'locked')
    Check 'locked worktree untouched (junction kept, still locked)' ((Test-Path (Join-Path $wt 'pd2-data\keep.txt')) -and ((& git -C $repo worktree list --porcelain) -match '^locked'))
    & git -C $repo worktree unlock $wt
    cmd /c rmdir (Join-Path $wt 'pd2-data')
    & git -C $repo worktree remove --force $wt

    # Nested worktree: refused, both stay.
    $outer = NewWt 'outer'
    $inner = Join-Path $outer 'inner'
    & git -C $repo worktree add -q -b scratch/inner $inner origin/main
    $r = Run @('-Path', $outer)
    Check 'refuses a target containing another worktree' ($r.Code -ne 0 -and $r.Out -match 'contains another worktree' -and (Test-Path $inner))
    & git -C $repo worktree remove --force $inner
    & git -C $repo worktree remove --force $outer

    # Branch rule: tip in origin/main -> deleted; tip with a commit past it and no merged PR -> kept.
    $wt = NewWt 'merged'
    $r = Run @('-Path', $wt, '-DeleteBranch')
    Check 'branch whose tip is in origin/main is deleted' ($r.Code -eq 0 -and $r.Out -match 'deleted local branch' -and -not (& git -C $repo branch --list 'scratch/merged'))
    $wt = NewWt 'ahead'
    & git -C $wt commit -q --allow-empty -m 'after merge'
    $r = Run @('-Path', $wt, '-DeleteBranch')
    Check 'branch with a commit past origin/main is kept' ($r.Code -eq 0 -and $r.Out -match 'kept branch' -and (& git -C $repo branch --list 'scratch/ahead'))
} finally {
    & git -C $repo worktree prune
    foreach ($b in (& git -C $repo branch --list 'scratch/*' --format '%(refname:short)')) { & git -C $repo branch -D $b | Out-Null }
    Remove-Item -Recurse -Force $scratch -ErrorAction SilentlyContinue
}
if ($fail) { Write-Host "$fail failed"; exit 1 } else { Write-Host 'all passed' }
