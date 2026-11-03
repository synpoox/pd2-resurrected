"""W7 game check: nova and orb hits (missiles pSrvHitFunc 5, pCltHitFunc 4) spawn PD2's sub-missile ring.

No plugin: D2R's own hit functions 5/4 match PD2's (docs/research/nova-orb-hits.md), so the check counts the client
missiles one Thunder Storm bolt leaves behind, read from game memory (missile_probe.py).

Setup (Slot4-7, game closed): merge, then build and install the mod's data (install.ps1 puts the savepaths back)
  py -3 tools/pd2_merge.py
  powershell -File tools/install.ps1 -Game C:/D2RTestSlots/SlotN -Data .
  a Sorceress with Thunder Storm (class skill slot 21) and mana to spare, e.g.
  modkit save-set <save> level=30 maxmana=800 mana=800 --skill 21=20 --project .
Run: in the Blood Moor next to monsters, Thunder Storm on the right button, then
  py testing/game_checks.py run nova-orb --slot N --path <repo>/tests/game --module nova_orb_checks
"""
import csv
import os
import time

from game_checks import check
import missile_probe

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BOLT, NOVA = "thunderstorm1", "thunderstormnova"


def _missiles(path: str) -> list[list[str]]:
    with open(path, encoding="latin-1", newline="") as f:
        return list(csv.reader(f, delimiter="\t"))


def ring_size(hit_par: str) -> int:
    """Sub-missiles of hit 5/4: one every max(par, 1) of 64 directions (1.13c 0x3c700, D2R 0x45fd20 and 0x1ae5d0)."""
    return len(range(0, 64, max(int(hit_par or 0), 1)))


@check("thunder-storm-nova", tags=["nova-orb", "game"])
def thunder_storm_nova(ctx):
    """One Thunder Storm bolt hit leaves PD2's ring of thunderstormnova client missiles: 64 / max(cHitPar1, 1)."""
    pd2 = _missiles(os.path.join(ROOT, "pd2-data", "data", "global", "excel", "missiles.txt"))
    col = pd2[0].index("cHitPar1")
    expected = ring_size(next(r for r in pd2[1:] if r and r[0] == BOLT)[col])
    rows = [r[0] for r in _missiles(os.path.join(ROOT, "out", "data", "global", "excel", "missiles.txt"))[1:]]
    nova = rows.index(NOVA)

    ctx.ensure_running()
    game = missile_probe.Game(ctx.game.pids()[0])
    ctx.game.click(0.5, 0.45, "right")
    sizes = missile_probe.bursts(game, nova, 20)
    if not sizes:
        ctx.skip("no Thunder Storm strike in 20 s (stand next to monsters, Thunder Storm on the right button)")
    assert all(n == expected for n in sizes), f"nova rings {sizes}, PD2 makes {expected} per strike"
    return f"{len(sizes)} strike(s), {sizes[0]} {NOVA} each (PD2: {expected})"
