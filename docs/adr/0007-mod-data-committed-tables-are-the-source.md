# The mod data is committed, and its tables are the source

The mod's whole data folder is committed as `files/data/`: PD2's tables, JSON, sprites and strings as the mod ships
them, with what the old `patches/` did folded in once (#1100). A data change is a direct edit of the table or JSON
file there, reviewed as a plain diff; there are no JSON patches. `modkit build` copies `files/data/` to `out/data/`
unchanged and `modkit install` installs that, so installing, testing and releasing need no PD2 install, extract or
merge. `tools/pd2_extract.py` and `tools/pd2_merge.py`, with the rules in `merge/`, are only the import run when PD2
itself releases a new version: its raw output is kept on the `pd2-import` branch and its change is applied to
`files/data/` as a three-way merge, reviewed as a diff. `pd2-import.txt` lists the files the import owns. The
table-diff gate compares the committed tables with PD2's baseline tables, which the self-hosted runner extracts
from the PD2 install. Blizzard's own D2R install and CASC files, saves and dumps are never committed; a unit test
refuses them and any path outside the allowed ones. Supersedes ADR 0002.

## Considered Options

- Commit the built folder at `out/data/` and keep `patches/` as the source (#1078): every data change stays a JSON
  op, and `modkit build` deletes `out/` before it builds.
- Git LFS: no file is near GitHub's 100 MB limit (the largest is 19.3 MB); LFS adds a bandwidth quota and worktree
  friction.

## Consequences

- `.gitattributes` keeps `files/data/` bytes as committed (`-text`): the tables are CRLF.
- A leftover `generated/data` would override the committed tables in a build: `install.ps1` refuses it.
- JSON keys D2R reads stay case-sensitive in a direct edit (AGENTS.md, HD data).
- A PD2 version change is a re-baseline: `pd2-baseline.json`, a new `pd2-import` commit, and its three-way merge.
- Every worktree checks out about 365 MB more.
- `check_public.allow` lists the generic false positives in the committed data (Blizzard support addresses in UI strings).
