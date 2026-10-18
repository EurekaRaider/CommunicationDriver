"""Build release assets only from successful Platform CI for the selected main commit."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import xml.etree.ElementTree as ET
import zipfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ci-run", type=Path, required=True)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--input", type=Path, default=Path("artifacts/release-input"))
    parser.add_argument("--output", type=Path, default=Path("artifacts/release"))
    parser.add_argument("--verify-only", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    run = json.loads(args.ci_run.read_text())
    expected = {
        "status": "completed",
        "conclusion": "success",
        "path": ".github/workflows/ci.yml",
        "event": "push",
        "head_branch": "main",
        "head_sha": args.commit,
    }
    for key, value in expected.items():
        if run.get(key) != value:
            raise SystemExit(f"Release requires {key}={value}; got {run.get(key)}")
    if run["head_repository"]["full_name"] != run["repository"]["full_name"]:
        raise SystemExit("Release CI must belong to the source repository")
    if args.verify_only:
        print(f"PASS Platform CI {run['id']} for {args.commit}")
        return

    version = re.search(
        r"project\(\s*CommunicationDriver\s+VERSION\s+([0-9.]+)",
        (root / "CMakeLists.txt").read_text(),
    ).group(1)
    notes = (root / "docs/releases" / f"{version}.md").read_text()
    rids = ["linux-x64", "linux-arm64", "osx-x64", "osx-arm64", "win-x64", "win-x86"]
    sources = []
    for rid in rids:
        package = args.input / f"native-{rid}"
        metadata = json.loads((package / "native-build.json").read_text())
        if metadata["rid"] != rid:
            raise SystemExit(f"Mismatched native package: {rid}")
        native = package / "runtimes" / rid / "native"
        if not (native / metadata["library"]).is_file():
            raise SystemExit(f"Missing native library: {rid}")
        if (
            rid.startswith("win-")
            and not (package / "lib" / rid / "communicationdriver_native.lib").is_file()
        ):
            raise SystemExit(f"Missing Windows import library: {rid}")
        headers = package / "include/communicationdriver"
        if {p.name for p in headers.iterdir()} != {
            "communicationdriver.h",
            "communicationdriver.hpp",
        }:
            raise SystemExit(f"Native package must contain exactly two public headers: {rid}")
        for header in headers.iterdir():
            if (
                header.read_bytes()
                != (root / "include/communicationdriver" / header.name).read_bytes()
            ):
                raise SystemExit(f"Native package header differs from release source: {rid}")
        sources.append((rid, package))

    nuget = args.input / "CommunicationDriver-NuGet" / f"CommunicationDriver.{version}.nupkg"
    with zipfile.ZipFile(nuget) as package:
        names = package.namelist()
        spec = ET.fromstring(package.read("CommunicationDriver.nuspec"))
        if spec.findtext("{*}metadata/{*}version") != version:
            raise SystemExit("NuGet version differs from release source")
        actual_rids = {p.split("/")[1] for p in names if p.startswith("runtimes/")}
        if actual_rids != set(rids) or not all(
            f"lib/{target}/CommunicationDriver.dll" in names for target in ["net48", "net10.0"]
        ):
            raise SystemExit("NuGet must contain all six native RIDs and both managed targets")
    skill = args.input / "CommunicationDriver-Skill" / f"CommunicationDriver-Skill-{version}.zip"
    with zipfile.ZipFile(skill) as package:
        if "skills/communicationdriver/SKILL.md" not in package.namelist():
            raise SystemExit("Missing standalone agent skill")

    args.output.mkdir(parents=True, exist_ok=False)
    for rid, package in sources:
        stem = f"CommunicationDriver-{version}-{rid}"
        with zipfile.ZipFile(args.output / f"{stem}.zip", "w", zipfile.ZIP_DEFLATED) as archive:
            for source in sorted(package.rglob("*")):
                if source.is_file():
                    archive.write(source, f"{stem}/{source.relative_to(package).as_posix()}")
    for source in [nuget, skill]:
        shutil.copy2(source, args.output / source.name)
    checksums = {
        p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(args.output.iterdir())
    }
    (args.output / "SHA256SUMS").write_text(
        "".join(f"{digest}  {name}\n" for name, digest in checksums.items())
    )
    args.output.parent.joinpath("release-manifest.json").write_text(
        json.dumps({"version": version, "commit": args.commit, "assets": checksums}, indent=2)
        + "\n"
    )
    args.output.parent.joinpath("release-notes.md").write_text(
        notes + f"\nValidated commit: `{args.commit}`. [Platform CI]({run['html_url']}).\n"
    )
    print(f"PASS {len(checksums) + 1} release assets for v{version}")


if __name__ == "__main__":
    main()
