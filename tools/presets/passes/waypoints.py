"""One stock waypoint per waypoint object of the DS1."""
from ..waypoints import place_waypoints

ORDER = 600


def run(b) -> None:
    placed, used = place_waypoints(b.lay, b.kit, b.seed, b.objects, b.taken)
    for src in sorted(used):
        b.depend(b.kit.sources[src])
    b.entities += placed
