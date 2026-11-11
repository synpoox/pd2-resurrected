"""The D2R/PD2 table reader, on its own so a rule that only reads tables does not take pd2_ids (the id lists, their
constants and the merge) into its cache key (#808): a change to pd2_ids rebuilt the presets rule, which reads only these."""
from __future__ import annotations

from pathlib import Path


def toolkit(root: Path) -> Path:
    """The nearest d2rAI-toolkit beside root or one of its parents (an agent worktree sits deep inside the main
    checkout), else the one beside root."""
    return next((p / "d2rAI-toolkit" for p in root.parents if (p / "d2rAI-toolkit" / "modkit").is_dir()),
                root.parent / "d2rAI-toolkit")


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
