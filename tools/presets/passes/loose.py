"""Join the per-tile pieces (and the painter's trees) to the preset's entities."""

ORDER = 500


def run(b) -> None:
    b.entities += b.loose
