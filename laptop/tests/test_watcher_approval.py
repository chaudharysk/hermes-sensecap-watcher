"""Lifecycle tests use a fake Hermes host and mock MQTT; nothing is published."""

import importlib.util
import json
import subprocess
import sys
import threading
import types
import unittest
from pathlib import Path
from unittest.mock import patch

PLUGIN = Path(__file__).resolve().parents[1] / "watcher-approval" / "__init__.py"
spec = importlib.util.spec_from_file_location("watcher_approval_under_test", PLUGIN)
plugin = importlib.util.module_from_spec(spec)
spec.loader.exec_module(plugin)


def queued(observer):
    messages = []
    while not observer.outbox.empty():
        messages.append(observer.outbox.get_nowait())
    return messages


class LifecycleTests(unittest.TestCase):
    def test_native_request_and_matching_answer(self):
        observer = plugin.WatcherApproval()
        observer.human_request(kind="approval", request_id="native-123",
                               prompt="Run the requested command?", platform="telegram")
        observer.human_resolved(kind="approval", request_id="native-123", outcome="once")
        request, resolution = queued(observer)
        self.assertEqual(request["type"], "approval_required")
        self.assertIn("Waiting for permission", request["message"])
        self.assertEqual(resolution, {"type": "approval_resolved",
                                     "kind": "approval",
                                     "request_id": request["request_id"],
                                     "outcome": "approved"})

    def test_password_and_unknown_inputs_are_not_published(self):
        observer = plugin.WatcherApproval()
        for kind in ("sudo", "unknown"):
            observer.human_request(kind=kind, request_id=kind, prompt="Private input")
            observer.human_resolved(kind=kind, request_id=kind, outcome="provided")
        self.assertEqual(queued(observer), [])

    def test_telegram_ftp_choice_opens_and_closes_a_notice(self):
        observer = plugin.WatcherApproval()
        observer.human_request(kind="clarify", request_id="ftp-choice",
                               prompt="Which protocol should I set up: SFTP, FTPS, or plain FTP?",
                               platform="telegram")
        observer.human_resolved(kind="clarify", request_id="ftp-choice", outcome="submitted")
        request, resolution = queued(observer)
        self.assertEqual(request["kind"], "clarify")
        self.assertIn("Waiting for your choice", request["message"])
        self.assertIn("SFTP", request["message"])
        # The already flashed firmware recognizes this envelope. kind=clarify
        # lets new firmware display WAITING FOR YOU, without granting permission.
        self.assertEqual(request["type"], "approval_required")
        self.assertEqual(resolution, {"type": "approval_resolved", "kind": "clarify",
                                     "request_id": "ftp-choice", "outcome": "approved"})

    def test_unanswered_choice_is_not_reported_as_answered(self):
        for outcome in ("timed_out", "cancelled", "undelivered", "terminated", "error", "once"):
            with self.subTest(outcome=outcome):
                observer = plugin.WatcherApproval()
                observer.human_request(kind="clarify", request_id="choice")
                observer.human_resolved(kind="clarify", request_id="choice", outcome=outcome)
                resolution = queued(observer)[-1]
                self.assertEqual(resolution["outcome"],
                                 "expired" if outcome == "timed_out" else "cancelled")

    def test_answer_for_wrong_kind_does_not_dismiss_a_choice(self):
        observer = plugin.WatcherApproval()
        observer.human_request(kind="clarify", request_id="choice")
        observer.human_resolved(kind="approval", request_id="choice", outcome="once")
        self.assertEqual(len(queued(observer)), 1)
        self.assertIn("choice", observer.pending)

    def test_answered_choice_restores_a_pending_permission_request(self):
        observer = plugin.WatcherApproval()
        observer.human_request(kind="approval", request_id="permission")
        observer.human_request(kind="clarify", request_id="choice")
        observer.human_resolved(kind="clarify", request_id="choice", outcome="submitted")
        messages = queued(observer)
        self.assertEqual(messages[-1]["kind"], "approval")
        self.assertEqual(messages[-1]["request_id"], "permission")

    def test_duplicate_request_and_stale_answer_cannot_clear_current(self):
        observer = plugin.WatcherApproval()
        observer.human_request(kind="approval", request_id="current", prompt="Check this")
        observer.human_request(kind="approval", request_id="current", prompt="Duplicate")
        observer.human_resolved(kind="approval", request_id="old", outcome="once")
        self.assertEqual(len(queued(observer)), 1)
        self.assertEqual(list(observer.pending), ["current"])

    def test_failures_and_denials_never_report_approved(self):
        cases = {"once": "approved", "session": "approved", "always": "approved",
                 "deny": "denied", "timeout": "expired", "cancelled": "cancelled",
                 "notify_failed": "cancelled", "transport_timeout": "cancelled",
                 "error": "cancelled", "future_unknown_choice": "cancelled"}
        for choice, expected in cases.items():
            with self.subTest(choice=choice):
                observer = plugin.WatcherApproval()
                observer.human_request(kind="approval", request_id="test")
                observer.human_resolved(kind="approval", request_id="test", outcome=choice)
                self.assertEqual(queued(observer)[-1]["outcome"], expected)

    def test_remaining_request_reappears_when_newest_is_answered(self):
        observer = plugin.WatcherApproval()
        for request_id in ("first", "second"):
            observer.human_request(kind="approval", request_id=request_id)
        observer.human_resolved(kind="approval", request_id="second", outcome="deny")
        observer.human_resolved(kind="approval", request_id="first", outcome="timeout")
        messages = queued(observer)
        self.assertEqual([m["request_id"] for m in messages],
                         ["first", "second", "second", "first", "first"])
        self.assertEqual([m["type"] for m in messages],
                         ["approval_required", "approval_required", "approval_resolved",
                          "approval_required", "approval_resolved"])

    def test_long_unicode_ids_stay_distinct_and_resolve(self):
        observer = plugin.WatcherApproval()
        native = "\U0001f642" * 40
        observer.human_request(kind="approval", request_id=native, prompt="\u754c" * 200)
        observer.human_request(kind="approval", request_id=native + "other")
        observer.human_resolved(kind="approval", request_id=native, outcome="once")
        messages = queued(observer)
        first, second, resolution = messages[:3]
        self.assertLessEqual(len(first["request_id"].encode("utf-8")), 95)
        self.assertNotEqual(first["request_id"], second["request_id"])
        self.assertEqual(first["request_id"], resolution["request_id"])
        for message in messages:
            encoded = json.dumps(message, ensure_ascii=True).encode("utf-8")
            self.assertLessEqual(len(encoded), 2047)

    def test_control_characters_cannot_truncate_wire_id_or_visible_text(self):
        observer = plugin.WatcherApproval()
        native_id = "id\x00suffix"
        observer.human_request(kind="approval", request_id=native_id,
                               prompt="Review\x00this\ncommand")
        observer.human_resolved(kind="approval", request_id=native_id, outcome="deny")
        request, resolution = queued(observer)
        self.assertNotIn("\x00", request["request_id"])
        self.assertNotIn("\x00", request["message"])
        self.assertIn("Review this command", request["message"])
        self.assertEqual(request["request_id"], resolution["request_id"])

    def test_legacy_prompt_is_generic_and_correlated(self):
        observer = plugin.WatcherApproval()
        context = {"command": "secret-command TOKEN=private", "description": "private reason",
                   "pattern_key": "test", "session_key": "telegram-chat", "surface": "gateway"}
        observer.legacy_request(**context)
        observer.legacy_request(**context)
        observer.legacy_resolved(choice="once", **context)
        observer.legacy_resolved(choice="deny", **context)
        first, second, resolve_first, restore_second, resolve_second = queued(observer)
        self.assertNotIn("private", json.dumps(first))
        self.assertEqual(resolve_first["request_id"], first["request_id"])
        self.assertEqual(restore_second["request_id"], second["request_id"])
        self.assertEqual(resolve_second["request_id"], second["request_id"])
        self.assertEqual(observer.pending, {})
        self.assertEqual(observer.legacy_ids, {})

    def test_legacy_smart_decisions_do_not_show_a_human_wait(self):
        observer = plugin.WatcherApproval()
        observer.legacy_request(command="something", surface="smart")
        observer.legacy_resolved(command="something", surface="smart", choice="smart_approve")
        self.assertEqual(queued(observer), [])


class PublisherTests(unittest.TestCase):
    def test_publish_uses_existing_container_retention_and_qos_one(self):
        event = {"type": "approval_required", "request_id": "test", "message": "Review this"}
        with patch.object(plugin.subprocess, "run") as run:
            plugin._publish(event)
        arguments = run.call_args.args[0]
        self.assertEqual(arguments[:3], ["/usr/bin/docker", "exec", "hermes-mqtt"])
        self.assertEqual(arguments[arguments.index("-t") + 1], "hermes/approval")
        self.assertEqual(json.loads(arguments[arguments.index("-m") + 1]), event)
        self.assertIn("-r", arguments)
        self.assertEqual(arguments[arguments.index("-q") + 1], "1")
        self.assertTrue(run.call_args.kwargs["check"])

    def test_publisher_has_no_subscriber_only_timeout_flag(self):
        # Mosquitto's argument parser rejects -W when CLIENT_PUB is selected.
        # Earlier tests mocked delivery and missed that external CLI contract.
        with patch.object(plugin.subprocess, "run") as run:
            plugin._publish({"type": "approval_required", "request_id": "test"})
        self.assertNotIn("-W", run.call_args.args[0])
        self.assertEqual(run.call_args.kwargs["timeout"], 5)

    def test_publisher_failures_report_actionable_reasons_without_payloads(self):
        cases = [
            ("Error: Unknown option '-W'.", "Mosquitto rejected publisher options"),
            ("permission denied while connecting to Docker", "Gateway user cannot access Docker"),
            ("No such container: hermes-mqtt", "MQTT container unavailable"),
            ("Container hermes-mqtt is not running", "MQTT container unavailable"),
            ("Error: Connection refused", "MQTT broker connection refused"),
            ("Connection Refused: not authorised.", "MQTT broker denied access"),
            ("Some other error with private request text", "Publisher exited with code 7"),
        ]
        for stderr, expected in cases:
            with self.subTest(stderr=stderr):
                exc = subprocess.CalledProcessError(7, ["private-command"], stderr=stderr)
                message = plugin._failure_message(exc)
                self.assertEqual(message, expected)
                self.assertNotIn("private", message)
        exc = subprocess.TimeoutExpired(["private-command"], 5)
        self.assertEqual(plugin._failure_message(exc), "MQTT send timed out")

    def test_blocked_mqtt_does_not_block_the_approval_callbacks(self):
        entered = threading.Event()
        release = threading.Event()
        resolved = threading.Event()
        emitted = []

        def send(payload):
            emitted.append(payload)
            if payload["type"] == "approval_required":
                entered.set()
                if not release.wait(3):
                    raise AssertionError("Test publisher was not released")
            else:
                resolved.set()

        observer = plugin.WatcherApproval()
        with patch.object(plugin, "_publish", side_effect=send):
            observer.start()
            try:
                observer.human_request(kind="approval", request_id="test")
                self.assertTrue(entered.wait(2))
                observer.human_resolved(kind="approval", request_id="test", outcome="once")
                self.assertFalse(release.is_set())
                release.set()
                self.assertTrue(resolved.wait(2))
            finally:
                release.set()
                observer.close()
        self.assertEqual([m["type"] for m in emitted],
                         ["approval_required", "approval_resolved"])

    def test_publish_failure_retries_and_preserves_resolution(self):
        requested = threading.Event()
        resolved = threading.Event()
        emitted = []
        attempts = []

        def send(payload):
            attempts.append(payload["type"])
            if len(attempts) == 1:
                raise subprocess.CalledProcessError(1, ["mock-mqtt"])
            emitted.append(payload)
            if payload["type"] == "approval_required":
                requested.set()
            else:
                resolved.set()

        observer = plugin.WatcherApproval()
        with patch.object(plugin, "_publish", side_effect=send):
            observer.start()
            try:
                observer.human_request(kind="approval", request_id="test")
                self.assertTrue(requested.wait(3))
                observer.human_resolved(kind="approval", request_id="test", outcome="deny")
                self.assertTrue(resolved.wait(2))
            finally:
                observer.close()
        self.assertEqual(len(attempts), 3)
        self.assertEqual([m["type"] for m in emitted],
                         ["approval_required", "approval_resolved"])

    def test_answered_request_is_not_replayed_when_worker_starts_late(self):
        observer = plugin.WatcherApproval()
        observer.human_request(kind="approval", request_id="already-answered")
        observer.human_resolved(kind="approval", request_id="already-answered", outcome="once")
        completed = threading.Event()
        emitted = []

        def send(payload):
            emitted.append(payload)
            completed.set()

        with patch.object(plugin, "_publish", side_effect=send):
            observer.start()
            try:
                self.assertTrue(completed.wait(2))
            finally:
                observer.close()
        self.assertEqual([m["type"] for m in emitted], ["approval_resolved"])


class RegistrationTests(unittest.TestCase):
    def context(self, supported):
        module = types.ModuleType("hermes_cli.plugins")
        module.VALID_HOOKS = set(supported)
        host = types.ModuleType("hermes_cli")
        context = types.SimpleNamespace(register_hook=lambda name, cb: hooks.update({name: cb}))
        hooks = {}
        return context, hooks, {"hermes_cli": host, "hermes_cli.plugins": module}

    def test_preferred_pair_is_used_once_even_when_legacy_pair_exists(self):
        expected = {"on_human_input_request", "on_human_input_resolved"}
        context, hooks, modules = self.context(expected | {"pre_approval_request", "post_approval_response"})
        with patch.dict(sys.modules, modules), patch.object(plugin.WatcherApproval, "start"):
            plugin.register(context)
        self.assertEqual(set(hooks), expected)

    def test_legacy_pair_is_supported(self):
        expected = {"pre_approval_request", "post_approval_response"}
        context, hooks, modules = self.context(expected)
        with patch.dict(sys.modules, modules), patch.object(plugin.WatcherApproval, "start"):
            plugin.register(context)
        self.assertEqual(set(hooks), expected)

    def test_chat_diagnostic_reports_loaded_mode_without_publishing(self):
        context, hooks, modules = self.context({"on_human_input_request", "on_human_input_resolved"})
        commands = {}
        context.register_command = lambda name, handler, description: commands.update({name: handler})
        with patch.dict(sys.modules, modules), patch.object(plugin.WatcherApproval, "start"), \
                patch.object(plugin, "_publish") as publish:
            plugin.register(context)
            report = commands["watcher"]("")
            self.assertIn("v1.2.0 active", report)
            self.assertIn("clarification choices", report)
            self.assertIn("No event published yet", report)
            hooks["on_human_input_request"](kind="clarify", request_id="test", prompt="Private question")
            report = commands["watcher"]("")
            self.assertIn("Pending notices: 1", report)
            self.assertNotIn("Private question", report)
            publish.assert_not_called()

    def test_unsupported_or_incomplete_pairs_fail_without_inert_hooks(self):
        for supported in (set(), {"pre_approval_request"}, {"on_human_input_request"}):
            context, hooks, modules = self.context(supported)
            with patch.dict(sys.modules, modules), patch.object(plugin.WatcherApproval, "start") as start:
                with self.assertRaisesRegex(RuntimeError, "no supported approval hook pair"):
                    plugin.register(context)
                start.assert_not_called()
                self.assertEqual(hooks, {})

    def test_live_health_contains_counts_but_no_prompt(self):
        observer = plugin.WatcherApproval()
        observer.human_request(kind="approval", request_id="secret-id", prompt="Private command")
        with patch.object(plugin, "_publish") as publish:
            observer.heartbeat()
        payload = publish.call_args.args[0]
        self.assertEqual(payload["pending"], 1)
        self.assertEqual(payload["requests"], 1)
        self.assertEqual(publish.call_args.kwargs, {"topic": "hermes/notifier/health", "retain": False})
        self.assertNotIn("Private", json.dumps(payload))
        self.assertNotIn("secret-id", json.dumps(payload))

    def test_health_failure_does_not_resolve_or_discard_pending_request(self):
        observer = plugin.WatcherApproval()
        observer.human_request(kind="approval", request_id="pending")
        with patch.object(plugin, "_publish", side_effect=FileNotFoundError()):
            observer.heartbeat()
        self.assertIn("pending", observer.pending)
        self.assertIn("Docker", observer.last_mqtt)


if __name__ == "__main__":
    unittest.main()
