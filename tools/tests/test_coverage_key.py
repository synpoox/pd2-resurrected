"""A map's coverage score is kept as JSON plus files and keyed on the code the scorer runs (#870)."""
import ast
import sys
import tempfile
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

    def test_nothing_the_scorer_reaches_lives_in_a_left_out_module(self):
        """Follow every presets name the scorer's modules use through the helpers it calls, at any depth (#885): none may
        resolve to a def in GENERATOR."""
        tools = C.ROOT / "tools"
        entries = scorer_entries([p for p in pd2_cache.sources(C.run, C.GENERATOR)
                                  if p.parent == tools and p.name != "pd2_presets.py"],
                                 tools / "presets", "presets")
        self.assertGreater(len(entries), 5)
        self.assertEqual(reach_left_out(entries, tools / "presets", "presets", C.GENERATOR), [])

    def test_a_helper_two_levels_deep_that_reaches_the_generator_fails(self):
        with tempfile.TemporaryDirectory() as d:
            tools = Path(d)
            pkg = tools / "presets"
            pkg.mkdir()
            (pkg / "__init__.py").write_text("")
            (pkg / "apply.py").write_text("from .helpers import mid\n\ndef score_it():\n    return mid()\n")
            (pkg / "helpers.py").write_text("from .build import deep_build\n\ndef mid():\n    return low()\n\n"
                                            "def low():\n    return deep_build()\n")
            (pkg / "build.py").write_text("def deep_build():\n    return 1\n")
            (tools / "scorer.py").write_text("from presets.apply import score_it\n\ndef run():\n    return score_it()\n")
            entries = scorer_entries([tools / "scorer.py"], pkg, "presets")
            self.assertEqual(entries, [(pkg / "apply.py", "score_it")])
            bad = reach_left_out(entries, pkg, "presets", (pkg / "build.py",))
            self.assertEqual(len(bad), 1)
            self.assertIn("score_it -> mid -> low -> deep_build", bad[0])
            # the same chain with build.py on the scorer's side passes
            self.assertEqual(reach_left_out(entries, pkg, "presets", (pkg / "other.py",)), [])

    def test_a_module_reached_as_an_attribute_two_levels_deep_fails(self):
        with tempfile.TemporaryDirectory() as d:
            tools = Path(d)
            pkg = tools / "presets"
            pkg.mkdir()
            (pkg / "__init__.py").write_text("")
            (pkg / "apply.py").write_text("from . import helpers\n\ndef score_it():\n    return helpers.mid()\n")
            (pkg / "helpers.py").write_text("from . import build\n\ndef mid():\n    return build.deep_build()\n")
            (pkg / "build.py").write_text("def deep_build():\n    return 1\n")
            (tools / "scorer.py").write_text("import pd2_presets as presets\n\ndef run():\n    return presets.score_it()\n")
            (tools / "pd2_presets.py").write_text("from presets.apply import score_it\n")
            entries = scorer_entries([tools / "scorer.py"], pkg, "presets")
            self.assertEqual(len(reach_left_out(entries, pkg, "presets", (pkg / "build.py",))), 1)

    def test_a_scorer_module_importing_a_left_out_name_directly_fails(self):
        with tempfile.TemporaryDirectory() as d:
            tools = Path(d)
            pkg = tools / "presets"
            pkg.mkdir()
            (pkg / "build.py").write_text("def deep_build():\n    return 1\n")
            (tools / "scorer.py").write_text("from presets.build import deep_build\n")
            entries = scorer_entries([tools / "scorer.py"], pkg, "presets")
            self.assertEqual(len(reach_left_out(entries, pkg, "presets", (pkg / "build.py",))), 1)


def _module_file(base: Path, parts: list[str]) -> Path | None:
    p = base.joinpath(*parts)
    if p.with_suffix(".py").is_file():
        return p.with_suffix(".py")
    return p / "__init__.py" if (p / "__init__.py").is_file() else None


def _imports(path: Path, pkg: Path, pkg_name: str) -> tuple[dict, list]:
    """name -> (module file, original name; None for a module bound by name) for every import of the package's own
    modules in the file (inside a def too), and the modules of its star imports."""
    names, stars = {}, []
    for n in ast.walk(ast.parse(path.read_text("utf-8"))):
        if not isinstance(n, ast.ImportFrom):
            continue
        if n.level == 1:
            base, parts = path.parent, (n.module or "").split(".") if n.module else []
        elif n.level == 0 and n.module and n.module.split(".")[0] == pkg_name:
            base, parts = pkg.parent, n.module.split(".")
        else:
            continue
        mod = _module_file(base, parts) if parts else (base / "__init__.py")
        for a in n.names:
            if a.name == "*":
                if mod:
                    stars.append(mod)
                continue
            sub = _module_file(base, [*parts, a.name])
            names[a.asname or a.name] = (sub, None) if sub else (mod, a.name)
    return names, stars


def _top_level(tree: ast.Module) -> dict:
    nodes = {}
    for n in tree.body:
        if isinstance(n, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)):
            nodes[n.name] = n
        elif isinstance(n, (ast.Assign, ast.AnnAssign)):
            for t in (n.targets if isinstance(n, ast.Assign) else [n.target]):
                for x in ast.walk(t):
                    if isinstance(x, ast.Name):
                        nodes[x.id] = n
    return nodes


def scorer_entries(files: list[Path], pkg: Path, pkg_name: str) -> list[tuple[Path, str]]:
    """The (module file, name) pairs of the presets package that these scorer-side files use: `from presets.x import n`
    and attribute reads off the package (`presets.n`, `P.n`, bound by `import pd2_presets as ...`), which the package
    (pd2_presets re-exports its names) resolves."""
    out = set()
    for f in files:  # not the shim itself: its imports are its re-exports, resolved only where a file reads them
        tree = ast.parse(f.read_text("utf-8"))
        aliases = {"P"}
        for n in ast.walk(tree):
            if isinstance(n, ast.Import):
                aliases |= {a.asname or a.name for a in n.names if a.name in (pkg_name, f"pd2_{pkg_name}")}
        names, _ = _imports(f, pkg, pkg_name)
        shim = pkg.parent / f"pd2_{pkg_name}.py"
        reexport = _imports(shim, pkg, pkg_name) if shim.is_file() else ({}, [])
        for n in ast.walk(tree):
            if isinstance(n, ast.Attribute) and isinstance(n.value, ast.Name) and n.value.id in aliases:
                if n.attr in reexport[0] and reexport[0][n.attr][1]:
                    out.add(reexport[0][n.attr])
                else:
                    out.update((s, n.attr) for s in reexport[1] if n.attr in _top_level(ast.parse(s.read_text("utf-8")))
                               or n.attr in _imports(s, pkg, pkg_name)[0])
        out.update((m, o) for m, o in names.values() if m and o)
    return sorted(out, key=str)


def reach_left_out(entries: list[tuple[Path, str]], pkg: Path, pkg_name: str, left: tuple[Path, ...]) -> list[str]:
    """Follow each entry through the defs it calls, across modules and at any depth; one message (with the chain) for
    each chain that ends in a module left out of the key."""
    left = [Path(x).resolve() for x in left]

    def is_left(p: Path) -> bool:
        p = p.resolve()
        return any(p == g or g in p.parents for g in left)
    cache: dict = {}

    def parsed(p: Path):
        if p not in cache:
            cache[p] = (_top_level(ast.parse(p.read_text("utf-8"))), *_imports(p, pkg, pkg_name))
        return cache[p]
    def has(p: Path, name: str) -> bool:
        return name in parsed(p)[0] or name in parsed(p)[1]
    bad, seen = [], set()
    todo = [(m, n, (n,)) for m, n in entries]
    while todo:
        mod, name, chain = todo.pop()
        if (mod, name) in seen:
            continue
        seen.add((mod, name))
        if is_left(mod):
            bad.append(f"{name} in {mod.relative_to(pkg.parent).as_posix()} is left out of the key; reached: "
                       + " -> ".join(chain))
            continue
        defs, imps, stars = parsed(mod)
        if name in defs:
            for x in ast.walk(defs[name]):
                if isinstance(x, ast.Attribute) and isinstance(x.value, ast.Name) and x.value.id in imps \
                        and imps[x.value.id][1] is None:
                    todo.append((imps[x.value.id][0], x.attr, (*chain, x.attr)))
                elif isinstance(x, ast.Name) and x.id != name:
                    if x.id in defs:
                        todo.append((mod, x.id, (*chain, x.id)))
                    elif x.id in imps and imps[x.id][1]:
                        todo.append((imps[x.id][0], imps[x.id][1], (*chain, x.id)))
                    else:
                        todo.extend((s, x.id, (*chain, x.id)) for s in stars if has(s, x.id))
        elif name in imps and imps[name][1]:
            todo.append((imps[name][0], imps[name][1], chain))
        elif name not in imps:
            todo.extend((s, name, chain) for s in stars if has(s, name))
    return sorted(bad)


if __name__ == "__main__":
    unittest.main()
