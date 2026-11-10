"""Harvest the single-layer LIKE keys of the `alike` DT1s, which like() lends to tiles the kit has no key for."""
from ..consts import LIKE

ORDER = 100


def tile(lay, key, alike) -> bool:
    return len(key[1]) == 1 and bool(key[1][0]) and key[1][0][1] == LIKE and key[1][0][0] in alike
