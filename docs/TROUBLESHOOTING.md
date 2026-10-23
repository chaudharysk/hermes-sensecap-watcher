# Troubleshooting

## Normal activity works, but approval alerts do not

The status hook and approval plugin are separate senders. A working face does
not prove the permission notifier is installed. On the Linux laptop:

```bash
hermes plugins list --plain --no-bundled
```

If `watcher-approval` is missing, run `bash laptop/install.sh` from this repository.
If disabled, enable it. Then restart the gateway after active work finishes.
Use the same account and `HERMES_HOME` as the Telegram gateway. Merely copying
or running `diagnose.py` does not install a plugin.

Run the diagnostic while triggering a new genuine prompt:

```bash
python3 "${HERMES_HOME:-$HOME/.hermes}/watcher-diagnose.py" --seconds 600
```

| Result | What it establishes / next step |
| --- | --- |
| Watcher reply YES | Firmware can receive a ping and return through this broker |
| Status visible, no permission event | Status sender works; investigate the approval notifier |
| No live notifier heartbeat | Old/unloaded plugin, different profile, failed publishing, or a busy queue; inspect gateway logs |
| Permission event visible, no matching ack | Check firmware subscription/version, message ID, and broker ACLs |
| Matching ack YES | Request was applied to LVGL; inspect the actual display if still absent |
| Approval-only hook mode | This Hermes version cannot emit supported clarification notifications |

A retained approval event may be older than the test. The diagnostic never
answers a request or clears a pending alert. Share reviewed metadata, not raw
chat histories, command text, passwords, or device logs containing credentials.

## Docker or publisher errors

The plugin runs as the gateway's user. That account needs access to
`/usr/bin/docker`, container `hermes-mqtt`, and its local `mosquitto_pub`.
An interactive terminal working as another user does not prove the gateway has
the same access. The notifier publishes inside the container at `127.0.0.1:1883`.
The example broker deliberately keeps that listener container-local.

`-W` is used for the diagnostic subscriber timeout, not the publisher. Older
notifier revisions incorrectly supplied it to `mosquitto_pub`; use 1.2.0 here.

## No face / sleeping forever

Confirm Watcher Wi-Fi and broker LAN address. `localhost` in firmware refers to
the Watcher itself. Check the configured username/password, host port, firewall,
and ACLs. The gateway status hook publishes ONLINE on startup; an optional health
bridge requires a real endpoint in your installation. A running broker alone
does not establish that Hermes is online.

## Silent sound cues

Check the device volume. Cues are skipped or delayed during native recording,
voice interaction, or playback. Ordinary queued cues expire after 1.5 seconds;
permission/choice cues after eight seconds. Repeated status text and duplicate
pending request IDs do not replay a tune. WAVs are embedded; a filesystem flash
does not install or repair them.

## `[ai camera] failed to start`

This is a generic native task-flow error. It can hide model selection/download
errors or a startup timeout. The message alone does not identify a cause or
prove it is caused by Hermes. If it recurs, capture the actual ESP32 monitor
lines around the failure before making firmware changes:

```bash
idf.py -p YOUR_PORT monitor
```

Look for lines mentioning `tfm.ai_camera`, `Failed to download model`,
`Failed to set model`, and `Start failed`. These phrases are **log text to find,
not commands**. Exit with Ctrl+]. Monitoring does not build or flash. Review logs
for credentials, URLs, and device identity before sharing excerpts.

## Recovering from an installation problem

The patch installer refuses a modified or mismatched checkout. Use a fresh
checkout at the documented commit rather than forcing a patch into your working
firmware. Laptop installers save previous files in profile backup directories.
To roll back device code, use your privately retained working app/toolchain and
the matching app-only flash procedure; never erase factory identity to reset a
display integration. The project does not ship recovery images or factory data.
