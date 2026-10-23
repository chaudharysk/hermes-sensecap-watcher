#include "app_hermes_sound.h"
#include "app_audio_player.h"
#include "app_audio_recorder.h"
#include "app_voice_interaction.h"
#include "audio_player.h"
#include "esp_log.h"
#include "freertos/task.h"

#define ASSET(name) \
    extern const uint8_t name##_start[] asm("_binary_hermes_" #name "_wav_start"); \
    extern const uint8_t name##_end[] asm("_binary_hermes_" #name "_wav_end")
ASSET(start);
ASSET(search);
ASSET(done);
ASSET(error);
ASSET(approval);
ASSET(choice);
ASSET(offline);

typedef struct { const uint8_t *begin; const uint8_t *end; } clip_t;
#define CLIP(name) { name##_start, name##_end }
static const clip_t clips[] = {
    {NULL, NULL}, CLIP(start), CLIP(search), CLIP(done), CLIP(error),
    CLIP(approval), CLIP(choice), CLIP(offline),
};
typedef struct { hermes_sound_t sound; TickType_t at; } cue_t;
static QueueHandle_t cues;

static bool audio_available(void)
{
    return !app_vi_session_is_running() &&
           app_audio_recorder_status_get() == AUDIO_RECORDER_STATUS_IDLE &&
           app_audio_player_status_get() == AUDIO_PLAYER_STATUS_IDLE &&
           audio_player_get_state() == AUDIO_PLAYER_STATE_IDLE;
}

static void sound_task(void *arg)
{
    (void)arg;
    cue_t cue;
    TickType_t last = 0;
    bool played = false;
    for (;;) {
        if (xQueueReceive(cues, &cue, portMAX_DELAY) != pdTRUE) continue;
        for (;;) {
            if (cue.sound == HERMES_SOUND_SILENT) break;
            bool attention = cue.sound == HERMES_SOUND_APPROVAL || cue.sound == HERMES_SOUND_CHOICE;
            TickType_t now = xTaskGetTickCount();
            if (now - cue.at > pdMS_TO_TICKS(attention ? 8000 : 1500)) break;
            if (audio_available() && (!played || attention || now - last >= pdMS_TO_TICKS(1500))) {
                clip_t clip = clips[cue.sound];
                if (app_audio_player_mem_if_idle(clip.begin, clip.end - clip.begin) == ESP_OK) {
                    last = now;
                    played = true;
                }
                break;
            }
            /* Only the latest state matters; never accumulate a jingle backlog. */
            cue_t replacement;
            if (xQueueReceive(cues, &replacement, pdMS_TO_TICKS(100)) == pdTRUE) cue = replacement;
        }
    }
}

void app_hermes_sound_init(void)
{
    if (cues != NULL) return;
    cues = xQueueCreate(1, sizeof(cue_t));
    if (cues == NULL) return;
    if (xTaskCreate(sound_task, "hermes_sound", 3072, NULL, 3, NULL) != pdPASS) {
        vQueueDelete(cues);
        cues = NULL;
        ESP_LOGW("hermes_sound", "Sound worker unavailable");
    }
}

void app_hermes_sound_request(hermes_sound_t sound)
{
    if (cues == NULL || sound < HERMES_SOUND_SILENT || sound > HERMES_SOUND_OFFLINE) return;
    cue_t cue = {sound, xTaskGetTickCount()};
    xQueueOverwrite(cues, &cue);
}
