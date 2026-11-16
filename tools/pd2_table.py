"""The toolkit's table reader (shared/classic_import ids) for the tools, and the one place that finds the toolkit, sets its
environment names and D2R's data root. A rule that only reads tables imports this, not pd2_port (the mod's data), so a
change to that data does not rebuild it (#808)."""
from __future__ import annotations

import functools
import importlib.util
import os
import sys
from pathlib import Path


HERE = Path(__file__).resolve().parent


@functools.cache
def toolkit(root: Path) -> Path:
    """The d2rAI-toolkit folder, from the toolkit's own finder (tools/toolkit_path.py: D2R_TOOLKIT, else a walk up from
    the start folder, which works from agent worktrees; a toolkit.version that differs from the toolkit's HEAD warns,
    and fails under D2R_TOOLKIT_STRICT=1). Starts at root, else at this file (a scratch root has no toolkit above
    it). ValueError when there is none. Loading the finder needs the toolkit's path first: D2R_TOOLKIT or the first
    d2rAI-toolkit up the tree gives that, and the finder decides."""
    given = os.environ.get("D2R_TOOLKIT")
    for start in (root, HERE):
        kit = Path(given) if given else next((p / "d2rAI-toolkit" for p in (start, *start.parents)
                                              if (p / "d2rAI-toolkit" / "tools" / "toolkit_path.py").is_file()), None)
        finder = kit and kit / "tools" / "toolkit_path.py"
        if not finder or not finder.is_file():
            if given:
                raise ValueError(f"D2R_TOOLKIT={given} has no tools/toolkit_path.py (a toolkit older than the pin?)")
            continue
        spec = importlib.util.spec_from_file_location("toolkit_path", finder)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        try:
            return mod.find_toolkit(start)
        except mod.ToolkitNotFound:
            continue
        except mod.PinMismatch as e:
            raise ValueError(str(e)) from None
    raise ValueError(f"d2rAI-toolkit not found from {root}: set D2R_TOOLKIT or put d2rAI-toolkit beside the project")


try:
    import shared.classic_import  # noqa: F401  (PYTHONPATH has the toolkit)
except ImportError:
    sys.path.insert(0, str(toolkit(HERE.parent)))
# static, so a rule's cache key follows the toolkit module it reads (cache.toolkit_sources)
from shared.classic_import import cache  # noqa: E402
from shared.classic_import.ids import KEY, IdError, Table, excel, ident, load, read_list, write  # noqa: E402,F401

# The toolkit's cache reads MODKIT_*; this project's PD2R_* names (agents' and the runner's settings) map onto them, and
# the machine-wide folder is the default cache root, as it was before the toolkit's cache.
for _new, _old in (("MODKIT_SHARED_CACHE", "PD2R_CACHE"), ("MODKIT_CI_SCOPE", "PD2R_CI_SCOPE"),
                   ("MODKIT_MERGE_SLOTS", "PD2R_MERGE_SLOTS"), ("MODKIT_CI_RESERVED", "PD2R_CI_RESERVED"),
                   ("MODKIT_CACHE_GB", "PD2R_CACHE_GB")):
    if _old in os.environ:
        os.environ.setdefault(_new, os.environ[_old])
if Path(r"C:\pd2r-cache").is_dir():
    os.environ.setdefault("MODKIT_SHARED_CACHE", r"C:\pd2r-cache")
# what a rule reads that is not its code: the extract and the tools folder's other files are not in a rule's key
cache.NOT_TRACED = ("pd2-data", "tools")

# the build the port targets, in modkit's cache (MODKIT_CACHE, as modkit reads it)
D2R_DATA = Path(os.environ.get("MODKIT_CACHE") or toolkit(HERE.parent) / "d2r-data") / "93847" / "data"
