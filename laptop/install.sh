#!/usr/bin/env bash
set -euo pipefail

package_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
watcher_home="${HERMES_HOME:-$HOME/.hermes}"
plugin_dir="$watcher_home/plugins/watcher-approval"

if ! command -v hermes >/dev/null 2>&1; then
    echo "Hermes is not on PATH. Open the terminal where the hermes command works and try again." >&2
    exit 1
fi

# Read-only client check. This catches missing containers, Docker permissions,
# or a missing publisher before reporting a successful plugin installation.
if [ ! -x /usr/bin/docker ]; then
    echo "Docker is not available at /usr/bin/docker, which the notifier uses." >&2
    exit 1
fi
if ! /usr/bin/docker exec hermes-mqtt sh -c 'command -v mosquitto_pub >/dev/null'; then
    echo "Cannot find mosquitto_pub in hermes-mqtt. Fix the Docker/container error above before installing." >&2
    exit 1
fi

if [ -e "$plugin_dir" ]; then
    backup_dir="$watcher_home/plugins-backup/watcher-approval-$(date +%Y%m%d-%H%M%S)-$$"
    mkdir -p -- "$backup_dir"
    cp -a -- "$plugin_dir/." "$backup_dir/"
    echo "Previous plugin backed up to $backup_dir"
fi

mkdir -p -- "$plugin_dir"
cp -- "$package_dir/watcher-approval/__init__.py" "$plugin_dir/__init__.py"
cp -- "$package_dir/watcher-approval/plugin.yaml" "$plugin_dir/plugin.yaml"
cp -- "$package_dir/diagnose.py" "$watcher_home/watcher-diagnose.py"
hermes plugins enable watcher-approval

echo "Watcher approval notifier installed and enabled."
echo "After the current task finishes, restart Hermes to load the plugin."
echo "For phone/chat tasks handled by the gateway: hermes gateway restart"
echo "For laptop CLI tasks: exit the current Hermes session and start hermes again."
echo "Your existing watcher-status hook and health bridge continue to work."
echo "After restarting, run: python3 \"$watcher_home/watcher-diagnose.py\""
