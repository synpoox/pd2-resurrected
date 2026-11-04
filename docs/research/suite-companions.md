# RuffnecKk's suite plugins as item-rule companions

Resolves research ticket [#189](https://github.com/synpoox/pd2-resurrected/issues/189) (map
[#143](https://github.com/synpoox/pd2-resurrected/issues/143), decided in #158, [item-rules.md](../item-rules.md)). It covers
findings only. No plugin is bundled and no code is changed here; that is IR2.

## Answer in brief

| Plugin (source version) | Runs next to core-rules? | Closes | Differs from PD2 |
|---|---|---|---|
| Cast Triggers (1.1.12) | **No, while P12's hook is in.** Yes once IR2 removes it | skill on block; skill on cast | on-cast trigger and aim (approximated, as P12's is) |
| Ethereal Item Rules (1.0.3) | yes, no shared hook | set items rolling ethereal | none expected (chance kept at D2R's 5%) |
| Item Durability | yes, no shared hook | ethereal items keeping full durability | none at `max_durability_percent = 100` |
| Progressive Affixes | yes, no shared hook | rare affix minimum, rare jewels always 4 | the 4/5/6 split above the minimum is a guess until IR8 reads PD2's roll |
| Mass Identify | yes, no shared hook | shift-click identify with a Tome of Identify | not the Almanac (it only takes `ibk`) |
| Vendor Stock Refresh | yes, no shared hook | nothing | not needed: D2R already has the gamble refresh button |

Three findings change rows on main:

- **The gamble refresh button is D2R's own.** D2R's vendor panel has `button_refresh` (`PANEL\Vendors\Gambling_Refresh_Button`,
  `VendorPanelMessage:RefreshAll`) in `global/ui/layouts/controller/vendorpanellayouthd.json`, and Vendor Stock Refresh
  says "Gambling keeps its original refresh behavior". So it is not a deviation, and the "cut" row added in #191 should go.
- **PD2's ethereal bonus is 25%, not D2R's 50%.** No plugin covers it, and core-rules has no row for it (below).
- **PD2's weapons lose durability at half the rate** (wiki). Item Durability's resistance applies to every normal item,
  armor included, so it can only approximate this. It needs a row.

## Sources and method

| Source | Used for |
|---|---|
| https://github.com/RuffDood/RuffnecKk-D2RLoader-Suite at `6dc77f8` (2026-11-04), MIT | each plugin's hooks (`*Rva` constants and `InstallInlineHook` calls), config and rules |
| `plugin/core-rules/*.cpp`, `plugin/common/rvas_93847.h` | what core-rules hooks |
| PD2 Ghidra project (`ProjectDiablo.dll`) | PD2's ethereal rule |
| [PD2 wiki, General Changes](https://wiki.projectdiablo2.com/wiki/General_Changes) | PD2's other item rules |
| D2R 93847 data (`d2rAI-toolkit/d2r-data/93847`) | D2R's vendor panel |
| `generated/data/global/excel/itemstatcost.txt` | the merged stat ids |

No agent ran these in the game. The coexistence result comes from reading both plugins' hook sites; the owner
verifies the plugins in the game, and IR2's game check proves the bundle.

## 1. Hooks: core-rules against the suite

core-rules installs four inline hooks: `SrvSkillDispatch` `0x43acb0` (P12's skill on cast, `item_rules.cpp`), `QueueHit`
`0x44b600` (Smite), `BusyModeGate` `0x42d840` and `HitRecoveryGate` `0x42d700`. The rest are function-table slot writes
(skill, missile, aura event and property tables). It does not hook item event 16 (`0x583150`): W19's Smite moved to
`QueueHit`, so the overlap #158 feared there does not exist.

| Plugin | Inline hooks (93847) | Shared with core-rules |
|---|---|---|
| Cast Triggers | skill handler `0x43acb0`, damage builder `0x44c030`+30, player skill input `0x4fdb40` / `0x4f8de0`, item skill casts `0x5896e0`+24 / `0x589820`+22, stat event dispatch `0x44d570`, item events 15 / 16 / 20 (`0x584170`, `0x583150`, `0x583b30`), finalized damage report `0x451fb0`, damage copy/move/destroy, path points, D2RCore's wide event dispatch (pinned to Core 1.3.1), client proc aim | **`0x43acb0`** |
| Ethereal Item Rules | item generation `0x4432da`-`0x4434df`, cave `0x46d840` | none |
| Item Durability | durability update `0x441b10`, items.txt compile `0x315fd0`, ethereal maximum `0x44351f` | none |
| Progressive Affixes | magic `0x442c60`, crafted `0x58a120`, rare `0x58bba0` generators | none |
| Mass Identify | client packet queue and the targeting worker (client), identify `0x46e8c0` | none |
| Vendor Stock Refresh | vendor panel (client) and its layout files | none |

**`0x43acb0` is a hard clash.** Before it hooks a site, Cast Triggers checks that the site's entry bytes are vanilla.
If they are not, it accepts only one known owner (`celestialrayone.skill-srcdam-calc`) and otherwise logs "differs from
vanilla and has no complete tracked owner proof; plugin refused" and turns itself off (`ClassifyComposableSite`). If
core-rules hooks first, Cast Triggers refuses. If Cast Triggers hooks first, core-rules' entry-byte check fails
(`dospellcast skill on cast FAILED (bytes differ)`) and P12's skill on cast is off. Either way only one works. IR2's plan
(remove P12's listener walk and keep Ormus' property function) leaves no shared site.

## 2. Cast Triggers

A trigger is an itemstatcost row with `itemevent1` `doactive` and `itemeventfunc1` 20 (D2R's own skill-on-event
function). Cast Triggers raises the event at its trigger and lets through only the stats mapped to that trigger. The
item's stat value, skill and level work as for any D2R chance-to-cast stat (`tools/build-test-fixture.cjs`, `upsertItemStat`).

- **Skill on block:** its test fixture names the stat `item_skillonblock`, as PD2 does. The merged row (id 379) has
  PD2's `doblock` / 30. IR2 sets `doactive` / 20 and `[on_block] stat_id = 379`. A block requires a real shield block
  or Weapon Block. Expected Exact; the game check compares one proc's chance and level.
- **Skill on cast:** the merged `item_skilloncast` (378) has PD2's `dospellcast` / 33. IR2 sets `doactive` / 20 and
  leaves `[on_cast]` at all eligible non-channelled skills. Two things differ from PD2's event 33 (p12-ormus-robes):
  - PD2 casts at the caster's aim point for a skill whose ItemTarget is 5 (Nova, Frost Nova) and holds the event-cast
    flag so a proc never procs. D2R's func 20 and Cast Triggers' own proc aim do this their way.
  - Which skills count is Cast Triggers' rule: channelled skills are left out and have their own trigger.
  It stays an approximated row, replacing P12's.
- **Crit, crushing blow, open wounds, attack attempt, kill and channel triggers** stay unmapped (0). The crit trigger
  reads D2R's crit (`passive_critical_strike`, 337), so a cast-on-crit later must agree with IR3's crit rework.

## 3. Ethereal: PD2's rule from the DLL

`ProjectDiablo.dll` `0x2fb710` (`D2COMMON_10890_ITEMMODS_ApplyEthereal_INTERCEPT`) replaces 1.13c's ApplyEthereal. It
sets the ethereal flag, then multiplies by 5/4: armor class (stat 31) on armor, and min/max damage (21-24) and missile
min/max (159, 160) on weapons. It never touches durability. So PD2's ethereal items have **a 25% bonus and full
durability**.

- **Item Durability** with `[ethereal] max_durability_percent = 100` gives `(max * 100 + 50) / 100` = the normal
  maximum. That is Exact. Leave everything else vanilla.
- **Ethereal Item Rules** with `[generation] enabled = true`, `chance_percent = 5` (D2R's) and `allow_set_items = true`
  lets set items roll ethereal at the same chance as other qualities, as the wiki says. The chance PD2 uses for set
  items is not traced. Expected Exact.
- **The 25% bonus** is in neither plugin. D2R gives 50%, and PD2 rebalanced its exceptional and elite weapons for 25%
  (wiki), so today the port's ethereal items are too strong. This needs a new core-rules row (IR8).

## 4. Rare affixes

The wiki says rares get at least 4 affixes at item level 45+, 5 at 65+ and 6 at 85+, and rare jewels always get 4.
Progressive Affixes sets the whole 3/4/5/6 split per item-level band, so it can match PD2 exactly once IR8 reads PD2's
roll. The minimums alone do not give the split above them. Its config for PD2:

- `[rare_jewels]`: every band `[0, 100]`.
- `[regular_rare_items]`: bands 1/45/65/85 with PD2's split (the plugin's defaults have the same minimums, but the split
  is the author's).
- `[magic]`: thresholds 255, so no magic item is forced to two affixes (PD2 has no such rule).
- `[crafted]`: D2R's own split. PD2's crafted rule is not checked.

Staffmods with no upper item level are not covered: the plugin "does not change which affixes are eligible". That stays
IR8's.

## 5. Identify

Mass Identify identifies the inventory (and optionally the cube and stashes) on a shift-right-click of a Tome of
Identify (`ibk` only), one charge per item unless `freeIdentification` is on. That is the wiki's "shortcut to identify
items with Tome of Identify (shift+click)", with the default config. It is approximated until a game check compares the
click and the charges with PD2. The Horadrim Almanac (`rid`, PD2's unlimited book identifying everything at once) is
not covered and stays IR5's.

## Rows to change on main

- Remove the unported row "Gamble screen refresh button" (cut): it is D2R's own feature.
- Add core-rules and unported rows: "Ethereal bonus 25% (PD2's ApplyEthereal `0x2fb710`)", IR8; "Weapons lose durability
  at half the rate (wiki)", IR8 or IR2 approximated.
- Skill on cast's approximated row moves from P12's trigger to Cast Triggers' (IR2).
