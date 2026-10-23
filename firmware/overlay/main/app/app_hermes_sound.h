#pragma once

typedef enum {
    HERMES_SOUND_SILENT, HERMES_SOUND_START, HERMES_SOUND_SEARCH,
    HERMES_SOUND_DONE, HERMES_SOUND_ERROR, HERMES_SOUND_APPROVAL,
    HERMES_SOUND_CHOICE, HERMES_SOUND_OFFLINE,
} hermes_sound_t;

void app_hermes_sound_init(void);
/* Nonblocking: safe from the LVGL task. SILENT cancels a queued cue. */
void app_hermes_sound_request(hermes_sound_t sound);
