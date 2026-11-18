# HD hover name plates for PD2-only monster codes (#1092)

Research for [#1092](https://github.com/synpoox/pd2-resurrected/issues/1092): rathmaBone / rathmaBoneClone (`$1`),
demonpillar (`$3`) and ZharTheMad (`$4`) show no name plate on hover. Static work only (D2R 93847 dump under
D2RLoader 1.3.1, stamp `0x6ab3782c`; files/data at `80cd1575`; PD2's own archives).

## Cause

**The mod ships no classic COF files for `$1`, `$3` and `$4`, and D2R's client picks the unit under the cursor from
the classic COF's bounding box in HD as well as in legacy.** With no COF the test fails, the monster is never the
hovered unit, and the name plate never shows. There is no HD-only hover path for monsters.

### Evidence

| Fact | Level | Source |
|---|---|---|
| No `data/global/monsters/$1`, `$3` or `$4` folder exists in `files/data`, in any installed slot (`PD2Resurrected.mpq/data/global` has only `animdata.d2`, `excel`, `items`, `missiles`, `tiles`, `ui`), or in D2R's CASC (6,229 `global/monsters` entries, none under a `$` code) | proven | file listings |
| PD2 has them: `pd2assets.mpq` holds `data\global\monsters\$1\` (9 COF, 18 DCC, `palshift.dat`), `$3\` (6 COF, 6 DCC), `$4\` (8 COF, 16 DCC, `palshift.dat`); 3.9 MB in all. `pd2-baseline.json` extracts only `missiles/*.dcc` from that archive, so they never reached `files/data` | proven | `pd2assets.mpq` listing; `pd2-baseline.json` `extract` |
| animdata.d2 already has every `$1`, `$3`, `$4` record (`$1A1HTH` ... `$4WLHTH`), so the timing side is done | proven | `files/data/global/animdata.d2` |
| `0x9b840` (D2Client `Unit/CUnit.cpp`) is the cursor hit test. For a monster it reads the monstats2 record (`0x975b0`); if the record's hit-box flag is set (`+4 & [0x1d996d0]`) it tests the cursor against the box `+0x10e/+0x110/+0x112/+0x114` (htTop/htLeft/htWidth/htHeight) and is done; otherwise it calls `0x1526a0` | proven (flag = `noGfxHitTest` is likely; Lucion's box works) | decompile `0x9b840` |
| `0x1526a0` (D2Client `Engine/Gfx.cpp`) finds the unit's COF for its mode and weapon class; found: cursor inside the COF box (+8 xMin, +0xc xMax, +0x10 yMin, +0x14 yMax, 16 px slack) returns 1; not found: logs `[AnimTbls/5] Gfx.cpp:3740 ERROR: Could not find COF file for: UNIT_TYPE ...` and returns 0, so `0x9b840` exits with no hover | proven | decompile `0x1526a0` (line 3740 = `0xe9c`, the constant in its log call) |
| The branch does not look at HD vs legacy; the HD renderer is not consulted | proven for this function | decompile `0x9b840` |
| That exact log line is in the slots: 99,877 times for `CLASS: 1158` = LucionSpawner (`$8`, no COF either) in Slot4/Slot5 `blz-log.txt`, i.e. every frame the cursor test runs over it | proven | `C:\D2RTestSlots\Slot4\blz-log.txt`, `Slot5\blz-log.txt` |
| Same mechanism already fixed once: Lucion (`$7`, no COF) got a plate in HD and legacy only after #1091 set the monstats2 hit box (the flag branch skips `0x1526a0`); on #1007, switching rathmaBone's Code to `XU` (a code with COFs) brought its plate back | proven in game (#1091 shots, #1007 comment) | |
| Strings, HD model and selection flags are fine: `Rathma`, `DemonPillar`, `ZharTheMad` are in both `strings/` and `strings-legacy/monsters.json`; `monsters.json` maps the three Ids (nihlathakboss, trappedsoul1, summoner); monstats2 `isSel` 1, `noSel` empty | proven | files/data |

Working PD2-only monster for comparison: rathmaPoison (Mendeln) has the same kind of PD2-only Id, the same HD model
(nihlathakboss) and the same name colour code, but Code `XU`, which has COFs in D2R's CASC. Its plate shows. The only
difference that matters is the Code's COF.

### The conflict with the owner's observation

The owner reports that these three have legacy sprites and can be hovered in legacy view. With the data on `main`
that cannot happen: no COF or DCC for `$1/$3/$4` is installed, and the legacy draw and the hover test both need the
COF. PD2 itself has the sprites (in `pd2assets.mpq`), which may be what was meant, or the observation came from a
build that had them some other way. The owner check below settles it in one look.

## Fix spec (Sonnet-followable, data only)

Ship PD2's classic monster files for the three codes, as committed `files/data` (ADR 0007), the way the missile DCCs
already are. This gives the real COF hit box in both modes, the legacy art, and ends the COF error spam. No engine work.

1. Branch from `origin/main`, own worktree. This touches `files/` and `pd2-baseline.json`: no other open PR may touch
   them at the same time (PR #1108 rule).
2. `pd2-baseline.json`, `"pd2assets.mpq"` list: add
   `"data/global/monsters/$1/*"`, `"data/global/monsters/$3/*"`, `"data/global/monsters/$4/*"`
   (check the glob syntax `classic_import` uses for the existing `"data/global/missiles/*.dcc"`; it must match files in
   subfolders `cof/`, `tr/`, `s1/`, `s3/`). This keeps the files on the next PD2 import.
3. Copy the files into `files/data/global/monsters/<code>/...` with **lower-case** paths, the same tree as the archive
   (`files/data/global/monsters/$1/cof/$1a1hth.cof`, `.../$1/tr/$1trlita1hth.dcc`, `.../$4/cof/palshift.dat`, ...).
   Either run `py -3 tools/pd2_extract.py` after step 2 and copy `pd2-data/data/global/monsters/{$1,$3,$4}` over, or
   read them with `d2rAI-toolkit/shared/classic_import/mpq.py` `Archive(...pd2assets.mpq).read(name)`. Expect 67 files,
   3,868,158 bytes. Do not copy `$7`/`$8` (see open questions).
4. `pd2-import.txt` lists import-owned files and says "regenerate with tools/pd2_import.py, never by hand". Either
   regenerate it, or, if that is out of scope, add the 67 paths in sorted order and say so in the PR (ask the
   orchestrator which).
5. Gate: run `tools/pd2_diff.py --scope alpha`; if it flags the new files as unexpected, add a narrow
   `docs/deviations.md` row (PD2 legacy monster sprites for `$1/$3/$4`, #1092). `py -3 -m unittest discover -s tools/tests`
   (`test_no_game_files` allows `files/data/**`; `.dcc/.cof/.dat` are not banned extensions).
6. Changelog fragment `changelog.d/1092-pd2-monster-sprites.md`: `#### Fixes` /
   `- Rathma, the Demonic Sentinel and Zhar show their name when you point at them.`
7. Do not reopen PR #1128's hit-box rows: the COF box replaces them. (They would also work in HD, as Lucion's does,
   but leave no legacy art and fixed boxes that do not follow the animation.)

Risk: `$4` has an `s3` layer (`$4S3LIT*.dcc`) while Zhar's monstats2 row has `S3` empty. That is PD2's own row and
COF pairing, so it should draw as in PD2; if the legacy view shows a missing piece, set `S3`=1 and `S3v`=`lit` on
monstats2 `zhar` (compare PD2's row first).

## Owner check

Hell, `cheat mspawn rathmaBone`, `cheat mspawn demonpillar`, `cheat mspawn ZharTheMad` (one at a time), cursor on the
body:

- **Before the fix (now)**, legacy view (Alt+G or the graphics toggle): is a sprite drawn at all? Then quit and look in
  `blz-log.txt` beside the game slot for `Could not find COF file for: UNIT_TYPE: 1, CLASS: 950` (rathmaBone),
  `952` (clone), `1074` (demonpillar) or `1075` (ZharTheMad). If those lines are there, the cause above is confirmed
  on the owner's machine.
- **After the fix**: name plate shows on hover in HD and in legacy; the legacy view draws PD2's sprite; the log has no
  `CLASS: 950/952/1074/1075` COF lines.

## Open questions

1. The owner's "they have legacy sprites and can be hovered in legacy view" does not match `main`'s data (no COF/DCC
   for these codes anywhere installed). Which build, or which observation, was that?
2. LucionSpawner (`$8`) and InvaderPortal: owner chose unhoverable. `$8` still logs ~100k COF errors per session (log
   noise only). Shipping `$8` COFs would make it hoverable unless its monstats2 row gets `noSel`=1; leave as is unless the
   owner wants the log quiet.
3. Lucion (`$7`): PD2 has his legacy sprites too (36 files). Shipping them would give him legacy art (he is invisible
   in legacy today); his #1091 hit box keeps working either way. Owner's call.
4. Other PD2 `!`-codes (`!3` Na-Krul, `!8` Iskatu, `!9`) were not checked here; the same rule applies to any code with
   no COF in `files/data` or D2R's CASC.

## Addresses used (build 93847, RVA)

`0x9b840` cursor hit test (CUnit.cpp), `0x1526a0` COF-box cursor test (Gfx.cpp:3740 error), `0x152390` COF-box
on-screen test (Gfx.cpp, same error at level 3), `0x1919f0` controller targeting (TargetingController.cpp, also
skips a unit whose COF test fails). Reading only; the fix needs no hook.
