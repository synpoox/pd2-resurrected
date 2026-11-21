# Installs the built plugins into a D2R folder (a test slot or a frozen install copy), and optionally a modkit
# project's data into its mods folder. The game must be closed.
#   powershell -ExecutionPolicy Bypass -File tools\install.ps1 -Game C:\D2RTestSlots\Slot4
#   powershell -ExecutionPolicy Bypass -File tools\install.ps1 -Game C:\D2RTestSlots\Slot4 -Data tests\game\fixtures\split-throw
#   powershell -ExecutionPolicy Bypass -File tools\install.ps1 -Game C:\D2RTestSlots\Slot4 -Data . -NoValidate
#
# Plugins are mod-scoped (#1180): <Game>\mods\<mod>\d2rloader\plugins\. -Data runs `modkit build` and `modkit install` and then
# puts back each modinfo.json savepath, because modkit install resets it (a slot would otherwise play other saves).
# A test slot of the testing config installs through the toolkit's testing\slot_install.py instead (modkit install
# refuses slots): set MODKIT_TESTING_CONFIG, and MODKIT_RUN to the slot lock's owner.
# modkit install writes only the out\data files that differ from the slot's, and touches nothing
# outside <Game>\mods\<mod>, so swapping a slot between builds takes seconds.
# -NoValidate builds past modkit's validation errors, as the release workflow does: main still has known ones (#171).
param(
    [Parameter(Mandatory)] [string]$Game,
    [string]$Data = "",
    [string]$Cartographer = "",  # cartographer.dll (default: CARTOGRAPHER_DLL, else d2r-tools\dist\cartographer beside the toolkit)
    [string]$Toolkit = "",  # d2rAI-toolkit checkout (default: the toolkit's finder: D2R_TOOLKIT, else a walk up, so worktrees find it)
    [switch]$NoValidate,
    [switch]$KeepExtras  # keep third-party plugins in <Game>\d2rloader\plugins (a slot set up on purpose)
)
$ErrorActionPreference = 'Stop'
if (-not $Toolkit) {
    # The finder lives in the toolkit: D2R_TOOLKIT, else the first d2rAI-toolkit up the tree, gives its script; it then decides
    # (and warns when toolkit.version differs from the toolkit's HEAD).
    $boot = if ($env:D2R_TOOLKIT) { $env:D2R_TOOLKIT } else {
        $dir = Split-Path $PSScriptRoot -Parent
        while ($dir -and -not (Test-Path (Join-Path $dir "d2rAI-toolkit\tools\ToolkitPath.ps1"))) { $dir = Split-Path $dir -Parent }
        if ($dir) { Join-Path $dir "d2rAI-toolkit" }
    }
    if (-not $boot) { throw "d2rAI-toolkit not found beside any parent folder: set D2R_TOOLKIT or pass -Toolkit" }
    . (Join-Path $boot "tools\ToolkitPath.ps1")
    $Toolkit = Find-D2rToolkit -From $PSScriptRoot
}
$Game = (Resolve-Path $Game).Path.TrimEnd('\')
if (-not (Test-Path (Join-Path $Game "D2R.exe"))) { throw "$Game is not a D2R folder" }
# A test slot belongs to one run: refuse before writing anything unless MODKIT_RUN holds its lock (#1241)
if ($Game -match '^[A-Z]:\\D2RTestSlots\\Slot(\d+)\\?$') {
    if (-not $env:MODKIT_RUN) { throw "$Game is a test slot: set MODKIT_RUN to the owner of its lock (testing\game_slots.py lock $($Matches[1]) <owner>)" }
    py -3 (Join-Path $Toolkit "testing\game_slots.py") check $Matches[1] $env:MODKIT_RUN
    if ($LASTEXITCODE -ne 0) { throw "run $env:MODKIT_RUN does not hold the lock of ${Game}: nothing written" }
}
if (Get-Process D2R, D2RLoader -ErrorAction SilentlyContinue | Where-Object { $_.Path -and $_.Path.StartsWith($Game, 'OrdinalIgnoreCase') }) {
    throw "the game is running from ${Game}: close it first"
}

# cartographer.dll (d2r-tools) is mandatory to play; release.yml ships it with --extra-plugin (#1188).
if (-not $Cartographer) { $Cartographer = $env:CARTOGRAPHER_DLL }
if (-not $Cartographer) {
    $tools = Join-Path (Split-Path $Toolkit -Parent) "d2r-tools"
    $Cartographer = Join-Path $tools "dist\cartographer\cartographer.dll"
}
if (-not (Test-Path $Cartographer)) { throw "$Cartographer missing: build it in d2r-tools (tools\build.ps1 -Plugin cartographer) or pass -Cartographer" }
& (Join-Path $PSScriptRoot "check_cartographer.ps1") -Dll $Cartographer  # refuses a DLL older than tools\cartographer.pin (#1300)

# Our plugins are mod-scoped (#1180): <Game>\mods\<mod>\d2rloader\plugins, beside the companions. File-name order
# runs core-rules before Cast Triggers (d2rl-ruffneckk-cast-triggers.dll) with no load_order.
$repoMod = (Get-Content (Join-Path $PSScriptRoot "..\mod.json") -Raw | ConvertFrom-Json).name
$plugins = Join-Path $Game "mods\$repoMod\d2rloader\plugins"
New-Item -ItemType Directory -Force $plugins | Out-Null
foreach ($name in @("core-rules", "core-events", "core-ui")) {
    $dll = Join-Path $PSScriptRoot "..\plugin\build\Release\$name.dll"
    if (-not (Test-Path $dll)) { throw "$dll missing: run plugin\build.ps1 first" }
    Copy-Item $dll $plugins -Force
    Write-Output "installed $name.dll -> $plugins"
}
Copy-Item $Cartographer $plugins -Force
Write-Output "installed cartographer.dll -> $plugins"
# The game-wide folder: our old copies there (core-*, cartographer, the removed Charm Inventory) would load a second
# copy, so they always go; other strays (third-party plugins) would make the test run on an unclean install unless
# -KeepExtras. The removed Charm Inventory also goes from the mod folder. Move, never delete.
$gamePlugins = Join-Path $Game "d2rloader\plugins"
$harness = @("test-harness.dll", "d2r-testharness-console.txt")  # the toolkit's test harness stays game-wide
$isOurs = { param($n) $n -like "core-*.dll" -or $n -eq "cartographer.dll" -or $n -like "d2rl-charm-inv.*" }
$moved = @(Get-ChildItem $gamePlugins -Force -ErrorAction SilentlyContinue | Where-Object {
    $harness -notcontains $_.Name -and ((& $isOurs $_.Name) -or -not $KeepExtras) })
$moved += @(Get-ChildItem $plugins -Filter "d2rl-charm-inv.*" -Force)
if ($moved) {
    $backup = Join-Path "C:\D2RTestSlots\backups\stray-plugins-$(Get-Date -Format yyyy-MM-dd)" (Split-Path $Game -Leaf)
    New-Item -ItemType Directory -Force $backup | Out-Null
    foreach ($f in $moved) {
        $dest = Join-Path $backup $f.Name
        if (Test-Path $dest) { $dest = Join-Path $backup "$($f.BaseName)-$(Get-Date -Format HHmmss)$($f.Extension)" }
        Move-Item -LiteralPath $f.FullName $dest
        Write-Output "moved $(if (& $isOurs $f.Name) { 'old' } else { 'stray' }) $($f.Name) -> $dest"
    }
}

if ($Data) {
    $Data = (Resolve-Path $Data).Path
    # files/data holds the committed tables (ADR 0007); a leftover generated/data would be copied over them by modkit build.
    if ((Test-Path (Join-Path $Data "files\data\global\excel\levels.txt")) -and (Test-Path (Join-Path $Data "generated\data"))) {
        throw "$Data\generated\data would override the committed tables in files\data: delete generated\ (only tools/pd2_import.py writes it)"
    }
    $mod = (Get-Content (Join-Path $Data "mod.json") -Raw | ConvertFrom-Json).name
    $root = Join-Path $Game "mods\$mod"
    $modinfos = @((Join-Path $root "modinfo.json"), (Join-Path $root "$mod.mpq\modinfo.json"))
    $saved = @{}
    foreach ($f in $modinfos) { if (Test-Path $f) { $saved[$f] = (Get-Content $f -Raw | ConvertFrom-Json).savepath } }
    # Fetch the companions (not for a test slot) before modkit install: a network failure must not leave the savepath reset by modkit install (#1188).
    $companions = (Test-Path (Join-Path $Data "companions\manifest.json"))
    Push-Location $Toolkit
    try {
        # modkit install refuses a test slot of the testing config (MODKIT_TESTING_CONFIG), so a slot goes through the
        # toolkit's slot_install.py, which sets the slot's own savepath and needs the slot lock held by MODKIT_RUN (#602).
        $slot = py -3 -c "import sys; from pathlib import Path; from modkit.install import configured_slot; print(configured_slot(Path(sys.argv[1])) or '')" $Game
        if ($LASTEXITCODE -ne 0) { throw "reading the testing config failed ($LASTEXITCODE)" }
        if ($companions -and -not $slot) {
            Push-Location $Data
            try {
                py -3 (Join-Path $Toolkit "release\companions.py") fetch
                if ($LASTEXITCODE -ne 0) { throw "companions fetch failed ($LASTEXITCODE)" }
            } finally { Pop-Location }
        }
        if ($NoValidate) { py -3 -m modkit build $Data --no-validate } else { py -3 -m modkit build $Data }
        if ($LASTEXITCODE -ne 0) { throw "modkit build failed ($LASTEXITCODE)" }
        if ($slot) {
            if (-not $env:MODKIT_RUN) { throw "$Game is test slot ${slot}: set MODKIT_RUN to the owner of its lock (testing\game_slots.py lock $slot <owner>)" }
            py -3 testing\slot_install.py --slot $slot $Data
            if ($LASTEXITCODE -ne 0) { throw "slot_install failed ($LASTEXITCODE)" }
            $saved = @{}  # slot_install wrote the slot's savepath
        } else {
            py -3 -m modkit --game $Game install $Data
            if ($LASTEXITCODE -ne 0) { throw "modkit install failed ($LASTEXITCODE)" }
            # The companion plugins are mandatory to play (#1188); a test slot stays clean of them.
            if ($companions) {
                Push-Location $Data
                try {
                    py -3 (Join-Path $Toolkit "release\companions.py") install --game $Game
                    if ($LASTEXITCODE -ne 0) { throw "companions install failed ($LASTEXITCODE)" }
                } finally { Pop-Location }
            }
        }
    } finally { Pop-Location }
    # Cartographer's cube signatures beside its tables keep the cube executor with core-rules (#987); the toolkit's release/companions.py overlay() does the same.
    $sig = Join-Path $PSScriptRoot "..\companions\cartographer\signatures.txt"
    if (Test-Path $sig) {
        $tables = Join-Path $root "$mod.mpq\data\global\excel\d2rloader\cartographer"
        New-Item -ItemType Directory -Force $tables | Out-Null
        Copy-Item $sig $tables -Force
        Write-Output "installed signatures.txt -> $tables"
    }
    foreach ($f in $saved.Keys) {
        $info = Get-Content $f -Raw | ConvertFrom-Json
        $info.savepath = $saved[$f]
        [IO.File]::WriteAllText($f, ($info | ConvertTo-Json))  # UTF-8 without a BOM, as modkit writes it
        Write-Output "savepath kept: $($saved[$f]) in $f"
    }
}
