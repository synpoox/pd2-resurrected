# Map affixes (#1305, part of #1303)

Rare Cartographer maps roll PD2's map affixes in PD2's tier bands. Tables only: the rows are in `magicprefix.txt` and `magicsuffix.txt` (maps are never magic, so the magic rows serve rare maps).

## What ported

58 of PD2's 242 map affix rows (18 prefixes, 40 suffixes). Each keeps PD2's name, group, frequency, level and ranges. The row keeps its PD2 row number (a save stores it), so the port edits the neutral row `tools/pd2_cutmaps.py` left (#964) instead of adding one. No new affix ids, so `merge/ids-*.tsv` are unchanged (it adds two string ids, see Strings). A PD2 re-import would neutralise the rows again; the committed tables are the source (ADR 0007).

Bands: PD2's `t1m,t2m` stays `t1m,t2m`, `t2m,t3m` stays, and `t3m,t4m` becomes `t3m` (T4 is cut). `of the Adventurer` (`t1m..t4m`) becomes `t1m,t2m,t3m`.

| PD2 stat | Becomes | Affixes |
|---|---|---|
| `map_mon_hp%` | `mon-life` | Large, Huge, Colossal; of Health, Vitality, Vim |
| `map_mon_ed%` | `mon-damage` | Strong, Mighty, Formidable |
| `map_mon_velocity%` | `mon-speed` | of Swiftness, Quickness, Velocity |
| `map_glob_arealevel` | `map-level` (+1) | of the Adventurer (PD2 frequency 0: it never rolls on its own) |
| `map_play_regen` | `regen` | Unhealthy, Sickly, Diseased |
| `map_play_res_all` | `res-all` | of Hindrance, Aversion, Permeability |
| `map_play_ac%` | `ac%` | of Cracking, Crumbling, Disintegration |
| `map_play_block` | `block` | of Fumbling, Bumbling, Blundering |
| `map_play_balance1` | `balance1` | of Imbalance, Inequality, Uneveness |
| `map_play_lightradius` | `light` | of Darkness (PD2 frequency 0) |
| filler `map_glob_density` | `map-density` | |
| filler `map_play_addxp` | `addxp` | |
| filler `map_play_mag_gold%` | `mag%` | magic find only: the third slot holds one mod, so gold find is not rolled (approximated) |

Which rows roll: 27 of the 58 (9 prefixes, 18 suffixes). These have `spawnable` 1, `rare` 1 and frequency above 0. The other 31 do not roll on Cartographer maps:

- 27 rows are PD2's second (high-frequency) set. They have `rare` blank, so they are magic-only and never roll on a rare map. They are kept for parity with PD2.
- 4 rows have frequency 0: `of Darkness` x3 and `of the Adventurer` (as in PD2).

The player stats are the player's while in the map (Cartographer's rule).

The rows cannot roll until `t1m`/`t2m`/`t3m` are children of `cmap` (lane 1, #1308).

`ac%` (`of Cracking`, `of Crumbling`, `of Disintegration`) may be inert on the player, to be confirmed in game.

## Left out (184 rows, five follow-ups)

The headline needs code, so the row stays neutral (never spawns). One issue per group, listed in `docs/deviations.md`:

- [#1309](https://github.com/synpoox/pd2-resurrected/issues/1309) monster elemental damage, 54 rows
- [#1310](https://github.com/synpoox/pd2-resurrected/issues/1310) monster attack modifiers, 36 rows
- [#1311](https://github.com/synpoox/pd2-resurrected/issues/1311) monster defence modifiers, 54 rows
- [#1312](https://github.com/synpoox/pd2-resurrected/issues/1312) extra monster groups and boss, 10 rows
- [#1313](https://github.com/synpoox/pd2-resurrected/issues/1313) monster rarity and drop-type bonuses, 30 rows

## Placeholders and rare names

The five Cartographer affixes (ids 9201-9205: Teeming, Savage, Hastened, of the Swarm, of Embers) are kept as they are: tierless, frequency 10, rolling beside the ported ones. The rare names Labyrinth and Warren are kept; PD2 has no map rare names.

## Strings

`Large` and `of the Adventurer` had no affix string; ids 51540 and 51541 in `item-nameaffixes.json`.

## Test

`tools/tests/test_map_affixes.py`: every affix row with a `cmap`/`t1m`..`t3m` type names only real properties and only those types, and the rolling set (spawnable 1, rare 1, frequency above 0) is exactly the 27 expected rows, so a dead pool fails.
