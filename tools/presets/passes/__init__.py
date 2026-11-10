"""The passes build() runs over a preset, in ORDER. A pass is a module here with an integer `ORDER` and `run(b)`, where
`b` is the Build (presets/build.py); it is found by name, so adding a pass edits no shared list. Keep ORDER numbers
apart by tens or hundreds so a new pass fits between two."""
from __future__ import annotations

import sys
from types import ModuleType

from .. import discover


def ordered() -> list[ModuleType]:
    return discover.modules(sys.modules[__name__])
