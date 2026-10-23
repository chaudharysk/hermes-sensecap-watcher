#!/usr/bin/env python3
"""Optional offline WAV regeneration from the included original CC0 OGG files.

Requires imageio-ffmpeg. Firmware builds do not invoke this script.
"""
import json
from pathlib import Path
import subprocess
import wave
import imageio_ffmpeg

ROOT = Path(__file__).resolve().parents[1] / "firmware/overlay/main/view/hermes_audio"

def main():
    manifest = json.loads((ROOT / "manifest.json").read_text())
    for name, entry in manifest["sounds"].items():
        result = subprocess.run([imageio_ffmpeg.get_ffmpeg_exe(), "-v", "error",
            "-i", str(ROOT / entry["original"]), "-t", "0.85", "-ac", "1", "-ar", "16000",
            "-af", "volume=0.45", "-f", "s16le", "pipe:1"], check=True, capture_output=True)
        pcm = result.stdout
        if name in {"approval", "error"}: pcm = pcm + bytes(2880) + pcm
        with wave.open(str(ROOT / ("hermes_" + name + ".wav")), "wb") as output:
            output.setparams((1, 2, 16000, 0, "NONE", "not compressed"))
            output.writeframes(pcm)
    print("WAVs regenerated. Review hashes before changing the release manifest.")

if __name__ == "__main__":
    main()
