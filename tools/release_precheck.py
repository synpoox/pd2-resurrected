"""Run before the toolkit's release/stage.py: refuse an out/data that still holds the retired converter's output (#965).

    python tools/release_precheck.py [--root DIR]

The release stages from out/data, which `modkit build` fills from files/data but never cleans, so a checkout built
before the converter was retired can still carry its folders into the 7z.
"""
import argparse
import sys
from pathlib import Path

import pd2_merge


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", type=Path, default=pd2_merge.ROOT)
    a = ap.parse_args(argv)
    if old := pd2_merge.legacy_output(a.root / "out" / "data"):
        print(f"release: out/data holds the retired converter's output {old}: rebuild from a clean out/ and generated/",
              file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
