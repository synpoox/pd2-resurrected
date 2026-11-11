"""Void (#783): the per-tile kit leaves the tiles where legacy draws nothing bare (presets/void.py). The kit picks a stock
piece for any key it knows, and a PD2 floor tile that draws nothing may share its key with a D2R tile that does."""
from .. import void

ORDER = 250  # after the border pass sets the tiles the kit fills (200), before the kit places them (300)


def run(b) -> None:
    gone = void.of(b) - b.taken
    if gone:
        b.rest = {c: k for c, k in b.rest.items() if c not in gone}
