#!/usr/bin/env bash
# Optional: requires a health URL that actually exists in YOUR Hermes deployment.
set -u
: "${HERMES_HEALTH_URL:?Set this to your verified local gateway health endpoint}"
last_status=""
while true; do
    if curl -fsS --max-time 2 "$HERMES_HEALTH_URL" >/dev/null; then
        status=ONLINE
    else
        status=OFFLINE
    fi
    if [ "$status" != "$last_status" ]; then
        if /usr/bin/docker exec hermes-mqtt mosquitto_pub -h 127.0.0.1 \
            -t hermes/status -m "$status" -q 1 -r; then
            last_status="$status"
        fi
    fi
    sleep 5
done
