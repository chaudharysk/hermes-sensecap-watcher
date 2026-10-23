"""Generic gateway status publisher. Never sends prompts, commands, or answers."""
import asyncio
import logging
import subprocess

logger = logging.getLogger("hooks.watcher-status")
_active = set()
_generation = 0
_lock = asyncio.Lock()
_idle_task = None

def publish(status):
    subprocess.run(["/usr/bin/docker", "exec", "hermes-mqtt", "mosquitto_pub",
                    "-h", "127.0.0.1", "-t", "hermes/status", "-m", status, "-q", "1", "-r"],
                   check=True, timeout=5, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)

async def send(status):
    try:
        await asyncio.to_thread(publish, status)
    except (OSError, subprocess.SubprocessError):
        logger.warning("Watcher status publish failed; check Docker and broker access")

def status_for_tools(names):
    if not isinstance(names, (list, tuple)): names = []
    text = " ".join(str(name) for name in names).lower()
    if any(word in text for word in ("web", "search", "browser")): return "SEARCHING"
    if names: return "RUNNING"
    return "THINKING"

async def back_to_online(generation):
    await asyncio.sleep(3)
    async with _lock:
        if generation == _generation and not _active:
            await send("ONLINE")

async def handle(event_type, context):
    global _generation, _idle_task
    async with _lock:
        _generation += 1
        if _idle_task is not None:
            _idle_task.cancel()
            _idle_task = None
        session = str(context.get("session_id") or "default")
        if event_type == "gateway:startup":
            await send("ONLINE")
        elif event_type in {"agent:start", "agent:step"}:
            _active.add(session)
            await send(status_for_tools(context.get("tool_names", [])))
        elif event_type == "agent:end":
            _active.discard(session)
            if _active:
                await send("THINKING")
            else:
                await send("DONE")
                _idle_task = asyncio.create_task(back_to_online(_generation))
