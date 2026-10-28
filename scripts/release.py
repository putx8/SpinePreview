#!/usr/bin/env python3
"""Validate release tags and package the offline source and macOS applications."""
import argparse
import hashlib
import json
from pathlib import Path
import plistlib
import re
import subprocess

RUNTIMES = ("2.1", "3.8", "4.0")
TAG = re.compile(r"v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?")


def metadata(tag):
    match = TAG.fullmatch(tag)
    if not match:
        raise ValueError("Expected vMAJOR.MINOR.PATCH or vMAJOR.MINOR.PATCH-prerelease")
    if match[4] and any(part.isdigit() and len(part) > 1 and part[0] == "0" for part in match[4].split(".")):
        raise ValueError("Numeric prerelease identifiers must not contain leading zeros")
    return {"tag": tag, "version": tag[1:], "base_version": ".".join(match.group(1, 2, 3)),
            "prerelease": "true" if match[4] else "false"}


def asset_names(tag):
    metadata(tag)
    return [f"SpinePreview{v}-{tag}-macos-universal.zip" for v in RUNTIMES] + [
        f"SpinePreview-{tag}-source.zip", f"SpinePreview-{tag}-source.tar.gz"]


def digest(path):
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


def source_archives(root, revision, tag, output):
    prefix = f"SpinePreview-{tag}/"
    for extension in ("zip", "tar.gz"):
        subprocess.run(["git", "-C", str(root), "archive", f"--format={extension}",
                        f"--prefix={prefix}", f"--output={output / f'SpinePreview-{tag}-source.{extension}'}",
                        revision], check=True)


def package(root, tag, revision, output):
    info = metadata(tag)
    commit = subprocess.check_output(["git", "-C", str(root), "rev-parse", f"{revision}^{{commit}}"], text=True).strip()
    head = subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()
    if commit != head:
        raise ValueError("Package revision must match the checked-out build source")
    if output.exists() and any(output.iterdir()):
        raise ValueError(f"Output directory is not empty: {output}")
    output.mkdir(parents=True, exist_ok=True)
    for runtime in RUNTIMES:
        app = root / "build" / f"SpinePreview{runtime}.app"
        with (app / "Contents/Info.plist").open("rb") as stream:
            plist = plistlib.load(stream)
        if plist.get("CFBundleShortVersionString") != info["base_version"] or plist.get("SpinePreviewReleaseVersion") != info["version"]:
            raise ValueError(f"Rebuild {app.name} with SPINE_RELEASE_VERSION={info['version']}")
        subprocess.run(["codesign", "--verify", "--deep", "--strict", str(app)], check=True)
        for binary in [app / "Contents/MacOS" / f"SpinePreview{runtime}",
                       app / "Contents/Frameworks/libEGL.dylib", app / "Contents/Frameworks/libGLESv2.dylib"]:
            subprocess.run(["lipo", str(binary), "-verify_arch", "arm64", "x86_64"], check=True)
        subprocess.run(["ditto", "-c", "-k", "--sequesterRsrc", "--keepParent", str(app),
                        str(output / f"SpinePreview{runtime}-{tag}-macos-universal.zip")], check=True)
    source_archives(root, commit, tag, output)
    hashes = {name: digest(output / name) for name in asset_names(tag)}
    (output / "SHA256SUMS").write_text("".join(f"{value}  {name}\n" for name, value in hashes.items()))
    (output / "manifest.json").write_text(json.dumps({**info, "commit": commit, "sha256": hashes}, indent=2) + "\n")
    (output / "release-notes.md").write_text(f"""SpinePreview {tag}

- `SpinePreview2.1-{tag}-macos-universal.zip`: Spine 2.1.25 C runtime (JSON only).
- `SpinePreview3.8-{tag}-macos-universal.zip`: Spine 3.8 runtime (JSON and binary).
- `SpinePreview4.0-{tag}-macos-universal.zip`: Spine 4.0 runtime (JSON and binary).
- `SpinePreview-{tag}-source.zip` / `.tar.gz`: source from commit `{commit}`, including bundled dependencies and licenses for offline builds.
- `SHA256SUMS`: SHA-256 checksums of all application and source archives.

Requires macOS 12 or later. All apps include ARM64 and x86_64 code and use ANGLE Metal.
Applications use ad-hoc signatures; Developer ID signing and Apple notarization are not configured.
Application release version: `{info['version']}`. Runtime versions are independent of the application release tag.
""")
    verify(output, tag)
    print(f"Packaged {tag}: {output}")


def verify(output, tag):
    metadata(tag)
    manifest = json.loads((output / "manifest.json").read_text())
    expected = asset_names(tag)
    if manifest.get("tag") != tag or set(manifest.get("sha256", {})) != set(expected):
        raise ValueError("Release manifest does not match the requested tag or asset set")
    archives = {p.name for p in output.iterdir() if p.name.endswith((".zip", ".tar.gz"))}
    if archives != set(expected):
        raise ValueError("Missing or unexpected release archives")
    for name in expected:
        if digest(output / name) != manifest["sha256"][name]:
            raise ValueError(f"Checksum mismatch: {name}")
    checksums = "".join(f"{manifest['sha256'][name]}  {name}\n" for name in expected)
    if (output / "SHA256SUMS").read_text() != checksums:
        raise ValueError("SHA256SUMS does not match the manifest")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("metadata").add_argument("tag")
    pack = commands.add_parser("package")
    pack.add_argument("--tag", required=True)
    pack.add_argument("--revision", help="Git revision; defaults to the release tag")
    pack.add_argument("--output", type=Path, required=True)
    check = commands.add_parser("verify")
    check.add_argument("--tag", required=True)
    check.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.command == "metadata":
            for key, value in metadata(args.tag).items():
                print(f"{key}={value}")
        elif args.command == "verify":
            verify(args.output.resolve(), args.tag)
        else:
            package(Path(__file__).resolve().parent.parent, args.tag, args.revision or args.tag, args.output.resolve())
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Release error: {error}\n")


if __name__ == "__main__":
    main()
