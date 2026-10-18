"""Publish each validated version once, resuming only drafts from the same commit."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

from prepare_release import library_version, release_asset_names, verify_ci


def gh(*args, body=None, missing_ok=False):
    result = subprocess.run(["gh", *args], input=body, text=True, capture_output=True)
    if result.returncode:
        if missing_ok and "HTTP 404" in result.stderr:
            return None
        raise SystemExit(result.stderr.strip())
    return result.stdout


def api(repository, path, body=None, missing_ok=False):
    args = ["api", f"repos/{repository}/{path}"]
    if body is not None:
        args += ["--method", "PATCH", "--input", "-"]
    result = gh(*args, body=json.dumps(body) if body is not None else None, missing_ok=missing_ok)
    return json.loads(result) if result is not None else None


def check_assets(release, version, files=None):
    assets = {a["name"]: a for a in release["assets"]}
    if set(assets) != release_asset_names(version) or any(
        a["size"] <= 0 or a["state"] != "uploaded" for a in assets.values()
    ):
        raise SystemExit("Release must contain all nine complete assets")
    if files is not None:
        for file in files:
            asset = assets[file.name]
            digest = "sha256:" + hashlib.sha256(file.read_bytes()).hexdigest()
            if asset["size"] != file.stat().st_size or (
                asset.get("digest") and asset["digest"] != digest
            ):
                raise SystemExit(f"Uploaded asset differs from prepared file: {file.name}")


def release_state(repository, version, commit):
    # List releases to include drafts, then retain their numeric ID for publication.
    pages = json.loads(
        gh("api", "--paginate", "--slurp", f"repos/{repository}/releases?per_page=100")
    )
    release = next((r for page in pages for r in page if r["tag_name"] == f"v{version}"), None)
    if release and not release["draft"]:
        check_assets(release, version)
    elif release and release["target_commitish"] != commit:
        raise SystemExit("Draft belongs to a different commit; rerun its original CI")
    return release


def publish(repository, version, commit, assets, notes):
    release = release_state(repository, version, commit)
    if release and not release["draft"]:
        print(f"SKIP v{version}: already published; assets preserved")
        return
    files = sorted(assets.iterdir())
    if {p.name for p in files} != release_asset_names(version) or not all(
        p.is_file() for p in files
    ):
        raise SystemExit("Prepared release must contain exactly nine asset files")
    tag = f"v{version}"
    reference = api(repository, f"git/ref/tags/{tag}", missing_ok=True)
    if reference:
        if reference["object"]["type"] != "commit" or reference["object"]["sha"] != commit:
            raise SystemExit("Existing release tag points to a different commit")
    else:
        gh(
            "api",
            f"repos/{repository}/git/refs",
            "--method",
            "POST",
            "-f",
            f"ref=refs/tags/{tag}",
            "-f",
            f"sha={commit}",
        )
    if release is None:
        gh(
            "release",
            "create",
            tag,
            "--repo",
            repository,
            "--draft",
            "--verify-tag",
            "--target",
            commit,
            "--title",
            f"CommunicationDriver {version}",
            "--notes-file",
            str(notes),
        )
    gh("release", "upload", tag, "--repo", repository, "--clobber", *(str(p) for p in files))
    release = release_state(repository, version, commit)
    if not release or not release["draft"]:
        raise SystemExit("Release must remain a draft until all assets are verified")
    check_assets(release, version, files)
    published = api(
        repository, f"releases/{release['id']}", body={"draft": False, "make_latest": "legacy"}
    )
    if published["draft"]:
        raise SystemExit("Release remained a draft")
    print(f"PASS published {published['html_url']}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ci-run", type=Path, required=True)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    run = json.loads(args.ci_run.read_text())
    verify_ci(run, args.commit)
    root = Path(__file__).resolve().parents[1]
    version = library_version(root)
    repository = run["repository"]["full_name"]
    if args.check:
        release = release_state(repository, version, args.commit)
        needed = release is None or release["draft"]
        if os.environ.get("GITHUB_OUTPUT"):
            with open(os.environ["GITHUB_OUTPUT"], "a") as output:
                output.write(f"version={version}\npublish={str(needed).lower()}\n")
        print(f"{'PUBLISH' if needed else 'SKIP'} v{version}")
    else:
        publish(
            repository,
            version,
            args.commit,
            Path("artifacts/release"),
            Path("artifacts/release-notes.md"),
        )


if __name__ == "__main__":
    main()
