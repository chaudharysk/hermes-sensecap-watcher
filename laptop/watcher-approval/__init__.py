"""Observe Hermes approvals and choices without answering any human prompt."""

import hashlib
import json
import logging
import os
import queue
import subprocess
import threading
import uuid
from collections import OrderedDict, deque

logger = logging.getLogger("plugins.watcher-approval")

DOCKER = "/usr/bin/docker"
CONTAINER = "hermes-mqtt"
TOPIC = "hermes/approval"
VERSION = "1.2.0"


def _wire_id(request_id):
    """Keep IDs within Watcher's 95-byte limit, without truncation collisions."""
    value = str(request_id or "")
    if not value:
        return ""
    if "\x00" not in value and len(value.encode("utf-8")) <= 95:
        return value
    return "sha256-" + hashlib.sha256(value.encode("utf-8")).hexdigest()


def _message(prompt, kind="approval"):
    # Native human-input hooks already force-redact this text. Legacy hooks
    # never pass their raw command or description to this function.
    text = "".join(c if ord(c) >= 32 and ord(c) != 127 else " "
                   for c in str(prompt or ""))
    text = " ".join(text.split())
    text = text.encode("utf-8")[:180].decode("utf-8", errors="ignore")
    if kind == "clarify":
        return "Waiting for your choice: " + text if text else "Choose an option in Telegram"
    return "Waiting for permission: " + text if text else "Waiting for your permission in Hermes"


def _outcome(choice, kind="approval"):
    value = str(choice or "").lower()
    if kind == "clarify":
        # The existing display protocol uses approved to clear an answered
        # notice. With kind=clarify this means answered, never permission granted.
        if value == "submitted":
            return "approved"
        return "expired" if value in {"timeout", "timed_out"} else "cancelled"
    if value in {"once", "session", "always"}:
        return "approved"
    if value == "deny":
        return "denied"
    if value in {"timeout", "timed_out"}:
        return "expired"
    # Cancelled, undelivered, failed, and unknown outcomes never mean approved.
    return "cancelled"


def _publish(payload, topic=TOPIC, retain=True):
    encoded = json.dumps(payload, ensure_ascii=True, separators=(",", ":"))
    if len(encoded.encode("utf-8")) > 2047:
        raise ValueError("Watcher approval notification exceeds MQTT message limit")
    subprocess.run(
        [DOCKER, "exec", CONTAINER, "mosquitto_pub",
         "-h", "127.0.0.1", "-t", topic, "-m", encoded,
         "-q", "1"] + (["-r"] if retain else []),
        # -W belongs to mosquitto_sub; mosquitto_pub rejects it. Bound this
        # subprocess with Python's timeout instead of a subscriber-only flag.
        timeout=5,
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
        text=True,
    )


def _failure_message(exc):
    """Identify common publisher failures without revealing raw stderr/payloads."""
    if isinstance(exc, subprocess.TimeoutExpired):
        return "MQTT send timed out"
    if isinstance(exc, FileNotFoundError):
        return "Docker executable not found"
    if isinstance(exc, PermissionError):
        return "Hermes cannot execute Docker"
    if isinstance(exc, subprocess.CalledProcessError):
        stderr = exc.stderr or ""
        if isinstance(stderr, bytes):
            stderr = stderr.decode("utf-8", errors="replace")
        stderr = stderr.lower()
        if "unknown option" in stderr:
            return "Mosquitto rejected publisher options"
        if "permission denied" in stderr:
            return "Gateway user cannot access Docker"
        if "no such container" in stderr or "is not running" in stderr:
            return "MQTT container unavailable"
        if "not authorized" in stderr or "not authorised" in stderr:
            return "MQTT broker denied access"
        if "connection refused" in stderr:
            return "MQTT broker connection refused"
        return "Publisher exited with code " + str(exc.returncode)
    return "Publisher failed: " + type(exc).__name__


class WatcherApproval:
    def __init__(self):
        self.pending = OrderedDict()
        self.legacy_ids = {}
        self.lock = threading.Lock()
        self.outbox = queue.Queue()
        self.stopping = threading.Event()
        self.worker = None
        self.mode = "Not registered"
        self.last_mqtt = "No event published yet"
        self.received_requests = 0
        self.received_resolutions = 0

    def start(self):
        self.worker = threading.Thread(
            target=self._run, name="watcher-approval-mqtt", daemon=True
        )
        self.worker.start()

    def close(self):
        self.stopping.set()
        self.outbox.put(None)
        if self.worker is not None:
            self.worker.join(timeout=0.2)

    def _request_locked(self, request_id, prompt, kind="approval"):
        self.received_requests += 1
        if request_id in self.pending:
            return
        # Keep the legacy envelope so the firmware already flashed can display
        # choices. New firmware uses kind to label them WAITING FOR YOU.
        payload = {"type": "approval_required", "kind": kind, "request_id": request_id,
                   "message": _message(prompt, kind)}
        self.pending[request_id] = payload
        self.outbox.put(payload)
        logger.info("Watcher %s request queued", kind)

    def _resolve_locked(self, request_id, choice, kind="approval"):
        self.received_resolutions += 1
        pending = self.pending.get(request_id)
        if pending is None or pending["kind"] != kind:
            return
        del self.pending[request_id]
        self.outbox.put({"type": "approval_resolved", "kind": kind, "request_id": request_id,
                         "outcome": _outcome(choice, kind)})
        if self.pending:
            # Watcher displays one request. Restore the newest remaining one
            # after a concurrent request is answered, rather than hiding it.
            self.outbox.put(next(reversed(self.pending.values())))
        logger.info("Watcher %s resolution queued", kind)

    def human_request(self, kind="", request_id="", prompt="", **kwargs):
        if kind not in {"approval", "clarify"}:
            return
        request_id = _wire_id(request_id)
        if not request_id:
            logger.warning("Ignoring human-input request without a Hermes request ID")
            return
        with self.lock:
            self._request_locked(request_id, prompt, kind)

    def human_resolved(self, kind="", request_id="", outcome="", **kwargs):
        if kind not in {"approval", "clarify"}:
            return
        with self.lock:
            self._resolve_locked(_wire_id(request_id), outcome, kind)

    def report(self, raw_args=""):
        """Read-only /watcher command verifies the plugin in the actual chat host."""
        with self.lock:
            return ("Watcher notifier v" + VERSION + " active\n"
                    + "Prompts: " + self.mode + "\n"
                    + "Pending notices: " + str(len(self.pending)) + "\n"
                    + "MQTT: " + self.last_mqtt + "\n"
                    + "Sent means broker accepted; use watcher-diagnose.py to check the display.")

    def heartbeat(self):
        # Live, non-retained evidence from the actual loaded plugin process.
        # No commands, prompts, answers, chat IDs, or credentials are included.
        with self.lock:
            payload = {"type": "notifier_health", "version": VERSION, "pid": os.getpid(),
                       "mode": self.mode, "pending": len(self.pending),
                       "requests": self.received_requests, "resolutions": self.received_resolutions}
        try:
            _publish(payload, topic="hermes/notifier/health", retain=False)
        except (OSError, subprocess.SubprocessError, ValueError) as exc:
            with self.lock:
                self.last_mqtt = _failure_message(exc)

    @staticmethod
    def _legacy_key(command, pattern_key, session_key, surface, kwargs):
        # Older hooks lack a request ID. Correlate pre/post using their common
        # coordinates and a digest; never publish or log the raw command.
        coordinates = [str(session_key or ""), str(surface or ""),
                       str(pattern_key or ""), str(command or ""),
                       str(kwargs.get("turn_id") or ""),
                       str(kwargs.get("tool_call_id") or "")]
        encoded = json.dumps(coordinates, ensure_ascii=True).encode("utf-8")
        return hashlib.sha256(encoded).hexdigest()

    def legacy_request(self, command="", pattern_key="", session_key="",
                       surface="", **kwargs):
        if surface == "smart":
            return
        key = self._legacy_key(command, pattern_key, session_key, surface, kwargs)
        with self.lock:
            request_id = uuid.uuid4().hex
            self.legacy_ids.setdefault(key, deque()).append(request_id)
            self._request_locked(request_id, "")

    def legacy_resolved(self, command="", pattern_key="", session_key="",
                        surface="", choice="", **kwargs):
        if surface == "smart" or str(choice).startswith("smart_"):
            return
        key = self._legacy_key(command, pattern_key, session_key, surface, kwargs)
        with self.lock:
            ids = self.legacy_ids.get(key)
            if not ids:
                logger.warning("No matching legacy Watcher permission request")
                return
            request_id = ids.popleft()
            if not ids:
                del self.legacy_ids[key]
            self._resolve_locked(request_id, choice)

    def _run(self):
        while not self.stopping.is_set():
            try:
                payload = self.outbox.get(timeout=10)
            except queue.Empty:
                self.heartbeat()
                continue
            if payload is None:
                return
            retry_seconds = 1
            while not self.stopping.is_set():
                if payload["type"] == "approval_required":
                    with self.lock:
                        still_pending = payload["request_id"] in self.pending
                    if not still_pending:
                        # If answered while MQTT was unavailable, deliver its
                        # resolution without replaying an obsolete prompt.
                        break
                try:
                    _publish(payload)
                    with self.lock:
                        self.last_mqtt = payload["kind"] + " " + payload["type"] + " sent"
                    logger.info("Watcher %s published", payload["type"])
                    break
                except (OSError, subprocess.SubprocessError, ValueError) as exc:
                    # Do not log subprocess arguments, command text, or URLs.
                    failure = _failure_message(exc)
                    logger.warning("Watcher MQTT publish failed (%s); retrying", failure)
                    with self.lock:
                        self.last_mqtt = failure + "; retrying"
                    if self.stopping.wait(retry_seconds):
                        return
                    retry_seconds = min(retry_seconds * 2, 8)


def register(ctx):
    try:
        from hermes_cli.plugins import VALID_HOOKS
    except ImportError as exc:
        raise RuntimeError(
            "This Hermes version cannot report its supported plugin hooks. "
            "Watcher permission notifications require a newer Hermes version."
        ) from exc

    observer = WatcherApproval()
    if {"on_human_input_request", "on_human_input_resolved"} <= set(VALID_HOOKS):
        ctx.register_hook("on_human_input_request", observer.human_request)
        ctx.register_hook("on_human_input_resolved", observer.human_resolved)
        mode = "approvals and clarification choices"
    elif {"pre_approval_request", "post_approval_response"} <= set(VALID_HOOKS):
        ctx.register_hook("pre_approval_request", observer.legacy_request)
        ctx.register_hook("post_approval_response", observer.legacy_resolved)
        mode = "approvals only; this Hermes version lacks clarification hooks"
        logger.warning("Watcher clarification choices need Hermes human-input hooks")
    else:
        raise RuntimeError(
            "This Hermes version has no supported approval hook pair. "
            "Watcher permission notifications require a newer Hermes version."
        )
    on_unload = getattr(ctx, "on_unload", None)
    if callable(on_unload):
        on_unload(observer.close)
    observer.mode = mode
    register_command = getattr(ctx, "register_command", None)
    if callable(register_command):
        register_command("watcher", observer.report, "Check Watcher notification status")
    observer.start()
    logger.info("watcher-approval enabled using %s", mode)
