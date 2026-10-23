#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "cJSON.h"
#include "esp_log.h"
#include "mqtt_client.h"

#include "view_hermes.h"
#include "app_hermes_sound.h"

static const char *TAG = "hermes";
static esp_mqtt_client_handle_t hermes_mqtt_client = NULL;

#include "hermes_config.h"
#define HERMES_STATUS_TOPIC "hermes/status"
#define HERMES_APPROVAL_TOPIC "hermes/approval"
#define HERMES_DIAGNOSTIC_TOPIC "hermes/diagnostic"
#define HERMES_ACK_TOPIC "hermes/watcher/ack"
#define HERMES_MESSAGE_CAPACITY 2048

static char incoming[HERMES_MESSAGE_CAPACITY];
static int incoming_length;
static int incoming_total;
static bool incoming_approval;
static bool incoming_diagnostic;
static bool receiving;

static const char *json_string(const cJSON *object, const char *key)
{
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsString(value) ? value->valuestring : NULL;
}

static bool topic_is(const esp_mqtt_event_t *event, const char *topic)
{
    return event->topic != NULL && event->topic_len == (int)strlen(topic) &&
           memcmp(event->topic, topic, event->topic_len) == 0;
}

static bool is_outcome(const char *text)
{
    return text != NULL && (strcasecmp(text, "approved") == 0 || strcasecmp(text, "denied") == 0 ||
                           strcasecmp(text, "cancelled") == 0 || strcasecmp(text, "expired") == 0);
}

static void acknowledge(const char *id, bool displayed, const char *type)
{
    if (id == NULL || strlen(id) >= 96) return;
    cJSON *reply = cJSON_CreateObject();
    if (reply == NULL) return;
    cJSON_AddStringToObject(reply, "type", type);
    cJSON_AddStringToObject(reply, "request_id", id);
    cJSON_AddStringToObject(reply, "firmware", "hermes-ui-1.2");
    cJSON_AddBoolToObject(reply, "displayed", displayed);
    char *encoded = cJSON_PrintUnformatted(reply);
    if (encoded != NULL) {
        /* Enqueue copies the payload; never block the MQTT callback on I/O. */
        esp_mqtt_client_enqueue(hermes_mqtt_client, HERMES_ACK_TOPIC, encoded, 0, 1, 0, true);
        free(encoded);
    }
    cJSON_Delete(reply);
}

static void handle_message(const char *message, bool approval_topic)
{
    while (*message == ' ' || *message == '\n' || *message == '\r' || *message == '\t') ++message;
    if (*message != '{') {
        const char *status = message;
        if (strncasecmp(status, "HERMES ", 7) == 0) status += 7;
        /* Compatibility for a plain-text publisher without request IDs. */
        if (strcasecmp(status, "APPROVED") == 0 || strcasecmp(status, "APPROVAL_GRANTED") == 0) {
            view_hermes_resolve_approval(NULL, "approved");
        } else if (strcasecmp(status, "DENIED") == 0 || strcasecmp(status, "APPROVAL_DENIED") == 0) {
            view_hermes_resolve_approval(NULL, "denied");
        } else if (strcasecmp(status, "APPROVAL_CANCELLED") == 0) {
            view_hermes_resolve_approval(NULL, "cancelled");
        } else if (strcasecmp(status, "APPROVAL_EXPIRED") == 0) {
            view_hermes_resolve_approval(NULL, "expired");
        } else if (approval_topic) {
            /* Plain approval messages are requests; resolutions need an ID. */
            ESP_LOGI(TAG, "Permission event received (plain text)");
            view_hermes_request_approval(NULL, message, NULL);
        } else {
            const char *reason = strchr(status, ':');
            if (view_hermes_is_approval_state(status, reason != NULL ? reason + 1 : NULL)) {
                ESP_LOGI(TAG, "Permission status received (plain text)");
            }
            view_hermes_set_status(message);
        }
        return;
    }

    const char *end = NULL;
    cJSON *root = cJSON_ParseWithOpts(message, &end, true);
    if (root == NULL || !cJSON_IsObject(root)) {
        ESP_LOGW(TAG, "Ignoring malformed Hermes JSON");
        cJSON_Delete(root);
        return;
    }
    const char *type = json_string(root, "type");
    if (type == NULL) type = json_string(root, "event");
    const char *state = json_string(root, "state");
    if (state == NULL) state = json_string(root, "status");
    const char *detail = json_string(root, "message");
    if (detail == NULL) detail = json_string(root, "detail");
    if (detail == NULL) detail = json_string(root, "reason");
    if (detail == NULL) detail = json_string(root, "prompt");
    const char *request_id = json_string(root, "request_id");
    if (request_id == NULL) request_id = json_string(root, "approval_id");
    const char *review_url = json_string(root, "approval_url");
    if (review_url == NULL) review_url = json_string(root, "review_url");
    const char *outcome = json_string(root, "outcome");
    const char *kind = json_string(root, "kind");
    if (type != NULL && strcasecmp(type, "approval_resolved") == 0) {
        /* A delayed resolution must match the displayed request. */
        if (request_id != NULL && request_id[0] != '\0' && is_outcome(outcome)) {
            view_hermes_resolve_approval(request_id, outcome);
            ESP_LOGI(TAG, "Permission resolution received");
        } else {
            ESP_LOGW(TAG, "Ignoring approval resolution without a valid ID/outcome");
        }
    } else if ((approval_topic && type == NULL) ||
               view_hermes_is_approval_state(type, detail) ||
               view_hermes_is_approval_state(state, detail)) {
        ESP_LOGI(TAG, "Permission event received (request ID: %s, phone link: %s)",
                 request_id != NULL && request_id[0] != '\0' ? "present" : "absent",
                 review_url != NULL && review_url[0] != '\0' ? "present" : "absent");
        if (kind != NULL && strcasecmp(kind, "clarify") == 0) {
            view_hermes_request_input(request_id, detail);
        } else {
            view_hermes_request_approval(request_id, detail, review_url);
        }
        acknowledge(request_id, view_hermes_approval_visible(request_id), "approval_display");
    } else if (state != NULL) {
        view_hermes_set_state(state, detail);
    } else if (type != NULL && detail != NULL &&
               (strcasecmp(type, "status") == 0 || strcasecmp(type, "status_update") == 0 ||
                strcasecmp(type, "status_changed") == 0)) {
        view_hermes_set_status(detail);
    } else {
        ESP_LOGW(TAG, "Ignoring Hermes JSON with no recognized status/permission event");
    }
    cJSON_Delete(root);
}

static void receive_message(esp_mqtt_event_handle_t event)
{
    if (event->current_data_offset == 0) {
        receiving = false;
        incoming_length = 0;
        incoming_approval = topic_is(event, HERMES_APPROVAL_TOPIC);
        incoming_diagnostic = topic_is(event, HERMES_DIAGNOSTIC_TOPIC);
        if (!incoming_approval && !incoming_diagnostic && !topic_is(event, HERMES_STATUS_TOPIC)) return;
        if (event->total_data_len <= 0 || event->total_data_len >= (int)sizeof(incoming)) {
            ESP_LOGW(TAG, "Ignoring oversized/empty Hermes message (%d bytes)", event->total_data_len);
            return;
        }
        incoming_total = event->total_data_len;
        receiving = true;
    }

    /* ESP-MQTT may omit the topic on continuation fragments. */
    if (!receiving) return;
    if (event->data == NULL || event->data_len < 0 ||
        event->current_data_offset != incoming_length || event->total_data_len != incoming_total ||
        event->data_len > incoming_total - incoming_length) {
        receiving = false;
        ESP_LOGW(TAG, "Discarding inconsistent Hermes message fragments");
        return;
    }
    memcpy(incoming + incoming_length, event->data, event->data_len);
    incoming_length += event->data_len;
    if (incoming_length == incoming_total) {
        incoming[incoming_length] = '\0';
        receiving = false;
        if (memchr(incoming, '\0', incoming_length) != NULL) {
            ESP_LOGW(TAG, "Ignoring Hermes message with embedded NUL");
            return;
        }
        ESP_LOGD(TAG, "Hermes message received on %s (%d bytes)",
                 incoming_approval ? HERMES_APPROVAL_TOPIC : HERMES_STATUS_TOPIC, incoming_length);
        if (incoming_diagnostic) {
            /* A ping never changes the face or clears a real pending request. */
            cJSON *probe = cJSON_ParseWithOpts(incoming, NULL, true);
            const char *type = json_string(probe, "type");
            if (type != NULL && strcmp(type, "ping") == 0)
                acknowledge(json_string(probe, "request_id"), false, "pong");
            cJSON_Delete(probe);
        } else {
            handle_message(incoming, incoming_approval);
        }
    }
}

static void hermes_mqtt_event_handler(void *handler_args,
                                      esp_event_base_t base,
                                      int32_t event_id,
                                      void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id)
    {
        case MQTT_EVENT_CONNECTED: {
            ESP_LOGI(TAG, "Connected to Hermes MQTT");
            int status_sub = esp_mqtt_client_subscribe(hermes_mqtt_client, HERMES_STATUS_TOPIC, 1);
            int approval_sub = esp_mqtt_client_subscribe(hermes_mqtt_client, HERMES_APPROVAL_TOPIC, 1);
            esp_mqtt_client_subscribe(hermes_mqtt_client, HERMES_DIAGNOSTIC_TOPIC, 1);
            if (status_sub < 0 || approval_sub < 0) {
                ESP_LOGW(TAG, "Failed to queue Hermes subscriptions (status=%d, approval=%d)",
                         status_sub, approval_sub);
            }
            view_hermes_set_connected(true);
            break;
        }

        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "Hermes subscription acknowledged (id=%d)", event->msg_id);
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "Disconnected from Hermes MQTT");
            receiving = false;
            view_hermes_set_connected(false);
            break;

        case MQTT_EVENT_DATA:
        {
            receive_message(event);
            break;
        }

        default:
            break;
    }
}

void app_hermes_init(void)
{
    app_hermes_sound_init();
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = HERMES_MQTT_URI,
        .credentials.username = HERMES_MQTT_USERNAME,
        .credentials.authentication.password = HERMES_MQTT_PASSWORD,
    };

    hermes_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);

    esp_mqtt_client_register_event(
        hermes_mqtt_client,
        ESP_EVENT_ANY_ID,
        hermes_mqtt_event_handler,
        NULL
    );

    esp_mqtt_client_start(hermes_mqtt_client);

    ESP_LOGI(TAG, "Hermes MQTT client started");
}
