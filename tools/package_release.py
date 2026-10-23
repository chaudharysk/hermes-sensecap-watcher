#!/usr/bin/env python3
"""Create a ZIP from only the reviewed manifest; no Git history or local config."""
import argparse
import json
from pathlib import Path
import zipfile
from check_release import ROOT, MANIFEST, check

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "dist/hermes-watcher-public.zip")
    args = parser.parse_args()
    issues, _ = check()
    if issues:
        parser.error("release check failed; run tools/check_release.py for details")
    output = args.output.resolve()
    if output.exists(): parser.error("output already exists; choose a new filename")
    if output.is_relative_to(ROOT) and not output.is_relative_to(ROOT / "dist"):
        parser.error("use dist/ or an output path outside the repository")
    output.parent.mkdir(parents=True, exist_ok=True)
    files = json.loads((ROOT / MANIFEST).read_text())["files"]
    with zipfile.ZipFile(output, "x", zipfile.ZIP_DEFLATED) as archive:
        for name in sorted([*files, MANIFEST]):
            info = zipfile.ZipInfo("hermes-watcher-public/" + name, (2026, 10, 24, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = (0o100755 if name.endswith(".sh") else 0o100644) << 16
            archive.writestr(info, (ROOT / name).read_bytes())
    with zipfile.ZipFile(output) as archive:
        if archive.testzip() is not None: raise RuntimeError("ZIP integrity check failed")
    print("Created verified source archive:", output)

if __name__ == "__main__":
    main()
