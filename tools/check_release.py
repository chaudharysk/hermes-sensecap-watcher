#!/usr/bin/env python3
"""Check the reviewed export manifest and common private-data patterns, offline."""
import hashlib
import ipaddress
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = "release-manifest.json"
IGNORED_DIRS = {".git", "__pycache__", ".venv", "dist"}
BINARY_SUFFIXES = {".wav", ".ogg"}
REVIEWED_PHOTOS = {"docs/images/watcher-working.jpg", "docs/images/watcher-done.jpg"}

def content_findings(data):
    findings = []
    if re.search(r"-{5}BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-{5}", data):
        findings.append("private key")
    if re.search(r"(?:gh[pousr]_[A-Za-z0-9]{20,}|github_pat_[A-Za-z0-9_]{30,}|sk-[A-Za-z0-9_-]{24,})", data):
        findings.append("possible access token")
    if re.search(r"\b\d{7,12}:[A-Za-z0-9_-]{30,}\b", data):
        findings.append("possible bot token")
    if re.search(r"(?:[A-Za-z]:[/\\]Users[/\\][^\s/\\]+|/(?:home|Users)/[A-Za-z0-9_.-]+)", data):
        findings.append("personal absolute path")
    for address in re.findall(r"(?<![\d.])(?:\d{1,3}\.){3}\d{1,3}(?![\d.])", data):
        try: value = ipaddress.ip_address(address)
        except ValueError: continue
        if any(value in ipaddress.ip_network(net) for net in ((0x0A000000, 8), (0xAC100000, 12), (0xC0A80000, 16))):
            findings.append("private LAN address")
            break
    return findings

def check(root=ROOT):
    manifest = json.loads((root / MANIFEST).read_text(encoding="utf-8"))
    expected = manifest["files"]
    problems = []
    seen = set()
    for path in sorted(root.rglob("*")):
        rel = path.relative_to(root)
        if any(part in IGNORED_DIRS for part in rel.parts): continue
        if path.is_symlink():
            problems.append((rel.as_posix(), "symlink is not allowed"))
            continue
        if not path.is_file(): continue
        name = rel.as_posix()
        if name == MANIFEST: continue
        seen.add(name)
        if name not in expected:
            problems.append((name, "not in reviewed file manifest"))
            continue
        data = path.read_bytes()
        if hashlib.sha256(data).hexdigest() != expected[name]["sha256"]:
            problems.append((name, "differs from reviewed snapshot"))
        if name in REVIEWED_PHOTOS:
            # These specific photos were visually reviewed and had EXIF/XMP/IPTC
            # metadata removed. Their bytes are still pinned by the manifest.
            if not data.startswith(b"\xff\xd8\xff"):
                problems.append((name, "reviewed photo is not a JPEG"))
        elif path.suffix not in BINARY_SUFFIXES:
            try: text = data.decode("utf-8")
            except UnicodeDecodeError:
                problems.append((name, "unexpected binary content"))
                continue
            for issue in content_findings(text): problems.append((name, issue))
    for name in expected.keys() - seen: problems.append((name, "missing reviewed file"))
    return problems, len(seen)

def main():
    problems, count = check()
    for path, reason in problems:
        print(path + ": " + reason)  # Never print matching secret values.
    if problems:
        print("Release check failed. Review changes before publishing.")
        return 1
    print(f"Release check passed: {count} reviewed files; no configured pattern findings.")
    print("This checks the prepared export, not other folders or historical Git commits.")
    return 0

if __name__ == "__main__":
    sys.exit(main())
