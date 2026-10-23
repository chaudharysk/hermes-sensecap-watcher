# Tutorial: Hermes on a SenseCAP Watcher

## 1. What you need

- A normally functioning SenseCAP Watcher, provisioned with your own SenseCraft account.
- A USB data cable and the Watcher's data-capable USB port.
- A computer with Git, Python 3.11+, and the upstream firmware's ESP-IDF v5.2.1 toolchain.
- A Linux laptop running Hermes with Telegram already configured and working.
- Docker and Docker Compose on that laptop, usable by the same account running
  the Hermes gateway. Docker access grants powerful host privileges; use the
  access arrangement appropriate for your machine, not an open Docker socket.
- Watcher and laptop on the same trusted LAN, with a stable laptop IP address.

Use your own configuration and factory data. No Telegram token, Wi-Fi password,
model-provider key, or device identity from the project author is needed or supplied.

Firmware reference: [Seeed installation guide at the pinned revision](https://github.com/Seeed-Studio/SenseCAP-Watcher-Firmware/blob/8e37f7ccd3d836bb99f483e0f6e74e8252406644/examples/factory_firmware/docs/installation.md).
Hermes references: [gateway hooks](https://hermes-agent.nousresearch.com/docs/user-guide/features/hooks),
[plugins](https://hermes-agent.nousresearch.com/docs/user-guide/features/plugins),
[Telegram setup](https://hermes-agent.nousresearch.com/docs/user-guide/messaging/telegram/).

## 2. Get this integration

Download/extract this repository, or clone it with the folder name used below:

```bash
git clone https://github.com/chaudharysk/hermes-sensecap-watcher.git hermes-watcher-public
cd hermes-watcher-public
```

In the commands below, its folder is called `hermes-watcher-public`. Keep it separate
from the upstream firmware checkout. On Windows, use your ESP-IDF terminal for
firmware commands; on Linux, activate your installed IDF environment.

## 3. Set up the broker on the Linux laptop

If you already have a working `hermes-mqtt` container, keep it and adapt the
firmware connection settings to it. Do not start a second container with the
same name or replace an existing broker's data. The supplied laptop publisher
uses `/usr/bin/docker exec hermes-mqtt` and port 1883 on the container's loopback
interface. An existing deployment must allow those local clients or be adapted
to provide credentials.

For a **new** broker, from this repository:

```bash
cd broker
cp .env.example .env
nano .env
```

Set `WATCHER_LAN_IP` to the Linux laptop's actual LAN address. The example value
is documentation-only. Reserve this address in your router if possible.

Create the Watcher's broker password interactively. It is stored in a Docker
volume, not this repository:

```bash
docker compose run --rm --user root broker mosquitto_passwd -c /mosquitto/secrets/passwords watcher
docker compose run --rm --user root broker sh -c 'chown 1883:1883 /mosquitto/secrets/passwords && chmod 600 /mosquitto/secrets/passwords'
docker compose up -d
docker exec hermes-mqtt mosquitto_pub -h 127.0.0.1 -t hermes/status -m ONLINE -q 1 -r
cd ..
```

The password-creation command is for a new password file; `-c` replaces an
existing file. Remember the password for your private firmware configuration.
Container port 1884 requires authentication and is mapped to laptop port 1883.
The anonymous publisher listener binds only to container loopback and is not
published. The Watcher account can read the integration topics and send display
acknowledgements. See the [Mosquitto configuration reference](https://mosquitto.org/man/mosquitto-conf-5.html).

Allow Watcher-to-laptop TCP port 1883 through your local firewall. Do not forward
it through your router. Authentication here does not encrypt MQTT traffic; use
only a trusted LAN. TLS deployment is outside this starter tutorial.

## 4. Prepare a clean firmware checkout

From the `hermes-watcher-public` folder, move to its parent and create a sibling
firmware checkout:

```bash
cd ..
git clone https://github.com/Seeed-Studio/SenseCAP-Watcher-Firmware.git SenseCAP-Watcher-Firmware-public
cd SenseCAP-Watcher-Firmware-public
git checkout 8e37f7ccd3d836bb99f483e0f6e74e8252406644
git submodule update --init --recursive components/rlottie/rlottie
cd ..
python hermes-watcher-public/tools/apply_firmware.py SenseCAP-Watcher-Firmware-public --check
python hermes-watcher-public/tools/apply_firmware.py SenseCAP-Watcher-Firmware-public
```

Use `python3` instead of `python` where that is your Python command. The script
checks the exact base revision, refuses a changed checkout, validates every
patch hunk, and refuses to overwrite overlay files. It does not build or flash.
For a repeat installation, start with another fresh upstream checkout.

Edit this file in the **firmware checkout**:

```text
examples/factory_firmware/main/app/hermes_config.h
```

Set the broker URI to `mqtt://YOUR_LAPTOP_LAN_IP:1883`, username `watcher`, and
the password you just created. Enter valid C string literals (escape a quote as
`\"` and a backslash as `\\`). This file is ignored by the patched checkout's
Git rules. Never copy it into a public repository. The password will be present
in your compiled app image, so keep your build outputs private too.

The topics default to `hermes/status` and `hermes/approval`. This starter setup
is for one shared display. Multiple independent agents/devices need separate
topic namespaces and matching broker ACLs.

## 5. Build and install on your own Watcher

Only run this section when you intend to update the device. These commands were
not executed when preparing this public package.

```bash
cd SenseCAP-Watcher-Firmware-public/examples/factory_firmware
idf.py set-target esp32s3
idf.py build
```

Use the repository's stock defaults as a starting point; do not publish or copy
the project author's private `sdkconfig`. Keep your current working firmware
and build configuration privately if you need to return to it.

Before flashing, follow Seeed's guide to back up **your own** factory data to a
private location outside both repositories. `nvsfactory` contains device-specific
provisioning data. Preserve the installed partition layout and verify it matches
the selected upstream project. Do not erase the device or replace its factory
data with somebody else's backup.

Identify the ESP32 serial port, then use **app-only flashing**. Substitute your
port for the examples below; these are alternatives, not two commands to run:

```powershell
# Windows example
idf.py -p COM_PORT app-flash
```

```bash
# Linux example: replace with the actual ESP32 port
idf.py -p /dev/ttyACM0 app-flash
```

Do not use a full `flash` or `erase-flash` for this integration. No factory/NVS,
bootloader, partition-table, filesystem, or model image is supplied. The seven
sound WAVs are inside the app image and need no separate storage flash.

After reboot, check normal Watcher features first, then the Hermes face. Never
assume an unrelated camera/model problem is fixed by reinstalling the notifier.

## 6. Install the laptop senders

In the integration repository on your **Linux laptop**, run as the same user
and profile that runs Hermes:

```bash
bash laptop/install.sh
bash laptop/install_status.sh
hermes plugins list --plain --no-bundled
```

You should see `watcher-approval`, enabled, version `1.2.0`. If absent or an
installer fails, read that error before proceeding. Both installers respect
`HERMES_HOME` when set; the default is `$HOME/.hermes`. Set it to the gateway's
actual profile home if you use profiles. Existing copies are backed up outside
the plugin/hook discovery directories.

`install.sh` installs the approval observer and diagnostic. `install_status.sh`
installs generic gateway status events. The latter sends no user prompts or
command contents. An existing working status hook can be kept instead.

After any current task finishes, reload the gateway:

```bash
hermes gateway restart
```

CLI Hermes sessions must be restarted separately. Enabled on disk does not prove
the already-running gateway loaded the plugin. In Telegram, `/watcher` can report
the loaded notifier version, supported hook mode, and the last publish result.

## 7. Verify permissions and choice questions

Start a normal task from Telegram. The face should move through task states and
show success when it ends. For a real permission prompt, the Watcher should show
an amber **APPROVAL NEEDED** card and a double chime. A supported clarification
question produces **WAITING FOR YOU** and a different short cue.

Answer the real Telegram buttons. The matching request ID clears the card.
The Watcher does not grant permissions, supply answers, or create Telegram buttons.
A request opened in a local CLI must be answered in that CLI. The display's
Telegram hint is intended for tasks started through your Telegram gateway.

If alerts do not appear, run this on the laptop while triggering a **new** prompt:

```bash
python3 "${HERMES_HOME:-$HOME/.hermes}/watcher-diagnose.py" --seconds 600
```

The ten-minute window accommodates long thinking times. The report distinguishes
notifier heartbeats, broker events, Watcher pings, and matching display acks.
It prints event metadata rather than commands, prompts, or answers. Treat
diagnostic output as private until reviewed. See [troubleshooting](TROUBLESHOOTING.md).

## 8. Optional offline detection

Broker disconnection shows a sleeping face, but a broker can remain online after
Hermes stops. If your Hermes deployment exposes a working local health endpoint,
the optional `laptop/health_bridge.sh` can publish ONLINE/OFFLINE transitions:

```bash
export HERMES_HEALTH_URL='http://127.0.0.1:YOUR_HEALTH_PORT/health'
bash laptop/health_bridge.sh
```

Replace the example with an endpoint you have verified; do not assume every
Hermes installation has one. This runs in the foreground until Ctrl+C and is
not installed as a service automatically. It publishes only changes, retries
failed sends, and cannot dismiss a structured pending request.

## 9. Customize and contribute

Face colors, opacity, positioning, and movement are in `view_hermes.c`. Sound
selection and timing are in `app_hermes_sound.c`. Keep private broker settings
out of patches. Use [the contribution guide](../CONTRIBUTING.md) and
[privacy review](PRIVACY.md) before sharing changes.
