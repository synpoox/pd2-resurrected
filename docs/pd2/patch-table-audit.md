# PD2's patch table against our ports

Answers [#1817](https://github.com/synpoox/pd2-resurrected/issues/1817). PD2's pet AI changes were missed ([#1790](https://github.com/synpoox/pd2-resurrected/issues/1790), [summon-ai](summon-ai.md)) because nobody checked PD2's own code patches against what we ported. This page walks every record of `ProjectDiablo.dll`'s patch table and every function PD2's start-up installers write into a 1.13c table. It maps each one to the feature it changes and records our status. Static reads only; nothing was run in game.

**Build.** The patch table was parsed from the installed `C:\Games\PD2\ProjectD2\ProjectDiablo.dll` (sha256 `e7eec256…`), file offset `0x49b028`-`0x4a0744`. That is 1115 records of 20 bytes `{dll, offset, value or target, type, length}` ([plrmodes](plrmodes.md)). PD2.gpr's older build (`2ff6c327…`, table at VA `0x1049d028`) holds the same 1115 records with the same offsets, types and lengths; only the PD2 target addresses differ. The tables below give PD2 addresses in the installed build (PD2.gpr's sit `0x10` to `0x70` lower). 1.13c addresses are RVAs of the PD2 install's 1.13c `D2Game.dll` (`24b293b8…`), `D2Client.dll` and `D2Common.dll`.

**Record format.**
- **Module ids.** `dll` 0 is D2Client, 2 is D2Common and 3 is D2Game: a call rewrite's offset lands on an `e8` rel32 in that module in 338 of 350 D2Game, 331 of 342 D2Client and 50 of 55 D2Common records. 5 and 9 are inferred as D2Lang and D2Win, on the assumption that the enum is alphabetical over PD2's modules.
- **Type 1.** Rewrite the rel32 at `offset`, usually a call's, to `target`. Most targets are small register-to-stack thunks (`0x103a…`) that call the real PD2 function; the tables give that function.
- **Type 0.** Write `length` bytes of `value`. With length 0, it writes the 32-bit `value`: a pointer into a 1.13c function table, or a larger buffer size.

**Method.**
1. A script parsed the records, found the 1.13c function around each site (Ghidra, PD2.gpr), followed the thunks and grouped the records into 521 units: one per redirected callee and PD2 target, or per run of adjacent bytes.
2. Each unit was read: the decompiled PD2 function against the vanilla code it replaces.
3. Each unit was checked against `plugin/`, `docs/research/`, `docs/pd2/`, `docs/deviations.md`, `docs/roadmap.md` and `docs/features/`, by address and by name.

Status is one of four:
- **ported**: the code or data that does it.
- **deviation**: a `docs/deviations.md` row covers it.
- **missing**: a GitHub issue now tracks it.
- **not needed**: client-only work D2R already does, a feature cut on the roadmap, or no behaviour change. The row says which.

"Undecoded" rows are read only partly. Their issues ([#1847](https://github.com/synpoox/pd2-resurrected/issues/1847), [#1848](https://github.com/synpoox/pd2-resurrected/issues/1848)) ask for one issue per real rule once decoded.

## Summary

| Status | Records | Units |
|---|---|---|
| ported | 122 | 69 |
| deviation | 330 | 83 |
| missing | 168 | 108 |
| not needed | 495 | 261 |
| **total** | **1115** | **521** |

By module: D2Game 615 records, D2Client 410, D2Common 86, D2Lang 3, D2Win 1. A few features account for most of the records:
- PD2's client sound engine: 261 D2Client records. It is a deviation: D2R's sound tables are kept.
- The character save and trade buffer raises (`0x2000` to `0x20000`, for PD2's bigger stash): about 150 D2Game records. Not needed: D2R saves its own stash.
- The 1.13c object class limit (`0x23d`): a deviation, under the row for PD2's blank `<reserved>` objects rows 573-583.

The installer writes (section [Installer table writes](#installer-table-writes)) number 257:
- 99 are named by our code, and 23 more skill entries are ported or documented by function number ([#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) triage);
- 38 are named only by a doc, whose verdict stands;
- 6 are real gaps with their own issues ([#1869](https://github.com/synpoox/pd2-resurrected/issues/1869), [#1870](https://github.com/synpoox/pd2-resurrected/issues/1870), [#1871](https://github.com/synpoox/pd2-resurrected/issues/1871), [#1872](https://github.com/synpoox/pd2-resurrected/issues/1872), [#1873](https://github.com/synpoox/pd2-resurrected/issues/1873));
- 91 are still unchecked, tracked in [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849).

The unchecked set still holds 11 monster AI think replacements, the same kind of gap as the pets.

## Missing features and their issues

Filed by this audit, each a sub-issue of [#1817](https://github.com/synpoox/pd2-resurrected/issues/1817) (verdicts from the triage on PR [#1850](https://github.com/synpoox/pd2-resurrected/issues/1850)):
- [#1828](https://github.com/synpoox/pd2-resurrected/issues/1828): item sockets also roll on set items, and only for non-stackables, throwing weapons and quivers.
- [#1829](https://github.com/synpoox/pd2-resurrected/issues/1829): magic items get both affixes at high item level. Data fix: the Progressive Affixes companion's `[magic]` 255 to 65/85/90.
- [#1830](https://github.com/synpoox/pd2-resurrected/issues/1830): aura-enchanted bosses take their aura from PD2's list at level mlvl/7.
- [#1831](https://github.com/synpoox/pd2-resurrected/issues/1831): Frenzy stacks in the srvdo 9 helper.
- [#1832](https://github.com/synpoox/pd2-resurrected/issues/1832): new characters' starting item creator.
- [#1833](https://github.com/synpoox/pd2-resurrected/issues/1833): Telekinesis no longer picks up arrows and bolts.
- [#1834](https://github.com/synpoox/pd2-resurrected/issues/1834): skill start gate, frame+5 event and cooldown packets. Unclear: the gate's class list and scope.
- [#1835](https://github.com/synpoox/pd2-resurrected/issues/1835): summons' first think delay and the Revive path.
- [#1837](https://github.com/synpoox/pd2-resurrected/issues/1837): curses keep full duration on monsters in Nightmare and Hell.
- [#1838](https://github.com/synpoox/pd2-resurrected/issues/1838): max level (99) marker states.
- [#1839](https://github.com/synpoox/pd2-resurrected/issues/1839): missile pierce rolled at creation and capped (`pierce_idx`).
- [#1842](https://github.com/synpoox/pd2-resurrected/issues/1842): NPC vendors in map levels (Gheed's map store).
- [#1843](https://github.com/synpoox/pd2-resurrected/issues/1843): 2H Phase Blade roll.
- [#1844](https://github.com/synpoox/pd2-resurrected/issues/1844): stun shortened on hit; monster get-hit end event. Unclear: the stun part is PvP only; the get-hit end event is open.
- [#1846](https://github.com/synpoox/pd2-resurrected/issues/1846): per-level ambient light for levels 109-137. Unclear: the byte is levels.txt `Intensity`; whether D2R's HD renderer uses it is unknown.
- [#1847](https://github.com/synpoox/pd2-resurrected/issues/1847): decode the remaining D2Game patches. Decoded into [#1861](https://github.com/synpoox/pd2-resurrected/issues/1861) (Chill also lowers cast rate), [#1862](https://github.com/synpoox/pd2-resurrected/issues/1862) (Dragon Talon kick damage), [#1863](https://github.com/synpoox/pd2-resurrected/issues/1863) (pick up from up to 6 away), [#1864](https://github.com/synpoox/pd2-resurrected/issues/1864) (half freeze duration), [#1865](https://github.com/synpoox/pd2-resurrected/issues/1865) (stun length rules).
- [#1848](https://github.com/synpoox/pd2-resurrected/issues/1848): decode the remaining D2Common patches. Decoded into [#1866](https://github.com/synpoox/pd2-resurrected/issues/1866) (crafted item requirements), [#1867](https://github.com/synpoox/pd2-resurrected/issues/1867) (dual wield attack speed), [#1868](https://github.com/synpoox/pd2-resurrected/issues/1868) (elemental mastery on missiles).
- [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849): check the installer table writes no doc names. Decoded into [#1869](https://github.com/synpoox/pd2-resurrected/issues/1869) (Blade Sentinel), [#1870](https://github.com/synpoox/pd2-resurrected/issues/1870) (Frozen Armor and Enchant on allies), [#1871](https://github.com/synpoox/pd2-resurrected/issues/1871) (Lieutenant of Sin), [#1872](https://github.com/synpoox/pd2-resurrected/issues/1872) (Rathma's spire), [#1873](https://github.com/synpoox/pd2-resurrected/issues/1873) (Unsummon drops the Iron Golem's item).
- [#1851](https://github.com/synpoox/pd2-resurrected/issues/1851): Exploding and Poison shrines drop throwing potions (`tpgs`, `tpfs`).
- [#1852](https://github.com/synpoox/pd2-resurrected/issues/1852): blank-rarity unique rows weigh 0 (Embersworn drops for us). Data fix: Embersworn `spawnable` 1 to 0.

Closed by triage as close enough (now not needed):
- [#1836](https://github.com/synpoox/pd2-resurrected/issues/1836): Talic's Fire Pierce stacks. Close enough: the state comes only from the cut Uber Ancients' aura.
- [#1840](https://github.com/synpoox/pd2-resurrected/issues/1840): frame advance and frame-event scheduler. Close enough: the 0x90 cap and event 4 are vanilla's; D2R `0x491240` runs the same loop.
- [#1841](https://github.com/synpoox/pd2-resurrected/issues/1841): monster 991's leap hold. Close enough: monster 991 is `uberancientbarb3`, cut content.
- [#1845](https://github.com/synpoox/pd2-resurrected/issues/1845): 5 or 20 stat and skill points per click. Close enough: D2R already has bulk stat assignment; only the 5/20 steps differ.

Already tracked, not refiled:
- [#1803](https://github.com/synpoox/pd2-resurrected/issues/1803): the mercenary think.
- [#1800](https://github.com/synpoox/pd2-resurrected/issues/1800): the Shadow Warrior, including its leash skip at `+0xac550`.
- [#1144](https://github.com/synpoox/pd2-resurrected/issues/1144): `.set-home`'s start level.

The other pet issues, [#1801](https://github.com/synpoox/pd2-resurrected/issues/1801) and [#1802](https://github.com/synpoox/pd2-resurrected/issues/1802), map to the ported or tracked summon rows. [#1795](https://github.com/synpoox/pd2-resurrected/issues/1795) (the shared-hook sigscan) has no record here.

## Open questions

- PD2 builds more patch records past the end of the table parsed here ([#1874](https://github.com/synpoox/pd2-resurrected/issues/1874)): likely [#1834](https://github.com/synpoox/pd2-resurrected/issues/1834)'s cooldown records and the records [item-durability](../research/items/item-durability.md) cites. This audit does not cover them yet.
- [item-durability](../research/items/item-durability.md) cites patch records at D2Game `+0xec4f`, `+0x13033` and `+0xa16be`, outside this table range; [#1874](https://github.com/synpoox/pd2-resurrected/issues/1874) likely answers this.
- [ir3-combat-stats](../research/items/ir3-combat-stats.md) calls `0x103046a0` dead code, but D2Common's missile fill (`+0x6bd1a`, `+0x6bf87`) is routed to it here.
- The table-base globals the installers use (`0x105ddeb0` and the others) are filled at run time through `0x103a5f20(module, offset)`. The table names below come from w37's reads and from which of our docs name the functions, not from those offsets.

## Patch records by feature

Each row lists its record count, then each site. A D2Game call site is the `e8` byte; the record's offset is one byte later.

### Sound engine (volume groups)

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 261: D2Client call `+0x14f92`, `+0x14fb2`, `+0x15192`, `+0x1ddbd`, `+0x1df0e`, `+0x1df79`, `+0x1e212`, `+0x1e278`, `+0x1e290`, `+0x1e2a8`, `+0x1e2c0`, `+0x1e2f3`, `+0x1e305`, `+0x1e324`, `+0x1f367`, `+0x20b61`, `+0x22c6f`, `+0x22d11`, `+0x22d77`, `+0x231b1`, `+0x23506`, `+0x23683`, `+0x23695`, `+0x2373c`, `+0x23755`, `+0x237b0`, `+0x23836`, `+0x23898`, `+0x238e0`, `+0x23a00`, `+0x23d96`, `+0x23e04`, `+0x23f72`, `+0x23f8a`, `+0x23fae`, `+0x24003`, `+0x24029`, `+0x2404f`, `+0x24072`, `+0x24080`, `+0x240a3`, `+0x240b1`, `+0x240d7`, `+0x240e8`, `+0x24112`, `+0x2413f`, `+0x24157`, `+0x24189`, `+0x241a1`, `+0x241b9`, `+0x2420a`, `+0x242fb`, `+0x2440b`, `+0x2446c`, `+0x24554`, `+0x24563`, `+0x24a26`, `+0x24ac8`, `+0x28226`, `+0x282e2`, `+0x29a8e`, `+0x2c02d`, `+0x40b47`, `+0x40c80`, `+0x40d29`, `+0x46f48`, `+0x4786e`, `+0x479be`, `+0x479d2`, `+0x47a5a`, `+0x4804e`, `+0x484be`, `+0x49278`, `+0x4a84a`, `+0x4aabb`, `+0x4ab09`, `+0x4b264`, `+0x4b281`, `+0x4b60b`, `+0x4b69e`, `+0x4bcf1`, `+0x4e5f0`, `+0x4e60a`, `+0x4e624`, `+0x4e63e`, `+0x4e66f`, `+0x4e6b8`, `+0x506f8`, `+0x507ab`, `+0x53271`, `+0x57a66`, `+0x598b1`, `+0x59921`, `+0x59991`, `+0x5a2cb`, `+0x5a347`, `+0x5a384`, `+0x5a3bb`, `+0x5a45d`, `+0x5a492`, `+0x5a4da`, `+0x64cef`, `+0x65167`, `+0x65205`, `+0x65227`, `+0x66f86`, `+0x6849c`, `+0x68518`, `+0x68659`, `+0x6fc9b`, `+0x71963`, `+0x71a1c`, `+0x72c78`, `+0x736b2`, `+0x7380f`, `+0x7417c`, `+0x7987d`, `+0x79b13`, `+0x79c33`, `+0x7cde2`, `+0x7d081`, `+0x7d374`, `+0x7d5ad`, `+0x7d834`, `+0x7e660`, `+0x80687`, `+0x82761`, `+0x827e5`, `+0x827fa`, `+0x828e6`, `+0x84704`, `+0x8ad73`, `+0x90f82`, `+0x91095`, `+0x9617e`, `+0x9624d`, `+0x96440`, `+0x96652`, `+0x96851`, `+0x96a63`, `+0x96dde`, `+0x96fe1`, `+0x97096`, `+0x972ae`, `+0x98d49`, `+0x98e82`, `+0x98ec7`, `+0x991b3`, `+0x9ad3c`, `+0x9ae4a`, `+0x9b391`, `+0x9b3e7`, `+0x9b4bc`, `+0x9bd84`, `+0x9bdcc`, `+0x9be12`, `+0x9bf63`, `+0x9c15d`, `+0x9c19a`, `+0x9c1cc`, `+0x9c20e`, `+0x9c341`, `+0x9f80f`, `+0x9f84e`, `+0x9f9a6`, `+0x9f9cf`, `+0x9fddd`, `+0x9fead`, `+0xa02c9`, `+0xa0456`, `+0xa1c4c`, `+0xa1c72`, `+0xa20db`, `+0xa2124`, `+0xa2194`, `+0xa2bdf`, `+0xa2c05`, `+0xa2e75`, `+0xa2e9b`, `+0xa4541`, `+0xa45a1`, `+0xa6f9e`, `+0xa6fb2`, `+0xaa552`, `+0xaa5ca`, `+0xaa9b1`, `+0xadb09`, `+0xadb1e`, `+0xb15f4`, `+0xb255e`, `+0xb36df`, `+0xb759f`, `+0xb91a9`, `+0xbb4df`, `+0xbb630`, `+0xbcd87`, `+0xbce6e`, `+0xc4bbc`, `+0xc5020`, `+0xc6974`, `+0xc6c9a`, `+0xc6ccd`, `+0xc6cde`, `+0xc6df5`, `+0xc8872`, `+0xc8f23`, `+0xc916e`, `+0xc9c74` (to `0x88a70`) -> PD2 `0x102c6c60`<br>D2Client `+0x22625`: `8e` -> `92`<br>D2Client `+0x22642`: `8e` -> `92`<br>D2Client `+0x22e41`: `8e` -> `92`<br>D2Client `+0x22f51`: `8e` -> `92`<br>D2Client `+0x230e7`: `8e` -> `92`<br>D2Client call `+0x232ce`, `+0x232fc`, `+0x2331e`, `+0x23346`, `+0x23374`, `+0x2339f`, `+0x233c7`, `+0x233ef`, `+0x2341a`, `+0x23445`, `+0x23470`, `+0x53089`, `+0x53103`, `+0x53263`, `+0x5349e` (to `0x45570`) -> PD2 `0x102c82b0`<br>D2Client `+0x23ab7`: `8e` -> `92`<br>D2Client `+0x23b6d`: `8e` -> `92`<br>D2Client call `+0x44bcb`, `+0x44ea0`, `+0x71adf`, `+0x71bae` (to `0x5f020`) -> PD2 `0x102c5520`<br>D2Client call `+0x4509d` (to `0x5ef90`) -> PD2 `0x102c5460`<br>D2Client call `+0x4515d`, `+0x453b5`, `+0x89a9b` (to `0x5d110`) -> PD2 `0x102c73a0`<br>D2Client call `+0x45162`, `+0x453ba`, `+0x89aa0` (to `0x885a0`) -> PD2 `0x102c83b0`<br>D2Client call `+0x453bf` (to `0x88d20`) -> PD2 `0x102c79a0`<br>D2Client call `+0x453c4` (to `0x5d180`) -> PD2 `0x102c7360`<br>D2Client call `+0x453c9` (to `0x5df50`) -> PD2 `0x102c6a70`<br>D2Client `+0x5deb3`: `8e` -> `92`<br>D2Client `+0x5dedf`: `8e` -> `92`<br>D2Client `+0x5df16`: `8e` -> `92`<br>D2Client `+0x7d9ec`: `54` -> `6c`<br>D2Client call `+0x7da1a`, `+0x88ae3` (to `0x87d80`) -> PD2 `0x102c76d0`<br>D2Client `+0x87ffc`: `8e` -> `92`<br>D2Client `+0x8803b`: `8e` -> `92`<br>D2Client `+0x8828e`: `8e` -> `92`<br>D2Client `+0x88458`: `8e` -> `92`<br>D2Client `+0x8856b`: `8e` -> `92`<br>D2Client `+0x88735`: `8e` -> `92`<br>D2Client `+0x88aa4`: `8e` -> `92`<br>D2Client `+0x88c7f`: `8e` -> `92`<br>D2Client call `+0x892cd` (to `0x5d7a0`) -> PD2 `0x102c6b20`<br>D2Client call `+0x89aa5` (to `0x45520`) -> PD2 `0x102c5690`<br>D2Client call `+0x9cdae` (to `0x5ee70`) -> PD2 `0x102c5d70` | `+0x14f40`, `+0x14fd0`, `+0x1e160`, `+0x1ef10`, `+0x20a60`, `+0x225d0`, `+0x22c10`, `+0x22ca0`, `+0x22d60`, `+0x22da0`, `+0x22eb0`, `+0x23070`, `+0x23170`, `+0x23200`, `+0x23630`, `+0x237e0`, `+0x23a30`, `+0x23c00`, `+0x23f20`, `+0x242b0`, `+0x24490`, `+0x24680`, `+0x24a70`, `+0x29990`, `+0x2bf40`, `+0x40ca0`, `+0x44b50`, `+0x46ee0`, `+0x47970`, `+0x47d60`, `+0x483b0`, `+0x4a4f0`, `+0x4a8c0`, `+0x4b5d0`, `+0x4bc80`, `+0x4dc70`, `+0x50750`, `+0x53070`, `+0x530d0`, `+0x578f0`, `+0x598a0`, `+0x59910`, `+0x59980`, `+0x5de90`, `+0x64fd0`, `+0x68440`, `+0x684e0`, `+0x68560`, `+0x6fab0`, `+0x72c20`, `+0x73630`, `+0x73760`, `+0x796f0`, `+0x7cc60`, `+0x7cfc0`, `+0x7d200`, `+0x7d3c0`, `+0x7d610`, `+0x7d850`, `+0x7dfb0`, `+0x805a0`, `+0x82660`, `+0x827c0`, `+0x82820`, `+0x844f0`, `+0x87fe0`, `+0x88230`, `+0x88440`, `+0x88550`, `+0x886f0`, `+0x88a70`, `+0x88c20`, `+0x88dd0`, `+0x8ad30`, `+0x90f60`, `+0x91070`, `+0x96690`, `+0x96aa0`, `+0x98b60`, `+0x98dc0`, `+0x9a920`, `+0x9cd07`, `+0x9f7b0`, `+0x9f830`, `+0x9f950`, `+0x9fd70`, `+0x9fe50`, `+0xa0250`, `+0xa1b10`, `+0xa1cf0`, `+0xa2950`, `+0xa2c90`, `+0xa6f60`, `+0xb1420`, `+0xb2500`, `+0xb30f0`, `+0xb73e0`, `+0xb8f00`, `+0xbb470`, `+0xbb5b0`, `+0xc4b00`, `+0xc6940`, `+0xc6aa0`, `+0xc6d00`, `+0xc8710`, `+0xc8ce0`, `+0xc9030` | PD2's own client sound engine: sounds.txt record grows 0x8e -> 0x92 with a volume-group field read by PD2's play, update, init and close functions; per-sound 50 ms replay throttle | deviation | PD2 tables soundenviron and sounds: D2R's are kept | - |

### Level ambient light

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 4: D2Client call `+0x138d6` (to `0xc5a4` D2Common#10731) -> PD2 `0x102eeb30`<br>D2Client call `+0x52f44`, `+0xa9cfe`, `+0xa9d68` (to `0xa9a20`) -> PD2 `0x102eea60` | `+0x138c0`, `+0x52f10`, `+0xa9c90` | For levels 109-137 PD2 replaces the level light value (D2Common #10731) and the ambient light colour (D2Client 0xa9a20) with a per-level byte (+0x88 of PD2's level record) or 0x80 | missing | [#1846](https://github.com/synpoox/pd2-resurrected/issues/1846); unclear (triage): the byte is levels.txt Intensity; whether D2R's HD renderer uses it is unknown | - |

### Object class limit

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 8: D2Client `+0x224fa`..`+0x224fb`: `734a` -> `7343`<br>D2Client `+0x2257a`..`+0x2257b`: `7328` -> `731d`<br>D2Client `+0x22bb5`..`+0x22bbb`: `3d3d020000721f` -> `9090909090eb43`<br>D2Client `+0x247fe`..`+0x2480e`: `83f83b740c83f83c74076860..` -> `909090909090909090909090..` | `+0x224f0`, `+0x22570`, `+0x22b20`, `+0x247a0` | Client checks that exit past object class 572 are skipped, and the class 59/60 check in the object-mode packet is NOPed, so PD2's objects past 572 work | deviation | PD2's blank <reserved> objects rows 573-583: D2R's rows stand there | - |
| 6: D2Game `+0x2e8d1`: `3d` -> `e8`<br>D2Game call `+0x2e8d1(3d)` (to `0x2eb13`) -> PD2 `0x103a2660`<br>D2Game `+0x2e8d6`: `7d` -> `75`<br>D2Game `+0x2f3a0`..`+0x2f3a4`: `6681ff3d02` -> `9090909090`<br>D2Game `+0x2f3af`..`+0x2f3b0`: `7207` -> `eb07` | `+0x2e8b0`, `+0x2f300` | cmp 0x23d raised for objects 573-583 (103a2660) | deviation | PD2's blank <reserved> objects rows 573-583: D2R's rows stand there | - |
| 7: D2Common `+0xc59c`: `3d` -> `e8`<br>D2Common call `+0xc59c(3d)` (to `0xc7de`) -> PD2 `0x103a2660`<br>D2Common `+0xc5a1`..`+0xc5a2`: `0f8d` -> `0f85`<br>D2Common `+0x19fea`: `3d` -> `e8`<br>D2Common call `+0x19fea(3d)` (to `0x1a22c`) -> PD2 `0x103a2660`<br>D2Common `+0x19fef`: `7d` -> `75` | `+0x19fa0`, `+0xc060` | D2Common Preset.cpp +0xc59c and +0x19fea: the object class test cmp 0x23d (573) becomes PD2's range check 573-583 (0x103a2660) | deviation | PD2's blank <reserved> objects rows 573-583: D2R's rows stand there | - |

### Unlimited TP and ID books

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 2: D2Client call `+0x28c70`, `+0x3e93e` (to `0xce9e` D2Common#10353) -> PD2 `0x102fbc20` | `+0x28bc0`, `+0x3e7f0` | Client quantity read returns 99 for items 513/514 (rid, rtp) | ported | plugin/core-rules/almanac.cpp; rtp: plugin/core-rules/item_rules.cpp | - |
| 3: D2Game call `+0xd43ed`, `+0xd3206`, `+0xd2be5` (to `0xd1aa0`) -> PD2 `0x10382b80` | `+0xd2be0`, `+0xd31d0`, `+0xd43e0` | the item skill of a book is read through 0x10382b80, which returns rid/rtp's skill | ported | plugin/core-rules/almanac.cpp (IR5) | - |

### Phasing

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 1: D2Client call `+0x2b29f` (to `0xcf16` D2Common#11093) -> PD2 `0x103753e0` | `+0x2b1e0` | Client state hook re-runs PD2's phasing (0x10380c50) when state 13 or 244 turns on or off | deviation | Phasing: path calls and state refresh | - |
| 1: D2Client call `+0x55d9d` (to `0x54e10`) -> PD2 `0x102ee7b0` | `+0x55b20` | Tooltip prints StrPhasingStatDesc for Phasing SelfAura level 1 | deviation | Phasing tooltip line | - |
| 1: D2Game `+0xfbba4`..`+0xfbba7`: ptr PD2 `0x10345ee0` | - | state callback (+0xfbba4) refreshes Phasing (0x10345ee0) | ported | plugin/core-rules/item_rules.cpp (IR7) | - |

### Desecration panel

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 2: D2Client call `+0x9fb28` (to `0x9f460` useitem.cpp) -> PD2 `0x102f1af0`<br>D2Client `+0xdeccc`..`+0xdeccf`: ptr PD2 `0x102c0ff0` | `+0x9fb00` | Using item 621 on the cursor opens PD2's desecrate panel; a client packet handler is replaced to drive it | deviation | Desecration: made at the Sacrifice Altar's plugin panel, with the cube's art | - |

### Stat and skill point bulk assign

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 2: D2Client call `+0x79675` (to `0x14a10`) -> PD2 `0x10398450`<br>D2Client call `+0xbdf43` (to `0x143e0`) -> PD2 `0x10398370` | `+0x78fc0`, `+0xbdca0` | Shift or ctrl (or a controller button) on the stat or skill tree button spends 5 or 20 points at once instead of 1 | not needed | close enough ([#1845](https://github.com/synpoox/pd2-resurrected/issues/1845) triage): D2R already has bulk stat assignment; only the 5/20 steps differ | D2R's own bulk assign |

### PvP

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 26: D2Client call `+0x1948c` (to `0x51ae0`) -> PD2 `0x102ef530`<br>D2Client call `+0x1bc33`, `+0x1bcb3`, `+0x1bd9a`, `+0x1bdc6`, `+0x1be19` (to `0x1b7f0` Player.cpp) -> PD2 `0x102ef5c0`<br>D2Client call `+0x51cb9` (to `0x51650` Select.cpp) -> PD2 `0x102ef4f0`<br>D2Client call `+0x51cf3` (to `0x51390`) -> PD2 `0x102ef8c0`<br>D2Client call `+0x51dfb`, `+0x51e4c`, `+0x51ee5`, `+0x52164` (to `0x51860`) -> PD2 `0x102f3ac0`<br>D2Client call `+0x61a3c` (to `0x61490` automap.cpp) -> PD2 `0x102ee230`<br>D2Client `+0x659cd`: `8b` -> `e8`<br>D2Client call `+0x659cd(8b)` (to `0xbcbfd9e7`) -> PD2 `0x102ee270`<br>D2Client `+0x659d2`..`+0x659d9`: `6ff682eb01000004` -> `90909090909085c0`<br>D2Client call `+0x76b7a`, `+0x762db`, `+0x77773` (to `0xc208` D2Common#10057) -> PD2 `0x102faea0`<br>D2Client `+0xc1b1f`: `a1` -> `e8`<br>D2Client call `+0xc1b1f(a1)` (to `0x6fc8d4a4`) -> PD2 `0x102ee270`<br>D2Client `+0xc1b24`..`+0xc1b2a`: `f680eb01000004` -> `909090909085c0` | `+0x19350`, `+0x1bbc0`, `+0x1bc40`, `+0x1bcc0`, `+0x1bde0`, `+0x51ae0`, `+0x51d10`, `+0x51e30`, `+0x51e80`, `+0x619e0`, `+0x65760`, `+0x761c0`, `+0x76b20`, `+0xc1a80` | Client hover, select, automap and party checks gated on PD2's PvP arena tests (0x1037c7b0, 0x1037c740, 0x1037c820) and a client flag at +0x1eb | not needed | cut on roadmap: PvP | - |
| 5: D2Game call `+0xbfbcc`, `+0xbe85e`, `+0xbdddf` (to `0xbcaa0`) -> PD2 `0x10385130`<br>D2Game call `+0xbf89f`, `+0xbf7be` (to `0xbf2e0`) -> PD2 `0x103844b0` | `+0xbdd80`, `+0xbe640`, `+0xbf990` | vendors keep PvP potions and PvP charms (pvpp, cm1p-cm3p) in stock (0x10385130); PvP arenas hand out PvP potions (0x103844b0) | not needed | cut on roadmap: PvP | - |
| 3: D2Game call `+0xa4a49`, `+0xb0729`, `+0xb11a9` (to `0xa478` D2Common#10331) -> PD2 `0x102fafd0` | `+0xa4a10`, `+0xb0720`, `+0xb11a0` | +0xa4a49, +0xb0729, +0xb11a9: D2Common #10331 refused in the PvP arenas (0x102fafd0) | not needed | cut on roadmap: PvP | - |
| 1: D2Game call `+0xdc17f` (to `0xdac10`) -> PD2 `0x10300970` | `+0xdc0b0` | the damage percent between two units (+0xdac10: 17 and 25 percent between players) replaced by PD2's (0x10300970) | not needed | cut on roadmap: PvP | - |
| 1: D2Game call `+0xe1072` (to `0xa66a` D2Common#11003) -> PD2 `0x102fcae0` | `+0xe0f40` | +0xe1072: a missile of class 0x62 or 0xc0 gets +0x44 = 2 in the PvP arenas (0x102fcae0) | not needed | cut on roadmap: PvP | - |
| 8: D2Game, 8 byte writes at `+0xfb5a4`, `+0xfb5ac`, `+0xfb5b4`, `+0xfb5bc`, `+0xfb5c4`, `+0xfb5cc`, `+0xfb5e4`, `+0xfb5ec` | - | arena.cpp's handler table (+0xfb5a4-+0xfb5ec): every handler gated by PD2's arena check | not needed | cut on roadmap: PvP | - |

### Controller

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 2: D2Client call `+0xc3d5f` (to `0x58fb0` showitems.cpp) -> PD2 `0x102ee090`<br>D2Client call `+0xc3daf` (to `0x52d90`) -> PD2 `0x102ee0b0` | `+0xc39e0` | Controller-mode overlays drawn after the item and UI passes (controller flag 0x104ac34e) | deviation | Controller: PD2's own screens and binds | - |

### Cast spam guard

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 1: D2Client call `+0x6264d` (to `0x61d00`) -> PD2 `0x102925a0` | `+0x62580` | Client stamps GetTickCount+250 at player data +0x16c and sends message 0x5b when a 250 ms window passes 200 casts | not needed | client-only anti-spam; see docs/research/skills/w34-gate-b.md | - |

### Weapon mastery read

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 5: D2Client call `+0x2c347`, `+0x2c56d`, `+0x30a0f`, `+0x3136e`, `+0x8ecb2` (to `0xc922` D2Common#10804) -> PD2 `0x103066c0` | `+0x2c2d0`, `+0x2c4f0`, `+0x308c0`, `+0x31220`, `+0x8ea20` | Client copy of the D2Common #10804 mastery read (crit and others) for the character screen | not needed | client display mirror of the server rule (IR3) | - |

### Client mirror of a server rule

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 4: D2Client call `+0x735a9`, `+0xa5a10(e9)`, `+0xa5a25(e9)`, `+0xaf6ba` (to `0xc502` D2Common#10704) -> PD2 `0x102fb2c0` | `+0x73560`, `+0xa59e0`, `+0xaf640` | Client copy of D2Common #10704 wrapper that caps a path field at 2 while state 96 is on | not needed | client mirror of the D2Game call (same PD2 function 0x102fb2b0) | - |
| 7: D2Client call `+0x47ddb`, `+0x8f391` (to `0xc99a` D2Common#10071) -> PD2 `0x102e85b0`<br>D2Client call `+0x55beb`, `+0x55c06`, `+0x55c5c` (to `0xcca0` D2Common#10308) -> PD2 `0x102fa8f0`<br>D2Client call `+0x9ad0f`, `+0x8e994` (to `0xc898` D2Common#10244) -> PD2 `0x1036e620` | `+0x47d60`, `+0x55b20`, `+0x8e8e0`, `+0x8f260`, `+0x9a920` | Client copies of the D2Common #10071 inventory walk, #10308 stat-list copy and #10244 item-use check (item ids 0x10f, 0x152, 0x167, 0x230/0x231) | not needed | client mirror of the matching D2Game calls | - |

### Missile pierce (client)

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 1: D2Client call `+0xb363c` (to `0xb0a60`) -> PD2 `0x103750d0` | `+0xb30f0` | Client missile hit test reads stats 156 and 166 against player targets | not needed | client prediction; the server decides the hit | - |

### Classic struct growth

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 9: D2Client `+0x24da`..`+0x24db`: `f41d` -> `0a2a`<br>D2Client call `+0x1930b` (to `0xcfca` D2Common#10356) -> PD2 `0x102fa440`<br>D2Client `+0x452e6`: `66` -> `e8`<br>D2Client call `+0x452e6(66)` (to `0x1efe176`) -> PD2 `0x102f3e30`<br>D2Client `+0x452eb`..`+0x45303`: `0000c1e00a050005000081e1..` -> `909090909090909090909090..`<br>D2Client call `+0x834c7`, `+0x83697`, `+0xa7489` (to `0xc61c` D2Common#10874) -> PD2 `0x102fa5c0` | `+0x191e0`, `+0x24b0`, `+0x833f0`, `+0x835c0`, `+0xa7400` | Client game, player-data and item-data allocations grow (0x1df4 -> 0x2a0a, 0x7d4, 0xa0) and a bit field at +0x1eb is rewritten for PD2's extra fields | not needed | 1.13c struct layout; D2R has its own structs | - |

### PD2 client packets

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 2: D2Client call `+0x144a1` (to `0xd252` D2Net#10024) -> PD2 `0x10321b30`<br>D2Client `+0x1aa4e`: `0c` -> `2e` | `+0x143e0`, `+0x1a060` | A D2Net send wrapper and a client command id (0x0c -> 0x2e) for PD2's own messages | not needed | PD2 client-server protocol; plugins use their own | - |

### Item inventory art

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 15: D2Client call `+0x169ec`, `+0x16b77`, `+0x290da`, `+0x9525f`, `+0x9533c`, `+0x954e3`, `+0x955b7`, `+0x95709`, `+0x95849`, `+0x95a54`, `+0x95b73`, `+0x9fc18` (to `0x6b5a0` Gfx.cpp) -> PD2 `0x102f1fc0`<br>D2Client call `+0x6b66d`, `+0x6cfbb`, `+0x6dfc1` (to `0xcc04` D2Common#10893) -> PD2 `0x102fb080` | `+0x16910`, `+0x16a90`, `+0x28dc0`, `+0x6b5a0`, `+0x6cc00`, `+0x6dee0`, `+0x95140`, `+0x952a0`, `+0x953b0`, `+0x958a0`, `+0x9fb00` | Client picks item graphics (gold pile size by amount, a per-item cel byte at +0x141) | not needed | client render; D2R draws its own HD item art | - |

### Client file loading

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 11: D2Client call `+0x4301e`, `+0x44ec7`, `+0x718d5(e9)`, `+0x71c22`, `+0x71d67`, `+0x71ea7`, `+0x89e33`, `+0xa7f7b`, `+0xc1523` (to `0xd39c` D2Win#10065) -> PD2 `0x102fb260`<br>D2Client call `+0xbf0e1` (to `0xc1f0` D2Common#10331) -> PD2 `0x102ec440`<br>D2Client call `+0xc3bba` (to `0x50e70`) -> PD2 `0x102ec450` | `+0x44b50`, `+0xa7e20`, `+0xbf0d0`, `+0xc14f0`, `+0xc39e0` | Wrappers around D2Win #10065 and the archive loader for PD2's files | not needed | client loader; D2R loads its own CASC data | - |

### Chat commands

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 1: D2Client call `+0x439a8` (to `0xace20`) -> PD2 `0x102f11a0` | `+0x43920` | A ? chat line opens PD2's help instead of being sent | not needed | cut on roadmap: chat stat commands | - |

### OS version spoof

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 3: D2Client `+0x44f6f`: `ff` -> `e8`<br>D2Client call `+0x44f6f(ff)` (to `0xb7f41389`) -> PD2 `0x1036d5e0`<br>D2Client `+0x44f74`: `6f` -> `90` | - | GetVersionExA reports Windows XP (5.1.2600) | not needed | 1.13c compatibility shim | - |

### Client UI text

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 8: D2Client call `+0x591e0`, `+0xc0eaa` (to `0xbfec0` ui.cpp) -> PD2 `0x102af9d0`<br>D2Client call `+0x70b75` (to `0x147a0`) -> PD2 `0x102f1210`<br>D2Client call `+0x842e5`, `+0x8490d`, `+0x8508f` (to `0x82660`) -> PD2 `0x10385060`<br>D2Client call `+0x9a6ce` (to `0x8cfb0`) -> PD2 `0x102ed8b0`<br>D2Client call `+0xc1085` (to `0xbe240`) -> PD2 `0x1036e020` | `+0x58fb0`, `+0x707f0`, `+0x84080`, `+0x84750`, `+0x84f00`, `+0x99440`, `+0xc0d30` | Item name, hover and inventory panel text replaced by PD2's own builders (quality colours, cursor item panel, message buffers) | not needed | client render; D2R draws its own HD UI | - |
| 4: D2Client call `+0x49adc`, `+0x49b51` (to `0xce26` D2Common#10091) -> PD2 `0x102fbeb0`<br>D2Client call `+0x54b3a`, `+0x54e64` (to `0x546e0`) -> PD2 `0x102ee8d0` | `+0x49440`, `+0x54ac0`, `+0x54e10` | Client string and stat value helpers (D2Common #10091 switch, stats 488 and 489 shown as 100 minus value or scaled by stat 184) | not needed | client render; D2R prints stats from its own descfuncs | - |

### Client internals (not decoded)

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 30: D2Client `+0x14d6e`: `fa` -> `19`<br>D2Client `+0x18d51`..`+0x18d52`: `7e27` -> `9090`<br>D2Client call `+0x18d6e` (to `0xcfe2` D2Common#10029) -> PD2 `0x1036f940`<br>D2Client `+0x30393`: `8b` -> `e8`<br>D2Client call `+0x30393(8b)` (to `0x8b2f27ec`) -> PD2 `0x103a1d20`<br>D2Client `+0x30398`..`+0x303a1`: `f08bc5e83029fdff03f0` -> `90909090909090909090`<br>D2Client `+0x325af`: `01` -> `00`<br>D2Client `+0x3313b`: `8b` -> `e8`<br>D2Client call `+0x3313b(8b)` (to `0x448e319d`) -> PD2 `0x103a1c80`<br>D2Client `+0x33140`..`+0x331e0`: `24108d0c380fafcbb81f85eb..` -> `909090909090909090909090..`<br>D2Client `+0x517ef`: `0a` -> `07`<br>D2Client `+0x5bebd`..`+0x5bebe`: `85ed` -> `4590`<br>D2Client `+0x6175d`..`+0x61762`: `8a0d11c2bc6f` -> `b16290909090`<br>D2Client `+0x74444`..`+0x74449`: `57e8a6bd0000` -> `909090909090`<br>D2Client `+0x776ce`..`+0x776d3`: `0f84a6010000` -> `909090909090`<br>D2Client `+0x7b471`: `85` -> `33`<br>D2Client `+0x8c7c9`..`+0x8c7ca`: `7407` -> `9090`<br>D2Client `+0xa2aa5`: `85` -> `e8`<br>D2Client call `+0xa2aa5(85)` (to `0x8d55a96a`) -> PD2 `0x103a1ce0`<br>D2Client `+0xa2aaa`..`+0xa2af3`: `9df6000000900fbf138b0c95..` -> `909090909090909090909090..`<br>D2Client `+0xbdef5`..`+0xbdef6`: `ff15` -> `90e8`<br>D2Client call `+0xbdef6(15)` (to `0x6fc3cfff`) -> PD2 `0x102cc6a0`<br>D2Client `+0xc6c91`: `74` -> `eb`<br>D2Client `+0xe25f8`: `6b` -> `24`<br>D2Client `+0xe2640`: `24` -> `6b` | `+0x30340`, `+0x316c0`, `+0x32f30`, `+0x51650`, `+0x61490`, `+0x74350`, `+0x7b470`, `+0x8c760`, `+0xa2950`, `+0xbdca0`, `+0xc6aa0` | Small client code and data patches: a 250 -> 25 delay, select radius 10 -> 7, an automap colour fixed to 0x62, NOPed draw and inventory checks, replaced UI maths, a file-name byte pair | not needed | client render and input; D2R's client replaces it (not decoded further) | - |

### Replenish charges

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 7: D2Game call `+0xeb80`, `+0x108a6`, `+0x11859`, `+0x12f87`, `+0x13136`, `+0xd4c8b`, `+0xd684b` (to `0xea70`) -> PD2 `0x10345140` | `+0x107e0`, `+0x11490`, `+0x12ee0`, `+0xd4a70`, `+0xd6760`, `+0xeae0` | PD2's replenish rule (10345140) replaces vanilla's item charge replenish | ported | plugin/core-rules/item_rules.cpp (replenish) | - |

### Unit free guard

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 65: D2Game call `+0xeb96`, `+0xee85(e9)`, `+0xf24b(e9)`, `+0xfc0e`, `+0x115ed`, `+0x1319e`, `+0x20492`, `+0x204a1`, `+0x20995`, `+0x20a8c`, `+0x2cd26(e9)`, `+0x332b4`, `+0x35b4d(e9)`, `+0x36598`, `+0x37a3c`, `+0x3878d`, `+0x387c2`, `+0x38884`, `+0x3e4fe`, `+0x3e56d(e9)`, `+0x440c1`, `+0x490c4`, `+0x4bae5`, `+0x4c009`, `+0x4c110`, `+0x5199b`, `+0x548f6`, `+0x5496a`, `+0x6ccd6`, `+0x6d530`, `+0x6d55f`, `+0x6fee4`, `+0x71c92`, `+0x72b8a`, `+0x72dd7`, `+0x799de`, `+0x8f4a6`, `+0x90212`, `+0x902d1`, `+0x974d8`, `+0x9b50f`, `+0xa242a`, `+0xa2f51`, `+0xa74c6`, `+0xa7abf`, `+0xafd8d`, `+0xb0664`, `+0xb5dd4`, `+0xbd9a0`, `+0xbf016`, `+0xbf050`, `+0xc0d4c`, `+0xc9070`, `+0xd2cd5`, `+0xd2d7b`, `+0xd2e8b`, `+0xd40aa`, `+0xd6dcd`, `+0xe1271`, `+0xe1297`, `+0xe1370`, `+0xe222a`, `+0xed86f`, `+0xedad5`, `+0xedd46` (to `0xe20a0` SUnit.cpp) -> PD2 `0x10375390` | `+0x11490`, `+0x13180`, `+0x203d0`, `+0x20760`, `+0x209b0`, `+0x2cd10`, `+0x33290`, `+0x36470`, `+0x379e0`, `+0x386f0`, `+0x38860`, `+0x3e3f0`, `+0x43fa0`, `+0x48fc0`, `+0x4b7f0`, `+0x518b0`, `+0x54810`, `+0x6cc00`, `+0x6d0f0`, `+0x6fec0`, `+0x71b00`, `+0x72220`, `+0x79910`, `+0x8f470`, `+0x90170`, `+0x90260`, `+0x97420`, `+0x9b3b0`, `+0xa22e0`, `+0xa2ee0`, `+0xa7410`, `+0xa7a10`, `+0xafce0`, `+0xb0640`, `+0xbd920`, `+0xbee30`, `+0xc0b90`, `+0xc8fd0`, `+0xd2c70`, `+0xd2cf0`, `+0xd2d90`, `+0xd3cb0`, `+0xd6c80`, `+0xe1110`, `+0xe12d0`, `+0xe21f0`, `+0xeae0`, `+0xed710`, `+0xed960`, `+0xedb50`, `+0xee70`, `+0xfb70` | null and unit-type checks, then vanilla's unit free (10375390) | not needed | crash guard; D2R frees its own units | - |

### Random sockets on drop

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 4: D2Game `+0xebf7`..`+0xebfc`: `0f8485000000` -> `909090909090`<br>D2Game `+0xecb5`..`+0xecba`: `0f85fd000000` -> `909090909090`<br>D2Game call `+0x106af`, `+0x10ca8` (to `0xec90`) -> PD2 `0x10374c50` | `+0x10600`, `+0x10a20`, `+0xebb0`, `+0xec90` | the socket roll at item creation (+0xec90, caller +0xebb0) also runs for set items and runs only for non-stackable items or throwing weapons and quivers (types 42 43 44 87 5 6) (10374c50) | missing | [#1828](https://github.com/synpoox/pd2-resurrected/issues/1828); no port found; low confidence on the exact rule | D2R's item-creation socket roll |

### Rarity-0 uniques

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 1: D2Game `+0xf5ce`: `7d` -> `eb` | `+0xf370` | +0xf5ce jge to jmp: a unique row with blank or 0 rarity weighs 0 (vanilla and D2R weigh it 1) | missing | [#1852](https://github.com/synpoox/pd2-resurrected/issues/1852); Embersworn (lvl 74, blank rarity) can drop for us; data fix: Embersworn spawnable 1 to 0 | D2R 0x443910 sets weight 1 for rarity < 1 |

### Item and save serialisation buffers

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 94: D2Game, 94 byte writes at `+0xf781`, `+0xf7f5`, `+0xf80a`, `+0xf811`, `+0xf818`, `+0xf820`, `+0xf831`, `+0xf88d`, `+0xf8da`, `+0xf8e9`, `+0xf8f1`, `+0xf953`, `+0xf95a`, `+0xf962`, `+0xf973`, `+0xf9cc`, `+0xfa1b`, `+0xfa29`, `+0xfa31`, `+0xfa93`, `+0xfa9a`, `+0xfaa2`, `+0xfab3`, `+0xfb0c`, `+0xfb5b`, `+0xfb69`, `+0x537d1`, `+0x537de`, `+0x537fc`, `+0x5383f`, `+0x53887`, `+0x538ab`, `+0x538d1`, `+0x538ed`, `+0x538ff`, `+0x5391d`, `+0x5396c`, `+0x539c1`, `+0x53a34`, `+0x53a7d`, `+0x53ab9`, `+0x53afc`, `+0x53b38`, `+0x53e51`, `+0x53e7a`, `+0x53e82`, `+0x53e8e`, `+0x53e9a`, `+0x53f00`, `+0x53f11`, `+0x53f3c`, `+0x53f5b`, `+0x53f62`, `+0x53f6e`, `+0x53f73`, `+0x53f7f`, `+0x53fd7`, `+0x53ff4`, `+0x54001`, `+0x5401c`, `+0x54023`, `+0x5402f`, `+0x54051`, `+0x54245`, `+0x5424c`, `+0x54254`, `+0x5425d`, `+0x5426c`, `+0x54276`, `+0x542c4`, `+0x542cf`, `+0x542d7`, `+0x542ea`, `+0x5430b`, `+0x56251`, `+0x5625d`, `+0x56283`, `+0x562bd`, `+0x5630a`, `+0x56319`, `+0x56320`, `+0x56327`, `+0x5632f`, `+0x56337`, `+0x56354`, `+0x5635b`, `+0x56362`, `+0x5636c`, `+0x56378`, `+0x5639c`, `+0x563b1`, `+0x70fee`, `+0x71000`, `+0x7167e` | `+0x537d0`, `+0x53e50`, `+0x53f10`, `+0x54000`, `+0x56250`, `+0x70fb0`, `+0x715e0`, `+0xf780`, `+0xf8f0`, `+0xfa30` | buffer sizes 0x2000 to 0x20000 in the item and save writers | not needed | D2R serialises with its own buffers | - |

### Affix picker

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 12: D2Game call `+0x1026e`, `+0x102ba`, `+0x1030b`, `+0x1034f`, `+0x14b74`, `+0x14bc1`, `+0x14d34`, `+0x14df7`, `+0x15049`, `+0x15109`, `+0x1535f`, `+0x153de` (to `0x144d0`) -> PD2 `0x103890f0` | `+0x10240`, `+0x14b50`, `+0x14bf0`, `+0x14f20`, `+0x15240` | PD2's affix picker (103890f0) honours the affix rare column | ported | plugin/core-rules/loot_rules.cpp (affix rare) | - |

### Magic items force both affixes at high ilvl

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 2: D2Game call `+0x1040c`, `+0x1047c` (to `0x144d0`) -> PD2 `0x103855d0` | `+0x103c0` | the magic affix roll (+0x103c0, calls +0x1040b and +0x1047b to 103855d0) forces a prefix and a suffix at ilvl 65+ (85+ rings amulets jewels, 90+ charms, always type 105) | missing | [#1829](https://github.com/synpoox/pd2-resurrected/issues/1829); companions/config/ruffneckk-progressive-affixes.toml [magic] is all 255 and its header says no magic item is forced; likely a config fix; data fix: Progressive Affixes companion [magic] 255 to 65/85/90 | D2R's magic affix roll |

### Forced unique and set id

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 2: D2Game call `+0x10d57` (to `0xf370`) -> PD2 `0x103856d0`<br>D2Game call `+0x141f5` (to `0x13c20`) -> PD2 `0x10385670` | `+0x10a20`, `+0x141e0` | a forced unique or set id (103856d0, 10385670) skips vanilla's row filters | not needed | D2R's forced-unique path (cube outputs) already sets the row; moderate confidence | - |

### Weapon durability

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 2: D2Game `+0x12f16`: `04` -> `02`<br>D2Game `+0x12f36`: `0a` -> `05` | `+0x12ee0` | PD2's weapon durability rule | ported | plugin/core-rules/loot_rules.cpp (weapon durability) | - |

### Staff mods

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 3: D2Game call `+0x13e20` (to `0x135e0`) -> PD2 `0x1036f470`<br>D2Game call `+0x13e3b`, `+0x13e4c` (to `0x13320`) -> PD2 `0x1036f470` | `+0x13de0` | PD2's staffmods rule | ported | plugin/core-rules/loot_rules.cpp (staffmods, [#663](https://github.com/synpoox/pd2-resurrected/issues/663)) | - |

### Rare affix group check

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 5: D2Game `+0x14e36`: `66` -> `e8`<br>D2Game call `+0x14e36(66)` (to `0x1425ca74`) -> PD2 `0x1036f250`<br>D2Game `+0x14e3b`..`+0x14e46`: `0f84a50000008b405c3b435c` -> `9090909090909090909085c0` | `+0x14bf0` | same duplicate and group check plus a null guard (1036f250) | not needed | recompile of vanilla's rule | - |

### Rare affix count

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 5: D2Game `+0x152bf`..`+0x152c0`: `83f8` -> `56e8`<br>D2Game call `+0x152c0(f8)` (to `0x7508ddff`) -> PD2 `0x10384080`<br>D2Game `+0x152c5`..`+0x15301`: `1fb9c590c66af7e18b4f0403..` -> `909090909090909090909090..`<br>D2Game `+0x15303`: `54` -> `44` | `+0x15240` | PD2's rare affix count by item level (10384080) | ported | progressive affixes companion config; deviations row rare affix count at item level 45-64 | - |

### Room spawn roll

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 2: D2Game call `+0x1bc85`, `+0x1dd23` (to `0x1bbc0`) -> PD2 `0x10361120` | `+0x1db40` | same 66% room spawn roll with rand() and guards (10361120) | not needed | recompile of vanilla's rule | - |

### Mercenary think

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 8: D2Game `+0x1deb3`: `76` -> `eb`<br>D2Game `+0x1ded6`: `76` -> `eb`<br>D2Game `+0x1defb`: `76` -> `eb`<br>D2Game `+0x1e000`: `7d` -> `eb`<br>D2Game `+0x1e047`: `19` -> `19`<br>D2Game call `+0x1e05d` (to `0x1c8a0`) -> PD2 `0x10360fd0`<br>D2Game `+0x1e0eb`: `77` -> `eb`<br>D2Game `+0x1e11c`: `05` -> `00` | - | PD2's hireable think (+0x1dd40) rules | missing | [#1803](https://github.com/synpoox/pd2-resurrected/issues/1803) | D2R's hireable AI |

### Outer void level rules

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 3: D2Game call `+0x1f77f` (to `0x1ef50`) -> PD2 `0x10371590`<br>D2Game call `+0x1ff58` (to `0x1f2d0`) -> PD2 `0x10371600`<br>D2Game call `+0x204f2` (to `0x1f600` SUnitInactive.cpp) -> PD2 `0x10371530` | `+0x1f600`, `+0x1fb90`, `+0x204e0` | in-level portals, step drops and the bosses' list on room release (10371590, 10371600, 10371530) | deviation | deviations row the outer void: in-level portals, step drops, the bosses' list on room release ([#532](https://github.com/synpoox/pd2-resurrected/issues/532)) | - |

### Object spawn class limit

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 2: D2Game call `+0x201d2`, `+0xe13da` (to `0xe12d0`) -> PD2 `0x10371b30` | `+0x200e0`, `+0xe13b0` | spawn unit e12d0 accepts PD2's object rows past the class limit (10371b30) | deviation | PD2's blank <reserved> objects rows 573-583: D2R's rows stand there | - |

### Aura-enchanted bosses

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 3: D2Game `+0x242eb`..`+0x242ee`: ptr PD2 `0x1036e8f0`<br>D2Game call `+0x24e4d` (to `0x9f9a0` Skills.cpp) -> PD2 `0x1036f950`<br>D2Game `+0x24e52`..`+0x24e63`: `8b87b0e4d26f6aff508bce33..` -> `909090909090909090909090..` | `+0x24cd0` | bosses with umod aura pick an aura from PD2's list at level mlvl/7 clamped 1-13 (1036e8f0, 1036f950, NOP +0x24e52) | missing | [#1830](https://github.com/synpoox/pd2-resurrected/issues/1830); no port found | D2R 0x495cd0 (umod 30 of 0x494a60) |

### Frenzy stacks

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 2: D2Game call `+0x2966e`, `+0x296af` (to `0x28e70`) -> PD2 `0x10306790` | `+0x29600` | srvdo 9 helper (+0x28e70 to 10306790) stacks skill_frenzy (169) capped by calc4, sets stat 351 and fills aurastats per stack | missing | [#1831](https://github.com/synpoox/pd2-resurrected/issues/1831); low confidence; check D2R's srvdo 9 first | D2R's srvdo 9 |

### Timed state helper

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 2: D2Game call `+0x29799`, `+0x3eac7` (to `0x29240`) -> PD2 `0x10373680` | - | recompile of the timed-state helper (10373680) | not needed | recompile of vanilla's rule | - |

### Client connection records

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 4: D2Game call `+0x2bb8e`, `+0x2c46b` (to `0x4a9b0` Clients.cpp) -> PD2 `0x1036c480`<br>D2Game call `+0x4b830` (to `0x4ae90`) -> PD2 `0x103a0810`<br>D2Game call `+0x4bc16` (to `0xa2ec` Fog#10046) -> PD2 `0x1039ee80` | `+0x2bb10`, `+0x2c2d0`, `+0x4b7f0` | client and player connection handling (1036c480, 103a0810, 1039ee80) | not needed | multiplayer server; D2R runs its own | - |

### Character save format

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 14: D2Game call `+0x2c8aa`, `+0x2c93a`, `+0x2c9b6`, `+0x2d74c`, `+0x2d8d9`, `+0x2de9e`, `+0x54411`, `+0x7980f` (to `0x54320`) -> PD2 `0x1036e9c0`<br>D2Game call `+0x53f93`, `+0x53ea6`, `+0x5403e` (to `0xec7f0`) -> PD2 `0x1039c900`<br>D2Game call `+0x56388`, `+0x5618b` (to `0xedb50`) -> PD2 `0x1039d130`<br>D2Game call `+0x5622d` (to `0x49630` Clients.cpp) -> PD2 `0x1039f510` | `+0x2c830`, `+0x2c910`, `+0x2d670`, `+0x2d820`, `+0x2de30`, `+0x53e50`, `+0x53f10`, `+0x54000`, `+0x54400`, `+0x56140`, `+0x561a0`, `+0x56250`, `+0x79730` | save wrappers for PD2's stash pages and save sections (1036e9c0, 1039c900, 1039d130, 1039f510) | not needed | D2R saves its own stash; the set-home section belongs to [#1144](https://github.com/synpoox/pd2-resurrected/issues/1144) | - |

### Room activation

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 1: D2Game call `+0x2ccb8` (to `0x47a20`) -> PD2 `0x1036ff60` | `+0x2cbe0` | PD2's map events on room activation (1036ff60) | ported | plugin/core-events/map_events.cpp and rathma.cpp, with deviation rows | - |

### Game and task lifecycle

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 20: D2Game call `+0x2d07b`, `+0x2d117`, `+0x2d2d6`, `+0x2d35d`, `+0x2d902`, `+0x2ddad`, `+0x2e3dd`, `+0x52796` (to `0x4b7f0` Clients.cpp) -> PD2 `0x103a21b0`<br>D2Game call `+0x2d28a`, `+0x2d656`, `+0x2d760`, `+0x2d802`, `+0x2def1`, `+0xd14fe` (to `0x2d090`) -> PD2 `0x103a2170`<br>D2Game call `+0x2d92b`, `+0x2dfda`, `+0x52815` (to `0x2ce20`) -> PD2 `0x103a21f0`<br>D2Game call `+0x2db2d` (to `0x2c120`) -> PD2 `0x102ffa70`<br>D2Game call `+0x2e2db`, `+0x52712` (to `0x2e050` Game.cpp) -> PD2 `0x103003c0` | `+0x2cfd0`, `+0x2d090`, `+0x2d140`, `+0x2d2f0`, `+0x2d500`, `+0x2d670`, `+0x2d7e0`, `+0x2d820`, `+0x2d9b0`, `+0x2dc70`, `+0x2dec0`, `+0x2df10`, `+0x2e1e0`, `+0x2e360`, `+0x526f0` | game create, close and task records | not needed | multiplayer server; D2R runs its own | - |

### Set-home start level

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 1: D2Game call `+0x2e43e` (to `0x4b500`) -> PD2 `0x10371da0` | `+0x2e360` | start level (+0x2e43e to 10371da0) uses the player's set home | missing | [#1144](https://github.com/synpoox/pd2-resurrected/issues/1144) (W38b .set-home) | - |

### New character starting items

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 2: D2Game `+0x380b4`: `02` -> `01`<br>D2Game `+0x380d4`: `85` -> `33` | `+0x38090` | the starting item creator (+0x38090) passes 1 for 2 and always adds the item skill stat 107 | missing | [#1832](https://github.com/synpoox/pd2-resurrected/issues/1832); low confidence; effect needs decoding | D2R's starting items |

### Gold loss on death in PvP

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 1: D2Game call `+0x383a8` (to `0x38180`) -> PD2 `0x103732e0` | `+0x383a0` | gold loss on death skipped in PvP (10373270) | not needed | PvP cut on the roadmap | - |

### Skill on equip

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 1: D2Game `+0x38660`..`+0x38663`: ptr PD2 `0x1036f6b0` | `+0x38620` | item_skillonequip | ported | plugin/core-rules/item_rules.cpp (item_skillonequip) | - |

### Player data size

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 1: D2Game call `+0x3866d` (to `0xb29a` D2Common#10356) -> PD2 `0x102fa440` | `+0x38620` | PD2's bigger player data record (0x7d4) | not needed | D2R has its own player data | - |

### Crit rework

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 2: D2Game call `+0x3a72f` (to `0xa42a` D2Common#10973) -> PD2 `0x103045b0`<br>D2Game `+0x3a734`..`+0x3a743`: `85c0740c8b5508d1e2804d05..` -> `909090909090909090909090..` | `+0x3a4e0` | crit rule (103045b0) and NOP +0x3a734 | ported | plugin/core-rules/item_rules.cpp (crit rework) | - |

### Curses

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 7: D2Game call `+0x5034c`, `+0x50c47`, `+0x51e44`, `+0x42819`, `+0x44166`, `+0x3acf8`, `+0x9a22c` (to `0xa00b0`) -> PD2 `0x1036d840` | `+0x3abd0`, `+0x42730`, `+0x43fa0`, `+0x50250`, `+0x50b70`, `+0x9a1a0` | PD2's curse rules (1036d840) | ported | plugin/core-rules/item_rules.cpp (curses), with deviation rows | - |

### Parry and block roll

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 4: D2Game call `+0x3af04`, `+0xde660` (to `0xdb790`) -> PD2 `0x10303470`<br>D2Game call `+0x3aff6`, `+0xde558` (to `0x3ae10`) -> PD2 `0x10301ff0` | `+0x3ae10`, `+0x3aff0`, `+0xde380`, `+0xde5a0` | Holy Sword parry in the block roll (10303470, 10301ff0) | ported | plugin/core-rules/skill_functions.cpp (Holy Sword parry, deviation row) | - |

### Telekinesis item types

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 2: D2Game call `+0x43386` (to `0xa430` D2Common#10744) -> PD2 `0x102fb350`<br>D2Game `+0x4338f`..`+0x433ca`: `6a0455e89970fcff85c07547..` -> `909090909090909090909090..` | `+0x431c0` | Telekinesis (+0x431c0, 102fb350) no longer picks up arrows and bolts (type 56 removed) | missing | [#1833](https://github.com/synpoox/pd2-resurrected/issues/1833); no port found; minor | D2R's Telekinesis item check |

### Skill start rules

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 10: D2Game call `+0x4b7cb`, `+0xa1db9`, `+0xa1f0a`, `+0xa2012`, `+0xa209b`, `+0xa3840`, `+0xadbe4`, `+0xf16b4` (to `0xa19a0` Skills.cpp) -> PD2 `0x1036fa60`<br>D2Game call `+0x4fa53`, `+0xd94f8` (to `0x370e0`) -> PD2 `0x1036eb60` | `+0x4b500`, `+0x4f970`, `+0xa1d10`, `+0xa2060`, `+0xa37d0`, `+0xadb30`, `+0xd9470`, `+0xf14f0` | skill start (+0xa19a0 to 1036fa60, 8 sites) gates per player, schedules event 0xe at frame+5 for listed skills, sends packet 0x83 for skills 249 250 593; event lookup (1036eb60) re-registers skill_cooldown events | missing | [#1834](https://github.com/synpoox/pd2-resurrected/issues/1834); low confidence; unclear (triage): the cooldown gate's class list and scope | D2R's skill start |

### Summon first think delay and revive path

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 3: D2Game `+0x4d879`: `14` -> `08`<br>D2Game `+0x50ad1`: `0f` -> `08`<br>D2Game call `+0x51529` (to `0xa5da` D2Common#10704) -> PD2 `0x102fb2c0` | `+0x4d710`, `+0x509e0`, `+0x51340` | Valkyrie first think 20 to 8 frames (+0x4d879), Revive pet 15 to 8 (+0x50ad1), revive state 96 and path +0x44 capped at 2 (102fb2c0) | missing | [#1835](https://github.com/synpoox/pd2-resurrected/issues/1835); no port found | D2R's srvdo 16 and 58 |

### Talic's Fire Pierce stacks

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 1: D2Game call `+0x4dc46` (to `0xded30`) -> PD2 `0x10303a60` | - | Fend, Zeal and Fury (srvdo 13) hits add 20 passive_phys_pierce (478) to the attacker's state 224 (talicfirepierce) stat list before the melee hit (+0xded30 at +0x4dc46 to 10303a60) | not needed | close enough ([#1836](https://github.com/synpoox/pd2-resurrected/issues/1836) triage): state 224 comes only from the aura on colossal1, the cut Uber Ancients | D2R srvdo 13 0x5568e0 |

### Life drain cap

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 2: D2Game call `+0xdb5b7`, `+0x4e8ac` (to `0xa472` D2Common#10887) -> PD2 `0x102fbc60` | `+0xdb570` | PD2's life-drain cap | ported | plugin/core-rules/item_rules.cpp (life-drain cap) | - |

### Resist reduction

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 3: D2Game call `+0x502bf`, `+0x503a0`, `+0x9a2a0` (to `0x4e230`) -> PD2 `0x1036df80` | `+0x50250`, `+0x9a1a0` | PD2's resist reduction | ported | plugin/core-rules/skill_functions.cpp (resist reduction) | - |

### Curse effectiveness

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 1: D2Game call `+0x503b8` (to `0xa6e2` D2Common#10188) -> PD2 `0x102fad60` | `+0x50250` | PD2's curse effectiveness | ported | plugin/core-rules/item_rules.cpp (curse effectiveness) | - |

### Curse duration on monsters and Hit-Blinds state

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 5: D2Game `+0x505a0`..`+0x505a6`: `0fbf9182000000` -> `bad80000009090`<br>D2Game `+0x506a8`..`+0x506ae`: `e8b3d8ffff8bf0` -> `90909090909090` | `+0x50470`, `+0x505d0` | srvdo 30 skips the AiCurseDivisor divide (NOP +0x506a8), so curses keep full duration in NM and Hell; Dim Vision by hit uses state 216 (+0x505a0) | missing | [#1837](https://github.com/synpoox/pd2-resurrected/issues/1837); our difficultylevels AiCurseDivisor is 1/2/4; a data fix may do, check D2R's other uses first | D2R's srvdo 30 |

### Max level (99) aura

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 2: D2Game call `+0xde735`, `+0x5c03d` (to `0xeb090`) -> PD2 `0x1036f8c0` | `+0x5bf70`, `+0xde6d0` | level-up (+0x5c03c, +0xde734 to 1036f8c0) sets player data flags and marker states 0xd1-0xed at level 99 | missing | [#1838](https://github.com/synpoox/pd2-resurrected/issues/1838); no port found | D2R's level-up |

### Anya maps

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 2: D2Game call `+0x5fa18`, `+0x5cb64` (to `0x36470` QUESTS.CPP) -> PD2 `0x10372050` | - | PD2's Anya map rule (10372050) | ported | plugin/core-events/lane-c/anya_maps.cpp | - |

### Crit mastery

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 2: D2Game call `+0x69b64`, `+0xdc6a0` (to `0xa80e` D2Common#10804) -> PD2 `0x103065e0` | `+0x69b00`, `+0xdc530` | PD2's crit mastery | ported | plugin/core-rules/item_rules.cpp (crit mastery) | - |

### Stamina shrine velocity

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 1: D2Game call `+0x6acd4` (to `0xa76c` D2Common#10261) -> PD2 `0x10371250` | - | stamina shrine +35 velocity (10371250) | ported | plugin/core-rules/shrine_rules.cpp ([#1572](https://github.com/synpoox/pd2-resurrected/issues/1572)) | - |

### Shrine potion codes

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 8: D2Game `+0x6c281`..`+0x6c284`: `67706d20` -> `74706773`<br>D2Game `+0x6c601`..`+0x6c604`: `6f706d20` -> `74706673` | `+0x6c200`, `+0x6c580` | shrines 21 and 22 (Exploding, Poison) drop 'tpgs' and 'tpfs' (PD2's small gas and fire throwing potions, weapons.txt) instead of 'gpm ' and 'opm ' | missing | [#1851](https://github.com/synpoox/pd2-resurrected/issues/1851) | D2R's shrine drops 'opm'/'gpm' |

### PvP quest entry

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 11: D2Game `+0x6d13e`..`+0x6d168`: `8b470885c0750583ceffeb03..` -> `909090909090909090909090..`<br>D2Game `+0x6d13e`: `8b` -> `e8`<br>D2Game call `+0x6d13e(8b)` (to `0xc08bd98a`) -> PD2 `0x1036f1e0`<br>D2Game `+0x6d143`..`+0x6d14a`: `750583ceffeb038b` -> `85c00f85cd040000` | `+0x6d0f0` | quest-table entry +0x6d0f0 (1036f1e0) | not needed | PvP cut on the roadmap | - |

### Item rule

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 1: D2Game call `+0x6f63d` (to `0xb132` D2Common#10413) -> PD2 `0x102fe740` | `+0x6f620` | PD2's item rule | ported | plugin/core-rules/item_rules.cpp | - |

### Missile pierce cap

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 1: D2Game call `+0x6fe59` (to `0x6f660`) -> PD2 `0x10374f90` | `+0x6f930` | at missile creation (+0x6f660 in +0x6f930 to 10374f90) a piercing missile rolls item_pierce + skill_pierce + mastery chance up to 10 times (6 for missile 35) into pierce_idx (328), capping its pierces | missing | [#1839](https://github.com/synpoox/pd2-resurrected/issues/1839); no port found; vanilla rolls pierce per hit with no cap | D2R's missile creation |

### Trade stash pages

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 5: D2Game call `+0x717a6` (to `0xd5590`) -> PD2 `0x10382e10`<br>D2Game call `+0x71a00`, `+0x71a07`, `+0x73b38`, `+0x73b3f` (to `0x700c0` PlrTrade.cpp) -> PD2 `0x10382d90` | `+0x716d0`, `+0x719a0`, `+0x73aa0` | stash page handling in trade (10382e10, 10382d90) | not needed | PD2's stash pages; D2R has its own stash | - |

### Cube stack matching

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 1: D2Game call `+0x73462` (to `0x70cf0`) -> PD2 `0x10364ea0` | `+0x72fc0` | stack-aware cube matching: the call goes to the thunk 0x103a17e0 (PD2.gpr 0x103a1770), which calls the function 0x10364ea0 (PD2.gpr 0x10364e30) | ported | plugin/core-rules/cube_stacks.cpp | - |

### Cube stack executor

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 1: D2Game call `+0x73492` (to `0x72220` ItemMode.cpp,PlrTrade.cpp) -> PD2 `0x10365440` | `+0x72fc0` | the cube executor (10365440) takes what the row needs off stacks | ported | plugin/core-rules/cube_stacks.cpp (executor 0x3653d0) | - |

### Cube recipes

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 1: D2Game call `+0x734f1` (to `0x72fc0`) -> PD2 `0x10366fb0` | `+0x734e0` | PD2's cube recipe rules (10366fb0) | ported | plugin/core-rules/cube_rules.cpp, with deviation the pandemonium keys' recipe | - |

### Mode end event

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 2: D2Game call `+0x7514d`, `+0x75f96` (to `0x74e60`) -> PD2 `0x1037bac0` | `+0x750f0`, `+0x75e10` | the mode end-event (+0x74e60 to 1037bac0) skips scheduling while an unflagged event 2 is pending | not needed | recompile of vanilla's rule | - |

### Open wounds

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 1: D2Game call `+0x751de` (to `0xad24` D2Common#10527) -> PD2 `0x102fe6b0` | - | open wounds rework (102fe6b0) | ported | plugin/core-rules/item_rules.cpp (open wounds, ir3) | - |

### Animation frame events

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 12: D2Game call `+0x759be`, `+0x75a0a`, `+0x75d46`, `+0x75d5c`, `+0x75d8b`, `+0x76a1e` (to `0xb08a` D2Common#10853) -> PD2 `0x1037aaf0`<br>D2Game call `+0x76016`, `+0x781a5`, `+0x78215`, `+0x78818`, `+0x78885`, `+0x79f20` (to `0xdf7b0`) -> PD2 `0x1037a540` | `+0x75ce0`, `+0x75e10`, `+0x79e40` | PD2's frame advance (D2Common #10853 to 1037aaf0, 6 sites) and frame event scheduler (+0xdf7b0 to 1037a540) read the anim rate unsigned, cap frames at 0x90 and count event 4 | not needed | close enough ([#1840](https://github.com/synpoox/pd2-resurrected/issues/1840) triage): the 0x90 cap and event 4 are vanilla's | D2R 0x491240 runs the same loop |

### Monster mode assert

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 1: D2Game `+0x75e45`: `74` -> `eb` | `+0x75e10` | +0x75e45 je to jmp skips the fatal error when a monster in state 54 sets a mode | not needed | crash guard | - |

### Monster 991 leap hold

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 2: D2Game call `+0x76f28` (to `0xb084` D2Common#10426) -> PD2 `0x102fbd50`<br>D2Game call `+0x76fc4` (to `0xb08a` D2Common#10853) -> PD2 `0x102fbdb0` | `+0x76f43` | for monster 991 using skill 143 with flag 0x100, the anim never ends (#10426 to 102fbd50) and freezes at frame 8 (#10853 to 102fbdb0) | not needed | close enough ([#1841](https://github.com/synpoox/pd2-resurrected/issues/1841) triage): monster 991 is uberancientbarb3, cut content | D2R's frame advance |

### Crash guard

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 13: D2Game call `+0x77ef7`, `+0x7ad17`, `+0x7ad2a`, `+0xccabf`, `+0xccae2`, `+0xcd5dc`, `+0xcd5eb`, `+0xe0b18`, `+0xf0c80`, `+0xf0ea1`, `+0xf0f01`, `+0xf1196`, `+0xf11a3` (to `0xf02a0`) -> PD2 `0x10374090` | `+0x77e70`, `+0x7acb0`, `+0xcd530`, `+0xe0a60`, `+0xf0c60` | null guard (10374020) | not needed | crash guard | - |

### Gate A and B

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 4: D2Game call `+0x7849b`, `+0x78583` (to `0x78340`) -> PD2 `0x1037bb90`<br>D2Game call `+0x784ba`, `+0x7859e` (to `0x77fa0` PlrModes.cpp) -> PD2 `0x10373300` | `+0x78430`, `+0x78550` | PD2's skill gates | ported | plugin/core-rules/skill_functions.cpp (gate A/B) | - |

### Death handler

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 1: D2Game call `+0x798fd` (to `0x79730`) -> PD2 `0x1036fd40` | `+0x79850` | death handler with no rule outside PvP | not needed | PvP cut on the roadmap | - |

### State info to the client

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 1: D2Game call `+0x7a6e7` (to `0xa5d4` D2Common#10871) -> PD2 `0x102fb8d0` | `+0x7a5e0` | state stat list lookup (#10871 to 102fb8d0) sends aura and buff state levels and durations to the client | deviation | deviations row PD2's buff timers, mini skill icons and skill bar (Buff HUD companion) | - |

### Inventory guard

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 15: D2Game call `+0x7b1b4`, `+0x8ddcf`, `+0x8de10`, `+0x8de98`, `+0x8def8`, `+0x8df58`, `+0x8dfb8`, `+0x8e018`, `+0x8e078`, `+0x8e0eb`, `+0x9b90b`, `+0x9b938`, `+0x9b965`, `+0xd398d`, `+0xd7ef4` (to `0x8d3d0`) -> PD2 `0x10385380` | `+0x7b0a0`, `+0x8dda0`, `+0x8de50`, `+0x8deb0`, `+0x8df10`, `+0x8df70`, `+0x8dfd0`, `+0x8e030`, `+0x8e090`, `+0x9b630`, `+0xd3950`, `+0xd7e30` | null and owner checks, then the same call on both paths (10385380) | not needed | crash guard | - |

### Unit timer events

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 4: D2Game `+0x8ee2f`..`+0x8ee32`: ptr PD2 `0x104984c8`<br>D2Game `+0x8ee4c`..`+0x8ee4f`: ptr PD2 `0x104984c8`<br>D2Game `+0x8eeb0`..`+0x8eeb3`: ptr PD2 `0x104984c8`<br>D2Game `+0x8eec9`..`+0x8eecc`: ptr PD2 `0x104984c8` | `+0x8edd0` | PD2 moves D2Game's unit timer-event table (+0xfb110) to its own copy and replaces event 3 with its replenish-charges handler (0x10345390) | ported | plugin/core-rules/item_rules.cpp (ReplenishEvent) | D2R item event 3 (0x4730d0 caller) |

### Unit appearance

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 17: D2Game call `+0x8fe14`, `+0x8fe2e`, `+0x8fe48`, `+0x8fe5f`, `+0x8fe73`, `+0x8fe87`, `+0x8fe98`, `+0x8feb5`, `+0x8fecc`, `+0x8ff11`, `+0x8ff2b`, `+0x8ff48`, `+0x8ff5c`, `+0x8ff6d`, `+0x8ff91`, `+0x8ffa5`, `+0x8ffb6` (to `0x8f4e0` sunitproxy.cpp) -> PD2 `0x103751e0` | `+0x8fbe0` | the gear sent for a unit's look maps PD2's arrow and bolt quiver codes (aqv/aqv3, cqv/cqv3) to aqv2/cqv2 | not needed | client art: D2R draws its own item models | - |

### Undecoded D2Game patches

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 3: D2Game call `+0x91de4` (to `0xabb6` D2Common#11111) -> PD2 `0x1036ee20`<br>D2Game `+0x91de9`..`+0x91dee`: `57e8bb8df7ff` -> `909090909090`<br>D2Game call `+0x91e0d` (to `0xafa0` D2Common#10993) -> PD2 `0x1036ee90` | `+0x91b70` | +0x91b70: two D2Common calls (#11111, #10993) skipped when a state list check fails (0x1036ee20, 0x1036ee90), one call NOPed at +0x91de9 | missing | [#1847](https://github.com/synpoox/pd2-resurrected/issues/1847); - (needs a read) | - |
| 4: D2Game `+0x92c19`: `0c` -> `04`<br>D2Game `+0x92c5a`: `8b` -> `e8`<br>D2Game call `+0x92c5a(8b)` (to `0xa7517`) -> PD2 `0x103a1d00`<br>D2Game `+0x92c5f`..`+0x92c6b`: `008d4aff0faf884c01000003..` -> `909090909090909090909090..` | `+0x92bb0` | +0x92bb0 (a hit builder): a damage flag 0xc becomes 0x4, and the skill attack bonus ToHit + (lvl-1) x LevToHit is replaced by a skills.txt calc (0x103022a0 -> 0x1030ad70) | missing | [#1847](https://github.com/synpoox/pd2-resurrected/issues/1847); - (needs a read); [#1862](https://github.com/synpoox/pd2-resurrected/issues/1862) | - |
| 1: D2Game `+0x934f9`: `85` -> `33` | `+0x934a0` | +0x934f9: the result of call +0x91240 is forced to 0 (test eax,eax -> xor eax,eax) | missing | [#1847](https://github.com/synpoox/pd2-resurrected/issues/1847); - (needs a read) | - |
| 1: D2Game call `+0x986d8` (to `0xaf52` D2Common#10501) -> PD2 `0x102fb290` | `+0x98680` | +0x986d8: D2Common #10501 returns 0 for a unit with +0x98 set and +0x94 clear (0x102fb290) | missing | [#1847](https://github.com/synpoox/pd2-resurrected/issues/1847); - (needs a read) | - |
| 1: D2Game call `+0x9a397` (to `0xa59e` D2Common#10819) -> PD2 `0x102fc860` | `+0x9a1a0` | +0x9a397 (SkilPal.cpp): D2Common #10819 skipped while the unit has state 18 (0x102fc860) | missing | [#1847](https://github.com/synpoox/pd2-resurrected/issues/1847); - (needs a read) | - |
| 1: D2Game call `+0x9cdc5` (to `0x9cb30`) -> PD2 `0x103606e0` | `+0x9cd90` | +0x9cdc5: an AI target pick (+0x9cb30) replaced by 0x103606e0, which special-cases monai 102 and 106 and a player-owned pet | missing | [#1847](https://github.com/synpoox/pd2-resurrected/issues/1847); - (needs a read; may belong with [#1790](https://github.com/synpoox/pd2-resurrected/issues/1790)) | - |
| 1: D2Game call `+0x9d494` (to `0x9d160`) -> PD2 `0x1035fcc0` | `+0x9d3b0` | +0x9d494: when the game has a value at +0x26fe the call +0x9d160 is replaced by a check that sends event 0x14 (0x1035fcc0) | missing | [#1847](https://github.com/synpoox/pd2-resurrected/issues/1847); - (needs a read) | - |
| 3: D2Game `+0xa2b25`..`+0xa2b29`: `e8c6b9ffff` -> `9090909090`<br>D2Game `+0xa2c25`..`+0xa2c29`: `e8c6b8ffff` -> `9090909090`<br>D2Game `+0xa2d94`..`+0xa2db0`: `6a2656e8de80f6ff85c07411..` -> `909090909090909090909090..` | `+0xa2a40`, `+0xa2b40`, `+0xa2c40` | skill code +0xa2a40, +0xa2b40: a call to +0xbe4f0 NOPed; +0xa2c40: the Thunderstorm (state 38) branch NOPed | missing | [#1847](https://github.com/synpoox/pd2-resurrected/issues/1847); - (needs a read) | - |
| 1: D2Game call `+0xa3602` (to `0xe2600`) -> PD2 `0x1037a980` | `+0xa35d0` | +0xa3602: call +0xe2600 replaced by 0x1037a980, a percent-of-life computation | missing | [#1847](https://github.com/synpoox/pd2-resurrected/issues/1847); - (needs a read) | - |
| 1: D2Game `+0xa37b7`: `19` -> `06` | `+0xa3610` | +0xa37b7: a constant 0x19 becomes 6 | missing | [#1847](https://github.com/synpoox/pd2-resurrected/issues/1847); - (needs a read) | - |
| 3: D2Game `+0xa5eec`: `75` -> `eb`<br>D2Game `+0xa608d`: `75` -> `eb`<br>D2Game `+0xa630b`..`+0xa6310`: `0f8401020000` -> `909090909090` | `+0xa5eb0`, `+0xa62d0` | monster AI +0xa5eb0, +0xa608d, +0xa62d0: a jne/je that idles the monster 25 frames is forced, so the active branch always runs | missing | [#1847](https://github.com/synpoox/pd2-resurrected/issues/1847); - (needs a read) | - |
| 1: D2Game call `+0xbc0b2` (to `0x34130`) -> PD2 `0x10375300` | `+0xbc050` | +0xbc0b2 (SUnit.cpp): after +0x34130 PD2 looks up a fixed spot (0x1e46, 0x1446, else 0x1e6e) in a level (0x10375300) | missing | [#1847](https://github.com/synpoox/pd2-resurrected/issues/1847); - (needs a read) | - |
| 2: D2Game call `+0xc057c`, `+0xd9f65` (to `0xa1280`) -> PD2 `0x10373030` | `+0xd9bf0` | +0xc057c, +0xd9f65: after +0xa1280 the left and right skills' aura states (skills.txt flag 0x20) are removed and set again (0x10373030) | missing | [#1847](https://github.com/synpoox/pd2-resurrected/issues/1847); - (needs a read) | - |
| 1: D2Game call `+0xc16ec` (to `0xbcc60`) -> PD2 `0x10372ff0` | `+0xc1510` | +0xc16ec: after +0xbcc60 a follow-up call on a unit (0x10372ff0) | missing | [#1847](https://github.com/synpoox/pd2-resurrected/issues/1847); - (needs a read) | - |
| 1: D2Game `+0xc9fc9`..`+0xc9fcc`: ptr PD2 `0x10374d50` | `+0xc9f60` | a2q6.cpp object callback (+0xc9fc9) replaced by 0x10374d50, which runs three room-unit passes | missing | [#1847](https://github.com/synpoox/pd2-resurrected/issues/1847); - (needs a read) | - |
| 3: D2Game `+0xcfc58`..`+0xcfc59`: `751f` -> `9090`<br>D2Game call `+0xcfc61` (to `0xa430` D2Common#10744) -> PD2 `0x102fb2f0`<br>D2Game call `+0xcfdef` (to `0xd51a0` ItemMode.cpp) -> PD2 `0x103734e0` | `+0xcfbc0` | +0xcfbc0: a jne NOPed, D2Common #10744 through 0x102fb2f0, and after ItemMode +0xd51a0 a monster gets 0x103a4240 (0x103734e0) | missing | [#1847](https://github.com/synpoox/pd2-resurrected/issues/1847); - (needs a read) | - |
| 3: D2Game `+0xd0655`..`+0xd0656`: `7405` -> `9090`<br>D2Game call `+0xd070e` (to `0xa934` D2Common#10244) -> PD2 `0x1036e620` | - | +0xd0655 je NOPed; +0xd070e D2Common #10244 (an item type test) through PD2's list (0x1036e620) | missing | [#1847](https://github.com/synpoox/pd2-resurrected/issues/1847); - (needs a read) | - |
| 2: D2Game `+0xd9a41`..`+0xd9a42`: `7e2a` -> `9090`<br>D2Game call `+0xd9a5e` (to `0xaa12` D2Common#10029) -> PD2 `0x1036f940` | `+0xd9470` | ItemMode +0xd9a41 jle NOPed; D2Common #10029 called with its last argument 0 (0x1036f940) | missing | [#1847](https://github.com/synpoox/pd2-resurrected/issues/1847); - (needs a read) | - |
| 1: D2Game call `+0xdc477` (to `0xda7b0`) -> PD2 `0x10304dc0` | `+0xdc0b0` | +0xdc477: with item_halffreezeduration and no item_cannotbefrozen, PD2 halves the cold length fields too (0x10304dc0) | missing | [#1847](https://github.com/synpoox/pd2-resurrected/issues/1847); - (needs a read); [#1864](https://github.com/synpoox/pd2-resurrected/issues/1864) | - |
| 1: D2Game call `+0xdc88d` (to `0xa76c` D2Common#10261) -> PD2 `0x1036e510` | `+0xdc780` | SUnitDmg.cpp +0xdc88d: a stat add also adds stat 423 x3 and clamps stat 105 (0x1036e510) | missing | [#1847](https://github.com/synpoox/pd2-resurrected/issues/1847); - (needs a read) | - |
| 1: D2Game call `+0xde287` (to `0xdcc20`) -> PD2 `0x10302a30` | `+0xde0c0` | +0xde287: +0xdcc20 replaced by 0x10302a30 (a state 273 special) | missing | [#1847](https://github.com/synpoox/pd2-resurrected/issues/1847); - (needs a read) | - |
| 1: D2Game call `+0xe1e8d` (to `0xa5e0` D2Common#10874) -> PD2 `0x102fa5c0` | `+0xe1d90` | +0xe1e8d: D2Common #10874 through 0x102fa5c0 | missing | [#1847](https://github.com/synpoox/pd2-resurrected/issues/1847); - (needs a read) | - |
| 1: D2Game call `+0xe97d7` (to `0x36470` QUESTS.CPP) -> PD2 `0x103721c0` | `+0xe9600` | +0xe97d7: QUESTS.CPP +0x36470 through 0x103721c0 | missing | [#1847](https://github.com/synpoox/pd2-resurrected/issues/1847); - (needs a read) | - |
| 1: D2Game `+0xfa128`..`+0xfa12b`: ptr PD2 `0x103943e0` | - | client message 0x16 (pick up): gold and item types 76-78 are picked up from up to 6 away; after a pickup 0x10374850 runs on the item (0x103943e0) | missing | [#1847](https://github.com/synpoox/pd2-resurrected/issues/1847); - (needs a read); [#1863](https://github.com/synpoox/pd2-resurrected/issues/1863) | - |
| 6: D2Game, 6 byte writes at `+0xfa228`, `+0xfa240`, `+0xfa280`, `+0xfa288`, `+0xfa290`, `+0xfa388` | - | client messages 0x36 and 0x62 gated by 0x103971e0; 0x39 made a no-op; 0x41 (resurrect) recompiled with an arena branch; 0x42 takes 7 bytes (0x10395b50); 0x43 validated and never run | missing | [#1847](https://github.com/synpoox/pd2-resurrected/issues/1847); - (needs a read) | - |
| 1: D2Game `+0xfbb94`..`+0xfbb97`: ptr PD2 `0x10345a40` | - | callback table +0xfbb94: +0xa1e80 replaced by 0x10345a40, which restarts an aura skill (skills.txt flag 0x20) of the left skill | missing | [#1847](https://github.com/synpoox/pd2-resurrected/issues/1847); - (needs a read) | - |

### Crit and deadly strike

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 3: D2Game call `+0x92c82`, `+0x92a22`, `+0x93807` (to `0x92360`) -> PD2 `0x103022c0` | `+0x92940`, `+0x92bb0` | PD2's damage builder (0x103022c0) adds its crit and deadly strike multiplier (IR3) | ported | plugin/core-rules/item_rules.cpp (WeaponStrike) | WeaponDamage 0x44c030 |
| 4: D2Game `+0xdd52a`..`+0xdd52c`: `74286a` -> `9090e8`<br>D2Game call `+0xdd52c(6a)` (to `0x500e3f33`) -> PD2 `0x10303170`<br>D2Game `+0xdd531`..`+0xdd5b3`: `53e8d7d2f2ff85c089442410..` -> `909090909090909090909090..` | `+0xdd450` | +0xdd450: the strike multiplier call (0x10303170) replaces 1.13c's block | ported | plugin/core-rules/item_rules.cpp (WeaponStrike) | - |
| 2: D2Common call `+0x6bf87`, `+0x6bd1a` (to `0x6b180`) -> PD2 `0x103046b0` | `+0x6baa0` | D2Common missile fill +0x6baa0: PD2's gated missile crit roll (0x103046b0); IR3 calls it dead code because PD2 runs its own missile fill | ported | plugin/core-rules/item_rules.cpp (IR3 missile roll) | 0x3ba820 |

### Guilds

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 1: D2Game call `+0x973c8` (to `0x972c0`) -> PD2 `0x10371ff0` | `+0x97340` | a guild check (0x1034b4b0) before a monster interaction call (+0x972c0) | not needed | cut on roadmap: guilds | - |
| 6: D2Game, 6 byte writes at `+0xfa1d8`, `+0xfa1e0`, `+0xfa2a0`, `+0xfa2c8`, `+0xfa2e8`, `+0xfa308` | - | client messages 0x2c, 0x2d, 0x45 and PD2's new 0x4a, 0x4e, 0x52: guild actions and guild chat | not needed | cut on roadmap: guilds | - |

### NPC vendors in map levels

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 14: D2Game call `+0x9b6f5`, `+0x9b739` (to `0xbc840` SUNITNPC.CPP) -> PD2 `0x10384ca0`<br>D2Game call `+0xbd935`, `+0xbe71c`, `+0xbf8d2`, `+0xf3ba3` (to `0x8faa0`) -> PD2 `0x10384bf0`<br>D2Game call `+0xbe872` (to `0xa430` D2Common#10744) -> PD2 `0x10384dd0`<br>D2Game call `+0xbe887` (to `0xd1b20`) -> PD2 `0x10384fa0`<br>D2Game `+0xbf1ec`..`+0xbf1ee`: `5f5ee9` -> `9090e8`<br>D2Game call `+0xbf1ee(e9)` (to `0xbe2a0` SUNITNPC.CPP) -> PD2 `0x10384740`<br>D2Game `+0xbf88c`..`+0xbf88f`: `c6450700` -> `90909090`<br>D2Game `+0xfa330`..`+0xfa333`: ptr PD2 `0x10396f10` | `+0x9b630`, `+0xbd920`, `+0xbe640`, `+0xf3a40` | SUNITNPC patches: in a PD2 map level (game +0x1df4 = 0x12) vendor 147 (Gheed) uses a store not tied to the act (0x10384ca0, 0x10384bf0, 0x10384740); the sell/repair item-type test is PD2's list (0x10384dd0, 0x10384fa0); two byte patches in the trade code (+0xbf1ec, +0xbf88c); client message 0x57 reopens an NPC trade in a map level (0x10396f10) | missing | [#1842](https://github.com/synpoox/pd2-resurrected/issues/1842); - (verify Gheed's map store in game first) | - |

### Item prices

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 3: D2Game call `+0xbe8f0`, `+0xbea25`, `+0xbec7f` (to `0xbdd80`) -> PD2 `0x10385220` | `+0xbe640` | vendor buy and sell prices go through PD2's copy (0x10385220, item-prices) | ported | plugin/core-rules/item_prices.cpp | - |
| 1: D2Common call `+0x29d7d` (to `0x28e80` Items.cpp) -> PD2 `0x10385090` | `+0x29d60` | D2Common #10107's call goes to PD2's price copy (0x10385090) | ported | plugin/core-rules/item_prices.cpp | - |

### Crash guards

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 1: D2Game `+0x9d247`..`+0x9d267`: `741f68a6010000e887d0f6ff..` -> `909090909090909090909090..` | `+0x9d230` | +0x9d247: NOPs a fatal error and exit(-1) path | not needed | no behaviour change (crash guard) | - |
| 1: D2Game `+0xfa348`..`+0xfa34b`: ptr PD2 `0x10396ff0` | - | PD2's new client message 0x5a does nothing | not needed | no behaviour change | - |
| 1: D2Game `+0xfbba0`..`+0xfbba3`: ptr PD2 `0x10345e60` | - | callback +0xfbba0 (+0x79a60) runs only for a player with player data (0x10345e60) | not needed | no behaviour change (crash guard) | - |

### Thunks to 1.13c

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 4: D2Game call `+0xa0971`, `+0xb46a5`, `+0xb6802`, `+0xf1695` (to `0x9fd00`) -> PD2 `0x103a2060` | `+0xa0830`, `+0xb4660`, `+0xf14f0` | four calls of +0x9fd00 go through a PD2 pointer back to 1.13c code (0x103a2060) | not needed | no behaviour change (thunk) | - |
| 1: D2Game `+0xfa1e8`..`+0xfa1eb`: ptr PD2 `0x1031e8c0` | - | client message 0x2e goes through a PD2 pointer back to 1.13c's handler | not needed | no behaviour change (thunk) | - |

### Holy Sword parry

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 5: D2Game call `+0xa10f0`, `+0xdb79f`, `+0xdb7c1`, `+0xdb845` (to `0xdb030`) -> PD2 `0x10303600`<br>D2Game `+0xdb0c0`: `85` -> `33` | `+0xa10b0`, `+0xdb030`, `+0xdb790` | PD2's parry roll (0x10303600) replaces +0xdb030's, and +0xdb0c0's test is forced | ported | plugin/core-rules/skill_functions.cpp, plugin/common/parry_roll.h | - |

### Dark Wanderer

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 1: D2Game `+0xa6e90`..`+0xa6e91`: `741c` -> `9090` | `+0xa6e60` | the a3q7 gate's jump is NOPed, so his leave spot is undefined | deviation | Dark Wanderer's leave spot | - |

### Summon AI

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 2: D2Game `+0xab26e`: `00` -> `01`<br>D2Game `+0xab494`..`+0xab4b1`: `6a0253e8f2f1f5ff85c00f85..` -> `909090909090909090909090..` | `+0xab070`, `+0xab360` | CycleOfLife flag (+0xab26e) and the Vines poisoned/immune skip (+0xab494) | ported | plugin/core-rules/summon_ai.cpp ([#1802](https://github.com/synpoox/pd2-resurrected/issues/1802), origin/main 2eede0a4) | - |
| 7: D2Game call `+0xab66e`, `+0xab939` (to `0x1e140`) -> PD2 `0x10360d40`<br>D2Game `+0xab865`: `75` -> `eb`<br>D2Game `+0xab8e2`: `74` -> `eb`<br>D2Game `+0xab99d`: `74` -> `eb`<br>D2Game `+0xabb48`: `7e` -> `eb`<br>D2Game `+0xac337`: `74` -> `eb` | - | ShadowMaster think: PD2's leash and five branch patches | ported | plugin/core-rules/summon_ai.cpp ([#1790](https://github.com/synpoox/pd2-resurrected/issues/1790)) | 0x5cd740 |
| 1: D2Game call `+0xac550` (to `0x1e140`) -> PD2 `0x1036e9a0` | `+0xac410` | ShadowWarrior think (+0xac410): the leash call is skipped for a player-owned Shadow Warrior (0x1036e9a0) | not needed | dead code: row 105 is the only AI row with this think and PD2 replaces it (`0x10360240`), ported in plugin/core-rules/summon_ai.cpp ([#1800](https://github.com/synpoox/pd2-resurrected/issues/1800)) | 0x5ceea0 |

### Skill while equipped

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 1: D2Game `+0xaff16`..`+0xaff19`: ptr PD2 `0x1036f6b0` | `+0xafdb0` | the stat callback pointer at +0xaff16 goes to PD2's item_skillonequip callback (0x1036f6b0) | ported | plugin/core-rules/item_rules.cpp (IR7) | - |
| 2: D2Game `+0xfbb98`..`+0xfbb9b`: ptr PD2 `0x10345cd0`<br>D2Game `+0xfbc38`..`+0xfbc3b`: ptr PD2 `0x102e8f80` | - | item aura tick (+0xfbb98, +0xfbc38): PD2's tick for item_skillonequip auras | ported | plugin/core-rules/item_rules.cpp (IR7, ItemAuraTick) | - |

### Loot

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 1: D2Game call `+0xbb4b7` (to `0x75e10`) -> PD2 `0x10372e80` | `+0xbb3b0` | the Chaos seal sweep kills without drops (0x10372e80) | ported | plugin/core-rules/loot_rules.cpp | 0x5e5990 |

### Stash pages

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 2: D2Game call `+0xbdf33`, `+0xbdfc9` (to `0xac5e` D2Common#10071) -> PD2 `0x102e85b0` | - | +0xbdf33, +0xbdfc9: D2Common #10071 (inventory fit) through PD2's copy that walks its extra stash pages (0x102e85b0) | not needed | D2R's own stash and tabs | - |
| 1: D2Game `+0xfa320`..`+0xfa323`: ptr PD2 `0x10396ed0` | - | PD2's new client message 0x55 resets its stash page | not needed | D2R's own stash and tabs | - |

### 2H Phase Blade

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 1: D2Game call `+0xc0422` (to `0x11490`) -> PD2 `0x10374ca0` | - | every new Phase Blade (7cr) has a 1 in 280 (Normal), 140 (Nightmare) or 70 (Hell) chance to become a 2H Phase Blade (7cr2) (0x10374ca0, 0x10388ae0) | missing | [#1843](https://github.com/synpoox/pd2-resurrected/issues/1843) | item creation call at +0xc0422 |

### Save and packet buffers

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 1: D2Game `+0xcb0de`..`+0xcb0e1`: `00200000` -> `00000200` | `+0xcaf30` | CCmd.cpp's message size limit 0x2000 becomes 0x20000 | not needed | D2R has its own message and save handling | - |
| 21: D2Game, 21 byte writes at `+0xec7f1`, `+0xec7fd`, `+0xec806`, `+0xec812`, `+0xec8c3`, `+0xec8ce`, `+0xec8dd`, `+0xec8f5`, `+0xec907`, `+0xec92c`, `+0xec936`, `+0xec953`, `+0xec95d`, `+0xec99c`, `+0xec9a3`, `+0xec9c0`, `+0xeca0a`, `+0xeca15`, `+0xeca32`, `+0xeca41`, `+0xeca51` | `+0xec7f0` | +0xec7f0's 0x8000-byte stack buffer becomes 0x20000 (21 offsets) | not needed | D2R's own save format | - |

### Stash gold

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 1: D2Game call `+0xd14a2` (to `0x74200`) -> PD2 `0x103999f0` | - | +0xd14a2: gold to and from the stash through PD2's copy (0x103999f0) with a 25,000,000 cap | not needed | D2R's own stash gold; stash gold caps go with the cut stash layouts (docs/research/items/pd2-remaining-items.md:115) | - |

### Teleport blocked

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 2: D2Game call `+0xd9761` (to `0x1420`) -> PD2 `0x1036eb20`<br>D2Game call `+0xeb248` (to `0xd24b0`) -> PD2 `0x1036eaa0` | `+0xd9470`, `+0xeb090` | while a skill is used from an item (+0xd24b0 wrapped, player +0x170 set) Teleport (97) is refused, and a level that blocks it sends string 0x17c (0x1036eb20, 0x1036eaa0) | deviation | Teleport, Blink, LucionBlink, Mon Bloodwarp | - |

### Stun and get-hit timing

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 2: D2Game call `+0xdd41a` (to `0x78550`) -> PD2 `0x10302c10`<br>D2Game `+0xfa4a8`..`+0xfa4ab`: ptr PD2 `0x1037bac0` | `+0xdcf20` | a hit on a stunned player shortens the stun (state 21) by 2 frames (0x10302c10); the monster get-hit mode (+0xfa4a8, 1.13c +0x74e60) schedules its end event only when none is pending (0x1037bac0) | missing | [#1844](https://github.com/synpoox/pd2-resurrected/issues/1844); unclear (triage): the stun part is PvP only, the get-hit end event is open | - |

### Uber bosses

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 1: D2Game call `+0xde2ae` (to `0xdcab0`) -> PD2 `0x103055c0` | `+0xde0c0` | poison damage step (0x103055c0): the final pair's shared life | ported | plugin/core-events/rathma_life.cpp | - |

### .noexp

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 5: D2Game call `+0xde7b9`, `+0xde89c`, `+0xde95e`, `+0xde9dd`, `+0xdeb8d` (to `0xde6d0`) -> PD2 `0x10373280` | `+0xde750`, `+0xde870`, `+0xde8f0`, `+0xdea00` | experience gain (+0xde6d0) skipped while the player's .noexp flag is on, and in two level groups below 80 | deviation | .noexp toggle | - |

### Map system

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 2: D2Game call `+0xe14d2`, `+0xe1542` (to `0xe13b0`) -> PD2 `0x1038c030` | `+0xe1430` | area/map open handling (0x1038c030) | ported | plugin/core-events/map_open.cpp | - |
| 2: D2Game call `+0xe1b02`, `+0xe1be1` (to `0xe1610`) -> PD2 `0x1038b070` | `+0xe19c0` | on a level change PD2 refreshes the map stats and clears two map states (0x1038b070) | ported | plugin/core-events/map_open.cpp (MS3) | - |

### Character stat commands

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 1: D2Game call `+0xef3a3` (to `0xca5b0`) -> PD2 `0x10375470` | `+0xeef10` | monster kills counted in player data (+0x2cc) for .kills (0x10375470) | deviation | Character stat commands (.kills, .deaths, .played, .age, .allstats, .accountstats) | - |
| 1: D2Game `+0xfa120`..`+0xfa123`: ptr PD2 `0x10394380` | - | client message 0x15 (chat): PD2's dot commands are parsed first (0x103645a0) | deviation | Character stat commands (.kills, .deaths, .played, .age, .allstats, .accountstats) | - |

### Towns

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 1: D2Game call `+0xf1146` (to `0xa808` D2Common#10057) -> PD2 `0x102faf30` | - | the town test (D2Common #10057) also counts the PvP arenas and PD2's extra town levels (0x102faf30) | not needed | cut on roadmap: PvP and guild halls | - |
| 9: D2Common call `+0x127cf`, `+0x3575f`, `+0x3b234`, `+0x3c3a7`, `+0x444f9`, `+0x4451b`, `+0x4f142`, `+0x7b7f2`, `+0x7ba81` (to `0x2d320`) -> PD2 `0x1037c7b0` | `+0x12720`, `+0x356e0`, `+0x3b230`, `+0x3c390`, `+0x444f0`, `+0x4f120`, `+0x7ba00` | D2Common town test (+0x2d320, 9 sites) also counts levels 202-206, the guild halls (0x1037c7b0) | not needed | cut on roadmap: guild halls | - |

### Lock Inventory

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 12: D2Game, 12 byte writes at `+0xfa138`, `+0xfa140`, `+0xfa148`, `+0xfa150`, `+0xfa158`, `+0xfa160`, `+0xfa168`, `+0xfa170`, `+0xfa1b8`, `+0xfa210`, `+0xfa378`, `+0xfa380` | - | client messages 0x18-0x1f, 0x28, 0x33, 0x60, 0x61: PD2's wrappers refuse body-slot and charm-row moves while the lock (player +0x269) is on; 0x18 also handles PD2's stash pages and 0x1a a preferred ring slot | deviation | Lock Inventory (plugin/core-ui/inventory_lock.cpp) | PlrMsg 0x4f2fa0 |
| 1: D2Game `+0xfa358`..`+0xfa35b`: ptr PD2 `0x10397010` | - | PD2's new client message 0x5c (settings): 2 sets the inventory lock; 0 the aura display (states 209-211, 237, not ported); 5 a setting at +0x7d0; 6 the Compendium deposit (gold and items) | deviation | Lock Inventory (sub 2, plugin/core-ui/inventory_lock.cpp); Grail panel (sub 6) | - |

### Stack split

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 1: D2Game `+0xfa188`..`+0xfa18b`: ptr PD2 `0x10394f60` | - | client message 0x22: PD2's split handler (0x10394f60) | ported | core-ui split part (docs/research/items/stack-split.md) | - |

### Skill points

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 1: D2Game `+0xfa250`..`+0xfa253`: ptr PD2 `0x103956b0` | - | client message 0x3b (add skill) takes an optional count byte and spends that many points at once, capped by the skill's max level and the character level (0x103956b0) | not needed | close enough ([#1845](https://github.com/synpoox/pd2-resurrected/issues/1845) triage): D2R already has bulk point assignment; only the 5/20 steps differ | D2R's own bulk assign |

### Desecration

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 1: D2Game `+0xfa298`..`+0xfa29b`: ptr PD2 `0x10396220` | - | client message 0x44 (altar) runs PD2's Desecration roll | ported | plugin/core-events/lucion.cpp | - |

### Cube rules

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 1: D2Game `+0xfa2d8`..`+0xfa2db`: ptr PD2 `0x103965e0` | - | client message 0x4c (transmute) runs PD2's cube (stack-aware matching, corruption, cube ops) | ported | plugin/core-rules/cube_rules.cpp, plugin/core-rules/cube_stacks.cpp, plugin/core-events/cube_ops.cpp | - |

### Player death

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 1: D2Game `+0xfa480`..`+0xfa483`: ptr PD2 `0x1036fd40` | - | player dead mode (+0xfa480) runs PD2's handler (counters, PvP arena branch) | not needed | w37: no rule outside PvP and the stat commands | - |

### Monster death

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 1: D2Game `+0xfa498`..`+0xfa49b`: ptr PD2 `0x1037a3a0` | - | monster dead mode (+0xfa498) runs PD2's death steps (map events, uber and key drops) | ported | plugin/core-events/map_events.cpp, plugin/core-events/uber_drops.cpp, plugin/core-events/key_drops.cpp, plugin/core-events/lane-c/wanderer.cpp | - |

### Monsters Remaining

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 1: D2Game `+0xfbb80`..`+0xfbb83`: ptr PD2 `0x10345400` | - | the per-level monster ledger callback (0x10345400) sends packet 0xAD | deviation | Monsters Remaining (plugin/core-ui/monsters_remaining.cpp) | - |

### Undecoded D2Common patches

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 1: D2Common call `+0x182ec` (to `0x26f0`) -> PD2 `0x102fae10` | `+0x18280` | Chat.cpp +0x182ec: a PD2 call (0x103a1cd0) before +0x26f0 | missing | [#1848](https://github.com/synpoox/pd2-resurrected/issues/1848); - (needs a read) | - |
| 2: D2Common `+0x1efe3`: `7f` -> `7d`<br>D2Common `+0x1eff5`: `7f` -> `7d` | `+0x1ef90` | +0x1ef90: two jg become jge | missing | [#1848](https://github.com/synpoox/pd2-resurrected/issues/1848); - (needs a read) | - |
| 3: D2Common call `+0x2155d`, `+0x213fc` (to `0x251c0`) -> PD2 `0x102fb080`<br>D2Common `+0x252cd`: `03` -> `04` | `+0x21300`, `+0x21420`, `+0x251c0` | +0x2155d, +0x213fc: +0x251c0 replaced by PD2's item function 0x102fb080 (also called from D2Client); +0x252cd: an add becomes another opcode | missing | [#1848](https://github.com/synpoox/pd2-resurrected/issues/1848); - (needs a read) | - |
| 1: D2Common `+0x25804`..`+0x25807`: ptr PD2 `0x1030e290` | - | +0x25804: a pointer to 0x1030e290 written into code padding | missing | [#1848](https://github.com/synpoox/pd2-resurrected/issues/1848); - (needs a read) | - |
| 1: D2Common `+0x26473`..`+0x26474`: `03ee` -> `9090` | `+0x26190` | +0x26473: an add esi NOPed | missing | [#1848](https://github.com/synpoox/pd2-resurrected/issues/1848); - (needs a read) | - |
| 5: D2Common `+0x2d2c9`: `8d` -> `e8`<br>D2Common call `+0x2d2c9(8d)` (to `0x2f772`) -> PD2 `0x102fb040`<br>D2Common `+0x2d2ce`..`+0x2d2d0`: `00003b` -> `c20400` | `+0x2d2c0` | +0x2d2c0: replaced by a call to 0x102fb040 (a byte from a PD2 table lookup) and ret 4 | missing | [#1848](https://github.com/synpoox/pd2-resurrected/issues/1848); - (needs a read) | - |
| 3: D2Common `+0x334ac`..`+0x334cc`: `6a006a4456e8ba5600006a00..` -> `909090909090909090909090..`<br>D2Common `+0x334ac`: `6a` -> `e8`<br>D2Common call `+0x334ac(6a)` (to `0x56479eb1`) -> PD2 `0x103a37a0` | `+0x33110` | Units.cpp +0x334ac: two D2Common calls with 0x44 NOPed and replaced by 0x102fc0e0 | missing | [#1848](https://github.com/synpoox/pd2-resurrected/issues/1848); - (needs a read) | - |
| 8: D2Common call `+0x378eb`, `+0x379e6`, `+0x379fb`, `+0x37abb`, `+0x37b5f`, `+0x37c80`, `+0x37d00`, `+0x37d15` (to `0x376f0`) -> PD2 `0x102fbe10` | `+0x37840`, `+0x37920`, `+0x37a20`, `+0x37af0`, `+0x37c20`, `+0x37ca0` | +0x376f0 (8 sites) through 0x102fbe10: for a level above 136 a value is set from 0x1037f1a0 | missing | [#1848](https://github.com/synpoox/pd2-resurrected/issues/1848); - (needs a read; levels past 136 are PD2's maps) | - |
| 3: D2Common `+0x3a74f`..`+0x3a750`: `4e5c` -> `ce90`<br>D2Common call `+0x3a75a` (to `0x3a280`) -> PD2 `0x102fae50` | `+0x3a740` | +0x3a740: a register change and a stat add moved to the unit at +0x5c (0x102fae50) | missing | [#1848](https://github.com/synpoox/pd2-resurrected/issues/1848); - (needs a read) | - |
| 19: D2Common call `+0x6ac1f`, `+0x6ac6a`, `+0x6acb5`, `+0x6ad00`, `+0x6ad4b`, `+0x6ad96`, `+0x6ade1`, `+0x6ae2c`, `+0x6ae77`, `+0x6aebc`, `+0x6af01`, `+0x6af3b`, `+0x6af77`, `+0x6afbf`, `+0x6afff`, `+0x6b044`, `+0x6b08c`, `+0x6b0d4`, `+0x6b122` (to `0x38a80`) -> PD2 `0x102fa820` | `+0x6abe0` | +0x6abe0 (19 sites): elemental min/max damage stats get their skill mastery (stats 329, 330, 357, 331, 332) added (0x102fa820) | missing | [#1848](https://github.com/synpoox/pd2-resurrected/issues/1848); - (needs a read); [#1868](https://github.com/synpoox/pd2-resurrected/issues/1868) | - |

### Charm "only one unique" rule

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 12: D2Common call `+0x27028` (to `0x24280`) -> PD2 `0x102fc590`<br>D2Common `+0x2703b`..`+0x27044`: `83fe01751b83fb047516` -> `90909090909090909090`<br>D2Common `+0x2704d`..`+0x27050`: `3d300200` -> `505355e8`<br>D2Common call `+0x27050(00)` (to `0x3d09ec55`) -> PD2 `0x1036e890`<br>D2Common `+0x27055`..`+0x27063`: `310200007e0985f675143b5d..` -> `85c0741a9090909090909090..` | `+0x26db0` | D2Common +0x26db0 (inventory placement): a charm (types 13, 221) in the inventory is checked by 0x10381970, and PD2's body-location exceptions for four item classes (0x1036e890) | deviation | Charm "only one unique" rule | - |

### Combat item stats

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 6: D2Common call `+0x38bc4`, `+0x391e3`, `+0x393ca`, `+0x3947d` (to `0x38a80`) -> PD2 `0x102fa650`<br>D2Common call `+0x39d99`, `+0x39f15` (to `0x39ce0`) -> PD2 `0x102fa940` | `+0x38b70`, `+0x391a0`, `+0x39380`, `+0x39430`, `+0x39ce0` | PD2's stat evaluator (0x102fa650) and item stat refresh (0x102fa940): equipped_eth, item_dmgpercent_pereth, inc_splash_radius_permissinghp (IR3) | ported | plugin/core-rules/item_rules.cpp (IR3) | - |
| 2: D2Common `+0x39da0`..`+0x39dac`: `8b44243050565557e893ebff..` -> `909090909090909090909090..`<br>D2Common `+0x39f1a`..`+0x39f26`: `8b54243052565057e819eaff..` -> `909090909090909090909090..` | `+0x39ce0` | +0x39da0, +0x39f1a: the 1.13c calls PD2's refresh replaces are NOPed | ported | plugin/core-rules/item_rules.cpp (IR3) | - |

### Table end

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 1: D2Common `+0x9e4e4`..`+0x9e4e7`: `00000000` -> `00000000` | - | the last record writes 0 over 0 at D2Common +0x9e4e4 | not needed | no change | - |

### Client text

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 3: D2Lang `+0x92b2`..`+0x92b4`: `0fb608` -> `31c990` | - | D2Lang +0x92b2: movzx ecx,[eax] becomes xor ecx,ecx | not needed | client-only; D2R's own string code | - |

### Client UI

| Records | 1.13c function | Change | Status | Ours / evidence | D2R |
|---|---|---|---|---|---|
| 1: D2Win `+0xf2e2`: `05` -> `05` | - | D2Win +0xf2e2: writes the 5 that is already there | not needed | no change in this build | - |

## Installer table writes

PD2's start-up routine `0x10344580` runs installers that write PD2 functions into 1.13c tables ([w37](../research/pd2/w37-server-rules.md)). PD2 addresses here are PD2.gpr's. Status: **ported** (our code names the function), **documented** (a doc names it; its verdict stands), **missing** (a real gap with its own issue, from the [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) triage), **unchecked** (named nowhere; [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849)). The D2Client server-message table writes (`0x1036be80`) are PD2's own packets and go with the features above.

| Installer | Table | Entry | PD2 function (PD2.gpr) | Status | Ours / evidence |
|---|---|---|---|---|---|
| `0x10361150` | AI table (D2Game `+0x10e788`, 16-byte rows by monai row) | row 6 Fallen think | `0x1035a3c0` | ported | `plugin/core-events/treasure_fallen.cpp` |
| `0x10361150` | AI table (D2Game `+0x10e788`, 16-byte rows by monai row) | row 7 Brute think | `0x103578f0` | documented | [uber-bosses-dll](uber-bosses-dll.md) |
| `0x10361150` | AI table (D2Game `+0x10e788`, 16-byte rows by monai row) | row 22 GreaterMummy think | `0x1035f6e0` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x10361150` | AI table (D2Game `+0x10e788`, 16-byte rows by monai row) | row 25 WillOWisp think | `0x10356f30` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x10361150` | AI table (D2Game `+0x10e788`, 16-byte rows by monai row) | row 32 Npc think | `0x10357340` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x10361150` | AI table (D2Game `+0x10e788`, 16-byte rows by monai row) | row 35 CorruptArcher think | `0x103574f0` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x10361150` | AI table (D2Game `+0x10e788`, 16-byte rows by monai row) | row 45 Sarcophagus think | `0x103574e0` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x10361150` | AI table (D2Game `+0x10e788`, 16-byte rows by monai row) | row 47 FlyingScimitar think | `0x10357180` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x10361150` | AI table (D2Game `+0x10e788`, 16-byte rows by monai row) | row 49 ZakarumPriest think | `0x10357800` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x10361150` | AI table (D2Game `+0x10e788`, 16-byte rows by monai row) | row 50 Mephisto think | `0x10357ba0` | ported | `plugin/core-events/prime_minions.cpp` |
| `0x10361150` | AI table (D2Game `+0x10e788`, 16-byte rows by monai row) | row 51 Diablo think | `0x10357d50` | ported | `plugin/core-events/prime_minions.cpp` |
| `0x10361150` | AI table (D2Game `+0x10e788`, 16-byte rows by monai row) | row 53 Summoner think | `0x10358510` | ported | `plugin/core-events/rathma_ai.cpp`, `plugin/core-rules/skill_functions.cpp` |
| `0x10361150` | AI table (D2Game `+0x10e788`, 16-byte rows by monai row) | row 61 Hireable think | `0x1035a270` | documented | [summon-ai](summon-ai.md) |
| `0x10361150` | AI table (D2Game `+0x10e788`, 16-byte rows by monai row) | row 71 Regurgitator think | `0x1035a6b0` | ported | `plugin/core-events/guardian_ai.cpp` |
| `0x10361150` | AI table (D2Game `+0x10e788`, 16-byte rows by monai row) | row 72 DoomKnight think | `0x1035a930` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x10361150` | AI table (D2Game `+0x10e788`, 16-byte rows by monai row) | row 74 OblivionKnight think | `0x1035b0a0` | documented | [uber-bosses-dll](uber-bosses-dll.md) |
| `0x10361150` | AI table (D2Game `+0x10e788`, 16-byte rows by monai row) | row 87 Trap-Melee think | `0x1035b3f0` | ported | `plugin/core-events/dclone_ai.cpp` |
| `0x10361150` | AI table (D2Game `+0x10e788`, 16-byte rows by monai row) | row 89 Megademon think | `0x10357710` | missing | [#1871](https://github.com/synpoox/pd2-resurrected/issues/1871) |
| `0x10361150` | AI table (D2Game `+0x10e788`, 16-byte rows by monai row) | row 93 ArcaneTower think | `0x1035cb30` | missing | [#1872](https://github.com/synpoox/pd2-resurrected/issues/1872) |
| `0x10361150` | AI table (D2Game `+0x10e788`, 16-byte rows by monai row) | row 99 TrappedSoul think | `0x1035bc60` | ported | `plugin/core-events/dclone_ai.cpp`, `plugin/core-events/nihlathak_ai.cpp`, `plugin/core-events/nihlathak_ai.h` |
| `0x10361150` | AI table (D2Game `+0x10e788`, 16-byte rows by monai row) | row 102 BladeCreeper think | `0x1035fd10` | missing | [#1869](https://github.com/synpoox/pd2-resurrected/issues/1869) |
| `0x10361150` | AI table (D2Game `+0x10e788`, 16-byte rows by monai row) | row 105 ShadowWarrior init | `0x10360a10` | documented | [summon-ai](summon-ai.md) |
| `0x10361150` | AI table (D2Game `+0x10e788`, 16-byte rows by monai row) | row 105 ShadowWarrior think | `0x10360240` | documented | [summon-ai](summon-ai.md) |
| `0x10361150` | AI table (D2Game `+0x10e788`, 16-byte rows by monai row) | row 106 ShadowMaster init | `0x10360af0` | documented | [summon-ai](summon-ai.md) |
| `0x10361150` | AI table (D2Game `+0x10e788`, 16-byte rows by monai row) | row 114 ReanimatedHorde think | `0x1035da60` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x10361150` | AI table (D2Game `+0x10e788`, 16-byte rows by monai row) | row 115 SiegeBeast think | `0x1035f820` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x10361150` | AI table (D2Game `+0x10e788`, 16-byte rows by monai row) | row 120 Overseer think | `0x1035dca0` | ported | `plugin/core-events/lane-c/invaders.cpp`, `plugin/core-events/lucion_ai.cpp` |
| `0x10361150` | AI table (D2Game `+0x10e788`, 16-byte rows by monai row) | row 133 Ancient think | `0x1035cbc0` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x10361150` | AI table (D2Game `+0x10e788`, 16-byte rows by monai row) | row 135 BaalCrab think | `0x10357f00` | ported | `plugin/core-events/prime_minions.cpp` |
| `0x10361150` | AI table (D2Game `+0x10e788`, 16-byte rows by monai row) | row 141 BaalMinion think | `0x103580a0` | documented | [uber-bosses-dll](uber-bosses-dll.md) |
| `0x1036c320` | quest table 2 (D2Game `+0x10e050`) | 30 | `0x1038daf0` | documented | [map-system-dll](map-system-dll.md) |
| `0x1036c320` | quest table 2 (D2Game `+0x10e050`) | 35 | `0x1034bbe0` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x1036c320` | quest table 2 (D2Game `+0x10e050`) | 36 | `0x1034bc90` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x1036c320` | quest table 2 (D2Game `+0x10e050`) | 40 | `0x1034bce0` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x1036c320` | quest callbacks (D2Game `+0x107bb8`) | 4 | `0x103a94b0` | ported | `plugin/core-events/lucion_chests.cpp` |
| `0x1036c320` | quest callbacks (D2Game `+0x107bb8`) | 15 | `0x103a9670` | documented | [w37-server-rules](../research/pd2/w37-server-rules.md) |
| `0x1036c320` | quest callbacks (D2Game `+0x107bb8`) | 35 | `0x1034c340` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x1036c320` | quest callbacks (D2Game `+0x107bb8`) | 36 | `0x1034c480` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x1036c320` | quest callbacks (D2Game `+0x107bb8`) | 37 | `0x1034c5b0` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x1036c320` | quest callbacks (D2Game `+0x107bb8`) | 38 | `0x1034c280` | documented | [w37-server-rules](../research/pd2/w37-server-rules.md) |
| `0x1036c320` | quest callbacks (D2Game `+0x107bb8`) | 60 | `0x1034c6b0` | documented | [w37-server-rules](../research/pd2/w37-server-rules.md) |
| `0x1036c320` | quest callbacks (D2Game `+0x107bb8`) | 53 | `0x103aa800` | documented | [core-rules](../features/core-rules.md), [w37-server-rules](../research/pd2/w37-server-rules.md) |
| `0x1036c320` | quest callbacks (D2Game `+0x107bb8`) | 74 | `0x103aa9e0` | documented | [w37-server-rules](../research/pd2/w37-server-rules.md) |
| `0x1036c320` | quest callbacks (D2Game `+0x107bb8`) | 75 | `0x103aaa70` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x1036c320` | quest callbacks (D2Game `+0x107bb8`) | 76 | `0x103aab90` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x1036c320` | quest callbacks (D2Game `+0x107bb8`) | 77 | `0x103a9780` | ported | `plugin/core-events/rathma.h`, `plugin/core-events/uber_entry.cpp` |
| `0x1036c320` | quest callbacks (D2Game `+0x107bb8`) | 78 | `0x103aa270` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x1036c320` | quest callbacks (D2Game `+0x107bb8`) | 79 | `0x103aa460` | ported | `plugin/core-events/map_events.cpp` |
| `0x1036c320` | quest callbacks (D2Game `+0x107bb8`) | 80 | `0x103aa670` | documented | [deviations](../deviations.md), [map-system-dll](map-system-dll.md), [w37-server-rules](../research/pd2/w37-server-rules.md) |
| `0x103a1230` | shrine functions (D2Game `+0xfa5f8`, 12 bytes per shrine code) | 6 (+0) | `0x103a0de0` | documented | [core-rules](../features/core-rules.md), [w37-server-rules](../research/pd2/w37-server-rules.md) |
| `0x103a1230` | shrine functions (D2Game `+0xfa5f8`, 12 bytes per shrine code) | 7 (+0) | `0x103a0e80` | documented | [w37-server-rules](../research/pd2/w37-server-rules.md) |
| `0x103a1230` | shrine functions (D2Game `+0xfa5f8`, 12 bytes per shrine code) | 8 (+0) | `0x103a0de0` | documented | [core-rules](../features/core-rules.md), [w37-server-rules](../research/pd2/w37-server-rules.md) |
| `0x103a1230` | shrine functions (D2Game `+0xfa5f8`, 12 bytes per shrine code) | 9 (+0) | `0x103a0de0` | documented | [core-rules](../features/core-rules.md), [w37-server-rules](../research/pd2/w37-server-rules.md) |
| `0x103a1230` | shrine functions (D2Game `+0xfa5f8`, 12 bytes per shrine code) | 10 (+0) | `0x103a0de0` | documented | [core-rules](../features/core-rules.md), [w37-server-rules](../research/pd2/w37-server-rules.md) |
| `0x103a1230` | shrine functions (D2Game `+0xfa5f8`, 12 bytes per shrine code) | 11 (+0) | `0x103a0de0` | documented | [core-rules](../features/core-rules.md), [w37-server-rules](../research/pd2/w37-server-rules.md) |
| `0x103a1230` | shrine functions (D2Game `+0xfa5f8`, 12 bytes per shrine code) | 12 (+0) | `0x103a0de0` | documented | [core-rules](../features/core-rules.md), [w37-server-rules](../research/pd2/w37-server-rules.md) |
| `0x103a1230` | shrine functions (D2Game `+0xfa5f8`, 12 bytes per shrine code) | 13 (+0) | `0x103a0de0` | documented | [core-rules](../features/core-rules.md), [w37-server-rules](../research/pd2/w37-server-rules.md) |
| `0x103a1230` | shrine functions (D2Game `+0xfa5f8`, 12 bytes per shrine code) | 14 (+0) | `0x103a0f20` | documented | [w37-server-rules](../research/pd2/w37-server-rules.md) |
| `0x103a1230` | shrine functions (D2Game `+0xfa5f8`, 12 bytes per shrine code) | 15 (+0) | `0x103a0de0` | documented | [core-rules](../features/core-rules.md), [w37-server-rules](../research/pd2/w37-server-rules.md) |
| `0x103a1230` | shrine functions (D2Game `+0xfa5f8`, 12 bytes per shrine code) | 18 (+0) | `0x103a0fc0` | documented | [w37-server-rules](../research/pd2/w37-server-rules.md) |
| `0x1036b9f0` | item spells (D2Game `+0x107238`, 8 bytes {check, use}) | 2 (+4) | `0x10382f00` | documented | [w37-server-rules](../research/pd2/w37-server-rules.md) |
| `0x1036b9f0` | item spells (D2Game `+0x107238`, 8 bytes {check, use}) | 3 (+4) | `0x10382f90` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x1036b9f0` | item spells (D2Game `+0x107238`, 8 bytes {check, use}) | 5 (+4) | `0x10383010` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x1036b9f0` | item spells (D2Game `+0x107238`, 8 bytes {check, use}) | 12 (+4) | `0x10383060` | ported | `plugin/core-events/map_events.cpp`, `plugin/core-events/map_open.cpp`, `plugin/core-events/uber_entry.cpp` |
| `0x1036b9f0` | item spells (D2Game `+0x107238`, 8 bytes {check, use}) | 13 (+4) | `0x10383ea0` | ported | `plugin/core-rules/almanac.cpp` |
| `0x1036ba20` | cltdofunc | 8 | `0x103aaf10` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x1036ba20` | cltdofunc | 83 | `0x103ab070` | ported | `plugin/core-rules/skill_functions.cpp` |
| `0x1036ba20` | cltdofunc | 94 | `0x103ab180` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x1036ba20` | cltdofunc | 97 | `0x103ab280` | ported | `plugin/core-rules/skill_functions.cpp` |
| `0x1036ba20` | cltdofunc | 98 | `0x103ab470` | ported | `plugin/core-rules/skill_functions.cpp` |
| `0x1036ba20` | cltdofunc | 100 | `0x103ab580` | ported | `plugin/core-rules/skill_functions.cpp` |
| `0x1036ba20` | cltdofunc | 101 | `0x103ab630` | ported | `plugin/core-rules/skill_functions.cpp` |
| `0x1036ba20` | cltdofunc | 102 | `0x103ab760` | ported | `plugin/core-rules/skill_functions.cpp` |
| `0x1036ba20` | cltdofunc | 103 | `0x103ab7f0` | ported | ported by number ([#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) triage): aura client, cut: Left Click Auras companion |
| `0x1036ba20` | cltdofunc | 104 | `0x103ab800` | ported | ported by number ([#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) triage): core-rules.md rows (Find Potion/Item, Lightning Bolt/Fury, Split Throw) |
| `0x1036ba20` | cltdofunc | 105 | `0x103ab880` | ported | `plugin/core-rules/skill_functions.cpp` |
| `0x1036ba20` | cltdofunc | 106 | `0x103ab920` | ported | `plugin/core-rules/skill_functions.cpp` |
| `0x1036ba20` | cltdofunc | 107 | `0x103aba80` | ported | `plugin/core-rules/skill_functions.cpp` |
| `0x1036ba20` | cltdofunc | 108 | `0x103abb30` | ported | `plugin/core-rules/skill_functions.cpp` |
| `0x1036ba20` | cltdofunc | 109 | `0x103abba0` | ported | `plugin/core-rules/skill_functions.cpp` |
| `0x1036ba20` | cltdofunc | 110 | `0x103abbd0` | ported | `plugin/core-rules/skill_functions.cpp` |
| `0x1036ba20` | cltdofunc | 111 | `0x103abce0` | ported | `plugin/core-rules/skill_functions.cpp` |
| `0x1036ba20` | cltdofunc | 112 | `0x103abfb0` | ported | `plugin/core-rules/skill_functions.cpp` |
| `0x1036ba20` | cltdofunc | 113 | `0x103ac1e0` | ported | `plugin/core-rules/skill_functions.cpp` |
| `0x1036ba20` | cltdofunc | 114 | `0x103ac270` | ported | `plugin/core-rules/skill_functions.cpp` |
| `0x1036ba20` | cltdofunc | 115 | `0x103ac600` | ported | ported by number ([#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) triage): the Leap deviation |
| `0x1036ba20` | cltdofunc | 116 | `0x103ac7b0` | ported | ported by number ([#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) triage): lightning-javelins.md |
| `0x1036ba20` | cltdofunc | 117 | `0x103ac8b0` | ported | ported by number ([#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) triage): core-rules.md rows (Find Potion/Item, Lightning Bolt/Fury, Split Throw) |
| `0x1036ba20` | cltdofunc | 118 | `0x103ac910` | ported | ported by number ([#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) triage): core-rules.md rows (Find Potion/Item, Lightning Bolt/Fury, Split Throw) |
| `0x1036ba20` | cltdofunc | 119 | `0x103acb70` | ported | `plugin/core-rules/skill_functions.cpp` |
| `0x1036ba20` | srvdofunc | 4 | `0x103ae650` | missing | [#1873](https://github.com/synpoox/pd2-resurrected/issues/1873) |
| `0x1036ba20` | srvdofunc | 6 | `0x103ae7c0` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x1036ba20` | srvdofunc | 8 | `0x103ae930` | ported | ported by number ([#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) triage): core-rules router (Multiple Shot, Multiple Shot Proc, TraitorBossStunSpear) |
| `0x1036ba20` | srvdofunc | 23 | `0x103aebf0` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x1036ba20` | srvdofunc | 25 | `0x103aec40` | missing | [#1870](https://github.com/synpoox/pd2-resurrected/issues/1870) |
| `0x1036ba20` | srvdofunc | 39 | `0x103aee00` | ported | `plugin/core-rules/skill_functions.cpp` |
| `0x1036ba20` | srvdofunc | 44 | `0x103aee30` | missing | [#1869](https://github.com/synpoox/pd2-resurrected/issues/1869) |
| `0x1036ba20` | srvdofunc | 45 | `0x103af080` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x1036ba20` | srvdofunc | 46 | `0x103af1a0` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x1036ba20` | srvdofunc | 47 | `0x103af270` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x1036ba20` | srvdofunc | 48 | `0x103af2f0` | ported | ported by number ([#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) triage): W21 gate A (w32-blade-fury.md) |
| `0x1036ba20` | srvdofunc | 49 | `0x103af450` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x1036ba20` | srvdofunc | 50 | `0x103af640` | documented | [w29-assassin-finishers](../research/skills/w29-assassin-finishers.md) |
| `0x1036ba20` | srvdofunc | 51 | `0x103af800` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x1036ba20` | srvdofunc | 54 | `0x103afac0` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x1036ba20` | srvdofunc | 55 | `0x103afb40` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x1036ba20` | srvdofunc | 56 | `0x103afea0` | ported | ported by number ([#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) triage): W14's golem count rule, the Golem start deviation |
| `0x1036ba20` | srvdofunc | 57 | `0x103afee0` | ported | ported by number ([#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) triage): W14's golem count rule, the Golem start deviation |
| `0x1036ba20` | srvdofunc | 64 | `0x103aff30` | documented | [ir3-combat-stats](../research/items/ir3-combat-stats.md) |
| `0x1036ba20` | srvdofunc | 71 | `0x103b0080` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x1036ba20` | srvdofunc | 104 | `0x103b0210` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x1036ba20` | srvdofunc | 112 | `0x103b02d0` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x1036ba20` | srvdofunc | 113 | `0x103b0580` | documented | [core-rules](../features/core-rules.md), [w37-server-rules](../research/pd2/w37-server-rules.md) |
| `0x1036ba20` | srvdofunc | 114 | `0x103b06c0` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x1036ba20` | srvdofunc | 115 | `0x103b0930` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x1036ba20` | srvdofunc | 118 | `0x103b0a50` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x1036ba20` | srvdofunc | 119 | `0x103b0b90` | ported | ported by number ([#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) triage): druid-w24.md, skill_functions.cpp |
| `0x1036ba20` | srvdofunc | 139 | `0x103b0cf0` | ported | `plugin/core-rules/skill_functions.cpp` |
| `0x1036ba20` | srvdofunc | 143 | `0x103b0df0` | ported | `plugin/core-rules/skill_functions.cpp` |
| `0x1036ba20` | srvdofunc | 148 | `0x103b0f00` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x1036ba20` | srvdofunc | 150 | `0x103b1030` | ported | ported by number ([#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) triage): W19 and the Smite deviation |
| `0x1036ba20` | srvdofunc | 153 | `0x103b1300` | ported | `plugin/core-rules/skill_functions.cpp` |
| `0x1036ba20` | srvdofunc | 154 | `0x103b1590` | ported | `plugin/core-rules/skill_functions.cpp` |
| `0x1036ba20` | srvdofunc | 155 | `0x103b16d0` | ported | `plugin/core-rules/skill_functions.cpp` |
| `0x1036ba20` | srvdofunc | 156 | `0x103b1980` | ported | `plugin/core-rules/skill_functions.cpp` |
| `0x1036ba20` | srvdofunc | 157 | `0x103b1aa0` | ported | `plugin/core-rules/skill_functions.cpp` |
| `0x1036ba20` | srvdofunc | 158 | `0x103b1c10` | documented | [w10-barbarian](../research/skills/w10-barbarian.md) |
| `0x1036ba20` | srvdofunc | 159 | `0x103b1d00` | ported | `plugin/core-rules/skill_functions.cpp` |
| `0x1036ba20` | srvdofunc | 160 | `0x103b1dc0` | ported | `plugin/core-rules/skill_functions.cpp` |
| `0x1036ba20` | srvdofunc | 161 | `0x103b1fa0` | ported | `plugin/core-rules/skill_functions.cpp` |
| `0x1036ba20` | srvdofunc | 162 | `0x103b21e0` | ported | `plugin/core-rules/skill_functions.cpp` |
| `0x1036ba20` | srvdofunc | 163 | `0x103b2340` | ported | `plugin/core-rules/skill_functions.cpp` |
| `0x1036ba20` | srvdofunc | 164 | `0x103b25c0` | ported | `plugin/core-rules/skill_functions.cpp` |
| `0x1036ba20` | srvdofunc | 165 | `0x103b27c0` | ported | `plugin/core-rules/skill_functions.cpp` |
| `0x1036ba20` | srvdofunc | 166 | `0x103b29d0` | ported | `plugin/core-rules/skill_functions.cpp` |
| `0x1036ba20` | srvdofunc | 167 | `0x103b2b20` | ported | ported by number ([#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) triage): core-rules.md rows (Force Move, Find Potion/Item, Lightning Bolt/Fury, Split Throw) |
| `0x1036ba20` | srvdofunc | 168 | `0x103b2b30` | ported | ported by number ([#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) triage): core-rules.md rows (Force Move, Find Potion/Item, Lightning Bolt/Fury, Split Throw) |
| `0x1036ba20` | srvdofunc | 169 | `0x103b2bc0` | ported | `plugin/core-rules/skill_functions.cpp` |
| `0x1036ba20` | srvdofunc | 170 | `0x103b2c60` | ported | `plugin/core-rules/skill_functions.cpp` |
| `0x1036ba20` | srvdofunc | 171 | `0x103b30a0` | ported | `plugin/core-rules/skill_functions.cpp` |
| `0x1036ba20` | srvdofunc | 172 | `0x103b3360` | ported | `plugin/core-rules/skill_functions.cpp` |
| `0x1036ba20` | srvdofunc | 173 | `0x103b3520` | ported | `plugin/core-rules/skill_functions.cpp` |
| `0x1036ba20` | srvdofunc | 174 | `0x103b3570` | ported | `plugin/core-rules/skill_functions.cpp` |
| `0x1036ba20` | srvdofunc | 175 | `0x103b3ad0` | ported | `plugin/core-rules/skill_functions.cpp` |
| `0x1036ba20` | srvdofunc | 176 | `0x103b3cc0` | ported | `plugin/core-rules/skill_functions.cpp` |
| `0x1036ba20` | srvdofunc | 177 | `0x103b3e10` | ported | `plugin/core-rules/skill_functions.cpp` |
| `0x1036ba20` | srvdofunc | 178 | `0x103b4180` | ported | `plugin/core-rules/skill_functions.cpp` |
| `0x1036ba20` | srvdofunc | 179 | `0x103b42a0` | ported | `plugin/core-rules/skill_functions.cpp` |
| `0x1036ba20` | srvdofunc | 180 | `0x103b4870` | ported | `plugin/core-rules/skill_functions.cpp` |
| `0x1036ba20` | srvdofunc | 181 | `0x103b4cf0` | ported | `plugin/core-rules/skill_functions.cpp` |
| `0x1036ba20` | srvdofunc | 182 | `0x103b4e40` | ported | `plugin/core-rules/skill_functions.cpp` |
| `0x1036ba20` | srvdofunc | 183 | `0x103b5080` | ported | `plugin/core-rules/skill_functions.cpp` |
| `0x1036ba20` | srvdofunc | 184 | `0x103b5110` | ported | `plugin/core-rules/skill_functions.cpp` |
| `0x1036ba20` | srvdofunc | 185 | `0x103b52b0` | ported | `plugin/core-rules/skill_functions.cpp` |
| `0x1036ba20` | srvdofunc | 186 | `0x103b56d0` | ported | `plugin/core-rules/skill_functions.cpp` |
| `0x1036ba20` | srvdofunc | 187 | `0x103b58b0` | ported | ported by number ([#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) triage): lightning-javelins.md |
| `0x1036ba20` | srvdofunc | 188 | `0x103b59c0` | ported | ported by number ([#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) triage): core-rules.md rows (Force Move, Find Potion/Item, Lightning Bolt/Fury, Split Throw) |
| `0x1036ba20` | srvdofunc | 189 | `0x103b5a30` | ported | ported by number ([#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) triage): core-rules.md rows (Force Move, Find Potion/Item, Lightning Bolt/Fury, Split Throw) |
| `0x1036ba20` | srvstfunc | 10 | `0x103b5dd0` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x1036ba20` | srvstfunc | 27 | `0x103b5ed0` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x1036ba20` | srvstfunc | 28 | `0x103b6020` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x1036ba20` | srvstfunc | 29 | `0x103b61a0` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x1036ba20` | srvstfunc | 37 | `0x103b6310` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x1036ba20` | srvstfunc | 66 | `0x103b6460` | ported | `plugin/core-rules/skill_functions.cpp` |
| `0x1036ba20` | srvstfunc | 67 | `0x103b6520` | ported | `plugin/core-rules/skill_functions.cpp` |
| `0x1036ba20` | srvstfunc | 68 | `0x103b6690` | documented | [necromancer-w11](../research/skills/necromancer-w11.md) |
| `0x1036ba20` | srvstfunc | 69 | `0x103b2b20` | ported | ported by number ([#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) triage): core-rules.md rows (Force Move, Find Potion/Item, Split Throw) |
| `0x1036ba20` | srvstfunc | 70 | `0x103b66d0` | ported | ported by number ([#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) triage): core-rules.md rows (Force Move, Find Potion/Item, Split Throw) |
| `0x1036ba20` | srvstfunc | 71 | `0x103b6760` | ported | `plugin/core-rules/skill_functions.cpp` |
| `0x1036ba20` | srvstfunc | 72 | `0x103b67e0` | ported | `plugin/core-rules/skill_functions.cpp` |
| `0x1036ba20` | srvstfunc | 73 | `0x103b6990` | ported | `plugin/core-rules/skill_functions.cpp` |
| `0x1036ba20` | srvstfunc | 74 | `0x103b69d0` | documented | [necromancer-w14](../research/skills/necromancer-w14.md) |
| `0x1036ba20` | srvstfunc | 75 | `0x103b6a20` | ported | `plugin/core-rules/skill_functions.cpp` |
| `0x1036ba20` | srvstfunc | 76 | `0x103b6aa0` | ported | `plugin/core-rules/skill_functions.cpp` |
| `0x1036ba20` | srvstfunc | 77 | `0x103b6c80` | ported | `plugin/core-rules/skill_functions.cpp` |
| `0x1036ba20` | srvstfunc | 78 | `0x103b6cf0` | documented | [necromancer-w14](../research/skills/necromancer-w14.md), [w10-barbarian](../research/skills/w10-barbarian.md) |
| `0x1036ba20` | srvstfunc | 79 | `0x103b6eb0` | ported | ported by number ([#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) triage): core-rules.md rows (Force Move, Find Potion/Item, Split Throw) |
| `0x103ad290` | cltstfunc (D2Client `+0xde928`) | 9 | `0x103accc0` | ported | ported by number ([#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) triage): D2R's own |
| `0x103ad290` | cltstfunc (D2Client `+0xde928`) | 54 | `0x103acd00` | ported | ported by number ([#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) triage): aura client, cut: Left Click Auras companion |
| `0x103ad290` | cltstfunc (D2Client `+0xde928`) | 55 | `0x103acd30` | ported | `plugin/core-rules/skill_functions.cpp` |
| `0x103ad290` | cltstfunc (D2Client `+0xde928`) | 56 | `0x103acda0` | ported | `plugin/core-rules/skill_functions.cpp` |
| `0x103ad290` | cltstfunc (D2Client `+0xde928`) | 57 | `0x103ace50` | ported | `plugin/core-rules/skill_functions.cpp` |
| `0x103ad290` | cltstfunc (D2Client `+0xde928`) | 58 | `0x103ad040` | ported | ported by number ([#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) triage): core-rules.md (Lucion kit) |
| `0x103ae5d0` | D2Client function table (not identified) | 11 | `0x103ad2e0` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x103ae5d0` | D2Client function table (not identified) | 13 | `0x103ad6b0` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x103ae5d0` | D2Client function table (not identified) | 15 | `0x103ad7b0` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x103ae5d0` | D2Client function table (not identified) | 16 | `0x103ad9d0` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x103ae5d0` | D2Client function table (not identified) | 25 | `0x103ade80` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x103ae5d0` | D2Client function table (not identified) | 26 | `0x103ae000` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x103ae5d0` | D2Client function table (not identified) | 27 | `0x103ae140` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x103ae5d0` | D2Client function table (not identified) | 28 | `0x103ae420` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x1036c2b0` | skill and item event functions (auraeventfunc) | 3 | `0x10346020` | documented | [core-rules](../features/core-rules.md), [w28-sorceress-bolts](../research/skills/w28-sorceress-bolts.md) |
| `0x1036c2b0` | skill and item event functions (auraeventfunc) | 6 | `0x10346140` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x1036c2b0` | skill and item event functions (auraeventfunc) | 7 | `0x10346370` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x1036c2b0` | skill and item event functions (auraeventfunc) | 15 | `0x103463f0` | ported | `plugin/core-rules/item_rules.cpp` |
| `0x1036c2b0` | skill and item event functions (auraeventfunc) | 16 | `0x103469a0` | ported | `plugin/core-rules/item_rules.cpp`, `plugin/core-rules/skill_functions.cpp` |
| `0x1036c2b0` | skill and item event functions (auraeventfunc) | 19 | `0x10346df0` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x1036c2b0` | skill and item event functions (auraeventfunc) | 22 | `0x10346e60` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x1036c2b0` | skill and item event functions (auraeventfunc) | 23 | `0x10346f20` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x1036c2b0` | skill and item event functions (auraeventfunc) | 24 | `0x10347020` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x1036c2b0` | skill and item event functions (auraeventfunc) | 25 | `0x10347240` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x1036c2b0` | skill and item event functions (auraeventfunc) | 27 | `0x10347300` | ported | `plugin/core-rules/skill_functions.cpp` |
| `0x1036c2b0` | skill and item event functions (auraeventfunc) | 32 | `0x10347370` | ported | `plugin/core-rules/skill_functions.cpp` |
| `0x1036c2b0` | skill and item event functions (auraeventfunc) | 33 | `0x103473a0` | documented | [p12-ormus-robes](../research/items/p12-ormus-robes.md) |
| `0x103a9170` | missile srvdo | 1 | `0x103a75c0` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x103a9170` | missile srvdo | 6 | `0x103a7750` | ported | `plugin/core-rules/missile_functions.cpp` |
| `0x103a9170` | missile srvdo | 7 | `0x103a7840` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x103a9170` | missile srvdo | 30 | `0x103a7a50` | ported | `plugin/core-rules/missile_functions.cpp` |
| `0x103a9170` | missile srvdo | 34 | `0x103a7c70` | ported | `plugin/core-rules/missile_functions.cpp` |
| `0x103a9170` | missile srvdo | 50 | `0x103a8100` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x103a9170` | missile srvdo | 52 | `0x103a81c0` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x103a9170` | missile srvdo | 60 | `0x103a8360` | ported | `plugin/core-rules/missile_functions.cpp` |
| `0x103a9170` | missile srvdo | 61 | `0x103a84f0` | ported | `plugin/core-rules/missile_functions.cpp` |
| `0x103a9170` | missile srvdo | 62 | `0x103a86d0` | ported | `plugin/core-rules/missile_functions.cpp` |
| `0x103a9170` | missile srvdo | 63 | `0x103a8800` | documented | [w28-sorceress-bolts](../research/skills/w28-sorceress-bolts.md) |
| `0x103a9170` | missile srvdo | 64 | `0x103a8ca0` | ported | `plugin/core-rules/missile_functions.cpp` |
| `0x103a9170` | missile srvdo | 65 | `0x103a9020` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x103a7220` | missile srvhit | 15 | `0x103a6c90` | ported | `plugin/core-rules/missile_functions.cpp` |
| `0x103a7220` | missile srvhit | 16 | `0x103a6da0` | ported | `plugin/core-rules/missile_functions.cpp` |
| `0x103a7220` | missile srvhit | 17 | `0x103a6ea0` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x103a7220` | missile srvhit | 18 | `0x103a6fd0` | ported | `plugin/core-rules/missile_functions.cpp` |
| `0x103a7220` | missile srvhit | 19 | `0x103a7130` | ported | `plugin/core-rules/missile_functions.cpp` |
| `0x103a7460` | missile srvdmg | 38 | `0x103a7250` | documented | [w20-blessed-hammer](../research/skills/w20-blessed-hammer.md) |
| `0x103a7460` | missile srvdmg | 39 | `0x103a72d0` | documented | [w20-blessed-hammer](../research/skills/w20-blessed-hammer.md) |
| `0x103a7460` | missile srvdmg | 40 | `0x103a7410` | ported | `plugin/core-rules/missile_functions.cpp` |
| `0x103a6c50` | missile cltdo | 5 | `0x103a63c0` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x103a6c50` | missile cltdo | 6 | `0x103a6530` | ported | `plugin/core-rules/missile_functions.cpp` |
| `0x103a6c50` | missile cltdo | 65 | `0x103a6890` | ported | `plugin/core-rules/missile_functions.cpp` |
| `0x103a6c50` | missile cltdo | 66 | `0x103a69c0` | ported | `plugin/core-rules/missile_functions.cpp` |
| `0x103a6c50` | missile cltdo | 67 | `0x103a6aa0` | ported | `plugin/core-rules/missile_functions.cpp` |
| `0x103a6330` | missile clthit | 8 | `0x103a6030` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x103a6330` | missile clthit | 69 | `0x103a60d0` | ported | `plugin/core-rules/missile_functions.cpp` |
| `0x103a6330` | missile clthit | 70 | `0x103a61c0` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x103a6330` | missile clthit | 71 | `0x103a6220` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x103a6330` | missile clthit | 72 | `0x103a6300` | ported | `plugin/core-rules/missile_functions.cpp` |
| `0x103a13c0` | item property functions (D2Common) | 25 | `0x103a12a0` | ported | `plugin/core-rules/item_rules.cpp` |
| `0x10347970` | D2Game monster mode pairs (8 bytes {start, frame}) | 7 (+0) | `0x103478a0` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x10347970` | D2Game monster mode pairs (8 bytes {start, frame}) | 7 (+4) | `0x10347910` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x10347970` | D2Game monster mode pairs (8 bytes {start, frame}) | 8 (+0) | `0x103478a0` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x10347970` | D2Game monster mode pairs (8 bytes {start, frame}) | 8 (+4) | `0x10347910` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x10347970` | D2Game monster mode pairs (8 bytes {start, frame}) | 10 (+0) | `0x103478a0` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x10347970` | D2Game monster mode pairs (8 bytes {start, frame}) | 10 (+4) | `0x10347910` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x10347970` | D2Game monster mode pairs (8 bytes {start, frame}) | 11 (+0) | `0x103478a0` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x10347970` | D2Game monster mode pairs (8 bytes {start, frame}) | 11 (+4) | `0x10347910` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x10347970` | D2Game monster mode pairs (8 bytes {start, frame}) | 12 (+0) | `0x103478a0` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x10347970` | D2Game monster mode pairs (8 bytes {start, frame}) | 12 (+4) | `0x10347910` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x10347970` | D2Game monster mode pairs (8 bytes {start, frame}) | 13 (+0) | `0x103478a0` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x10347970` | D2Game monster mode pairs (8 bytes {start, frame}) | 13 (+4) | `0x10347910` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x10347970` | D2Game monster mode pairs (8 bytes {start, frame}) | 14 (+0) | `0x103478a0` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x10347970` | D2Game monster mode pairs (8 bytes {start, frame}) | 14 (+4) | `0x10347910` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x10347970` | D2Game monster mode pairs (8 bytes {start, frame}) | 15 (+0) | `0x103478a0` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x10347970` | D2Game monster mode pairs (8 bytes {start, frame}) | 15 (+4) | `0x10347910` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x10347970` | D2Game monster mode pairs (8 bytes {start, frame}) | 16 (+0) | `0x103478a0` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x10347970` | D2Game monster mode pairs (8 bytes {start, frame}) | 16 (+4) | `0x10347910` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x10347970` | D2Game monster mode pairs (8 bytes {start, frame}) | 18 (+0) | `0x103478a0` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x10347970` | D2Game monster mode pairs (8 bytes {start, frame}) | 18 (+4) | `0x10347910` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
| `0x1036c3f0` | D2Game function table (not identified) | 4 | `0x1038dac0` | unchecked | [#1849](https://github.com/synpoox/pd2-resurrected/issues/1849) |
