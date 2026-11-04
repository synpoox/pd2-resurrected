# merge/

The committed rules `tools/pd2_merge.py` applies to the extracted PD2 data (`pd2-data/`) to write `generated/data/`
(ADR 0002): id remaps, event names, function-number rewrites, D2R-only rows kept or off. Names and numbers only, never PD2
row data. Every id the port adds is assigned here once and is append-only (ADR 0003).

Each file is read by the rule kind its name maps to in `pd2_merge.RULES`; a file with no rule kind fails the merge.
Column-level rules (references, formulas, derived columns, function-number rewrites) are constants in
`tools/pd2_ids.py` (REFS, FORMULAS, DERIVED, FUNCS).

| File | Rule kind |
|---|---|
| `strings.json` | `tools/pd2_strings.py`: PD2's `patchstring.tbl` into complete D2R string files (decision #29). `python tools/pd2_strings.py --assign` appends newly referenced keys with IDs from 50,000; never edit or reuse an ID. The 20 changed guild, ladder and arena labels are left out (cut features). `ModStr5q`, `StrSkill110` and `StrSkill76` keep D2R's text: PD2's drops the `%d` D2R fills in. `Runeword171PD2` carries PD2's Runeword171 (Dominion); `runes.txt` must name it |
| `ids-<table>.tsv` | `tools/pd2_ids.py`: append-only id lists for the tables a save or D2R's code reads by row number: the P2 skill, state, stat and missile tables, and the item types, unique and set items, runewords and affixes |
| `item-tables.txt` | `tools/pd2_items.py`: the item tables merged by key or row with no id list, and the item types and codes that never drop |
| `tables.json` | `tools/pd2_tables.py`: the tables with no id list that the merge takes over from D2R, each with how its rows pair (name, position or key columns), whether PD2-only rows append or are left out, whether a repeated name takes PD2's whole group, and the columns that keep D2R's values. A table not named here ships as D2R's; `docs/unported.md` lists why |
| `missiles-hd.tsv` | `tools/pd2_missiles_hd.py`, after `ids-missiles.tsv`: writes `data/hd/missiles/missiles.json`, D2R's map plus one entry per missile PD2 adds, taking the model of the D2R missile with the same `CelFile`. Each line names the D2R missile (`like`) for a new missile with PD2-only art; a new missile with neither fails the merge. Temporary: the models of every `rathma*` and `lucion*` missile (hand-picked or by CelFile) are stand-ins until PD2's art is merged in |
| `standins.tsv` | `tools/pd2_standins.py`, after the id rules: each line points a merged skill, missile, item or property row's function columns at a D2R row (`=` its own, a name, `-` blank) until the core-rules rule is built. The merge fails if a PD2 row keeps a function number D2R lacks (`PD2_ONLY`). Every line's rule is on `docs/unported.md` (a row, or an exact data row). When a rule is built, delete its lines, its row and its numbers in `PD2_ONLY` |
