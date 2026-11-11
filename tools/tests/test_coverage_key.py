"""A map's coverage score is kept as JSON plus files and keyed on the code the scorer runs (#870)."""
import ast
import importlib
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import pd2_cache  # noqa: E402
import pd2_coverage as C  # noqa: E402
import pd2_presets as P  # noqa: E402


class KeptScoreTests(unittest.TestCase):
    def test_a_score_round_trips_through_json(self):
        ts = C.TypeScore(n=3, missing={(1, 2)}, holes={(0, 0), (3, 4)}, culprits={"a/b": {(1, 1)}}, piled=2, scored=False)
        s = C.Score("m", "Area", 5, {(1, 1)}, ring=frozenset({(2, 2)}), gaps=frozenset(), size=9, fidelity={"floor": ts})
        got, imgs = C.unpack(C.pack(s, {"m_walls.png": b"\x89PNG"}))
        self.assertEqual(got, s)
        self.assertEqual(imgs, {"m_walls.png": b"\x89PNG"})
        self.assertIs(type(got.ring), frozenset)
        self.assertEqual(C.pack(got, imgs), C.pack(s, imgs), "the same bytes again")

    def test_a_score_file_cannot_make_an_object(self):
        with self.assertRaises(KeyError):
            C.unpack({"score.json": b'{"@":"os.system","v":{}}'})

    def test_the_key_leaves_out_the_generator_but_not_the_scorers_modules(self):
        names = {p.relative_to(C.ROOT).as_posix() for p in pd2_cache.sources(C.run, C.GENERATOR)}
        for gen in ("tools/presets/build.py", "tools/presets/kit.py", "tools/presets/passes/edges_walls.py",
                    "tools/presets/hooks/far.py", "tools/pd2_cache.py"):
            self.assertNotIn(gen, names)
        for used in ("tools/presets/layout.py", "tools/presets/dt1.py", "tools/presets/apply.py", "tools/pd2_tiles.py",
                     "tools/pd2_painter.py", "tools/pd2_fidelity.py", "tools/pd2_coverage.py"):
            self.assertIn(used, names)

    def test_each_presets_module_is_the_scorers_or_the_generators(self):
        """A new module of the presets package picks a side: GENERATOR (left out of the score key) or SCORER_PRESETS."""
        home = C.ROOT / "tools" / "presets"
        have = {p.name for p in home.iterdir() if p.suffix == ".py" or (p / "__init__.py").is_file()}
        gen = {p.name for p in C.GENERATOR if p.parent == home}
        self.assertFalse(gen & set(C.SCORER_PRESETS))
        self.assertEqual(have, gen | set(C.SCORER_PRESETS), "add the new module to GENERATOR or SCORER_PRESETS")

    def test_nothing_the_scorer_uses_lives_in_a_left_out_module(self):
        """The scorer reaches the presets package as pd2_presets.<name>: each name it uses must be defined outside GENERATOR
        and must not use, in its own module, a name that module imports from a left-out one."""
        tools = C.ROOT / "tools"
        used = set()
        for mod in ("pd2_coverage", "pd2_fidelity", "pd2_tiles", "pd2_painter"):
            for n in ast.walk(ast.parse((tools / f"{mod}.py").read_text("utf-8"))):
                if isinstance(n, ast.Attribute) and isinstance(n.value, ast.Name) and n.value.id in ("presets", "P"):
                    used.add(n.attr)
        left = {Path(p).resolve() for p in C.GENERATOR}

        def left_out(path: Path) -> bool:
            path = path.resolve()
            return path in left or any(g in path.parents for g in left)
        checked = 0
        for name in sorted(used):
            obj = getattr(P, name, None)
            modname = getattr(obj, "__module__", None)
            if not modname or not modname.startswith("presets."):
                continue
            mod = importlib.import_module(modname)
            self.assertFalse(left_out(Path(mod.__file__)), f"presets.{name} is defined in {modname}, left out of the key")
            tree = ast.parse(Path(mod.__file__).read_text("utf-8"))
            foreign = set()
            for n in ast.walk(tree):
                if isinstance(n, ast.ImportFrom) and n.level == 1:
                    base = Path(mod.__file__).parent / (n.module or "").replace(".", "/")
                    for a in n.names:
                        if any(left_out(q) for q in (base, base.with_suffix(".py"), base / a.name, base / f"{a.name}.py")):
                            foreign.add(a.asname or a.name)
            node = next((n for n in tree.body if getattr(n, "name", None) == name), None)
            if node is not None:
                called = {x.id for x in ast.walk(node) if isinstance(x, ast.Name)}
                self.assertFalse(called & foreign, f"presets.{name} uses {called & foreign} from the generator")
            checked += 1
        self.assertGreater(checked, 5)


if __name__ == "__main__":
    unittest.main()
