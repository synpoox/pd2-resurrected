"""Stand-ins: unbuilt engine rules point at the closest D2R function (W3, core-rules).

merge/standins.tsv has one line per row: `table <tab> row <tab> from <tab> columns <tab> rule`. It sets the listed
function columns of a merged row (its mod name) from `from`: `=` is D2R's own row at that id, a name is that D2R row,
`-` blanks them. `rule` names the core-rules row it stands in for; each is a "not yet" row on docs/deviations.md. Delete
a row's line when its rule is built. d2rAI-toolkit's shared/classic_import standins applies them; PD2_ONLY (the function
numbers a PD2 row must not keep) is the mod's.
"""
from __future__ import annotations

from pathlib import Path

import pd2_table  # noqa: F401  (first: puts the toolkit on sys.path)
from shared.classic_import import standins
from shared.classic_import.standins import nums

# Function numbers PD2 rows must not keep: D2R's slot is empty, past the table's end, or holds a different function
# (core-rules hook list; table sizes from docs/research/skill-function-tables.md). Not listed: numbers equal to D2R's
# (srvdo 41/143, cltdo 15/83), built rules (srvdo 189, W1; srvdo 188 and cltdo 117 become the missile columns,
# pd2_port.MISSILE_FUNCS, W5; shit 63, cltdo 22 patched, W6; shit 61, W11; srvdo 182 / cltdo 111, W21; srvdo 176,
# chit 66, W17; cltdo 119, W22; srvdo 174 and 162, W18; chit 67, W30; srvdo 175, cltdo 110, srvst 77, W24; shit 62,
# W28; srvdo 178, W31; sdo 40 / cdo 72 move to 47 / 80, W20, like sdo 38 / cdo 70 (W2, with cltdo 118, srvst 79 and shit 65); shit 34, chit 6, dmg 19, W32;
# srvst 73 and 75, W19; srvst 71, #471; srvdo 173, W36; srvdo 177, #469; cltdo 113/114, #488; srvdo 179 and dmg 18, UB5; shit 64, UB4; auraeventfunc 16 is D2R's crushing blow, Smite's roll is on srvdo 150, #165), `verify` slots that hold D2R's
# same-numbered function, which is the stand-in (shit 5, 30; chit 4, 12), the monster kits' cdo 12 (D2R's is 1.13c's
# screen shake, its length an approximated row) and chit 11 (empty in both games, W36), and temp1's sdo 39 / cdo 71 (no row
# reaches it). Cube ops 30/31 are not here: cubemain rows have no unique name, so the item tables' merge (P3) keeps
# those recipes off.
PD2_ONLY = {
    "skills": {
        "srvdofunc": nums(109, 153, 155, 156, range(159, 162), 164, 165, 167, range(169, 171), 181, range(184, 187)),
        "cltdofunc": nums(97, range(100, 104), 105, 109),
        "srvstfunc": nums(64, range(67, 70), 74, 76),
        "cltstfunc": nums(54, 82),
        **{f"srvprgfunc{i}": nums(171, 172) for i in (1, 2, 3)},
        **{f"cltprgfunc{i}": nums(106, 107) for i in (1, 2, 3)},
    },
    "missiles": {
        "pSrvDoFunc": nums(38, 40),
        "pCltDoFunc": nums(range(69, 71), 72),
        "pSrvHitFunc": nums(60) | {"*12"},
        "pCltHitFunc": nums(65) | {"*16"},  # *12/*16: PD2 text that parses as no function
        "pSrvDmgFunc": nums(15, 17),
    },
    "misc": {"pSpell": nums(13)},  # PD2's map spell 12 moves to 16, which core-events installs (MS2)
    "properties": {f"func{i}": nums(25) for i in range(1, 8)},
    "states": {"remfunc": nums(12)},
    "objects": {"OperateFn": nums(range(75, 77), range(78, 80))},  # PD2's altars and tomes (LP1)
}


def apply(rule: Path, pd2_data: Path, d2r_data: Path, out_data: Path) -> None:
    """pd2_merge rule kind for merge/standins.tsv: edit the tables the id and item rules wrote, then check them."""
    standins.apply(rule, pd2_data, d2r_data, out_data, PD2_ONLY)
