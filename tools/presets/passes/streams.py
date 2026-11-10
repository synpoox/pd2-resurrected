"""Stock water planes over the water DT1s' blocked floor outside the pasted rooms."""
from ..water import streams

ORDER = 700


def run(b) -> None:
    for e, of in streams(b.lay, b.water, b.taken):
        b.entities.append(e)
        b.depend(of)
