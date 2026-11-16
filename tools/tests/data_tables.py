"""The built tables the tests read (#1103): files/data once it is committed (Part 2 of #1100), else a local out/data."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
if (ROOT / "files/data/global/excel/levels.txt").exists():
    DATA = ROOT / "files/data"
else:
    DATA = ROOT / "out/data" if (ROOT / "out/data/global/excel/levels.txt").exists() else None


def need(testcase):
    if DATA is None:
        testcase.skipTest("no built tables: files/data (Part 2 of #1100) or a local out/data")


def text(rel):
    return (DATA / rel).read_text(encoding="latin-1")


def rows(table):
    lines = text(f"global/excel/{table}.txt").split("\n")
    head = lines[0].rstrip("\r").split("\t")
    return [dict(zip(head, ln.rstrip("\r").split("\t"))) for ln in lines[1:] if ln.strip()]
