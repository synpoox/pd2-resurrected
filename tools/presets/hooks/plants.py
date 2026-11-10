"""A plant (tree, palm, bush, vine, foliage): left out of an indoor map when harvested from an outdoor stock room."""
import re

ORDER = 100
PLANT = re.compile(r"tree|palm|bush|vine|foliage|banyan")


def is_plant(e: dict) -> bool:
    return any(c["type"] in ("ModelDefinitionComponent", "PrefabPlacementDefinitionComponent")
               and PLANT.search((c.get("filename") or c.get("prefab") or "").rsplit("/", 1)[-1]) for c in e["components"])


def indoor(kit, src, ents) -> list:
    return [p for p in ents if src not in kit.outdoor or not is_plant(p[0])]
