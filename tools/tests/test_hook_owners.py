"""One owner per hook site, beside a standalone Cartographer (#945).

Cartographer (d2r-tools, not modified) installs inline hooks on the cube executor and on both item spell dispatchers.
Two plugins cannot own one entry (d2rAI-toolkit knowledge/engine-re.md), so PD2's plugins keep off those entries:
core-rules owns the cube executor and Cartographer's optional stamping part is turned off by a signatures.txt row;
Cartographer owns the item spell dispatchers and core-maps leaves them when it sits beside; the Almanac listens to item
interactions. CARTOGRAPHER_SRC (a d2r-tools plugins/cartographer/src checkout) adds a check against Cartographer's own
signatures; without it that test is skipped.
"""
import os
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PLUGIN = ROOT / "plugin"
ENGINE = (PLUGIN / "common" / "pd2_engine.h").read_text(encoding="utf-8")


def sources():
    return {p: p.read_text(encoding="utf-8") for d in ("core-rules", "core-maps", "core-ui") for p in (PLUGIN / d).rglob("*.cpp")}


def installs():
    """{rva constant: [files]} of every `InstallInlineHook(<constant>,` call in the PD2 plugins."""
    found = {}
    for path, text in sources().items():
        for name in re.findall(r"InstallInlineHook\(\s*(?:d2r::rva::)?(\w+)\s*,", text):
            found.setdefault(name, []).append(path.relative_to(PLUGIN).as_posix())
    return found


def entry(name):
    m = re.search(name + r"Entry\[\]\s*=\s*\{([^}]*)\}", ENGINE)
    return [int(b, 16) for b in re.findall(r"0x([0-9a-f]{2})", m.group(1))]


class HookOwners(unittest.TestCase):
    def test_every_hook_site_has_one_owner(self):
        for name, files in installs().items():
            self.assertEqual(len(files), 1, f"{name} is hooked in {files}")

    def test_cube_executor_is_core_rules(self):
        self.assertEqual(installs()["CubeExecutor"], ["core-rules/cube_rules.cpp"])

    def test_cube_search_keeps_the_desecration_trigger(self):
        self.assertEqual(installs()["CubeRecipeSearch"], ["core-maps/cube_ops.cpp"])
        self.assertIn("Desecrate(game, player)", (PLUGIN / "core-maps" / "cube_ops.cpp").read_text(encoding="utf-8"))

    def test_item_spell_hooks_yield_to_cartographer(self):
        spells = installs()
        self.assertEqual(spells["ItemSpellUse"], ["core-maps/map_open.cpp"])
        self.assertEqual(spells["ItemSpellCheck"], ["core-maps/map_open.cpp"])
        text = (PLUGIN / "core-maps" / "map_open.cpp").read_text(encoding="utf-8")
        self.assertIn("!g_yielded && context->InstallInlineHook(ItemSpellUse", text)
        self.assertIn("cartographer.dll", text)

    def test_yield_test_splits_the_path_on_a_backslash(self):
        # L"\/" is not an escape: MSVC reads it as "/" and a Windows plugin path never matched (the yield then
        # depended on load order)
        text = (PLUGIN / "core-maps" / "map_open.cpp").read_text(encoding="utf-8")
        self.assertIn('find_last_of(L"\\\\/")', text)

    def test_almanac_listens_to_inventory_and_cube_and_the_right_button(self):
        text = (PLUGIN / "core-rules" / "almanac.cpp").read_text(encoding="utf-8")
        self.assertIn(".containerMask = Boundary", text)
        self.assertIn("VK_RBUTTON", text)

    def test_almanac_is_an_interaction_listener_in_core_rules(self):
        self.assertFalse((PLUGIN / "core-maps" / "almanac.cpp").exists())
        text = (PLUGIN / "core-rules" / "almanac.cpp").read_text(encoding="utf-8")
        self.assertIn("registerListener", text)
        self.assertNotIn("InstallInlineHook", text)
        self.assertNotIn("UnlimitedIdentifySpell", (PLUGIN / "core-maps" / "map_open.cpp").read_text(encoding="utf-8"))

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
