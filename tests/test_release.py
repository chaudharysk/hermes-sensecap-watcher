import hashlib
import importlib.util
import json
from pathlib import Path
import unittest
import wave

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("release_check", ROOT / "tools/check_release.py")
scan = importlib.util.module_from_spec(spec)
spec.loader.exec_module(scan)

class ReleaseTests(unittest.TestCase):
    def test_scanner_catches_private_addresses_and_paths(self):
        # Construct fixtures so the repository itself contains no personal path.
        address = ".".join(["192", "168", "7", "9"])
        self.assertIn("private LAN address", scan.content_findings(address))
        home = "/" + "home" + "/" + "example"
        self.assertIn("personal absolute path", scan.content_findings(home))
        self.assertEqual(scan.content_findings("127.0.0.1 192.0.2.10 $HOME/.hermes"), [])

    def test_wavs_match_sources_manifest_and_player_format(self):
        folder = ROOT / "firmware/overlay/main/view/hermes_audio"
        manifest = json.loads((folder / "manifest.json").read_text())
        for name, metadata in manifest["sounds"].items():
            path = folder / ("hermes_" + name + ".wav")
            self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), metadata["sha256"])
            with wave.open(str(path)) as sound:
                self.assertEqual((sound.getnchannels(), sound.getsampwidth(), sound.getframerate()), (1, 2, 16000))
                self.assertLess(sound.getnframes() / sound.getframerate(), 1)

    def test_local_settings_are_not_shipped(self):
        self.assertFalse((ROOT / "firmware/overlay/main/app/hermes_config.h").exists())
        self.assertIn("hermes_config.h", (ROOT / ".gitignore").read_text())
        self.assertIn("hermes_config.h", (ROOT / "firmware/integration.patch").read_text())

if __name__ == "__main__":
    unittest.main()
