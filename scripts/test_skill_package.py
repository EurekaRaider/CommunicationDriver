"""Verify the complete skill in native staging directories or ZIP/NuGet archives."""

import argparse
import zipfile
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("packages", nargs="+")
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
skill = Path("skills/communicationdriver")
if not (root / skill / "SKILL.md").is_file():
    raise SystemExit("Skill entrypoint not found")
expected = {
    source.relative_to(root).as_posix(): source.read_bytes()
    for source in (root / skill).rglob("*")
    if source.is_file()
}
for entry in args.packages:
    path = Path(entry)
    if path.is_dir() and not (path / skill).is_dir():
        packages = sorted(path.glob("*.nupkg")) + sorted(path.glob("*.zip"))
    else:
        packages = [path]
    if not packages:
        raise SystemExit(f"No packages found: {path}")
    for package in packages:
        if package.is_dir():
            actual = {
                source.relative_to(package).as_posix(): source.read_bytes()
                for source in (package / skill).rglob("*")
                if source.is_file()
            }
        else:
            with zipfile.ZipFile(package) as archive:
                actual = {
                    name: archive.read(name)
                    for name in archive.namelist()
                    if name.startswith(skill.as_posix() + "/") and not name.endswith("/")
                }
        if actual != expected:
            raise SystemExit(f"Skill missing, stale, or incomplete: {package}")
        print(f"PASS complete skill ({len(expected)} files): {package}")
