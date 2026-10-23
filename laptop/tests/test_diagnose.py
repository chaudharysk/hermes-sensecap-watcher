import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("diagnose", Path(__file__).resolve().parents[1] / "diagnose.py")
diagnose = importlib.util.module_from_spec(spec)
spec.loader.exec_module(diagnose)

class DiagnosticTests(unittest.TestCase):
    def test_private_bodies_are_not_printed(self):
        for topic in ("hermes/status", "hermes/approval", "hermes/watcher/ack"):
            for body in ('PRIVATE command', '{"message":"PRIVATE command"}',
                         '{"type":"approval_required","request_id":"PRIVATE command"}',
                         '{"type":"approval_display","request_id":"PRIVATE command","displayed":true}'):
                self.assertNotIn("PRIVATE", diagnose.summarize(topic, body))

    def test_non_object_json_is_tolerated(self):
        for body in ('null', '123', '[]', '"private"'):
            self.assertIn("Unrecognized", diagnose.summarize("hermes/approval", body))

    def test_installed_file_is_read_without_executing_it(self):
        with tempfile.TemporaryDirectory() as folder:
            home = Path(folder)
            self.assertIn("MISSING", diagnose.installed_version(home))
            plugin = home / "plugins/watcher-approval/__init__.py"
            plugin.parent.mkdir(parents=True)
            plugin.write_text('VERSION = "1.2.0"\nraise RuntimeError("must not execute")\n')
            self.assertEqual(diagnose.installed_version(home), "1.2.0")

if __name__ == "__main__":
    unittest.main()
