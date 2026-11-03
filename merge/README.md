# merge/

The committed rules `tools/pd2_merge.py` applies to the extracted PD2 data (`pd2-data/`) to write `generated/data/`
(ADR 0002): id remaps, event names, function-number rewrites, D2R-only rows kept or off. Names and numbers only, never PD2
row data. Every id the port adds is assigned here once and is append-only (ADR 0003).

Each file is read by the rule kind its name maps to in `pd2_merge.RULES`; a file with no rule kind fails the merge.

- `ids-<table>.tsv`: append-only id lists (`pd2_ids`) for the tables a save or D2R's code reads by row number: the P2
  skill, state, stat and missile tables, and the item types, unique and set items, runewords and affixes.
- `item-tables.txt`: the item tables merged by key or row with no id list, and the item types and codes that never drop
  (`pd2_items`).
