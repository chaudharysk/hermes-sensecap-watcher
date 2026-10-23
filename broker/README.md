# Reference broker

See [tutorial section 3](../docs/TUTORIAL.md#3-set-up-the-broker-on-the-linux-laptop)
for creation of a private `.env`, interactive password setup, and startup.

| Connection | Endpoint | Access |
| --- | --- | --- |
| Watcher over LAN | Laptop port 1883 -> container port 1884 | Password + restricted watcher ACL |
| Laptop senders via `docker exec` | Container `127.0.0.1:1883` | Anonymous, container loopback only |

Only the authenticated listener is published to a specified host LAN address.
Both listeners use plaintext MQTT; this is not a TLS example or internet-facing
service. Keep host Docker access private. Passwords and retained messages are
stored in named Docker volumes, not the public source tree. The example uses
Mosquitto's major-version-2 image tag; deployments should record their actual
image version/digest when testing.

Existing broker users can keep their deployment and adapt settings. Do not
replace an active broker or its volumes just to follow a tutorial.
