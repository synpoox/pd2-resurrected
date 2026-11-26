# Moves companion DLLs (and their d2rloader\config\<name>.toml) that companions/manifest.json no longer lists out of a mod's
# plugin folder into a backup (#1920). The toolkit's prune only deletes files an earlier install recorded, so a companion that
# was installed before the record existed (or by another route) survives its removal from the manifest. core-* and aa-settings-host are ours.
function Move-UnlistedCompanions {
    param([string]$Plugins, [string]$Manifest, [string]$Backup)
    $m = Get-Content $Manifest -Raw | ConvertFrom-Json
    $listed = @($m.companions | ForEach-Object { $_.files.PSObject.Properties.Value }) + @($m.project_files.PSObject.Properties.Value) |
        Where-Object { $_ } | ForEach-Object { Split-Path $_ -Leaf }
    $config = Join-Path (Split-Path $Plugins -Parent) "config"
    $stray = @(Get-ChildItem $Plugins -Filter *.dll -File -Force -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -notlike "core-*" -and $_.Name -ne "aa-settings-host.dll" -and $listed -notcontains $_.Name })
    foreach ($f in $stray) {
        $toml = Join-Path $config (($f.BaseName -replace '^d2rl-', '') + ".toml")
        New-Item -ItemType Directory -Force $Backup | Out-Null
        foreach ($p in @($f.FullName, $toml)) {
            if (-not (Test-Path $p)) { continue }
            $dest = Join-Path $Backup (Split-Path $p -Leaf)
            if (Test-Path $dest) { $dest = Join-Path $Backup "$([IO.Path]::GetFileNameWithoutExtension($p))-$(Get-Date -Format HHmmss)$([IO.Path]::GetExtension($p))" }
            Move-Item -LiteralPath $p $dest
            Write-Output "moved unlisted companion $(Split-Path $p -Leaf) -> $dest"
        }
    }
}
