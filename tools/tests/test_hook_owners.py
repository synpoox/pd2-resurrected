"""One owner per hook site, beside a standalone Cartographer (#945).

Cartographer (d2r-tools, not modified) installs inline hooks on the cube executor and on both item spell dispatchers.
Two plugins cannot own one entry (d2rAI-toolkit knowledge/engine-re.md), so PD2's plugins keep off those entries:
core-rules owns the cube executor and Cartographer's optional stamping part is turned off by a signatures.txt row;
Cartographer owns the item spell dispatchers and core-events hooks neither (#964); the Almanac writes use slot 14 of the item spell
handler table (no hook, no listener). CARTOGRAPHER_SRC (a d2r-tools plugins/cartographer/src checkout) adds a check against Cartographer's own
signatures; without it that test is skipped.
"""
import os
import re
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PLUGIN = ROOT / "plugin"
sys.path.insert(0, str(ROOT / "tools"))
import pd2_ids  # noqa: E402

# Engine facts live in our header and (the shared ones, e.g. CubeExecutorEntry) in the toolkit's rvas_93847 headers (#976).
try:
    TOOLKIT_RVAS = pd2_ids.toolkit(ROOT) / "shared" / "plugin-common" / "src" / "rvas_93847"
except ValueError:  # no toolkit: the tests that read it skip
    TOOLKIT_RVAS = ROOT / "no-toolkit"
ENGINE = (PLUGIN / "common" / "pd2_engine.h").read_text(encoding="utf-8") + "".join(
    p.read_text(encoding="utf-8") for p in TOOLKIT_RVAS.glob("*.h")
)


def sources():
    return {p: p.read_text(encoding="utf-8") for d in ("core-rules", "core-events", "core-ui") for p in (PLUGIN / d).rglob("*.cpp")}


def installs():
    """{rva constant: [files]} of every `InstallInlineHook(<constant>,` call in the PD2 plugins."""
    found = {}
    for path, text in sources().items():
        for name in re.findall(r"InstallInlineHook\(\s*(?:d2r::rva::)?(\w+)\s*,", text):
            found.setdefault(name, []).append(path.relative_to(PLUGIN).as_posix())
    return found


def entry(name):
    m = re.search(name + r"Entry\[\]\s*=\s*\{([^}]*)\}", ENGINE)
    assert m, f"{name}Entry not found in pd2_engine.h or the toolkit's rvas_93847 headers ({TOOLKIT_RVAS})"
    return [int(b, 16) for b in re.findall(r"0x([0-9a-f]{2})", m.group(1))]


class HookOwners(unittest.TestCase):
    def test_every_hook_site_has_one_owner(self):
        for name, files in installs().items():
            self.assertEqual(len(files), 1, f"{name} is hooked in {files}")

    def test_cube_executor_is_core_rules(self):
        self.assertEqual(installs()["CubeExecutor"], ["core-rules/cube_rules.cpp"])

    def test_cube_search_keeps_the_desecration_trigger(self):
        self.assertEqual(installs()["CubeRecipeSearch"], ["core-events/cube_ops.cpp"])
        self.assertIn("Desecrate(game, player)", (PLUGIN / "core-events" / "cube_ops.cpp").read_text(encoding="utf-8"))

    def test_core_events_leaves_the_item_spell_dispatchers_to_cartographer(self):
        # PD2's map item spell is cut (#964): no PD2 plugin hooks either dispatcher, so Cartographer owns both
        spells = installs()
        self.assertNotIn("ItemSpellUse", spells)
        self.assertNotIn("ItemSpellCheck", spells)
        for path, text in sources().items():
            self.assertNotIn("cartographer.dll", text, path.name)

    def test_almanac_is_a_use_slot_write_not_a_hook_or_a_listener(self):
        # A right-click raises no ItemInteraction event (Slot4, #975) and Cartographer owns both dispatchers, so the
        # Almanac is a write of item spell use slot 14, which entry 18 names (item_rules.cpp)
        self.assertFalse((PLUGIN / "core-events" / "almanac.cpp").exists())
        text = (PLUGIN / "core-rules" / "almanac.cpp").read_text(encoding="utf-8")
        self.assertIn("PatchWriteU64(ItemSpellUseTable", text)
        self.assertNotIn("InstallInlineHook", text)
        self.assertNotIn("registerListener", text)
        self.assertNotIn("almanac-diag", text)
        self.assertNotIn("UnlimitedIdentifySpell", (PLUGIN / "core-events" / "map_open.cpp").read_text(encoding="utf-8"))

    def test_almanac_use_slot_matches_entry_18_and_stays_in_inventory_only(self):
        engine = (PLUGIN / "common" / "pd2_engine.h").read_text(encoding="utf-8")
        self.assertIn("AlmanacUseSlot = 14", engine)
        rules = (PLUGIN / "core-rules" / "item_rules.cpp").read_text(encoding="utf-8")
        self.assertIn("{UnlimitedPortalCheck, UnlimitedPortalUse, 1, 0, AlmanacUseSlot, 1}", rules)
        text = (PLUGIN / "core-rules" / "almanac.cpp").read_text(encoding="utf-8")
        self.assertIn("if (where == 0) {", text)
        self.assertNotIn("where == 0 || where == d2r::rva::CubePageCube", text)

    @unittest.skipUnless(TOOLKIT_RVAS.is_dir(), "d2rAI-toolkit not found (set D2R_TOOLKIT)")
    def test_signatures_row_turns_off_only_cartographers_cube_stamping(self):
        rows = [r.split("\t") for r in (ROOT / "companions" / "cartographer" / "signatures.txt").read_text(encoding="utf-8").splitlines()]
        self.assertEqual(rows[0], ["Part", "Signature"])
        self.assertEqual([r[0] for r in rows[1:]], ["CubeExec"])
        want = entry("CubeExecutor")
        bytes_ = [int(b, 16) for b in rows[1][1].split()]
        # Cartographer requires an override to keep the built-in entry bytes (the hook is written for them)...
        self.assertEqual(bytes_[:len(want)], want)
        # ...and the tail matches no code, so the part finds nothing and stays off
        self.assertNotEqual(bytes_[len(want):len(want) + 5], [0xB8, 0xF8, 0x16, 0x00, 0x00])

    @unittest.skipUnless(os.environ.get("CARTOGRAPHER_SRC"), "set CARTOGRAPHER_SRC to Cartographer's src folder")
    def test_cartographers_entries_are_ours(self):
        sigs = (Path(os.environ["CARTOGRAPHER_SRC"]) / "core" / "part_signatures.h").read_text(encoding="utf-8")
        for part, name in (("CubeExec", "CubeExecutor"), ("ItemSpellUse", "ItemSpellUse"), ("ItemSpellCheck", "ItemSpellCheck")):
            sig = re.search(part + r'\s*=\s*"([^"]+)"', sigs).group(1).split()
            want = entry(name)
            self.assertEqual([int(b, 16) for b in sig[:len(want)]], want, part)


if __name__ == "__main__":
    unittest.main()
