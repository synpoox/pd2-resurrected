# pd2-resurrected

A near-1:1 recreation of the latest Project Diablo II as a Diablo II: Resurrected mod (Reign of the Warlock mode only), with D2RLoader plugins for engine mechanics and a Warlock redesigned in PD2's style. Offline single-player first, then private multiplayer.

**Status: building toward the alpha** ([roadmap](docs/roadmap.md)).

- Plan and open decisions: [map issue #1](https://github.com/synpoox/pd2-resurrected/issues/1)
- Decisions: [`docs/adr/`](docs/adr/)
- Terms: [`GLOSSARY.md`](GLOSSARY.md)
- Research notes: [`docs/research/`](docs/research/)

No Blizzard files, PD2 archives or extracted PD2 data are committed here ([ADR 0002](docs/adr/0002-pd2-data-generated-not-committed.md)).

## Build

Needs the PD2 install named in `pd2-baseline.json` and the sibling `..\d2rAI-toolkit` at commit `ec17a9a` or later
(modkit's `generated/data/` layer).

```bash
python tools/pd2_extract.py
```

```bash
python tools/pd2_merge.py
```

```bash
python -m modkit build .
```

Run the last one with `..\d2rAI-toolkit` on `PYTHONPATH`. Tool tests (no PD2 data needed):

```bash
python -m unittest discover -s tools/tests
```
