"""Release gates and recovery without modifying GitHub or existing published assets."""

import copy
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import prepare_release as prepare
import publish_release as publisher

VERSION = "0.1.3"
COMMIT = "a" * 40
REPO = "example/CommunicationDriver"


class ReleaseTests(unittest.TestCase):
    def test_native_and_managed_package_versions_agree(self):
        root = Path(__file__).resolve().parents[1]
        project = ET.parse(root / "bindings/dotnet/CommunicationDriver/CommunicationDriver.csproj")
        self.assertEqual(prepare.library_version(root), project.findtext(".//Version"))

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.assets = Path(self.temp.name)
        for name in prepare.release_asset_names(VERSION):
            (self.assets / name).write_bytes(name.encode())
        self.complete = {
            "id": 123,
            "tag_name": f"v{VERSION}",
            "target_commitish": COMMIT,
            "draft": True,
            "assets": [
                {
                    "name": p.name,
                    "size": p.stat().st_size,
                    "state": "uploaded",
                    "digest": "sha256:" + hashlib.sha256(p.read_bytes()).hexdigest(),
                }
                for p in self.assets.iterdir()
            ],
        }
        self.published = {**self.complete, "draft": False, "html_url": "https://example/release"}
        self.reference = {"object": {"type": "commit", "sha": COMMIT}}

    def test_ci_requires_successful_main_push_from_same_repository_and_commit(self):
        run = {
            "status": "completed",
            "conclusion": "success",
            "path": ".github/workflows/ci.yml",
            "event": "push",
            "head_branch": "main",
            "head_sha": COMMIT,
            "repository": {"full_name": REPO},
            "head_repository": {"full_name": REPO},
        }
        prepare.verify_ci(run, COMMIT)
        for key, value in [
            ("status", "in_progress"),
            ("conclusion", "failure"),
            ("conclusion", "cancelled"),
            ("path", ".github/workflows/soak.yml"),
            ("event", "pull_request"),
            ("head_branch", "feature"),
            ("head_sha", "b" * 40),
            ("head_repository", {"full_name": "fork/repo"}),
        ]:
            with self.subTest(key=key, value=value), self.assertRaises(SystemExit):
                prepare.verify_ci({**run, key: value}, COMMIT)

    def test_draft_lookup_uses_all_release_pages(self):
        with patch.object(publisher, "gh", return_value=json.dumps([[], [self.complete]])) as gh:
            self.assertEqual(publisher.release_state(REPO, VERSION, COMMIT), self.complete)
            self.assertIn("--paginate", gh.call_args.args)

    def test_unpublished_version_is_eligible(self):
        with patch.object(publisher, "gh", return_value="[[]]"):
            self.assertIsNone(publisher.release_state(REPO, VERSION, COMMIT))

    def test_published_version_is_preserved_even_when_main_advances(self):
        with patch.object(publisher, "gh", return_value=json.dumps([[self.published]])) as gh:
            publisher.publish(REPO, VERSION, "b" * 40, Path("unused"), Path("unused"))
            self.assertEqual(gh.call_count, 1)

    def test_foreign_draft_commit_is_rejected(self):
        with patch.object(publisher, "gh", return_value=json.dumps([[self.complete]])):
            with self.assertRaisesRegex(SystemExit, "different commit"):
                publisher.release_state(REPO, VERSION, "b" * 40)

    def test_incomplete_published_release_fails_instead_of_silently_skipping(self):
        release = {**self.published, "assets": self.published["assets"][:-1]}
        with patch.object(publisher, "gh", return_value=json.dumps([[release]])):
            with self.assertRaisesRegex(SystemExit, "all nine"):
                publisher.release_state(REPO, VERSION, COMMIT)

    def test_new_version_creates_draft_then_uploads_then_publishes(self):
        with (
            patch.object(publisher, "release_state", side_effect=[None, self.complete]),
            patch.object(publisher, "api", side_effect=[None, self.published]) as api,
            patch.object(publisher, "gh", return_value="") as gh,
        ):
            publisher.publish(REPO, VERSION, COMMIT, self.assets, Path("notes.md"))
            self.assertEqual(gh.call_args_list[0].args[:2], ("api", f"repos/{REPO}/git/refs"))
            self.assertEqual(gh.call_args_list[1].args[:2], ("release", "create"))
            self.assertIn("--draft", gh.call_args_list[1].args)
            self.assertEqual(gh.call_args_list[2].args[:2], ("release", "upload"))
            self.assertEqual(
                api.call_args.kwargs["body"], {"draft": False, "make_latest": "legacy"}
            )

    def test_original_draft_and_tag_can_resume(self):
        with (
            patch.object(publisher, "release_state", side_effect=[self.complete, self.complete]),
            patch.object(publisher, "api", side_effect=[self.reference, self.published]),
            patch.object(publisher, "gh", return_value="") as gh,
        ):
            publisher.publish(REPO, VERSION, COMMIT, self.assets, Path("notes.md"))
            self.assertEqual(gh.call_count, 1)
            self.assertEqual(gh.call_args.args[:2], ("release", "upload"))

    def test_failed_upload_never_publishes(self):
        with (
            patch.object(publisher, "release_state", return_value=self.complete),
            patch.object(publisher, "api", return_value=self.reference) as api,
            patch.object(publisher, "gh", side_effect=SystemExit("upload failed")),
        ):
            with self.assertRaisesRegex(SystemExit, "upload failed"):
                publisher.publish(REPO, VERSION, COMMIT, self.assets, Path("notes.md"))
            self.assertEqual(api.call_count, 1)

    def test_wrong_tag_commit_is_never_moved(self):
        with (
            patch.object(publisher, "release_state", return_value=self.complete),
            patch.object(
                publisher, "api", return_value={"object": {"type": "commit", "sha": "b" * 40}}
            ),
            patch.object(publisher, "gh") as gh,
        ):
            with self.assertRaisesRegex(SystemExit, "tag points"):
                publisher.publish(REPO, VERSION, COMMIT, self.assets, Path("notes.md"))
            gh.assert_not_called()

    def test_wrong_uploaded_digest_never_publishes(self):
        release = copy.deepcopy(self.complete)
        release["assets"][0]["digest"] = "sha256:wrong"
        with (
            patch.object(publisher, "release_state", side_effect=[self.complete, release]),
            patch.object(publisher, "api", return_value=self.reference) as api,
            patch.object(publisher, "gh", return_value=""),
        ):
            with self.assertRaisesRegex(SystemExit, "differs"):
                publisher.publish(REPO, VERSION, COMMIT, self.assets, Path("notes.md"))
            self.assertEqual(api.call_count, 1)


if __name__ == "__main__":
    unittest.main()
