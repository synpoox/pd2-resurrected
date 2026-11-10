"""Discovery of the modules in a package (passes, hooks): no shared list to edit, so adding one touches no other PR's file."""
from __future__ import annotations

import importlib
import pkgutil
from types import ModuleType


def modules(package: ModuleType) -> list[ModuleType]:
    """The package's modules, by their `ORDER` number (an int each must declare) and then by name."""
    found = [importlib.import_module(f"{package.__name__}.{m.name}") for m in pkgutil.iter_modules(package.__path__)]
    for m in found:
        if not isinstance(getattr(m, "ORDER", None), int):
            raise TypeError(f"{m.__name__} declares no integer ORDER")
    return sorted(found, key=lambda m: (m.ORDER, m.__name__))
