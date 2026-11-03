# merge/

The committed rules `tools/pd2_merge.py` applies to the extracted PD2 data (`pd2-data/`) to write `generated/data/`
(ADR 0002): id remaps, event names, function-number rewrites, D2R-only rows kept or off. Names and numbers only, never PD2
row data. Every id the port adds is assigned here once and is append-only (ADR 0003).

Each file is read by the rule kind its name maps to in `pd2_merge.RULES`; a file with no rule kind fails the merge.

| File | Rule kind |
|---|---|
| `strings.json` | `tools/pd2_strings.py`: PD2's `patchstring.tbl` into complete D2R string files (decision #29). `python tools/pd2_strings.py --assign` appends newly referenced keys with IDs from 50,000; never edit or reuse an ID. The 20 changed guild, ladder and arena labels are left out (cut features). `ModStr5q`, `StrSkill110` and `StrSkill76` keep D2R's text: PD2's drops the `%d` D2R fills in. `Runeword171PD2` carries PD2's Runeword171 (Dominion); `runes.txt` must name it |
| `standins.tsv` | `tools/pd2_standins.py`, after the id rules: each line points a merged skill, missile, item or property row's function columns at a D2R row (`=` its own, a name, `-` blank) until the core-rules rule is built. The merge fails if a PD2 row keeps a function number D2R lacks (`PD2_ONLY`). Every line's rule is a row on `docs/unported.md`; delete the line when the rule is built |
