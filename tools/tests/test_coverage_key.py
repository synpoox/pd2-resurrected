"""A map's coverage score is kept as JSON plus files and keyed on the code the scorer runs (#870)."""
import ast
import contextlib
import io
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

    def test_an_outdated_score_file_fails_to_unpack_with_an_error_run_catches(self):
        for bad in (b'{"@":"Gone","v":{}}', b'not json', b'{"@":"Score","v":{"old":1}}'):
            with self.assertRaises((KeyError, ValueError, TypeError)):
                C.unpack({"score.json": bad})

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

    def test_the_dynamic_imports_of_presets_modules_are_flagged_or_absent(self):
        files = [p for p in pd2_cache.sources(C.run, C.GENERATOR) if p.suffix == ".py"]
        self.assertEqual(dynamic_uses(files, "presets"), [])

    def _tree(self, d: str, scorer: str, extra: dict | None = None) -> tuple[Path, Path]:
        tools = Path(d)
        pkg = tools / "presets"
        pkg.mkdir()
        (pkg / "__init__.py").write_text("")
        (pkg / "apply.py").write_text("def score_it():\n    return 1\n")
        (pkg / "build.py").write_text("def deep_build():\n    return 1\n")
        (tools / "pd2_presets.py").write_text("from presets.apply import score_it\nfrom presets.build import deep_build\n")
        (tools / "scorer.py").write_text(scorer)
        for name, text in (extra or {}).items():
            (pkg / name).write_text(text)
        return tools, pkg

    def _bad(self, scorer: str, extra: dict | None = None) -> list[str]:
        with tempfile.TemporaryDirectory() as d:
            tools, pkg = self._tree(d, scorer, extra)
            entries = scorer_entries([tools / "scorer.py"], pkg, "presets")
            return reach_left_out(entries, pkg, "presets", (pkg / "build.py",))

    def test_a_left_out_function_through_the_package_attribute_fails(self):
        self.assertTrue(self._bad("import presets\n\ndef run():\n    return presets.build.deep_build()\n"))

    def test_a_left_out_function_through_the_shim_re_export_fails(self):
        self.assertTrue(self._bad("import pd2_presets as presets\n\ndef run():\n    return presets.deep_build()\n"))

    def test_from_the_shim_import_of_a_left_out_function_fails(self):
        self.assertTrue(self._bad("from pd2_presets import deep_build\n"))
        self.assertTrue(self._bad("from pd2_presets import deep_build as d\n\ndef run():\n    return d()\n"))
        self.assertEqual(self._bad("from pd2_presets import score_it\n"), [])

    def test_a_dotted_import_as_an_alias_of_a_left_out_module_fails(self):
        self.assertTrue(self._bad("import presets.build as b\n\ndef run():\n    return b.deep_build()\n"))
        self.assertEqual(self._bad("import presets.apply as b\n\ndef run():\n    return b.score_it()\n"), [])

    def test_a_dotted_import_as_an_alias_inside_the_package_fails(self):
        extra = {"helpers.py": "import presets.build as b\n\ndef mid():\n    return b.deep_build()\n"}
        self.assertTrue(self._bad("from presets.helpers import mid\n", extra))

    def test_a_plain_dotted_import_inside_the_package_fails(self):
        extra = {"helpers.py": "import presets.build\n\ndef mid():\n    return presets.build.deep_build()\n"}
        self.assertTrue(self._bad("from presets.helpers import mid\n", extra))

    def test_aliasing_a_left_out_module_or_function_fails(self):
        self.assertTrue(self._bad("import presets\nb = presets.build\n"))
        self.assertTrue(self._bad("from presets import build\nf = build\n"))
        self.assertTrue(self._bad("from presets import build as b\n\ndef run():\n    return b.deep_build()\n"))
        extra = {"helpers.py": "from . import build\n\ndef mid():\n    f = build\n    return f.deep_build()\n"}
        self.assertTrue(self._bad("from presets.helpers import mid\n", extra))

    def test_dynamic_imports_and_getattr_on_the_package_are_flagged(self):
        cases = ("import importlib\n\ndef run(n):\n    return importlib.import_module(n)\n",
                 "import importlib\n\ndef run():\n    return importlib.import_module('presets.build')\n",
                 "def run():\n    return __import__('presets.build')\n",
                 "import pd2_presets as presets\n\ndef run(n):\n    return getattr(presets, n)()\n")
        for src in cases:
            with tempfile.TemporaryDirectory() as d:
                tools, _ = self._tree(d, src)
                self.assertEqual(len(dynamic_uses([tools / "scorer.py"], "presets")), 1, src)
        with tempfile.TemporaryDirectory() as d:
            tools, _ = self._tree(d, "import importlib\n\ndef run():\n    return importlib.import_module('json')\n")
            self.assertEqual(dynamic_uses([tools / "scorer.py"], "presets"), [])


def _module_file(base: Path, parts: list[str]) -> Path | None:
    p = base.joinpath(*parts)
    if p.with_suffix(".py").is_file():
        return p.with_suffix(".py")
    return p / "__init__.py" if (p / "__init__.py").is_file() else None


def _imports(path: Path, pkg: Path, pkg_name: str) -> tuple[dict, list]:
    """name -> (module file, original name; None for a module bound by name) for every import of the package's own
    modules in the file (inside a def too), and the modules of its star imports. `import presets.x as b` binds the module
    x to b; `import presets.x` binds the package."""
    names, stars = {}, []
    for n in ast.walk(ast.parse(path.read_text("utf-8"))):
        if isinstance(n, ast.Import):
            for a in n.names:
                parts = a.name.split(".")
                if parts[0] != pkg_name:
                    continue
                if a.asname:
                    mod = _module_file(pkg.parent, parts)
                    if mod:
                        names[a.asname] = (mod, None)
                elif (pkg / "__init__.py").is_file():
                    names[pkg_name] = (pkg / "__init__.py", None)
            continue
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


def scorer_entries(files: list[Path], pkg: Path, pkg_name: str) -> list[tuple[Path, str | None]]:
    """The (module file, name) pairs of the presets package that these scorer-side files use (name None: the module
    itself, as a value): `from presets.x import n`, `from pd2_presets import n`, attribute reads off the package
    (`presets.n`, `P.n`, bound by `import pd2_presets as ...`), which the shim (pd2_presets re-exports its names)
    resolves, and off a submodule (`presets.x.n`, `import presets.x as b` then `b.n`, `from presets import x`)."""
    out = set()
    shim = pkg.parent / f"pd2_{pkg_name}.py"
    reexport = _imports(shim, pkg, pkg_name) if shim.is_file() else ({}, [])

    def via_shim(attr: str) -> set:
        if attr in reexport[0] and reexport[0][attr][1]:
            return {reexport[0][attr]}
        return {(s, attr) for s in reexport[1] if attr in _top_level(ast.parse(s.read_text("utf-8")))
                or attr in _imports(s, pkg, pkg_name)[0]}
    for f in files:  # not the shim itself: its imports are its re-exports, resolved only where a file reads them
        tree = ast.parse(f.read_text("utf-8"))
        shims, real, mods = {"P"}, set(), {}
        for n in ast.walk(tree):
            if isinstance(n, ast.Import):
                for a in n.names:
                    if a.name == f"pd2_{pkg_name}":
                        shims.add(a.asname or a.name)
                    elif a.name == pkg_name:
                        real.add(a.asname or a.name)
                    elif a.name.startswith(pkg_name + "."):
                        m = _module_file(pkg.parent, a.name.split("."))
                        if m and a.asname:
                            mods[a.asname] = m
                        elif m:
                            real.add(pkg_name)
                            out.add((m, None))
            elif isinstance(n, ast.ImportFrom) and n.level == 0 and n.module == f"pd2_{pkg_name}":
                for a in n.names:
                    out |= via_shim(a.name) if a.name != "*" else {v for v in reexport[0].values() if v[1]}
        names, _ = _imports(f, pkg, pkg_name)
        for m, o in names.values():
            if m:
                out.add((m, o))
        mods |= {k: m for k, (m, o) in names.items() if m and not o}
        for n in ast.walk(tree):
            if not isinstance(n, ast.Attribute):
                continue
            v = n.value
            if isinstance(v, ast.Name) and v.id in shims | real:
                out |= via_shim(n.attr)
                mod = _module_file(pkg, [n.attr])
                if mod and (v.id in real or n.attr not in reexport[0]):  # the shim's `build` is the function
                    out.add((mod, None))
            elif isinstance(v, ast.Name) and v.id in mods:
                out.add((mods[v.id], n.attr))
            elif isinstance(v, ast.Attribute) and isinstance(v.value, ast.Name) and v.value.id in real:
                mod = _module_file(pkg, [v.attr])
                if mod:
                    out.add((mod, n.attr))
    return sorted(out, key=lambda e: (str(e[0]), e[1] or ""))


def dynamic_uses(files: list[Path], pkg_name: str) -> list[str]:
    """Where these files reach the presets package in a way no AST walk can follow: importlib.import_module and
    __import__ with a name that is not a plain string of another package, getattr on the package or a module of it.
    Method calls on objects and values passed through containers are out of scope (not seen)."""
    bad = []
    names = (pkg_name, f"pd2_{pkg_name}")
    for f in files:
        tree = ast.parse(f.read_text("utf-8"))
        bound = {"P", pkg_name}
        for n in ast.walk(tree):
            if isinstance(n, ast.Import):
                bound |= {a.asname or a.name.split(".")[0] for a in n.names if a.name.split(".")[0] in names}
            elif isinstance(n, ast.ImportFrom) and (n.module or "").split(".")[0] in names:
                bound |= {a.asname or a.name for a in n.names}
        for n in ast.walk(tree):
            if not isinstance(n, ast.Call):
                continue
            fn = n.func
            name = fn.id if isinstance(fn, ast.Name) else fn.attr if isinstance(fn, ast.Attribute) else ""
            if name in ("import_module", "__import__", "spec_from_file_location"):
                arg = n.args[0] if n.args else None
                if not (isinstance(arg, ast.Constant) and isinstance(arg.value, str)) or pkg_name in arg.value:
                    bad.append(f"{f.name}:{n.lineno}: dynamic import of a presets module ({name})")
            elif name in ("getattr", "vars") and n.args and isinstance(n.args[0], ast.Name) and n.args[0].id in bound:
                bad.append(f"{f.name}:{n.lineno}: {name} on {n.args[0].id} reaches a presets name no walk can follow")
    return bad


def reach_left_out(entries: list[tuple[Path, str | None]], pkg: Path, pkg_name: str, left: tuple[Path, ...]) -> list[str]:
    """Follow each entry through the defs it calls, across modules and at any depth; one message (with the chain) for
    each chain that ends in a module left out of the key. A name None is the module itself used as a value."""
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
    todo = [(m, n, (n or m.stem,)) for m, n in entries]
    while todo:
        mod, name, chain = todo.pop()
        if (mod, name) in seen:
            continue
        seen.add((mod, name))
        if is_left(mod):
            bad.append(f"{name or 'the module'} in {mod.relative_to(pkg.parent).as_posix()} is left out of the key; "
                       "reached: " + " -> ".join(chain))
            continue
        if name is None:
            continue
        defs, imps, stars = parsed(mod)
        if name in defs:
            based = {id(x.value) for x in ast.walk(defs[name]) if isinstance(x, ast.Attribute)}
            for x in ast.walk(defs[name]):
                if isinstance(x, ast.Attribute) and isinstance(x.value, ast.Name) and x.value.id in imps \
                        and imps[x.value.id][1] is None:
                    base = imps[x.value.id][0]
                    todo.append((base, x.attr, (*chain, x.attr)))
                    if base.name == "__init__.py" and (sub := _module_file(base.parent, [x.attr])):
                        todo.append((sub, None, (*chain, x.attr)))
                elif isinstance(x, ast.Attribute) and isinstance(x.value, ast.Attribute) \
                        and isinstance(x.value.value, ast.Name) and x.value.value.id in imps \
                        and imps[x.value.value.id][1] is None and imps[x.value.value.id][0].name == "__init__.py" \
                        and (sub := _module_file(imps[x.value.value.id][0].parent, [x.value.attr])):
                    todo.append((sub, x.attr, (*chain, x.value.attr, x.attr)))
                elif isinstance(x, ast.Name) and x.id != name and not (id(x) in based and x.id in imps and imps[x.id][1] is None):
                    if x.id in defs:
                        todo.append((mod, x.id, (*chain, x.id)))
                    elif x.id in imps and imps[x.id][1]:
                        todo.append((imps[x.id][0], imps[x.id][1], (*chain, x.id)))
                    elif x.id in imps and imps[x.id][0]:
                        todo.append((imps[x.id][0], None, (*chain, x.id)))
                    else:
                        todo.extend((s, x.id, (*chain, x.id)) for s in stars if has(s, x.id))
        elif name in imps and imps[name][1]:
            todo.append((imps[name][0], imps[name][1], chain))
        elif name not in imps:
            todo.extend((s, name, chain) for s in stars if has(s, name))
    return sorted(bad)


def _fake_run_world(root: Path):
    """What run() reads, as files and stand-ins: one map, scored by a counting fake."""
    from unittest import mock
    data, d2r, out, cache = root / "data", root / "d2r", root / "out", root / "cache"
    (data / "global" / "tiles").mkdir(parents=True)
    (data / "global" / "tiles" / "a.ds1").write_bytes(b"ds1")
    (data / C.presets.PRESETS).mkdir(parents=True, exist_ok=True)
    (data / C.presets.PRESETS / "a.json").write_text('{"entities":[]}')
    (d2r / C.MODELS).parent.mkdir(parents=True, exist_ok=True)
    (d2r / C.MODELS).write_bytes(b"m")
    votes = root / "votes.txt"
    votes.write_text("v")
    calls = []

    def fake_score(*a, **k):
        calls.append(1)
        return C.Score("a", "Area", 1, set(), size=0)
    fid = mock.MagicMock()
    fid.VOTES_FILE = votes
    fid.votes_stale.return_value = None
    fid.stock_types.return_value = {}
    fid.score_map.return_value = {}
    fid.image.return_value = {}
    fid.write.return_value = []
    fid.hd_pieces.return_value = []

    class Heights:
        def __init__(self, *roots):
            self.roots = roots
    fid.Heights = Heights
    pre = mock.MagicMock()
    pre.read_rule.return_value = (None, {}, [])
    pre.wet_dt1s.return_value = frozenset()
    pre.pd2_only_presets.return_value = [("Area", 0, ["x"], ["a.ds1"])]
    pre.PRESETS = C.presets.PRESETS
    pre.Tiles = C.presets.Tiles
    pre.stand_in.return_value = {}
    pre.layout.return_value = mock.MagicMock(keys={})
    patches = [mock.patch.dict(sys.modules, {"pd2_fidelity": fid}), mock.patch.object(C, "presets", pre),
               mock.patch.object(C, "model_lookup", lambda d: (lambda p: None, lambda p: [])),
               mock.patch.object(C, "load"), mock.patch.object(C, "score", fake_score),
               mock.patch.object(C, "images", lambda *a, **k: {"a.png": b"png"}),
               mock.patch.object(C, "plane_ground", lambda p: None), mock.patch.object(C, "covered", lambda *a: set()),
               mock.patch.object(C, "boxes", lambda *a, **k: iter(())),
               mock.patch.object(C, "ground_tiles", lambda *a, **k: set()),
               mock.patch.object(C, "wrong_tiles", lambda *a, **k: set()),
               mock.patch.object(C, "walkable_floor", lambda lay: set()),
               mock.patch.object(C, "floating_pieces", lambda *a, **k: ()), mock.patch.object(C, "report", lambda *a, **k: "r")]
    return data, d2r, out, cache, calls, patches


class RunRescoreTests(unittest.TestCase):
    def test_a_damaged_kept_score_is_scored_again_once_then_replayed(self):
        with tempfile.TemporaryDirectory() as d, contextlib.ExitStack() as stack:
            data, d2r, out, cache, calls, patches = _fake_run_world(Path(d))
            for p in patches:
                stack.enter_context(p)
            memo = (cache, pd2_cache.Policy())

            def go() -> str:
                buf = io.StringIO()
                with contextlib.redirect_stdout(buf):
                    C.run(data, d2r, out, memo=memo)
                return buf.getvalue()
            self.assertIn("(0 replayed", go())
            self.assertEqual(len(calls), 1)
            self.assertIn("(1 replayed", go())
            self.assertEqual(len(calls), 1)
            for bad in (b"not json", b"[1]", b'{"@":"Score","v":[]}', b"5", b'{"@":"TypeScore","v":{}}'):
                scores = list(cache.rglob("score.json"))
                self.assertEqual(len(scores), 1)
                scores[0].write_bytes(bad)
                before = len(calls)
                self.assertIn("(0 replayed", go(), bad)
                self.assertEqual(len(calls), before + 1, "scored again once")
                self.assertIn("(1 replayed", go(), "and kept: replayed the next time")
                self.assertEqual(len(calls), before + 1)
                self.assertEqual(len(list(cache.rglob("score.json"))), 1)


if __name__ == "__main__":
    unittest.main()
