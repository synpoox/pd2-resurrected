"""The D2R/PD2 table reader, on its own so a rule that only reads tables does not take pd2_ids (the id lists, their
constants and the merge) into its cache key (#808): a change to pd2_ids rebuilt the presets rule, which reads only these."""
from __future__ import annotations

import functools
import importlib.util
import os
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


KEY = {"itemtypes": "Code", "weapons": "code", "armor": "code", "misc": "code", "gamble": "code",
       "qualityitems": "mod1code"}  # when not the first (PD2's qualityitems starts with nummods)


class IdError(Exception):
    pass


class Table:
    def __init__(self, path: Path, key: str | None = None):
        text = path.read_bytes().decode("latin-1")  # keeps CRLF, which read_text would turn into LF
        self.newline = "\r\n" if "\r\n" in text else "\n"
        lines = text.split(self.newline)
        while lines and not lines[-1]:
            lines.pop()
        self.header = lines[0].split("\t")
        self.rows = [(line.split("\t") + [""] * len(self.header))[:len(self.header)] for line in lines[1:]]
        self.key = self.header.index(key) if key else 0

    def records(self) -> list[int]:
        """Row positions of the records, numbered as the game numbers them: an `Expansion` row (first column) is not one."""
        return [i for i, r in enumerate(self.rows) if r[0] != "Expansion"]

    def names(self) -> list[str]:
        return [self.rows[i][self.key] for i in self.records()]

    def text(self) -> str:
        return self.newline.join("\t".join(r) for r in [self.header, *self.rows]) + self.newline


def excel(root: Path) -> Path:
    return root / "global" / "excel"


def load(data: Path, table: str) -> Table:
    return Table(excel(data) / f"{table}.txt", KEY.get(table))
