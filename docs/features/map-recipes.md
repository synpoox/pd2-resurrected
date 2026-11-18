# Map cube recipes (#1306, part of #1303)

Table edits only: `cubemain.txt` (65 rows appended under the existing Catalyst rows), `misc.txt` (`upmp`, `fort`), and the orb strings. Art for the two orbs was already in place (#1292).

Shared choices:
- Maps are Cartographer items (`t1m`, `t2m`, `t3m`). A recipe that changes a map's quality (orbs) uses `usetype,<quality>`, which makes a new random map of the same type and tier. Heroic, corrupt and fortify use `usetype,rar,mod`, which keeps the map and adds the mods, so the affixes and the stats of earlier recipes stay and stack (d2r-tools `docs/cartographer/tables.md`, "Map level on the item"). Never `useitem` with a mod on a map (it breaks misc items).
- No recipe outputs a magic map. Maps never roll magic, so PD2's magic rows (upgrade magic to rare, imbue magic, scour magic, reroll magic, `irma`) are not ported. The orb strings no longer mention magic maps.
- PD2's stat ids are +53 here: `corruptor` 361 is 414, heroic 458 is 511.
- Refusal rows (op 30) stand directly before the first real row of their group and stop a normal map from taking a heroic, corrupt or fortify ingredient, as the Catalyst row does.
- Normal maps with equal stats stack to 50 (#1328); rare maps never stack. A recipe takes several maps of one kind with one line and `qty=N` (for example `t1m,nor,qty=3`), and `numinputs` is the summed qty, as in the rune and gem recipes. core-rules counts a stack as its quantity, so one stack of 3 or more, or separate maps, match; the unused leftover is remade by `cube_stacks` and keeps its Area Level. Separate `qty=1` lines of the same type do not match.

## Orbs (PD2 rows 3-20, 1965-1976, 2161-2163)
Per tier, each plain orb row is followed by its imbued row (`urma`, `rrra`, `irra`, PD2 1965-1968; PD2's `irma` rows are skipped, no magic maps):
- Angelic (`upma` + rune + perfect gem) and imbued `urma`: normal to rare.
- Horadrim (`rera` + rune + perfect gem) and imbued `rrra`: reroll a rare.
- Zakarum (`imra` + jewel + rune) and imbued `irra`: normal to rare.
- Arcane (`imma`): a new normal map of the same tier. It outputs `t1m,nor` / `t2m,nor` / `t3m,nor`, because `usetype,nor` on a normal map hands back the same map.
- Orb of Destruction (`scou`): rare back to normal.
Changed: every orb row refuses a corrupted map (op 18, `corruptor` 414 equal to 0), as in PD2.

## Tier upgrade (PD2 rows 21-34)
Three same-tier normal maps plus `upmp` give one next-tier normal map, for T1 to T2 and T2 to T3. PD2 lists seven input shapes per tier for its stack layouts; one row per tier with `qty=3` covers them, because a stack of 3 or more matches (#1324).

## 3 to 1 (PD2 rows 2125-2139)
Three normal maps of a tier (one stack of 3 or more works) give one normal map; three rare (separate items) give one rare. Changed: PD2's T1 normal row has an input with no `nor` quality (a bug); the port's rows name `nor` for all three tiers. PD2's disabled duplicate rows are not ported.

## Heroic (PD2 rows 202, 204)
Rare map plus `std`: gated on `heroic` 511 equal to 0 (op 18), so a second `std` is refused; adds the `heroed` flag, +20% magic find, +20% gold, +20% density, +10% experience (PD2's values). Refusal row for normal maps first.

## Corrupt (PD2 rows 203, 205, 211-221)
Per tier, a roll row (map + `wss`, `corruptor` 0) adds a random `corruptnum` 1-1979 and returns the shard; the outcome rows (op 16, 11 per tier, thresholds 180 to 1980 in steps of 180) then set the `corrupt` mark and add 2000 to `corruptor`. PD2 adds 3001, but the stat has 12 save bits (4095): 1979 + 2000 stays under it and above every threshold, so a corrupted map never matches a roll or outcome row again. Ported outcomes (PD2's `corrupt` marks 78-88, with PD2's per-tier ranges): deadlystrike (`mon-deadly`, density, experience), nofreeze-hp (`mon-nofreeze-hp`: cannot be frozen and monster life, density, experience), swing-cast, damageresist, speed-all, mag-gold, density, arealevel, monster rarity (`map-uniques`), res-all-max and dropjewelry (`map-drop-jewelry`: Jeweler's drop, [#1318](https://github.com/synpoox/pd2-resurrected/issues/1318)). Monster rarity (`map-uniques`) is back on swing-cast, damageresist, speed-all, mag-gold and res-all-max (PD2's 10-15, 15-20, 20-25 per tier). Changes: swing-cast and speed-all use shared-roll combo properties (`map-swing-cast`: `swing1` + `cast1`; `map-speed-all`: `swing1` + `cast1` + `move2`), because a cube output holds 5 mods (`corrupt`, `corruptnum` and three more) and the combo frees the slot for monster rarity. Not ported: the unique-map outcomes.

## Fortify (PD2 rows 1917-1922)
Rare map plus `fort`: +100% monster life, +20% monster damage, -50% density, +100% experience, +100% chance to drop additional items ([#1317](https://github.com/synpoox/pd2-resurrected/issues/1317): `map_drop_bonus`), +30% faster run/walk for the player (PD2: player velocity 30). The experience and the drop bonus are one combined property, `map-xp-drop`, because the output holds 5 mods.

## Anya (#1324)
Anya sells `upmp` and `fort` as PD2's Drehya does: `AnyaMin`/`AnyaMax` 1, no magic columns, price 200000 (`upmp`) and 15000 (`fort`). The vendor columns already equal PD2's `misc.txt`, so no table row changed.

## Class ear + map (PD2 rows 2186-2206, #1347)
A rare map plus one invader ear (`ivez` Amazon, `ivea` Assassin, `iveb` Barbarian, `ived` Druid, `iven` Necromancer, `ives` Sorceress, `ivep` Paladin) gives the same map with the ear's mods, kept on top of its own (`usetype,rar,mod`). The recipe is gated on `treacherous` (stat 402, PD2 276) equal to 0, so a map takes one ear. A refusal row per ear stops a normal map. PD2 has no "corrupted ear" condition: the only gate is `treacherous`. core-events already reads `treacherous` and the ear-drop stats on a map ([ms4-area-map-stats](../research/maps/ms4-area-map-stats.md)), so this is table-only.

Every row adds the class ear drop (10), +50% drop bonus (`map-drop-bonus`) and `treacherous` = 1 Amazon, 2 Sorceress, 3 Necromancer, 4 Paladin, 5 Barbarian, 6 Druid, 7 Assassin, then two more mods:
- Amazon: monster attack and cast speed +50 (`mon-cast-speed`), monster move speed +25 (`mon-speed`).
- Assassin: monster deadly strike +10, faster hit recovery +120 (`mon-deadly`, `mon-fhr`).
- Barbarian: monster damage +25%, crushing blow +20 (`mon-damage`, `mon-crush`).
- Druid: monster physical as fire +25 (`mon-phys-as-fire`), player max resists -5 (`res-all-max`).
- Necromancer: player life regen -100 (`regen`), monster physical as poison +50 (`mon-phys-as-pois`).
- Sorceress: monster physical as lightning +50 (`mon-phys-as-ltng`), player all resist -25 (`res-all`).
- Paladin: player damage reduction -5% (`red-dmg%`), monster attack rating +50% (`mon-tohit-pct`).

Changes: one row per ear takes `cmap,rar` (any tier), where PD2's `map` is any map; PD2's refusal row for unique maps is not ported (no unique maps). PD2's stat names map to the Cartographer catalogue as in [map-affixes](map-affixes.md); `map_mon_att` (to-hit) is `mon-tohit-pct`.

## Not ported
Dungeon and unique map rows, the `isc` and gem drop-test rows, and PD2's disabled rows (no `enabled` value). Listed in `docs/deviations.md`.
