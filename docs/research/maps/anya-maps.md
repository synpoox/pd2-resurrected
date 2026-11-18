# Anya's Hell maps (#1323)

PD2 keeps Malah's scroll and Anya's own rare class item, and in Hell only also puts 3 random T1 maps (item level 99, normal quality) in the player's inventory (on the ground when full). Implemented in `plugin/core-events/lane-c/anya_maps.cpp`, part `anyamaps`.

**Hook:** D2R 93847 `0x546cc0` (A5Q3AnyaGiftExecute, the deferred event queued by the a5q3 handler `0x54d610`). The detour runs the original, then if `game+0x104 == 2` it finds the player (FindUnit; closure `+8` guid, `+0x10` game) and calls QuestCreateItem 3 times with a `t1m` code, item level 99, quality 2, drop flag 1. Each map is picked uniformly from the items rows whose type (items record `+0x12e`, int16 itemtypes row) is the `t1m` itemtypes row; both come from the loaded tables, never a fixed list. Quest 0x25 bit 10 (set before the event is queued) makes it fire once per character per difficulty.

| RVA | Name | Signature (each matches once; no fixed RVA is used) |
|---|---|---|
| `0x546cc0` | A5Q3AnyaGiftExecute | `48 89 5c 24 08 48 89 54 24 10 57 48 83 ec 30 44 8b 41 08 48 8b d9 48 8b 49 10 33 d2` |
| `0x517530` | QuestCreateItem (game, player, code, ilvl, quality, drop) | `48 89 6c 24 18 48 89 74 24 20 57 48 81 ec a0 00 00 00 41 8b c0 48 8b f2 48 8b e9 4c 8d 44 24 60 0f b6 89 06 01 00 00` |
| `0x314110` | ItemRecord (dataFlag, class id) -> items record | the toolkit's `d2r::sig::ItemRecord`: `40 57 48 83 ec 30 8b fa` |
| `0x48fe80` | FindUnit (game, type, id) | the toolkit's `d2r::sig::FindUnit` (a call-site entry). Entry bytes, matching once: `48 89 5c 24 08 48 89 74 24 18 57 48 83 ec 20 41 8b d8 8b f2 48 8b f9 48 85 c9 75 13 88 4c 24 38 48 8d 4c 24 38 e8 ?? ?? ?? ?? 84 c0 74 01 cc 83 fe 05 75 09 48 8d 87 30 36 00 00` |

On a scan failure the part logs one line and stays off. One log line per grant: `anya maps: gave 3 T1 maps (<codes>)`.
