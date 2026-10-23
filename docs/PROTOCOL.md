# MQTT protocol

All topics in this starter package share the `hermes/` namespace. The Watcher
uses QoS 1 subscriptions. Status and approval publishers use retained messages;
diagnostic pings, display acknowledgements, and notifier health are not retained.

| Topic | Direction | Payload |
| --- | --- | --- |
| `hermes/status` | Laptop to display | Plain state text or state JSON |
| `hermes/approval` | Laptop to display | Pending request or matching resolution |
| `hermes/diagnostic` | Diagnostic to display | Ping only |
| `hermes/watcher/ack` | Display to broker | Pong or applied-alert acknowledgement |
| `hermes/notifier/health` | Loaded plugin to broker | Version/process/event counters |

## Task states

```json
{"state":"thinking","message":"Planning the next step"}
```

Plain `ONLINE`, `OFFLINE`, `THINKING`, `SEARCHING`, `RUNNING`, `DONE`, and `ERROR`
are supported, as are legacy messages prefixed with `HERMES `. States are case
insensitive; underscores/hyphens/spaces normalize. Additional expressions include
listening, speaking, sleeping, and waiting. Idle online/offline do not waste space
on a state card. Details can be sent as `message` or `detail`.

## Requests and resolutions

```json
{"type":"approval_required","kind":"approval","request_id":"example-1","message":"Review the pending command in Hermes"}
```

```json
{"type":"approval_required","kind":"clarify","request_id":"example-2","message":"Choose an option in Hermes"}
```

```json
{"type":"approval_resolved","kind":"approval","request_id":"example-1","outcome":"approved"}
```

Valid wire outcomes are `approved`, `denied`, `cancelled`, and `expired`. For
`kind=clarify`, `approved` means the question was answered; it grants no command
permission. The plugin maps actual Hermes outcomes and never answers a prompt.
Normal state updates cannot clear a pending alert. A resolution must match the
current request ID. The plugin restores another pending request when concurrent
requests in that process resolve. It has no global coordinator across processes.

IDs must fit 95 UTF-8 bytes. The plugin hashes longer IDs consistently. Incoming
MQTT payloads must fit 2047 bytes; fragmented messages are assembled and checked.
Optional `approval_url`/`review_url` fields accept HTTP(S) URLs up to 319 bytes and
can display a QR code. Only supply a real review page you control. This project
does not implement such a page; answering in the original Hermes chat needs no QR URL.

The plugin's native human-input hook receives Hermes-redacted prompt text and
may forward a shortened version. Redaction does not guarantee a task description
is non-sensitive. Legacy approval hooks publish a generic message instead of
the command. The gateway status hook sends only generic state names.

## Diagnostics

```json
{"type":"ping","request_id":"unique-probe-id"}
```

The response has `type=pong`, the same ID, and `firmware=hermes-ui-1.2`.
An applied structured request produces `type=approval_display`, its ID, and a
`displayed` boolean. This confirms the LVGL request state was applied; it cannot
inspect the physical LCD or prove the user saw the prompt.

Plugin health is emitted after ten seconds of worker-queue inactivity. It is
not a persistent service monitor and may be absent during publish failures or
continuous work. It includes version, process ID, mode, pending count, and
request/resolution counts, but no prompts or typed answers.

## Recovery limitations

Retained requests can reappear after reconnect. A plugin/gateway crash before
publishing a resolution can leave a stale alert. Pending request state is held
in memory on the laptop, not rebuilt from the chat service after restart. Verify the
real request in Hermes before clearing a stale retained message. One retained
topic cannot represent every pending request across independent gateways.

MQTT display messages are not an authorization channel. Even a forged
`approved` display message cannot authorize a Hermes command, but it could
mislead the display; restrict access to the broker accordingly.
