"""#1055: CI runs unittest discover, which skips module-level test_ functions."""
import ast
import unittest
from pathlib import Path


class NoOrphanTestFunctions(unittest.TestCase):
    def test_no_module_level_test_functions(self):
        bad = []
        for f in sorted(Path(__file__).parent.glob("test_*.py")):
            tree = ast.parse(f.read_text(encoding="utf-8"))
            bad += [f"{f.name}:{n.name}" for n in tree.body
                    if isinstance(n, (ast.FunctionDef, ast.AsyncFunctionDef)) and n.name.startswith("test_")]
        self.assertEqual(bad, [], "wrap these in a unittest.TestCase so CI runs them")
