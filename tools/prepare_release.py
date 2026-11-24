"""Seed the toolkit's packaging changelog without editing CHANGELOG.md in a PR (#2108)."""
from datetime import date
import json
from pathlib import Path
import re
import shutil
import tempfile
import tomllib


def prepare(root: Path) -> Path:
    version = json.loads((root / "mod.json").read_text("utf-8"))["version"]
    config = (root / "release.toml").read_text("utf-8")
    repos = tomllib.loads(config)["repos"]
    changelog = (root / repos["changelog"]).read_text("utf-8")
    heading = f"### {version} ({date.today().isoformat()})\n\n"
    if not re.search(rf"^### {re.escape(version)} \(", changelog, re.M):
        start = changelog.index("### ")
        changelog = changelog[:start] + heading + changelog[start:]
    output = root / "dist"
    output.mkdir(exist_ok=True)
    (output / "release-changelog.md").write_text(changelog, "utf-8", newline="\n")
    old = f'changelog = "{repos["changelog"]}"'
    if config.count(old) != 1:
        raise ValueError("release.toml must name one changelog")
    config = config.replace(old, 'changelog = "dist/release-changelog.md"')
    fragments = Path(tempfile.mkdtemp(prefix="release-fragments-", dir=output))
    for source in (root / repos["fragments"]).glob("*.md"):
        shutil.copy2(source, fragments / source.name)
    old = f'fragments = "{repos["fragments"]}"'
    if config.count(old) != 1:
        raise ValueError("release.toml must name one fragments folder")
    config = config.replace(old, f'fragments = "{fragments.relative_to(root).as_posix()}"')
    path = output / "release-stage.toml"
    path.write_text(config, "utf-8", newline="\n")
    return path


if __name__ == "__main__":
    print(prepare(Path(__file__).resolve().parents[1]))
