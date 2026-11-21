# Playing on Linux

PD2 Resurrected runs on Linux through Wine/Proton, with D2RLoader started in the same Wine prefix as Battle.net.
The launcher is not used: it needs Windows and WebView2, and the [manual install](../README.md#manual) does the same job.
D2RLoader lists "Linux supported via Wine/Proton" on its [download page](https://d2rloader.net/download.html).

## Tested setup

| | |
|---|---|
| OS | Ubuntu 24.04.5 LTS, kernel 7.0 |
| GPU | NVIDIA RTX 5070, driver 595.99.02 |
| Lutris | 0.5.22, default Wine runner (umu-run, UMU-Proton) |
| D2R | 3.4.0, build 95032, Battle.net copy |
| D2RLoader | 1.3.1-beta |
| PD2 Resurrected | 0.2.1-alpha |

Other distributions and GPUs should work the same way; only this setup is tested.

## Install (Battle.net copy, Lutris)

1. In Lutris, hit the **+**, and search for "battle.net". Install it, sign in, then install D2R with Reign of the 
   Warlock from Battle.net.
2. Unzip `D2RLoader-<version>.zip` into the D2R folder in the Wine prefix,
   `<prefix>/drive_c/Program Files (x86)/Diablo II Resurrected/`, so that `D2RLoader.exe` sits beside `D2R.exe`.
3. Install the mod into that folder as in the README's [manual install](../README.md#manual): unpack
   `PD2Resurrected-<version>.7z` into `mods/` and set `default_mod = "PD2Resurrected"` in
   `d2rloader/config/d2rloader.toml`.
4. In Lutris, right-click Battle.net and duplicate it and give it a name like "PD2R". The name is not important, but I
   will refer to the duplicate as "PD2R" going forward. 
   Right click "PD2R", **Configure, Game options**: set **Executable** to `D2RLoader.exe` in the D2R folder, and
   **Working directory** to the folder containing D2RLoader.exe.
5. Start Battle.net and sign in, then start the copy. If you did this right after step 1, you can just start PD2R
   directly.

D2RLoader shows "This D2R version hasn't been fully tested yet" on D2R 3.4. Click OK: the mod runs D2RLoader's 3.3
engine on the 3.4 data (see [Requirements](../README.md#requirements)).

Characters are saved in the prefix, in
`<prefix>/drive_c/users/steamuser/Saved Games/Diablo II Resurrected/mods/PD2Resurrected/`.

## Known issues

- `d2rloader/logs/d2rloader.log` warns `Failed to write .seija game version slot.` (and the same for the game build
  and active mod slots). No effect in game has been seen.
- Keep D2R on a supported build. Battle.net offers D2R updates when you press Play; a new D2R build may not run the
  mod until D2RLoader and the mod support it.

## Not tested

- Steam copies of D2R under Proton. The usual route is a Steam launch option that starts `D2RLoader.exe` instead of
  `D2R.exe`.
- Starting D2RLoader with Battle.net closed.
- Steam Deck.
- TCP/IP co-op.
