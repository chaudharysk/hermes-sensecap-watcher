#!/usr/bin/env bash
set -euo pipefail
package_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
watcher_home="${HERMES_HOME:-$HOME/.hermes}"
target="$watcher_home/hooks/watcher-status"
if [ -e "$target" ]; then
    backup="$watcher_home/hooks-backup/watcher-status-$(date +%Y%m%d-%H%M%S)-$$"
    mkdir -p -- "$backup"
    cp -a -- "$target/." "$backup/"
    echo "Existing status hook backed up outside the hooks discovery folder."
fi
mkdir -p -- "$target"
cp -- "$package_dir/watcher-status/HOOK.yaml" "$target/HOOK.yaml"
cp -- "$package_dir/watcher-status/handler.py" "$target/handler.py"
echo "Status hook installed. Restart the gateway after your active task finishes."
