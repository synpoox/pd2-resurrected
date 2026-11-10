"""The per-tile pieces: the patch lookup first, then the plain kit for the tiles it did not cover."""
from ..kit import indoor
from ..layout import Layout, draws_trees
from ..patches import patches
from ..place import place

ORDER = 300


def run(b) -> None:
    lay, kit = b.lay, b.kit
    found, pkit = patches(lay, b.stock, set(b.rest), kit.far)
    if not draws_trees(lay):
        pkit = indoor(pkit)
    b.loose, used, _ = place(found, pkit, b.seed, b.edge, b.near, b.far)
    for src in sorted(used):
        b.depend(pkit.sources[src])
    more, used, b.missing = place(Layout(lay.width, lay.height, {c: k for c, k in b.rest.items() if c not in found.keys},
                                         lay.flags),
                                  kit, b.seed, b.edge, b.near, b.far)
    for src in sorted(used):
        b.depend(kit.sources[src])
    b.loose += more
