# Map cube recipes (#1306, part of #1303)

Table edits only: `cubemain.txt` (60 rows appended under the existing Catalyst rows), `misc.txt` (`upmp`, `fort`), and the orb strings. Art for the two orbs was already in place (#1292).

Shared choices:
- Maps are Cartographer items (`t1m`, `t2m`, `t3m`). Outputs use `usetype,<quality>`, never `useitem` with a mod on a map (it breaks misc items). So an orb makes a new random map of the same tier; it does not keep the old map's level or event.
- No recipe outputs a magic map. Maps never roll magic, so PD2's magic rows (upgrade magic to rare, imbue magic, scour magic, reroll magic, 3 magic to 1 magic) are not ported. The orb strings no longer mention magic maps.
- PD2's stat ids are +53 here: corruption roll stat 361 is 414, heroic 458 is 511.
- Refusal rows (op 30, listed first) stop a normal map from taking a heroic, corrupt or fortify ingredient, as the Catalyst row does.

## Orbs (PD2 rows 3-20, 2161-2163)
Per tier, rows `tNm ... (Angelic/Horadrim/Zakarum/Arcane Orb)` and `scour`: upgrade normal to rare (`upma` + rune + perfect gem), reroll rare (`rera` + rune + perfect gem), imbue rare (`imra` + jewel + rune), reroll normal (`imma`), scour rare to normal (`scou`). Changed: the roll row is gated on corruptnum 0 (op 18, stat 414) so a corrupted map is refused, as in PD2.

## Tier upgrade (PD2 rows 21-34)
Three same-tier normal maps plus `upmp` give one next-tier normal map, for T1 to T2 and T2 to T3. PD2 lists seven input shapes per tier because of its shared map type; three cover the stack, 2+1 and 1+1+1 cases here.

## 3 to 1 (PD2 rows 2125-2139)
Three normal maps of a tier give one normal map; three rare give one rare. Changed: PD2's T1 normal row has no `nor` output quality (bug); T1 gets `nor` like T2 and T3. PD2's disabled duplicate rows are not ported.

## Heroic (PD2 rows 202, 204)
Rare map plus `std` gives a rare heroic map: `heroed` flag, +20% magic find, +20% gold, +20% density, +10% experience (PD2's values). Refusal row for normal maps first.

## Corrupt (PD2 rows 203, 205, 211-221)
Per tier, a roll row (map + `wss`, corruptnum 0) sets a random corruptnum 1-1889 and returns the shard; the outcome rows (op 16, ascending thresholds) then set the `corrupt` mark and corruptnum 3001. Ported outcomes: swing-cast, damageresist, speed-all, mag-gold, density, arealevel, res-all-max, each with PD2's per-tier ranges. Not ported (need code, see follow-up): deadlystrike, nofreeze-hp, monster rarity, dropjewelry, and the unique-map outcomes.

## Fortify (PD2 rows 1917-1921)
Rare map plus `fort`: +100% monster life, +20% monster damage, -50% density, +100% experience, +30% velocity. Not ported: the extra item drop chance (skirmish effect, needs code).

## Not ported
Dungeon and unique map rows, ear rows, the `isc` and gem drop-test rows, and PD2's disabled rows (no `enabled` value). Listed in `docs/deviations.md`.
