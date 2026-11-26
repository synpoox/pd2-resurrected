"""D2RLoader flushes each plugin line to disk (~5 ms) on the game thread, so no per-event line stays (#2101)."""
import re
import unittest
from pathlib import Path

PLUGIN = Path(__file__).resolve().parent.parent.parent / "plugin"


def bodies(text: str, name: str) -> list[str]:
    """Each top-level definition of `name` (its lines from the signature to the closing brace)."""
    out = []
    for m in re.finditer(rf"^(?:auto|void)\s+{name}\(", text, re.M):
        i = text.index("{", m.end())
        depth = 0
        for j in range(i, len(text)):
            depth += {"{": 1, "}": -1}.get(text[j], 0)
            if depth == 0:
                out.append(text[m.start():j + 1])
                break
    return out


class NoHotLogs(unittest.TestCase):
    def test_sampling_helpers_are_gone(self):
        """#2101: the sampled hot-path lines and their PD2R_HOT_LOGS switch were removed, not kept for game checks."""
        self.assertFalse((PLUGIN / "common/hot_log.h").exists())
        for f in PLUGIN.rglob("*"):
            if f.suffix in (".cpp", ".h") and "build" not in f.parts:
                self.assertNotRegex(f.read_text(encoding="utf-8", errors="replace"),
                                    r"HotSample|HotLogsOn|PD2R_HOT_LOGS", f.name)


class StashAffinityLogs(unittest.TestCase):
    def test_retry_path_lines_are_log_once(self):
        """#2094: Route, MoveTo and MapRefusal run every tidy pass, so each log line sits under a LogOnce `.First(` guard."""
        text = "".join(f.read_text(encoding="utf-8") for f in (PLUGIN / "core-ui/stash_affinity_parts").glob("*.cpp"))
        for fn in ("Route", "MoveTo", "MapRefusal"):
            lines = bodies(text, fn)[0].splitlines()
            calls = [i for i, line in enumerate(lines) if "plog::" in line]
            for i in calls:
                window = "\n".join(lines[max(0, i - 2):i + 1])
                self.assertRegex(window, r"\.First\(|\.exchange\(true\)",
                                 f"{fn}: unguarded log line {lines[i].strip()}")


if __name__ == "__main__":
    unittest.main()
