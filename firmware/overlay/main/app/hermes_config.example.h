#pragma once

/* Copy to hermes_config.h in your PRIVATE firmware checkout. Never publish it.
 * Use the LAN address of your own broker host, not localhost on the Watcher.
 * The example broker accepts the Watcher on host port 1883 with authentication.
 * Plain MQTT is intended only for a trusted private LAN, not the internet.
 */
#define HERMES_MQTT_URI "mqtt://YOUR_BROKER_IP:1883"
#define HERMES_MQTT_USERNAME "watcher"
#define HERMES_MQTT_PASSWORD "CHANGE_ME_LOCALLY"
