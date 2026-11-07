# PD2-origin bugs

Bugs that exist in PD2 itself (its tables, strings or `ProjectDiablo.dll`) and that we fixed in the port instead of
copying them. Each fix is a deliberate deviation from the PD2 baseline, so it is listed here rather than in
[deviations.md](deviations.md).

A bug belongs here only when it shows on live PD2 too. A bug our port brought in (a D2R string that doesn't fit a PD2
column, say) is an ordinary `bug`.

## How to record one

1. Label the issue `pd2-origin` (with `bug`). Say in the body how it shows on live PD2.
2. The PR that fixes it links the issue as usual (`Closes #<n>`).
3. Add a row below in that PR.

List them with `gh issue list --label pd2-origin --state all`.

| Bug | What PD2 does | Our fix | Issue | PR |
|---|---|---|---|---|
| Blade Fury fragment direction | A fragment's direction is a signed `% 64` (`0x103a7c70`, `0x103a6530`): when the offset takes it below direction 0 the index is negative and reads before 1.13c's 64-direction tables (the Y table's tail as x, the bytes before it as y), so the fragment flies along a short, wrong vector, roughly west where it should leave near east | the index wraps to 0-63 | [#107](https://github.com/synpoox/pd2-resurrected/issues/107) | [#164](https://github.com/synpoox/pd2-resurrected/pull/164) |
| Frostwarded and Winterlord's transform colour | magicprefix's `transformcolor` is `dblue`, which colors.txt does not have (Dark Blue is `dblu`); 1.13c reads the column as a code lookup into colors (D2Common `0x6fdc7f00`), so the affix gives no dark blue tint | `dblu` (`pd2_ids.FIXES`) | [#141](https://github.com/synpoox/pd2-resurrected/issues/141) | [#172](https://github.com/synpoox/pd2-resurrected/pull/172) |
| of Anima on amulets | magicsuffix's `itype4` is `amu`, which itemtypes.txt does not have (amulets are `amul`); the lookup finds no item type, so the suffix never rolls on amulets | `amul` (`pd2_ids.FIXES`) | [#141](https://github.com/synpoox/pd2-resurrected/issues/141) | [#172](https://github.com/synpoox/pd2-resurrected/pull/172) |
| Antidote and Thawing Potion description | misc's `spelldesc` 1 names `ItemStatsAnti` / `ItemStatsThaw`, which no string table has; 1.13c's key lookup falls back to string 5382 (D2Common `0x6fdae350`), so the potions show an "An evil force" line | no description line, as D2R's own rows (`pd2_ids.FIXES`) | [#141](https://github.com/synpoox/pd2-resurrected/issues/141) | [#172](https://github.com/synpoox/pd2-resurrected/pull/172) |
| Mesa Map name | misc's `namestr` is `t99`, which no string table has, so the (unspawnable) map is named "An evil force" | `t99` reads "Mesa Map" (`merge/strings.json` texts) | [#141](https://github.com/synpoox/pd2-resurrected/issues/141) | [#172](https://github.com/synpoox/pd2-resurrected/pull/172) |
