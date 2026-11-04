# Lightning javelins (srvdo 188, cltdo 117)

Answers work item W5 (#52): what do PD2's skill functions srvdo 188 and cltdo 117 (Lightning Bolt, Lightning Fury) do,
and does D2R need a plugin for them? They only create the skill's missile, which D2R's do dispatcher already does from
the missile columns, so the row is data only. Read from the PD2 baseline install `C:\Games\PD2\ProjectD2`:
`ProjectDiablo.dll` and its 1.13c `D2Game.dll` / `D2Client.dll`. D2R side from the 93847 dump (`ImageStamp 0x6ab3782c`).
Static disassembly with capstone, then one game run. Addresses are RVAs.

## Where PD2 puts them

1.13c's server do table (D2Game `0x1074a8`) is empty from slot 153 to its end (bound `0xbf`). `ProjectDiablo.dll` reaches
it through the offset entry `0x4aa4a4` (getter `0x31cfe0`, DLL index 6) and its install routine `0x36bb1c` writes the
slots: srvdo 188 = `0x3b59c0`, srvdo 187 = `0x3b58b0`. The same routine also overwrites vanilla slots (4, 6, 8, 23, 25,
39, 44, ...), which matters to the hook list's `verify` rows (Q4), not to this one.

The client do table (global `0x105de71c`) gets cltdo 117 = `0x3ac8b0` and cltdo 116 = `0x3ac7b0`.

## What they do

| Function | Body |
|---|---|
| srvdo 188 | missile = `srvmissile` (record `+0x46`) if set, else `srvmissilea` (`+0x48`); D2Game `0xa28f0(game, missile, unit, skill, level, 0, 0, 0, 0, 0)`; sets unit `+0xc4` bit `0x40`; returns 1 |
| cltdo 117 | missile = `cltmissile` (`+0xe8`) if set, else `cltmissilea` (`+0xea`); D2Client `0xa15c0`, the call vanilla's Attack client do (`0xa2230`) uses |
| srvdo 187 | item cast: MissileParams flags `0x10000`, from the caster's position to the target unit's position; `0x311b20` -> D2Game `0x6f930` |
| cltdo 116 | item cast, client side, flags `0x10000` |

1.13c's do dispatcher (D2Game `0xa1b1d`-`0xa1c28`) runs the row's srvdofunc and then creates `srvmissile` through the same
`0xa28f0` with the same arguments; the client dispatcher's tail (`0xa21ee`) creates `cltmissile` through `0xa0f60`. So 188
with an empty `srvmissile` does what an empty srvdofunc with the missile in `srvmissile` does. PD2's rows keep the
missile in `srvmissilea` / `cltmissilea` and `srvmissile` / `cltmissile` empty.

D2R 93847's dispatcher (`0x43acb0`) does the same: srvdofunc at record `+0x4e` (bound `0xbe`), then `srvmissile`
(`+0x66`) through `0x4333f0`. D2R's own Lightning Bolt and Lightning Fury rows already have no do function and the
missile in `srvmissile` / `cltmissile`.

## The merge rule

`pd2_ids.MISSILE_FUNCS` moves a row's `srvmissilea` into `srvmissile` (`cltmissilea` into `cltmissile`) and blanks
srvdofunc 188 / cltdofunc 117; the table-diff gate reads PD2's rows the same way. The merged rows equal D2R's in these
columns.

## Item casts (not done)

PD2 rows point `ItemEffect` / `ItemCltEffect` at 187/116 (Lightning Bolt, Lightning Fury), 188/117 (Fire Bolt, Ice
Bolt, Ice Blast, Fire Ball, Lightning, Glacial Spike, Frozen Orb, ...) and 183/10 (the novas). D2R's slots for these
are empty and its dispatcher reads `ItemEffect` (`+0x20a`) only for an item cast with a value above 1 and a non-empty
slot, so an item cast (Crackleshot's hit skill, Thunderstroke, "of Lightning Fury" charges) runs the skill's normal cast
instead. It does not crash; the difference is the aim (PD2's 187 throws at the target unit's position) and the missile
flag `0x10000`. Listed in `docs/unported.md`.

## Game check

`tests/game/lightning_javelin_checks.py` (`lightning-bolt`, `lightning-fury`) casts once and counts the client missiles
of PD2's `cltmissilea` row, read from game memory by `tests/game/missile_probe.py`. It runs on the fixture
`tests/game/fixtures/lightning-javelin` (the merged rows' function and missile columns on D2R's tables, castable in
town), because the full merged data does not pass modkit validation yet.

Run on Slot4 (2026-11-03, build 93847): one Lightning Bolt cast made 1 `lightningjavelin`, one Lightning Fury cast made
1 `lightningfury`, PD2's count. The probe sees client missiles only; the server side rests on the code comparison above.
