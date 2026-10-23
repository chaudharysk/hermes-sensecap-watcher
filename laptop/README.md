# Laptop components

Start with the [complete tutorial](../docs/TUTORIAL.md). Run these installers in
the same Linux account/profile as the Hermes Telegram gateway:

```bash
bash laptop/install.sh
bash laptop/install_status.sh
```

The permission plugin observes real approval and clarification hooks; the status
hook observes gateway task activity. Neither answers a permission request.
`install.sh` also copies `diagnose.py` into the profile as `watcher-diagnose.py`.

Both senders expect Docker at `/usr/bin/docker`, a broker container named
`hermes-mqtt`, and a loopback publisher listener at container port 1883. These
are generic reference-deployment conventions, not personal machine credentials.
The [broker example](../broker/README.md) provides that listener separately from
its authenticated network listener. Restart your gateway after installation and
after active work finishes. Do not assume `plugins list` reloads a running process.

Version 1.2.0 supports native approval/clarification hooks where present and a
legacy approval-only fallback. Current Hermes API differences are detected at
registration; unsupported versions fail with a clear message. `/watcher` reports
the loaded observer mode. Tests mock Hermes/Docker/MQTT and never publish.
