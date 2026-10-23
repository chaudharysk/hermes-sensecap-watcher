# Release preparation checks

Prepared on 2026-10-24. These are source/package checks, not a new firmware or
hardware certification.

## Completed

- Reviewed the local integration and its relationship to the native Watcher UI,
  audio player, MQTT receiver, and laptop senders.
- Created an isolated export from selected files; copied no local Git history,
  factory/device backup, personal SDK configuration, or flash image.
- Replaced the private broker address with an ignored local configuration header
  and a public example. Added optional username/password fields for the reference
  authenticated LAN listener.
- Verified the public upstream commit exists in Seeed's repository.
- Ran **28** existing laptop notifier/diagnostic tests with mocked Hermes/MQTT.
- Ran **6** release/status-hook tests, including overlapping sessions, stale idle
  timers, generic-only status publication, asset integrity, and privacy checks.
- Applied the patch and overlay to a separate checkout of the pinned upstream
  revision. Compared all patched firmware integration files to the working
  source, verified the private header is Git-ignored, and verified that a repeat
  application refuses a changed checkout.
- Confirmed all seven WAVs have the expected hashes and PCM16/mono/16 kHz format.
- Checked local Markdown file links, the reviewed export manifest, configured
  private-data patterns, and the final archive's exact file list/integrity.
- Confirmed the original tracked working changes were unchanged by preparation.

## Not performed

- No firmware build, flash, reset, or device connection.
- No connection to the owner's broker, Telegram account, or Hermes laptop.
- No live Docker Compose/Mosquitto deployment of the public broker example.
- No fresh hardware test of the sanitized broker configuration or added public
  status-hook reference. The owner reported their existing device setup working.
- GitHub publication is separate from these source/hardware validation checks.
- No exhaustive audit of the original repository's complete history or private
  directories. The reviewed publication scope is this isolated export.

## Differences from the existing working setup

The firmware uses a local broker settings header instead of a fixed private
address and supports broker credentials. The public tutorial offers a new
reference broker deployment and generic status hook; the approval plugin and
diagnostic come from the existing integration. These portability changes still
require a build and end-to-end hardware test by each deployer.

The included CI runs offline Python/package checks only. A passing badge does
not establish camera operation, sound quality, physical alert visibility, or
successful real Telegram delivery. Record those separately when testing a release.
