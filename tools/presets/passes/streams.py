"""Stock liquid planes (water, lava, swamp) over the liquid DT1s' blocked floor outside the pasted rooms. Sets the
liquid mask (presets/liquids.py), which #738's edges read with `liquids.of(b)`."""
from .. import liquids
from ..water import streams

ORDER = 700


def run(b) -> None:
    for e, of in streams(b.lay, b.water, b.taken, liquids.of(b)):
        b.entities.append(e)
        b.depend(of)
