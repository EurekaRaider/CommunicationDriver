"""Package the agent skill with the library's version and portable paths."""

import argparse
import re
import zipfile
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("--out", default="artifacts/skills")
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
skill = root / "skills" / "communicationdriver"
if not (skill / "SKILL.md").is_file():
    raise SystemExit("Skill entrypoint not found")
version = re.search(
    r"project\(\s*CommunicationDriver\s+VERSION\s+([0-9.]+)",
    (root / "CMakeLists.txt").read_text(encoding="utf-8"),
).group(1)
out = Path(args.out)
out.mkdir(parents=True, exist_ok=True)
archive = out / f"CommunicationDriver-Skill-{version}.zip"
with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as package:
    package.write(root / "LICENSE", "LICENSE")
    for source in sorted(skill.rglob("*")):
        if source.is_file():
            package.write(source, source.relative_to(root).as_posix())
print(archive)
