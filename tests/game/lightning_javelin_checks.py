"""W5 game check: Lightning Bolt and Lightning Fury (PD2 srvdo 188, cltdo 117) throw PD2's missile once per cast.

No plugin: PD2's 188/117 only create srvmissilea / cltmissilea, which D2R's do dispatcher creates from srvmissile /
cltmissile after any do function, so the merge moves the missile there (docs/research/lightning-javelins.md). The check
counts the client missiles one cast makes, read from game memory (missile_probe.py).

Setup (Slot4-7, game closed): install the fixture, the merged rows' function and missile columns on D2R's own tables,
cast in town allowed (the full merged data does not pass modkit validation yet); install.ps1 puts the savepaths back
  powershell -File tools/install.ps1 -Game C:/D2RTestSlots/SlotN -Data tests/game/fixtures/lightning-javelin
  an Amazon holding javelins with Lightning Bolt (class skill slot 14) and Lightning Fury (slot 29), e.g.
  modkit save-set <save> level=30 --skill 14=1 --skill 29=1 --project tests/game/fixtures/lightning-javelin
Run: in town, Lightning Bolt (then Lightning Fury) on the right button, then
  py testing/game_checks.py --path <repo>/tests/game --module lightning_javelin_checks run lightning-bolt --slot N (then lightning-fury)
"""
import csv
import os

from game_checks import check
import missile_probe

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
FIXTURE = os.path.join("tests", "game", "fixtures", "lightning-javelin", "out")


def _table(name: str, base: str) -> list[list[str]]:
    with open(os.path.join(ROOT, base, "data", "global", "excel", f"{name}.txt"), encoding="latin-1", newline="") as f:
        return list(csv.reader(f, delimiter="\t"))


def pd2_missile(skill: str) -> str:
    """The client missile PD2's cltdo 117 creates for `skill`: cltmissile, else cltmissilea (ProjectDiablo 0x3ac8b0)."""
    rows = _table("skills", "pd2-data")
    h = rows[0]
    row = next(r for r in rows[1:] if r and r[0] == skill)
    return row[h.index("cltmissile")] or row[h.index("cltmissilea")]


def _one_per_cast(ctx, skill: str) -> str:
    missile = pd2_missile(skill)
    row = [r[0] for r in _table("missiles", FIXTURE)[1:]].index(missile)
    ctx.ensure_running()
    game = missile_probe.Game(ctx.game.pids()[0])
    ctx.game.click(0.6, 0.45, "right")
    sizes = missile_probe.bursts(game, row, 3)
    if not sizes:
        ctx.skip(f"no {missile} in 3 s (javelins in hand, {skill} on the right button)")
    assert sizes == [1], f"one cast made {sizes} {missile}, PD2 makes 1"
    return f"one cast, 1 {missile} (PD2: 1)"


@check("lightning-bolt", tags=["lightning-bolt", "game"])
def lightning_bolt(ctx):
    """One Lightning Bolt cast throws one lightningjavelin, PD2's cltmissilea."""
    return _one_per_cast(ctx, "Lightning Bolt")


@check("lightning-fury", tags=["lightning-fury", "game"])
def lightning_fury(ctx):
    """One Lightning Fury cast throws one lightningfury, PD2's cltmissilea."""
    return _one_per_cast(ctx, "Lightning Fury")
