#!/usr/bin/env python3
"""Validate release identity and the three native binary downloads."""
import argparse
import re
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "backend"))
from forest_backend.changelog import entries as changelog_entries


def version(root=ROOT):
    cmake = (root / "CMakeLists.txt").read_text()
    match = re.search(r"project\(ForestLauncher VERSION (0\.\d+\.\d+) ", cmake)
    if not match:
        raise ValueError("Missing pre-1.0 CMake version")
    return match[1]


def release_notes(tag, root=ROOT):
    current = version(root)
    if tag != f"v{current}":
        raise ValueError(f"Tag {tag!r} does not match source version v{current}")
    changelog = root / "docs" / "CHANGELOG.md"
    if not changelog.is_file():
        raise ValueError("Missing docs/CHANGELOG.md")
    if changelog.stat().st_size > 16 * 1024 * 1024:
        raise ValueError("Changelog exceeds 16 MiB")
    releases = changelog_entries(changelog.read_text(encoding="utf-8"))
    matches = [entry["body"] for entry in releases if entry["version"] == current]
    if not matches or not matches[0].strip():
        raise ValueError(f"docs/CHANGELOG.md needs a non-empty ## {current} section")
    if len(matches[0].encode("utf-8")) > 128 * 1024:
        raise ValueError("Current release notes exceed 128 KiB")
    return matches[0] + "\n"


def debian_dependencies(generated):
    # These Ubuntu names and their Debian equivalents provide the same Qt
    # SONAMEs on our amd64 targets. Preserve dpkg-shlibdeps version bounds.
    names = {"libqt6gui6t64": "libqt6gui6", "libqt6widgets6t64": "libqt6widgets6"}
    result = []
    for dependency in generated.strip().split(","):
        dependency = dependency.strip()
        match = re.fullmatch(r"(libqt6gui6t64|libqt6widgets6t64)(\s*\([^)]*\))?", dependency)
        if match:
            dependency += " | " + names[match[1]] + (match[2] or "")
        result.append(dependency)
    return ", ".join(result)


def validate_tag(tag):
    value = version()
    if tag != f"v{value}":
        raise ValueError(f"Tag {tag!r} does not match source version v{value}")
    release_notes(tag)
    if subprocess.check_output(["git", "cat-file", "-t", tag], cwd=ROOT, text=True).strip() != "tag":
        raise ValueError("Releases require an annotated tag")
    commit = subprocess.check_output(["git", "rev-parse", f"{tag}^{{commit}}"], cwd=ROOT)
    if commit != subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT):
        raise ValueError("Checkout is not the tagged commit")
    return value


def verify_assets(tag, output):
    value = validate_tag(tag)
    output = output.resolve()
    expected = {
        f"forest-launcher-{value}-x86_64.pkg.tar.zst",
        f"forest-launcher_{value}_amd64.deb",
        f"forest-launcher-{value}.fc44.x86_64.rpm",
    }
    actual = {p.name for p in output.iterdir() if p.is_file()}
    if actual != expected:
        raise ValueError(f"Unexpected release files; missing={expected - actual}, extra={actual - expected}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("version", "notes", "deb-dependencies", "validate", "verify-assets"))
    parser.add_argument("--tag")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.command == "version":
        print(version())
    elif args.command == "notes":
        sys.stdout.write(release_notes(args.tag))
    elif args.command == "deb-dependencies":
        print(debian_dependencies(sys.stdin.read()))
    elif args.command == "validate":
        print(validate_tag(args.tag))
    else:
        if not args.output:
            parser.error("--output is required")
        verify_assets(args.tag, args.output)


if __name__ == "__main__":
    main()
