#!/usr/bin/env python3
"""Check the real broker/notifier/Watcher path without changing any approval.

Run on the Hermes laptop while requesting a NEW permission prompt in Telegram.
Only diagnostic pings are published. Never prints command/prompt/answer bodies.
"""
import argparse
import ast
import hashlib
import json
import os
from pathlib import Path
import queue
import subprocess
import threading
import time
import uuid

PREFIX = ["/usr/bin/docker", "exec", "hermes-mqtt"]

def installed_version(home):
    path = home / "plugins/watcher-approval/__init__.py"
    if not path.exists():
        return "MISSING from " + str(path)
    try:
        tree = ast.parse(path.read_text())
        for node in tree.body:
            if isinstance(node, ast.Assign) and any(
                    isinstance(t, ast.Name) and t.id == "VERSION" for t in node.targets):
                version = ast.literal_eval(node.value)
                return str(version)[:40]
    except (OSError, SyntaxError, ValueError):
        return "cannot read version"
    return "unknown version"

def summarize(topic, payload):
    """Return bounded metadata only, even when status contains a private prompt."""
    if topic == "hermes/status":
        return "Status message received (body hidden)"
    try:
        data = json.loads(payload)
    except ValueError:
        return "Non-JSON message received (body hidden)"
    if not isinstance(data, dict):
        return "Unrecognized message (body hidden)"
    if topic == "hermes/notifier/health" and data.get("type") == "notifier_health":
        values = {k: data.get(k) for k in ("version", "pid", "pending", "requests", "resolutions")}
        return "LIVE notifier " + json.dumps(values, ensure_ascii=True)[:220]
    if topic == "hermes/approval":
        event = data.get("type")
        if event not in {"approval_required", "approval_resolved"}:
            return "Approval-topic message received (unrecognized envelope)"
        kind = "choice" if data.get("kind") == "clarify" else "permission"
        digest = hashlib.sha256(str(data.get("request_id", "")).encode()).hexdigest()[:8]
        return kind + " " + event + " id=" + digest
    if topic == "hermes/watcher/ack":
        if data.get("type") == "pong":
            return "Watcher diagnostic reply received"
        if data.get("type") == "approval_display":
            digest = hashlib.sha256(str(data.get("request_id", "")).encode()).hexdigest()[:8]
            return "Watcher applied alert=" + str(data.get("displayed") is True) + " id=" + digest
    return "Other diagnostic message received"

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seconds", type=int, default=180,
                        help="watch duration, 15–3600 seconds (default: 180)")
    options = parser.parse_args()
    if not 15 <= options.seconds <= 3600:
        parser.error("--seconds must be between 15 and 3600")
    home = Path(os.environ.get("HERMES_HOME") or Path.home() / ".hermes")
    print("Installed notifier:", installed_version(home), flush=True)
    print(f"Watching for {options.seconds} seconds. Trigger a NEW Telegram permission/choice now.", flush=True)
    print("No approval will be answered, cleared, or replaced by this check.", flush=True)
    # -W is a subscriber option, never a publisher option.
    args = PREFIX + ["mosquitto_sub", "-h", "127.0.0.1", "-v", "-W", str(options.seconds)]
    for topic in ("hermes/status", "hermes/approval", "hermes/notifier/health", "hermes/watcher/ack"):
        args += ["-t", topic]
    try:
        process = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    except OSError:
        print("FAIL: cannot start Docker. Run this on the Linux laptop as the Hermes user.")
        return 1
    messages = queue.Queue()
    def read():
        for line in process.stdout:
            messages.put(line)
    reader = threading.Thread(target=read, daemon=True)
    reader.start()
    deadline = time.monotonic() + options.seconds
    next_ping = time.monotonic() + 1
    ping_id = uuid.uuid4().hex
    notifier = watcher = approval = status = False
    shown = set()
    requests = set()
    try:
        while time.monotonic() < deadline:
            now = time.monotonic()
            if now >= next_ping:
                next_ping = now + 8
                try:
                    subprocess.run(PREFIX + ["mosquitto_pub", "-h", "127.0.0.1",
                        "-t", "hermes/diagnostic", "-m", json.dumps({"type": "ping", "request_id": ping_id}),
                        "-q", "1"], check=True, timeout=4, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
                except (OSError, subprocess.SubprocessError):
                    print("FAIL: diagnostic publish failed (Docker/container/broker/access).", flush=True)
                    break
            try:
                line = messages.get(timeout=0.2).strip()
            except queue.Empty:
                if process.poll() is not None:
                    break
                continue
            topic, _, payload = line.partition(" ")
            print(summarize(topic, payload), flush=True)
            status |= topic == "hermes/status"
            try:
                data = json.loads(payload)
                if not isinstance(data, dict): continue
            except ValueError:
                continue
            notifier |= topic == "hermes/notifier/health" and data.get("type") == "notifier_health"
            watcher |= topic == "hermes/watcher/ack" and data.get("type") == "pong" and data.get("request_id") == ping_id
            if topic == "hermes/approval" and data.get("type") == "approval_required":
                approval = True
                requests.add(str(data.get("request_id", "")))
            if topic == "hermes/watcher/ack" and data.get("type") == "approval_display" and data.get("displayed") is True:
                shown.add(str(data.get("request_id", "")))
    finally:
        if process.poll() is None:
            process.terminate()
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
    print("\nRESULT")
    print("Notifier heartbeat:", "YES" if notifier else "NOT SEEN: old/unloaded plugin, wrong profile, or publish failure")
    print("Watcher reply:", "YES" if watcher else "NOT SEEN: older firmware, device disconnected, or different broker")
    print("Permission/choice event:", "SEEN (may include retained request)" if approval else "NOT SEEN during this check")
    print("Matching display acknowledgement:", "YES" if requests & shown else "NOT SEEN")
    if status and not approval:
        print("The status publisher is visible; no structured approval request was observed.")
    if process.returncode not in (0, 27, -15) and not (status or notifier or watcher or approval):
        print("Subscriber failed: check Docker access, hermes-mqtt, and mosquitto_sub.")
    print("Paste this output back. It contains event metadata, not your commands or answers.")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
