# PD2's unidentified server rules ([W37](https://github.com/synpoox/pd2-resurrected/issues/245))

Answers [W37](https://github.com/synpoox/pd2-resurrected/issues/245) (core-rules Q6): the Act 3 quest 5 patch, the skill palette and NPC patches, the perfect
shrine, the player death handler and the two more PlrModes hooks, and the open rows `.set-home` and `skilldelay`.
Static reading only, nothing run.

**Build.** Read from the Ghidra project `C:\ghidra-work\pd2-project` (`ProjectDiablo.dll` sha256 `2ff6c327…`, the
build [plrmodes](../../pd2/plrmodes.md) and the earlier research used, and its 1.13c `D2Game.dll` `24b293b8…`, base
`0x6fc20000`). The PD2 install in `C:\Games\PD2\ProjectD2` (and `Live\`) updated on 2026-11-05 to `e7eec256…`; its
addresses moved (gate A's stub `0x103a41b0` is `0x103a4220` there), so the VAs below are the Ghidra build's. D2R side:
the 93847 dump in `C:\ghidra-work\project`.

## How PD2 changes D2Game here

Besides the call-rewrite patch table ([plrmodes](../../pd2/plrmodes.md)), PD2's start-up routine `0x10344580` runs installers
(`0x1036b9f0`, `0x1036ba20`, `0x103ad290`, `0x1036c320`, `0x103a1230`, ...) that write PD2 functions into 1.13c
function-pointer tables. Each table is found through a per-version offset (`0x103a5f20(dll, offset)`; version 0 =
1.13c). Many of those PD2 functions are vanilla code compiled again, which is why vanilla file names show in their
asserts (`a3q5.cpp`, `SkilPal.cpp`, `SUNITNPC.CPP`). A file name in an assert is not a rule change: each function
below was diffed against the vanilla entry it replaces.

| Installer | 1.13c table (D2Game RVA) | Entries read here |
|---|---|---|
| `0x1036c320` | quest callbacks `0x107bb8` | `+0xd4` Compelling Orb (a3q5) |
| `0x1036ba20` | skill tables (srvst `0x107338`, srvdo, cltdo) | srvst 72, srvdo 113 |
| `0x103ad290` | client skill starts `0xde928` | cltst 57 |
| `0x1036b9f0` | item spells `0x107238` (8 bytes per spell: check, use) | `+0x6c` = spell 13 use |
| `0x103a1230` | shrine functions `0xfa5f8` (12 bytes per shrine code) | codes 6-15 and 18 |

## Act 3 quest 5 (`a3q5.cpp`)

Quest table `0x107bb8 + 0xd4`: the Compelling Orb's operate function (smash it with Khalim's Will, `qf2`). Vanilla
`0x6fd0a140`, PD2 `0x103aa800`. Same steps (two hits, quest flags 0x12, take the flail, break the orb, open the way to
the Durance), with three null checks added: the object, the game, and the player's quest record 0x13 (vanilla
dereferences a missing record). PD2 finds the player through D2Game's own unit lookup (`0xe03a0`) where vanilla walks
the hash inline. No rule change: a crash guard. D2R has its own Khalim's Will smash.

**Verdict: already native (skip). Size: none.**

The same installer replaces 18 other quest-table entries (`0x103a94b0`, `0x103a9670`, `0x1034c280`-`0x1034c6b0`,
`0x103aa9e0`-`0x103aa670`, and 4 in a second table, D2Game `0x10e050`). They were not read; most are likely the same kind of
recompile (owner question 1).

## Skill palette (`SkilPal.cpp`)

The only two functions that name `SkilPal.cpp` are srvst 72 (`0x103b67e0`) and cltst 57 (`0x103ace50`): PD2's Charge
starts, decoded and built in [W16](https://github.com/synpoox/pd2-resurrected/issues/91) ([w16-charge](../skills/w16-charge.md)).

**Verdict: already built ([W16](https://github.com/synpoox/pd2-resurrected/issues/91)). Size: none.**

## NPC patch (`SUNITNPC.CPP`)

`0x10383ea0` is item spell 13's use (table `0x107238 + 0x6c`): the unlimited ID book (`rid`) identifies every
unidentified item in the inventory and Cube (skill `0x201` = 513); for skill `0x202` (514, the unlimited TP book `rtp`)
it calls the Town Portal use (`0x10382f00`, spell 2). Built in
[IR5](https://github.com/synpoox/pd2-resurrected/issues/240) (core-rules row "Unlimited TP and ID books", `plugin/core-rules/almanac.cpp`).

One part not covered by IR5's row: PD2's srvdo 113 (`0x103b0580`, the skill-bar use of books and scrolls) first looks
for an `rid` or `rtp` in the inventory or Cube whose item skill is the cast skill (PD2 513/514, merged 582/583
"Book of Unlimited Identify/Townportal") and calls `0x10383ea0` directly, so the book is never used up; any other book
goes to vanilla's srvdo 113 (`0x1031b200` stub). In the port, 582/583 keep srvdo 113 and run D2R's own srvdo 113.
Whether D2R's reaches the IR5 item spell hooks, and keeps the book, is not read.

**Verdict: already built (IR5) for the item use; verify the skill-bar use (S, a game check, port only if it fails).**

## Gem shrine and the "perfect shrine"

`PerfectShrine` is a string key in `patchstring.tbl`: "Perfect Gem Shrine activated". Shrine table `0xfa5f8 + 0xd8`
(shrine code 18, Gem Upgrade): vanilla `0x6fc8b430`, PD2 `0x103a0fc0`. Three changes:

1. **Stacked gems.** The upgrade goes through `0x10371200`: when the gem's quantity (stat 70) is over 1, it takes one
   off the stack and creates one upgraded gem; otherwise vanilla's upgrade (`0x6fc8b070`), which replaces the whole
   item with one new gem. The port's gems stack to 50 (`files/.../misc.txt`), and D2R's gem shrine is vanilla's, so a
   stack of 50 chipped gems would most likely become one flawed gem (not run).
2. **No gem.** One random chipped gem, as vanilla (PD2 rolls with `rand`, vanilla with the object's seed).
3. **Perfect Gem Shrine.** On the chipped-gem path only, `rand() % 5000 < 2` (1 in 2500) sends a game chat line
   (packet 0x26, the player's name and "Perfect Gem Shrine activated") to every client in the game. No item or stat
   change was found with it: it is an announcement.

The same installer changes ten more shrine codes:

4. **Shrines are shared.** Codes 6 (armor), 8-11 (resists), 12 (skill), 13 (mana recharge), 15 (experience)
   (`0x103a0de0`), 7 (combat, `0x103a0e80`) and 14 (stamina, `0x103a0f20`) run vanilla's effect on the user, then on
   every other player in the game whose position is within a squared distance of 450 (`0x1037d840`, about 21
   subtiles) of the user.
5. **Resist shrines raise the cap.** After codes 8-11 (`0x103a0d60`), the shrine state's stat list gets +10 of the
   matching max resist: fire (state 131) `maxfireresist`, cold (132) `maxcoldresist`, lightning (130)
   `maxlightresist`, poison (133) `maxpoisonresist`. shrines.txt does not hold it (its Arg0 is the +75%).

D2R does none of these.

**Verdict: port 1 (S), 4 and 5 (S together), 3 (S, owner's call: flavour only). 2 needs nothing.**

## Player death handler (`0x1036fcd0`)

Written into D2Game's player mode table at `0xfa480` (dead, mode 17) and at the call `0x798fe`; vanilla `0x6fc99730`.
Outside the PvP arenas (`0x1037c7b0`: levels 157, 159, 166) it does vanilla's death (corpse and item drop, then the
pets: PD2's `0x1037fb10` is vanilla's `0x6fcb7730`, which kills the player's pets), and adds:

- death counters at player data `+0x39b` and `+0x417`: the `.deaths` command, cut ([#210](https://github.com/synpoox/pd2-resurrected/issues/210));
- on a hardcore game (flag `0x800`) `0x10382670(0)`: resets PD2's stash page, not ported (D2R's stash is kept);
- two lookups whose results are dropped (`0x103a1840`, `0x1037fe10`): no effect found.

In a PvP arena it leaves no corpse, warps the player, keeps an Iron Golem (pet type 3) and counts a kill for the
killer: cut with PvP.

**Verdict: skip (no rule outside PvP, the stat commands and PD2's stash pages). Size: none.**

## The two more PlrModes hooks

- **`0x1036e950`** replaces D2Game `0x54320`, the character save, at 12 call sites (one in the death path,
  `0x79810`). Around the save it packs PD2's extra stash pages (items on pages 4 and 5 moved by their page offset,
  `0x10382450`, `0x103828e0`, then restored, `0x10382990`, `0x10382540`), skips the save in level 166, and clears a
  client flag in the PvP arenas. D2R saves its own stash tabs. **Verdict: skip. Size: none.**
- **`0x10374020`** replaces D2Game `0xf02a0`, the player relation test (party, hostile and loot flags between two
  players), at 13 call sites: the same walk with null and unit-type checks added. **Verdict: skip (crash guard). Size:
  none.**

## `.set-home`

`0x10363e90` (registered by `0x10364240`) handles `.set-home 0-5`: it stores the act in player data `+0x435` (0
clears it; an act the player has not reached in this difficulty is refused, `0x1037f110`). The byte is saved as a
3-byte `ht` section (`0x1039ddd0` writes it, `0x1039de30` reads it, only from save version `0x901`). On joining a game,
`0x10371d30` (the call at D2Game `0x2e43f`, which places the player in the start level) puts the player in that act's
town when the act is open in the current difficulty, else in vanilla's town. A first branch, for games with flag
`0x10000` and a value at game `+0x2702`, places the player in another level; it was not decoded (owner question 3).

D2R starts the player in the town of the highest act reached and has no such command. Port: a chat-command hook (no
D2RLoader service reads chat), a per-character store (a sidecar file, not the save), and a hook on D2R's start-level
placement. **Verdict: port. Size: M.**

## `skilldelay` (states `remfunc` 12)

PD2's handler for remove function 12 was not located in this pass: no installer read here writes a remove-function
table, and the patch table's pointer writes (`0xfa120`-`0xfa388`, `0xfb5a4`-`0xfb5ec`, the unit-event table
`0xfb110`, which PD2 moves to `0x104984c8`) are other tables. D2R's remove-function table (`0x23601a0`, 30 slots,
`cmp ax,0x1d` at `0x21ae73`) has slot 12 empty, so the merged `skilldelay` row does nothing when the state ends:
D2R's own behaviour, the row's stand-in. **Verdict: still verify (S): find PD2's slot 12 first (the start: the 1.13c
code that reads states.txt's remfunc and the PD2 patch over it).**
