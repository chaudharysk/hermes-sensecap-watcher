# What belongs in a public release

This source package was assembled from an explicit set of integration files.
The working firmware tree, its `.git` history, and device storage were not copied.
The original broker address was replaced by an example configuration. An unrelated
network setting elsewhere in the original tree was excluded with that entire file.

## Excluded

- Factory NVS (`nvsfactory`), runtime NVS, serial/MAC identities, provisioning data.
- Firmware binaries, flash dumps, partition backups, and model/storage images.
- Personal `sdkconfig`, connection settings, Wi-Fi credentials, local IP addresses.
- Telegram tokens/chat identifiers, model API keys, Hermes profiles and history.
- Logs, screenshots/photos, recordings, editor configuration, and local Git history.
- The existing laptop/package archives; this release ZIP is made from reviewed files only.

No personal photos or screenshots were used as release artwork. The included
audio files are selected public CC0 assets with provenance and checksums.

## Keep your configuration private

`hermes_config.example.h` is public. Its local copy, `hermes_config.h`, is not.
The apply script adds an ignore entry to the separate firmware checkout. Local
`.env` files and generated secrets must remain untracked. Credentials compiled
into firmware remain in the resulting binary even if the header is ignored.
Publish source, not your configured app image or entire working checkout.

The example broker stores hashed passwords and retained messages in Docker
volumes; those volumes are not release assets. Approval notices can include
shortened Hermes-redacted task text. Treat the MQTT broker and display as part
of your private environment. Never put passwords into approval details or QR URLs.

## Before publishing

```bash
python -B tools/check_release.py
```

This verifies the reviewed file manifest, hashes, unexpected files, and configured
patterns for personal paths, LAN addresses, private keys, and token formats. It
prints filenames/categories instead of matching values. It is a useful guard,
not proof that every possible secret format has been detected. It deliberately
does not audit excluded directories, arbitrary Git history, or your entire disk.

Also inspect `git diff --cached --name-only` and the staged diff before your
first public commit. `.gitignore` does not remove anything already tracked or
erase history. Start the public repository from this clean export. If a secret
has already been published, remove it from history and rotate the credential.

See [publishing instructions](PUBLISHING.md) for the exact folder to upload.
