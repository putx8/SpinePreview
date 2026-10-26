import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tarfile
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("release", ROOT / "scripts/release.py")
release = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(release)


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)

    def fixture(self, tag="v1.2.3"):
        output = self.directory / "assets"
        output.mkdir()
        hashes = {}
        for name in release.asset_names(tag):
            (output / name).write_bytes(b"test archive")
            hashes[name] = release.digest(output / name)
        (output / "manifest.json").write_text(json.dumps({"tag": tag, "sha256": hashes}))
        (output / "SHA256SUMS").write_text("".join(f"{value}  {name}\n" for name, value in hashes.items()))
        (output / "release-notes.md").write_text("Test release")
        return output

    def test_tags_and_prereleases(self):
        self.assertEqual(release.metadata("v1.2.3")["prerelease"], "false")
        self.assertEqual(release.metadata("v1.2.3-rc.1")["base_version"], "1.2.3")
        self.assertEqual(release.metadata("v1.2.3-rc.1")["prerelease"], "true")
        for tag in ("v1.2", "1.2.3", "v01.2.3", "v1.2.3-rc.01", "v1.2.3/../../file", "v1.2.3\nother"):
            with self.subTest(tag=tag), self.assertRaises(ValueError):
                release.metadata(tag)

    def test_checksums_and_tag_mismatch(self):
        output = self.fixture()
        release.verify(output, "v1.2.3")
        with self.assertRaises(ValueError):
            release.verify(output, "v1.2.4")
        (output / release.asset_names("v1.2.3")[0]).write_bytes(b"corrupt")
        with self.assertRaisesRegex(ValueError, "Checksum mismatch"):
            release.verify(output, "v1.2.3")

    def test_missing_archive_or_changed_checksums(self):
        output = self.fixture()
        (output / "SHA256SUMS").write_text("incorrect")
        with self.assertRaisesRegex(ValueError, "SHA256SUMS"):
            release.verify(output, "v1.2.3")
        (output / release.asset_names("v1.2.3")[0]).unlink()
        with self.assertRaisesRegex(ValueError, "Missing or unexpected"):
            release.verify(output, "v1.2.3")

    def test_source_is_committed_snapshot_with_dependencies(self):
        repo = self.directory / "repo"
        repo.mkdir()
        subprocess.run(["git", "init", "-q", str(repo)], check=True)
        (repo / "Vendor").mkdir()
        (repo / "Vendor/dependency.txt").write_text("offline dependency")
        subprocess.run(["git", "-C", str(repo), "add", "Vendor"], check=True)
        subprocess.run(["git", "-C", str(repo), "-c", "user.name=Release Test", "-c", "user.email=release@example.invalid",
                        "commit", "-qm", "fixture"], check=True)
        (repo / "private-untracked.txt").write_text("do not export")
        (repo / "Vendor/dependency.txt").write_text("uncommitted edit")
        output = self.directory / "archives"
        output.mkdir()
        release.source_archives(repo, "HEAD", "v1.2.3", output)
        expected = "SpinePreview-v1.2.3/Vendor/dependency.txt"
        with zipfile.ZipFile(output / "SpinePreview-v1.2.3-source.zip") as archive:
            self.assertEqual(archive.namelist(), ["SpinePreview-v1.2.3/", "SpinePreview-v1.2.3/Vendor/", expected])
            self.assertEqual(archive.read(expected), b"offline dependency")
        with tarfile.open(output / "SpinePreview-v1.2.3-source.tar.gz") as archive:
            self.assertEqual(archive.extractfile(expected).read(), b"offline dependency")
            self.assertFalse(any("private-untracked" in name or "/.git/" in name for name in archive.getnames()))

    def publish(self, state, fail_upload=False, tag="v1.2.3"):
        output = self.fixture(tag)
        executable = self.directory / "gh"
        executable.write_text('''#!/usr/bin/env python3
import json, os, sys
with open(os.environ["MOCK_LOG"], "a") as stream:
    stream.write(json.dumps(sys.argv[1:])+"\\n")
command = sys.argv[2]
if command == "view":
    state = os.environ["MOCK_STATE"]
    if state == "missing": sys.exit(1)
    print("true" if state == "draft" else "false")
if command == "upload" and os.environ["MOCK_FAIL"] == "1": sys.exit(1)
''')
        executable.chmod(0o755)
        log = self.directory / "gh.jsonl"
        result = subprocess.run(["bash", str(ROOT / "scripts/publish-release.sh"), tag, str(output)],
            env={**os.environ, "PATH": str(self.directory)+os.pathsep+os.environ["PATH"],
                 "GH_REPO": "example/test", "MOCK_LOG": str(log), "MOCK_STATE": state,
                 "MOCK_FAIL": "1" if fail_upload else "0"}, capture_output=True, text=True)
        return result, [json.loads(line) for line in log.read_text().splitlines()]

    def test_publish_only_after_successful_upload(self):
        result, calls = self.publish("missing")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual([call[1] for call in calls], ["view", "create", "upload", "edit"])
        self.assertIn("--draft", calls[1])
        self.assertIn("--verify-tag", calls[1])
        self.assertIn("--draft=false", calls[-1])

    def test_resume_draft(self):
        result, calls = self.publish("draft")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual([call[1] for call in calls], ["view", "upload", "edit"])

    def test_preserve_published_release(self):
        result, calls = self.publish("published")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual([call[1] for call in calls], ["view"])

    def test_upload_failure_leaves_draft(self):
        result, calls = self.publish("missing", fail_upload=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn("edit", [call[1] for call in calls])

    def test_prerelease_flags(self):
        result, calls = self.publish("missing", tag="v1.2.3-rc.1")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("--prerelease", calls[1])
        self.assertIn("--prerelease", calls[-1])
        self.assertIn("--latest=false", calls[-1])


if __name__ == "__main__":
    unittest.main()
