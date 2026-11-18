# Map affixes (#1305, part of #1303)

Rare Cartographer maps roll PD2's map affixes in PD2's tier bands. Tables only: the rows are in `magicprefix.txt` and `magicsuffix.txt` (maps are never magic, so the magic rows serve rare maps).

## What ported

236 of PD2's 242 map affix rows (112 prefixes, 124 suffixes); 58 came with #1305 and 178 with #1309-#1313. Each keeps PD2's name, group, frequency, level and ranges. The row keeps its PD2 row number (a save stores it), so the port edits the neutral row `tools/pd2_cutmaps.py` left (#964) instead of adding one. No new affix ids, so `merge/ids-*.tsv` are unchanged (it adds two string ids, see Strings). A PD2 re-import would neutralise the rows again; the committed tables are the source (ADR 0007).

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
| `map_play_lightradius` | `light` | of Darkness (PD2 frequency 0); its filler `map_play_mag_gold%` is the combo `map-mag-gold` (magic find and gold find, one roll) |
| filler `map_glob_density` | `map-density` | |
| filler `map_play_addxp` | `addxp` | |
| filler `map_play_mag_gold%` | `mag%` | magic find only: the third slot holds one mod, so gold find is not rolled (approximated) |

Which rows roll: 102 of the 236 (spawnable 1, rare 1, frequency above 0). The rest are PD2's second (high-frequency) set, which have `rare` blank (magic-only, kept for parity with PD2), or frequency 0 (`of Darkness` x3, `of the Adventurer`, as in PD2).

The player stats are the player's while in the map (Cartographer's rule).

The rows cannot roll until `t1m`/`t2m`/`t3m` are children of `cmap` (lane 1, #1308).

`ac%` (`of Cracking`, `of Crumbling`, `of Disintegration`) may be inert on the player, to be confirmed in game.

## Monster and area stats (#1309-#1313)

The rows of #1309-#1313 now carry PD2's mods, through Cartographer's catalogue properties (`docs/cartographer/contracts.md`). Names are PD2's, row ids kept, ranges and `spawnable`/`rare` PD2's (`t3m,t4m` becomes `t3m`).

| Issue | PD2 stat | Becomes | Rows |
|---|---|---|---|
| #1309 | `map_mon_extra_fire/ltng/cold/pois/mag` | `mon-mastery-*` | 54 (with the phys-as-extra lines) |
| #1309 | `map_mon_phys_as_extra_*` | `mon-phys-as-*` | |
| #1310 | `map_mon_att_pierce` | `mon-att-pierce` (attack rating and missile pierce, one roll) | 30 of 36 |
| #1310 | `map_mon_crush`, `map_mon_openwounds` | `mon-crush`, `mon-openwounds` | |
| #1310 | `map_mon_lifesteal_hp%` | `mon-lifesteal-hp` (life steal and monster life, one roll) | |
| #1310 | `map_mon_att_cast_speed` | `mon-cast-speed` (attack and cast speed, one roll) | |
| #1311 | `map_mon_ac%`, `_abs_*%`, `_red_dmg`, `_regen`, `_balance1`, `_curseresist_hp%` | `mon-armor-pct`, `mon-absorb-*-pct`, `mon-dr`, `mon-regen`, `mon-fhr`, `mon-curse-res` | 54 |
| #1312 | `map_glob_add_mon_*` (9 groups) | `map-extra-<group>`, one property and mod stat per group | 10 |
| #1312 | `map_glob_extra_boss` | `map-extra-boss` | |
| #1313 | `map_glob_monsterrarity` | `map-uniques` | 30 |
| #1313 | `map_mon_dropweapons/armor/crafting/jewelry` | `map-drop-weapons/armor/crafting/jewelry` | |

A row has three mod slots, so when PD2 pairs two stats on one roll the port uses a combined property (`func3` shared roll): `mon-att-pierce`, `mon-cast-speed`, `mon-lifesteal-hp`, `mon-nofreeze-hp`, `map-xp-drop`.

### Stats that need a Param (statroutes)

Cartographer's drop class and extra group stats take a Param (a treasure class, a monstats Id), but a property cannot carry one. Instead of one stat with a Param, each Param has its own mod stat and property (append-only ids pinned in `merge/ids-itemstatcost.tsv`), and `files/data/global/excel/d2rloader/cartographer/statroutes.txt` routes each stat to the behaviour and Param:

- itemstatcost 637-640 `cartographer_map_drop_weapons/armor/crafting/jewelry` (`drop_tc`, TC `MapStat DropWeapons` and so on, PD2's classes), properties 448-451;
- itemstatcost 641-649 `cartographer_map_extra_doll/succ/vamp/cow/horde/ghost/souls/fetish/shriek` (`extra_groups`, monstats Id `bonefetish6`, `succubusMod`, `vampire8MapMod`, `hellbovine`, `reanimatedhorde6`, `wraithMapMod`, `willowisp1MapMod`, `fetishmap`, `baalminion3MapMod`), properties 452-460.

PD2 names the groups by monstats number; the port names the Id, which is what `extra_groups` reads. Each stat has its own tooltip string (`MapExtraGroupDoll` to `MapExtraGroupShriek` in `item-modifiers.json`), so the line names the group: "+1 Extra Monster Group: Hell Temptresses".

## Still neutral

- Splashing (6 rows, [#1310](https://github.com/synpoox/pd2-resurrected/issues/1310)): Cartographer has no splash stat.
- Approximated: `map_play_mag_gold%` carries magic find only, except on `of Darkness` (combo `map-mag-gold`); `map_mon_curseresist_hp%` carries curse resistance only; the phys-as cold and poison lengths (PD2 params 25 and 125) become Cartographer's fixed 50 and 100 frames; PD2's own `abs` stat names cross-wire the elements (`map_mon_abs_fire%` points at cold, and so on), so the port maps by each property's name (Crimson = fire), an owner-approved deviation (docs/deviations.md). The meaning of `mon-att-pierce` (Cartographer's `mon-tohit` and `mon-missile-pierce`) is not checked in game.

## Placeholders and rare names

The five Cartographer affixes (ids 9201-9205: Teeming, Savage, Hastened, of the Swarm, of Embers) are kept as they are: tierless, frequency 10, rolling beside the ported ones. The rare names Labyrinth and Warren are kept; PD2 has no map rare names.

## Strings

`of the Adventurer` had no affix string: id 51541 in `item-nameaffixes.json`. `Large` uses D2R's own `Large` key (`ui.json`).

## Test

`tools/tests/test_map_affixes.py`: every affix row with a `cmap`/`t1m`..`t3m` type names only real properties and only those types, and the rolling set (spawnable 1, rare 1, frequency above 0) is the 102 rows and holds the 27 of #1305, so a dead pool fails. The Splashing rows stay neutral, every `statroutes.txt` row names a real stat, treasure class or monstats Id, and `corrupted`/`corruptor` route to `None` so the map panel skips them (#1349).
