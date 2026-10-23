import importlib.util
from pathlib import Path
import unittest
from unittest.mock import AsyncMock, patch

class StatusTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        path = Path(__file__).resolve().parents[1] / "laptop/watcher-status/handler.py"
        spec = importlib.util.spec_from_file_location("status_under_test", path)
        self.hook = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.hook)
        self.hook.send = AsyncMock()

    async def asyncTearDown(self):
        if self.hook._idle_task is not None:
            self.hook._idle_task.cancel()

    async def test_ending_one_session_keeps_another_working(self):
        await self.hook.handle("agent:start", {"session_id": "one"})
        await self.hook.handle("agent:start", {"session_id": "two"})
        await self.hook.handle("agent:end", {"session_id": "one"})
        self.hook.send.assert_awaited_with("THINKING")
        self.assertEqual(self.hook._active, {"two"})
        self.assertIsNone(self.hook._idle_task)

    async def test_old_idle_timer_cannot_overwrite_new_task(self):
        previous = self.hook._generation
        await self.hook.handle("agent:start", {"session_id": "next"})
        self.hook.send.reset_mock()
        with patch.object(self.hook.asyncio, "sleep", new=AsyncMock()):
            await self.hook.back_to_online(previous)
        self.hook.send.assert_not_awaited()

    async def test_private_context_is_not_published(self):
        await self.hook.handle("agent:step", {"session_id": "one", "message": "private content",
                                               "tool_names": ["web_search"]})
        self.hook.send.assert_awaited_once_with("SEARCHING")

if __name__ == "__main__":
    unittest.main()
