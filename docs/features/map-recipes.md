# Map cube recipes (#1306, part of #1303)

Table edits only: `cubemain.txt` (65 rows appended under the existing Catalyst rows), `misc.txt` (`upmp`, `fort`), and the orb strings. Art for the two orbs was already in place (#1292).

Shared choices:
- Maps are Cartographer items (`t1m`, `t2m`, `t3m`). A recipe that changes a map's quality (orbs) uses `usetype,<quality>`, which makes a new random map of the same type and tier. Heroic, corrupt and fortify use `usetype,rar,mod`, which keeps the map and adds the mods, so the affixes and the stats of earlier recipes stay and stack (d2r-tools `docs/cartographer/tables.md`, "Map level on the item"). Never `useitem` with a mod on a map (it breaks misc items).
- No recipe outputs a magic map. Maps never roll magic, so PD2's magic rows (upgrade magic to rare, imbue magic, scour magic, reroll magic, `irma`) are not ported. The orb strings no longer mention magic maps.
- PD2's stat ids are +53 here: `corruptor` 361 is 414, heroic 458 is 511.
- Refusal rows (op 30) stand directly before the first real row of their group and stop a normal map from taking a heroic, corrupt or fortify ingredient, as the Catalyst row does.
- Maps do not stack. A recipe takes several maps of one kind with one line and `qty=N` (for example `t1m,nor,qty=3`); separate `qty=1` lines of the same type do not match.

## Orbs (PD2 rows 3-20, 1965-1976, 2161-2163)
Per tier, each plain orb row is followed by its imbued row (`urma`, `rrra`, `irra`, PD2 1965-1968; PD2's `irma` rows are skipped, no magic maps):
- Angelic (`upma` + rune + perfect gem) and imbued `urma`: normal to rare.
- Horadrim (`rera` + rune + perfect gem) and imbued `rrra`: reroll a rare.
- Zakarum (`imra` + jewel + rune) and imbued `irra`: normal to rare.
- Arcane (`imma`): a new normal map of the same tier. It outputs `t1m,nor` / `t2m,nor` / `t3m,nor`, because `usetype,nor` on a normal map hands back the same map.
- Orb of Destruction (`scou`): rare back to normal.
Changed: every orb row refuses a corrupted map (op 18, `corruptor` 414 equal to 0), as in PD2.

## Tier upgrade (PD2 rows 21-34)
Three same-tier normal maps plus `upmp` give one next-tier normal map, for T1 to T2 and T2 to T3. PD2 lists seven input shapes per tier; the stack shapes cannot match here (maps do not stack), so one row per tier with three single maps.

## 3 to 1 (PD2 rows 2125-2139)
Three normal maps of a tier give one normal map; three rare give one rare. Changed: PD2's T1 normal row has an input with no `nor` quality (a bug); the port's rows name `nor` for all three tiers. PD2's disabled duplicate rows are not ported.

## Heroic (PD2 rows 202, 204)
Rare map plus `std`: gated on `heroic` 511 equal to 0 (op 18), so a second `std` is refused; adds the `heroed` flag, +20% magic find, +20% gold, +20% density, +10% experience (PD2's values). Refusal row for normal maps first.

## Corrupt (PD2 rows 203, 205, 211-221)
Per tier, a roll row (map + `wss`, `corruptor` 0) adds a random `corruptnum` 1-1889 and returns the shard; the outcome rows (op 16, thresholds rising to 1890) then set the `corrupt` mark and add 2200 to `corruptor`. PD2 adds 3001, but the stat has 12 save bits (4095): 1889 + 2200 stays under it and above every threshold, so a corrupted map never matches a roll or outcome row again. Ported outcomes: swing-cast, damageresist, speed-all, mag-gold, density, arealevel, res-all-max, each with PD2's per-tier ranges. Changes: monster rarity was dropped from five outcomes (swing-cast, damageresist, density, arealevel, res-all-max; Cartographer has no such stat), and swing-cast and speed-all are split into separate mods (`swing1`, `cast1`, `move2`). Not ported (need code, see #1318): deadlystrike, nofreeze-hp, monster rarity, dropjewelry, and the unique-map outcomes.

## Fortify (PD2 rows 1917-1922)
Rare map plus `fort`: +100% monster life, +20% monster damage, -50% density, +100% experience, +30% faster run/walk for the player (PD2: player velocity 30). Not ported: the extra item drop chance (skirmish effect, needs code, #1317).

## Not ported
Dungeon and unique map rows, ear rows, the `isc` and gem drop-test rows, and PD2's disabled rows (no `enabled` value). Listed in `docs/deviations.md`.
