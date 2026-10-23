#!/usr/bin/env python3
"""Apply the integration to an unchanged, pinned upstream checkout. No build/flash."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]

def git(target, *args):
    return subprocess.check_output(["git", "-C", str(target), *args], text=True).strip()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("checkout", type=Path, help="fresh SenseCAP-Watcher-Firmware checkout")
    parser.add_argument("--check", action="store_true", help="validate without writing anything")
    args = parser.parse_args()
    target = args.checkout.resolve()
    upstream = json.loads((ROOT / "firmware/upstream.json").read_text())
    if target == ROOT or ROOT in target.parents or target in ROOT.parents:
        parser.error("use a separate upstream checkout, outside this release repository")
    try:
        if Path(git(target, "rev-parse", "--show-toplevel")).resolve() != target:
            parser.error("checkout must be the upstream Git root")
        if git(target, "rev-parse", "HEAD") != upstream["commit"]:
            parser.error("checkout must be at the pinned commit in firmware/upstream.json")
        if git(target, "status", "--porcelain", "--untracked-files=normal"):
            parser.error("checkout has changes; use a fresh checkout so personal work is not overwritten")
        overlay = ROOT / "firmware/overlay"
        copies = []
        project = target / upstream["project"]
        for source in sorted(overlay.rglob("*")):
            if source.is_symlink():
                parser.error("symlinks are not allowed in the release overlay")
            if not source.is_file(): continue
            destination = project / source.relative_to(overlay)
            if not destination.resolve().is_relative_to(target):
                parser.error("destination escapes the checkout")
            if destination.exists():
                parser.error("overlay destination already exists: " + str(destination.relative_to(target)))
            copies.append((source, destination))
        patch = ROOT / "firmware/integration.patch"
        # Git verifies every hunk before any file is changed.
        git(target, "apply", "--check", str(patch))
        if args.check:
            print("Patch and overlay checks passed; no files changed.")
            return
        git(target, "apply", str(patch))
        for source, destination in copies:
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, destination)
        config = project / "main/app/hermes_config.h"
        # Preserve a pre-existing ignored local config, if there is one.
        if not config.exists():
            shutil.copyfile(project / "main/app/hermes_config.example.h", config)
        print("Integration applied. Edit main/app/hermes_config.h in the firmware project.")
        print("No build or flash performed. Local broker settings are ignored by Git.")
    except subprocess.CalledProcessError:
        parser.exit(1, "Git validation/apply failed. Review the error above; no build or flash was run.\n")

if __name__ == "__main__":
    main()
