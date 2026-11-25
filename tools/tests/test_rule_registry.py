"""Every core-rules skill rule registers itself with a unique order (#2060): a new skill/<rule>.cpp changes no shared file."""
import re
import unittest
from pathlib import Path

SKILL = Path(__file__).resolve().parents[2] / "plugin" / "core-rules" / "skill"
INSTALL = re.compile(r"^auto (Install\w+)\(const D2RL::PluginContext\*", re.M)
RULE = re.compile(r"^PD2R_RULE(?:_N)?\((\w+),\s*(-?\d+)", re.M)


class RuleRegistry(unittest.TestCase):
    def test_every_install_is_registered_with_a_unique_order(self):
        orders = {}
        for path in sorted(SKILL.glob("*.cpp")):
            text = path.read_text(encoding="utf-8")
            installs = INSTALL.findall(text)
            registered = {name: int(order) for name, order in RULE.findall(text)}
            self.assertEqual(sorted(installs), sorted(registered), f"{path.name}: PD2R_RULE per Install function")
            for name, order in registered.items():
                self.assertNotIn(order, orders, f"{name} and {orders.get(order)} share order {order}")
                orders[order] = name
        self.assertTrue(orders)

    def test_no_shared_list_names_a_registered_rule(self):
        names = [n for path in SKILL.glob("*.cpp") for n, _ in RULE.findall(path.read_text(encoding="utf-8"))]
        for shared in (SKILL.parent / "skill_functions.cpp", SKILL / "common.h"):
            text = shared.read_text(encoding="utf-8")
            for name in names:
                self.assertNotIn(name + "(", text, f"{shared.name} still names {name}")

    def test_cmake_lists_skill_sources_only_by_glob(self):
        lines = [line for line in (SKILL.parents[1] / "CMakeLists.txt").read_text(encoding="utf-8").splitlines()
                 if "core-rules/skill/" in line and not line.lstrip().startswith("#")]
        self.assertEqual(len(lines), 1, lines)
        self.assertIn("GLOB", lines[0])


if __name__ == "__main__":
    unittest.main()
