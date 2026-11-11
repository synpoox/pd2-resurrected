"""presets: passes and hooks are discovered by their ORDER, with no shared list."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from presets import discover, hooks, passes  # noqa: E402


class DiscoveryTests(unittest.TestCase):
    def test_every_pass_declares_an_order_and_a_run(self):
        mods = passes.ordered()
        self.assertTrue(mods)
        for m in mods:
            self.assertIsInstance(m.ORDER, int, m.__name__)
            self.assertTrue(callable(m.run), m.__name__)
        self.assertEqual([m.ORDER for m in mods], sorted(m.ORDER for m in mods))

    def test_the_passes_run_in_the_orders_build_needs(self):
        names = [m.__name__.rsplit(".", 1)[1] for m in passes.ordered()]
        before = [("paste", "border"), ("border", "tiles"), ("tiles", "trees"), ("trees", "loose"),
                  ("loose", "waypoints"), ("waypoints", "streams"), ("streams", "ground")]
        for a, b in before:
            self.assertLess(names.index(a), names.index(b))

    def test_every_hook_declares_an_order_and_a_known_function(self):
        known = {"tile", "locate", "drop", "settle", "indoor"}
        for m in discover.modules(hooks):
            self.assertIsInstance(m.ORDER, int, m.__name__)
            self.assertTrue(known & set(vars(m)), m.__name__)

    def test_hooks_each_lists_the_functions_by_name_in_order(self):
        self.assertEqual([f.__module__.rsplit(".", 1)[1] for f in hooks.each("drop")], ["far", "blockers", "structure", "tall"])


if __name__ == "__main__":
    unittest.main()
