#include "view_hermes.h"
#include "app_hermes_sound.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "lvgl.h"
#include "sensecap-watcher.h"

/* Small vector overlay: no sprite sheets, frame buffers, or per-frame allocation. */
enum {
    ORB_SIZE = 180, ORB_Y = 16, CARD_Y = 270, CARD_WIDTH = 252,
    FACE_OPACITY = 156, CARD_OPACITY = 176, FRAME_MS = 40,
    TRANSITION_MS = 420, DETAIL_CAPACITY = 240, REQUEST_CAPACITY = 96,
    URL_CAPACITY = 320,
};

typedef enum {
    ONLINE, OFFLINE, THINKING, SEARCHING, RUNNING, DONE, ERROR,
    LISTENING, SPEAKING, SLEEPING, WAITING, APPROVAL, EMOTION_COUNT,
} emotion_t;

typedef struct {
    const char *title;
    const char *hint;
    uint32_t accent;
    int16_t eye_height;
    int16_t smile;
} expression_t;

static const expression_t expressions[EMOTION_COUNT] = {
    [ONLINE]    = {"ONLINE", "Ready when you are", 0x70DAEB, 18, 9},
    [OFFLINE]   = {"OFFLINE", "Waiting for Hermes", 0xA0A7CB, 3, 3},
    [THINKING]  = {"THINKING", "Working it out", 0xA89AFF, 18, 2},
    [SEARCHING] = {"SEARCHING", "Looking for answers", 0x67C8FF, 19, 3},
    [RUNNING]   = {"WORKING", "Task in progress", 0x52DBCD, 12, 1},
    [DONE]      = {"DONE", "All taken care of", 0x8CE4B0, 8, 13},
    [ERROR]     = {"ERROR", "Check Hermes for details", 0xFF8A92, 13, -7},
    [LISTENING] = {"LISTENING", "I'm listening", 0x7BDCEC, 22, 5},
    [SPEAKING]  = {"SPEAKING", "Here's what I found", 0xD4A3FF, 18, 7},
    [SLEEPING]  = {"RESTING", "Taking a quiet moment", 0xA0A7CB, 3, 3},
    [WAITING]   = {"WAITING", "Waiting for the next step", 0xE8C58B, 14, 2},
    [APPROVAL]  = {"APPROVAL NEEDED", "Review the request in Hermes", 0xFFD28C, 21, 2},
};

static lv_obj_t *orb;
static lv_obj_t *card;
static lv_obj_t *title_label;
static lv_obj_t *detail_label;
static lv_obj_t *hint_label;
static lv_obj_t *status_dot;
#if LV_USE_QRCODE
static lv_obj_t *approval_qr;
#endif
static emotion_t emotion = OFFLINE;
static bool connected;
static bool approval_pending;
static bool input_pending;
static bool displayed_input;
static char detail_text[DETAIL_CAPACITY];
static char approval_id[REQUEST_CAPACITY];
static char approval_url[URL_CAPACITY];
static uint32_t started_at;
static uint32_t changed_at;
static uint32_t blink_at;
static uint32_t next_blink_at;
static uint32_t activity_at;
static uint32_t blink_count;
static bool blinking;
static int32_t morph = 255;
static int32_t source_eye = 3;
static int32_t source_smile = 3;
static lv_color_t accent;
static lv_color_t source_accent;

static int32_t mix_value(int32_t from, int32_t to, int32_t amount)
{
    return from + (to - from) * amount / 255;
}

static int32_t wave(uint32_t elapsed, uint32_t period, int32_t amplitude)
{
    int32_t angle = (elapsed % period) * 360 / period;
    return (int32_t)lv_trigo_sin(angle) * amplitude / LV_TRIGO_SIN_MAX;
}

static void copy_text(char *dst, size_t size, const char *src)
{
    snprintf(dst, size, "%s", src != NULL ? src : "");
    /* Do not leave half of a UTF-8 character at a bounded buffer's end. */
    size_t length = strlen(dst);
    if (src != NULL && strlen(src) >= size && length > 0) {
        size_t last = length - 1;
        while (last > 0 && ((unsigned char)dst[last] & 0xC0) == 0x80) --last;
        unsigned char c = (unsigned char)dst[last];
        size_t needed = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
        if (last + needed > length) dst[last] = '\0';
    }
}

static bool begins(const char *text, const char *word)
{
    size_t len = strlen(word);
    return strncmp(text, word, len) == 0 &&
           (text[len] == '\0' || text[len] == ' ' || text[len] == ':' || text[len] == '.');
}

static void normalize_status(char *dst, size_t size, const char *text)
{
    size_t n = 0;
    if (text == NULL) {
        dst[0] = '\0';
        return;
    }
    while (isspace((unsigned char)*text)) ++text;
    for (; *text != '\0' && n + 1 < size; ++text) {
        char c = (char)toupper((unsigned char)*text);
        if (c == '_' || c == '-' || isspace((unsigned char)c)) c = ' ';
        if (c != ' ' || (n > 0 && dst[n - 1] != ' ')) dst[n++] = c;
    }
    if (n > 0 && dst[n - 1] == ' ') --n;
    dst[n] = '\0';
}

static emotion_t parse_emotion(const char *text)
{
    char normalized[96];
    normalize_status(normalized, sizeof(normalized), text);
    const char *s = normalized;
    if (begins(s, "HERMES")) s += 6;
    while (*s == ' ' || *s == ':') ++s;
    if (begins(s, "PERMISSION REQUIRED") || begins(s, "PERMISSION REQUEST") ||
        begins(s, "PERMISSION REQUESTED") || begins(s, "PERMISSION PROMPT") ||
        begins(s, "APPROVAL NEEDED") || begins(s, "APPROVAL REQUIRED") ||
        begins(s, "APPROVAL REQUEST") || begins(s, "APPROVAL PENDING") ||
        begins(s, "AWAITING APPROVAL") || begins(s, "WAITING FOR APPROVAL") ||
        begins(s, "WAITING APPROVAL") || begins(s, "NEEDS PERMISSION") ||
        begins(s, "WAITING FOR PERMISSION") || begins(s, "WAITING PERMISSION") ||
        begins(s, "WAITING FOR YOUR PERMISSION") || begins(s, "WAITING FOR YOUR APPROVAL") ||
        begins(s, "AWAITING YOUR PERMISSION") || begins(s, "AWAITING YOUR APPROVAL") ||
        begins(s, "AWAITING PERMISSION") || begins(s, "PENDING APPROVAL") ||
        begins(s, "PENDING PERMISSION") || begins(s, "NEEDS CONFIRMATION") ||
        begins(s, "CONFIRMATION REQUIRED") || begins(s, "WAITING FOR CONFIRMATION") ||
        begins(s, "AWAITING CONFIRMATION") || begins(s, "REQUIRES APPROVAL") ||
        begins(s, "REQUIRES CONFIRMATION") || begins(s, "NEEDS YOUR APPROVAL") ||
        begins(s, "NEEDS YOUR PERMISSION") || begins(s, "NEED YOUR PERMISSION") ||
        begins(s, "REQUIRES PERMISSION") || begins(s, "REQUESTING PERMISSION") ||
        begins(s, "NEEDS APPROVAL") || begins(s, "APPROVAL REQUESTED") ||
        strcmp(s, "APPROVAL") == 0) return APPROVAL;
    if (begins(s, "ERROR") || begins(s, "FAILED") || begins(s, "FAILURE") ||
        begins(s, "DENIED") || begins(s, "EXPIRED")) return ERROR;
    if (begins(s, "OFFLINE") || begins(s, "DISCONNECTED")) return OFFLINE;
    if (begins(s, "THINKING") || begins(s, "PROCESSING") || begins(s, "PLANNING")) return THINKING;
    if (begins(s, "SEARCHING") || begins(s, "LOOKUP") || begins(s, "BROWSING")) return SEARCHING;
    if (begins(s, "RUNNING") || begins(s, "TERMINAL") || begins(s, "EXECUTING") ||
        begins(s, "WORKING") || begins(s, "CODING")) return RUNNING;
    if (begins(s, "DONE") || begins(s, "SUCCESS") || begins(s, "COMPLETE") ||
        begins(s, "COMPLETED")) return DONE;
    if (begins(s, "LISTENING")) return LISTENING;
    if (begins(s, "SPEAKING") || begins(s, "TALKING")) return SPEAKING;
    if (begins(s, "SLEEPING") || begins(s, "SLEEP") || begins(s, "STANDBY")) return SLEEPING;
    if (begins(s, "WAITING") || begins(s, "AWAITING") || begins(s, "PAUSED") ||
        begins(s, "BLOCKED") || begins(s, "ACTION REQUIRED") ||
        begins(s, "REQUIRES ACTION") || begins(s, "NEEDS INPUT") ||
        begins(s, "PENDING")) return WAITING;
    return ONLINE;
}

bool view_hermes_is_approval_state(const char *state, const char *detail)
{
    emotion_t next = parse_emotion(state);
    if (next == APPROVAL) return true;
    /* Generic waits can be for a network/task; promote only explicit requests. */
    if (next != WAITING || detail == NULL) return false;
    if (parse_emotion(detail) == APPROVAL) return true;
    char normalized[DETAIL_CAPACITY];
    normalize_status(normalized, sizeof(normalized), detail);
    return strstr(normalized, "YOUR PERMISSION") != NULL ||
           strstr(normalized, "YOUR APPROVAL") != NULL ||
           strstr(normalized, "PERMISSION REQUIRED") != NULL ||
           strstr(normalized, "APPROVAL REQUIRED") != NULL ||
           strstr(normalized, "REQUIRES PERMISSION") != NULL ||
           strstr(normalized, "REQUIRES APPROVAL") != NULL;
}

static void draw_line(lv_draw_ctx_t *ctx, lv_point_t a, lv_point_t b,
                      lv_color_t color, int32_t width, lv_opa_t opacity)
{
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color = color;
    d.width = width;
    d.opa = opacity;
    d.round_start = true;
    d.round_end = true;
    lv_draw_line(ctx, &d, &a, &b);
}

static void draw_pill(lv_draw_ctx_t *ctx, int32_t x, int32_t y, int32_t w,
                      int32_t h, lv_color_t color, lv_opa_t opacity)
{
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = color;
    d.bg_opa = opacity;
    d.radius = LV_RADIUS_CIRCLE;
    lv_area_t area = {x - w / 2, y - h / 2, x - w / 2 + w - 1, y - h / 2 + h - 1};
    lv_draw_rect(ctx, &d, &area);
}

static void draw_arc(lv_draw_ctx_t *ctx, lv_point_t center, int32_t radius,
                     int32_t start, int32_t length, int32_t width,
                     lv_color_t color, lv_opa_t opacity)
{
    lv_draw_arc_dsc_t d;
    lv_draw_arc_dsc_init(&d);
    d.color = color;
    d.width = width;
    d.opa = opacity;
    d.rounded = true;
    start = (start % 360 + 360) % 360;
    int32_t end = length == 360 ? start + 360 : (start + length) % 360;
    lv_draw_arc(ctx, &d, &center, radius, start, end);
}

static void draw_smile(lv_draw_ctx_t *ctx, int32_t x, int32_t y,
                       int32_t width, int32_t depth, lv_color_t color)
{
    /* Parabolic curve can smoothly morph between smile, neutral, and frown. */
    lv_point_t previous = {x - width / 2, y};
    for (int32_t i = 1; i <= 12; ++i) {
        lv_point_t next = {x - width / 2 + width * i / 12,
                           y + depth * i * (12 - i) / 36};
        draw_line(ctx, previous, next, color, 3, LV_OPA_COVER);
        previous = next;
    }
}

static void orb_draw_cb(lv_event_t *event)
{
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(event);
    lv_area_t area;
    lv_obj_get_coords(orb, &area);
    uint32_t elapsed = lv_tick_elaps(started_at);
    uint32_t since_change = lv_tick_elaps(changed_at);
    lv_point_t center = {(area.x1 + area.x2) / 2, (area.y1 + area.y2) / 2};
    bool resting = emotion == OFFLINE || emotion == SLEEPING;
    center.y += wave(elapsed, resting ? 6400 : 4800, resting ? 2 : 3);
    if (emotion == DONE && since_change < 1100) {
        center.y -= LV_ABS(wave(since_change, 1100, 7));
    } else if (emotion == ERROR && since_change < 480) {
        center.x += wave(since_change, 160, 4);
    }

    lv_color_t white = lv_color_hex(0xF4F8FF);
    lv_color_t deep = lv_color_hex(0x101927);
    int32_t pulse = 165 + wave(elapsed, 3200, 45);
    lv_draw_rect_dsc_t disc;
    lv_draw_rect_dsc_init(&disc);
    disc.radius = LV_RADIUS_CIRCLE;
    disc.bg_color = lv_color_mix(accent, deep, 22);
    disc.bg_opa = FACE_OPACITY;
    disc.bg_grad.dir = LV_GRAD_DIR_VER;
    disc.bg_grad.stops_count = 2;
    disc.bg_grad.stops[0].color = disc.bg_color;
    disc.bg_grad.stops[0].frac = 0;
    disc.bg_grad.stops[1].color = lv_color_mix(accent, deep, 64);
    disc.bg_grad.stops[1].frac = 255;
    disc.border_width = 1;
    disc.border_color = accent;
    disc.border_opa = 100;
    lv_area_t face = {center.x - 63, center.y - 63, center.x + 63, center.y + 63};
    lv_draw_rect(ctx, &disc, &face);

    /* Restrained perimeter: quiet at rest, moving only for active tasks. */
    draw_arc(ctx, center, 71, 0, 360, 1, accent, 45);
    bool busy = emotion == THINKING || emotion == SEARCHING || emotion == RUNNING ||
                emotion == LISTENING || emotion == SPEAKING;
    int32_t rotation = busy ? (elapsed % 3000) * 360 / 3000 : 232;
    if (emotion == SEARCHING) rotation = (elapsed % 1900) * 360 / 1900;
    draw_arc(ctx, center, 71, rotation, emotion == APPROVAL ? 104 : 66, 3, accent, pulse);
    draw_arc(ctx, center, 71, rotation + 172, 28, 2, accent, 75);
    draw_arc(ctx, center, 58, 225, 65, 1, white, 60);
    for (int32_t i = 0; i < 4; ++i) {
        draw_arc(ctx, center, 76, 45 + i * 90, 3, 1, accent, 85);
    }

    int32_t gaze = 0;
    int32_t gaze_y = 0;
    if (emotion == THINKING) {
        gaze = wave(elapsed, 4600, 4);
        gaze_y = -3;
    } else if (emotion == SEARCHING) {
        gaze = wave(elapsed, 2600, 7);
    } else if (emotion == ONLINE) {
        uint32_t glance = elapsed % 9000;
        if (glance < 1800) gaze = wave(glance, 3600, 3);
    }
    int32_t eye_height = mix_value(source_eye, expressions[emotion].eye_height, morph);
    int32_t smile = mix_value(source_smile, expressions[emotion].smile, morph);
    int32_t closure = 0;
    if (blinking) {
        uint32_t t = lv_tick_elaps(blink_at);
        closure = t < 80 ? t * 255 / 80 : t < 120 ? 255 : t < 260 ? (260 - t) * 255 / 140 : 0;
    }
    eye_height = LV_MAX(3, eye_height * (255 - closure) / 255);
    for (int32_t i = 0; i < 2; ++i) {
        int32_t x = center.x + (i == 0 ? -21 : 21) + gaze;
        int32_t y = center.y - 9 + gaze_y;
        if (emotion == DONE && closure < 80) {
            /* Happy closed eyes, drawn as gentle arches. */
            draw_smile(ctx, x, y + 2, 16, -6, white);
        } else if (resting && eye_height <= 5) {
            /* Relaxed closed eyelids remain closed while offline/resting. */
            draw_smile(ctx, x, y - 1, 14, 3, white);
        } else {
            draw_pill(ctx, x, y, 12, eye_height, white, LV_OPA_COVER);
        }
        if (emotion == THINKING || emotion == ERROR || emotion == APPROVAL) {
            int32_t slant = emotion == ERROR ? (i == 0 ? -3 : 3) : (i == 0 ? 2 : -1);
            draw_line(ctx, (lv_point_t){x - 6, center.y - 27},
                       (lv_point_t){x + 6, center.y - 27 + slant}, white, 2, 165);
        }
    }
    if (emotion == SPEAKING) {
        draw_pill(ctx, center.x, center.y + 20, 14,
                   9 + wave(elapsed, 420, 4), white, LV_OPA_COVER);
    } else if (emotion == LISTENING) {
        draw_arc(ctx, (lv_point_t){center.x, center.y + 20}, 5, 0, 360, 2, white, LV_OPA_COVER);
    } else {
        draw_smile(ctx, center.x, center.y + 14, emotion == DONE ? 34 : 26, smile, white);
    }

    if (resting) {
        /* A small drifting Z makes the offline face read as asleep. */
        int32_t x = center.x + 29;
        int32_t y = center.y - 35 + wave(elapsed, 4600, 2);
        draw_line(ctx, (lv_point_t){x, y}, (lv_point_t){x + 7, y}, accent, 2, 150);
        draw_line(ctx, (lv_point_t){x + 7, y}, (lv_point_t){x, y + 6}, accent, 2, 150);
        draw_line(ctx, (lv_point_t){x, y + 6}, (lv_point_t){x + 7, y + 6}, accent, 2, 150);
    } else if (emotion == APPROVAL) {
        /* A visible request indicator with a slow amber pulse, not a flash. */
        draw_line(ctx, (lv_point_t){center.x, center.y + 36},
                   (lv_point_t){center.x, center.y + 43}, accent, 3, pulse);
        draw_pill(ctx, center.x, center.y + 49, 3, 3, accent, pulse);
    } else if (emotion == LISTENING || emotion == SPEAKING) {
        for (int32_t i = -2; i <= 2; ++i) {
            int32_t height = 5 + LV_ABS(wave(elapsed + (i + 2) * 90, 750, 7));
            draw_pill(ctx, center.x + i * 7, center.y + 40, 3, height, accent, 190);
        }
    } else {
        for (int32_t i = 0; i < 3; ++i) {
            int32_t opacity = busy ? 90 + wave(elapsed + i * 240, 1300, 75) : 90;
            draw_pill(ctx, center.x + (i - 1) * 9, center.y + 39, 3, 3, accent, opacity);
        }
    }
}

static void card_y_cb(void *obj, int32_t value)
{
    lv_obj_set_style_translate_y(obj, value, 0);
}

static void title_opa_cb(void *obj, int32_t value)
{
    lv_obj_set_style_text_opa(obj, value, 0);
}

static void animate_card(void)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, card);
    lv_anim_set_exec_cb(&a, card_y_cb);
    lv_anim_set_values(&a, 8, 0);
    lv_anim_set_time(&a, 340);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
    lv_anim_set_var(&a, title_label);
    lv_anim_set_exec_cb(&a, title_opa_cb);
    lv_anim_set_values(&a, 80, LV_OPA_COVER);
    lv_anim_start(&a);
}

static void update_hint(void)
{
    const char *hint = expressions[emotion].hint;
    if (approval_pending && !connected) hint = "Link offline - request unconfirmed";
#if LV_USE_QRCODE
    else if (approval_pending && approval_qr != NULL &&
             !lv_obj_has_flag(approval_qr, LV_OBJ_FLAG_HIDDEN)) hint = "Scan to review on your phone";
#endif
    else if (approval_pending && input_pending) hint = "Choose an option in Hermes";
    lv_label_set_text(hint_label, hint);
}

static void set_display(emotion_t next, const char *detail)
{
    char bounded[DETAIL_CAPACITY];
    copy_text(bounded, sizeof(bounded), detail);
    bool changed = next != emotion || strcmp(detail_text, bounded) != 0 ||
                   (next == APPROVAL && displayed_input != input_pending);
    if (!changed) return;
    displayed_input = next == APPROVAL && input_pending;
    if (next != emotion) {
        hermes_sound_t sound = HERMES_SOUND_SILENT;
        if (next == THINKING && (emotion == ONLINE || emotion == OFFLINE || emotion == DONE))
            sound = HERMES_SOUND_START;
        else if (next == SEARCHING) sound = HERMES_SOUND_SEARCH;
        else if (next == DONE) sound = HERMES_SOUND_DONE;
        else if (next == ERROR) sound = HERMES_SOUND_ERROR;
        else if (next == OFFLINE || next == SLEEPING) sound = HERMES_SOUND_OFFLINE;
        app_hermes_sound_request(sound);
        source_eye = mix_value(source_eye, expressions[emotion].eye_height, morph);
        source_smile = mix_value(source_smile, expressions[emotion].smile, morph);
        source_accent = accent;
        morph = 0;
        emotion = next;
        changed_at = lv_tick_get();
        blinking = false;
    }
    copy_text(detail_text, sizeof(detail_text), bounded);
    bool quiet = next == ONLINE || next == OFFLINE || next == SLEEPING;
    lv_label_set_text(title_label, quiet ? "" : expressions[next].title);
    if (next == APPROVAL) {
        lv_label_set_text(title_label, input_pending ? LV_SYMBOL_WARNING "  WAITING FOR YOU" :
                          LV_SYMBOL_WARNING "  APPROVAL NEEDED");
    }
    lv_label_set_text(detail_label, detail_text);
    lv_obj_set_height(detail_label, next == APPROVAL && input_pending ? 36 : 18);
    if (detail_text[0] == '\0') lv_obj_add_flag(detail_label, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_clear_flag(detail_label, LV_OBJ_FLAG_HIDDEN);
    if (quiet) {
        /* Idle connectivity is expressed by the face, saving screen space. */
        lv_obj_add_flag(title_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(hint_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(status_dot, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_height(card, 42);
        lv_obj_align(detail_label, LV_ALIGN_CENTER, 0, 0);
        if (detail_text[0] == '\0') lv_obj_add_flag(card, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_clear_flag(card, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(card, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(title_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(hint_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(status_dot, LV_OBJ_FLAG_HIDDEN);
        lv_obj_align(detail_label, LV_ALIGN_TOP_MID, 0, 42);
        lv_obj_set_height(card, detail_text[0] == '\0' ? 62 :
                          next == APPROVAL && input_pending ? 104 : 84);
    }
    /* Keep the native Home title clear; approval temporarily takes priority. */
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, next == APPROVAL ? 190 : CARD_Y);
    lv_color_t target = lv_color_hex(expressions[next].accent);
    lv_obj_set_style_border_color(card, target, 0);
    lv_obj_set_style_bg_color(status_dot, target, 0);
    lv_obj_set_style_text_color(hint_label, target, 0);
    update_hint();
    if (!lv_obj_has_flag(card, LV_OBJ_FLAG_HIDDEN)) animate_card();
    lv_obj_invalidate(orb);
}

static void raise_attention(void)
{
    if (!approval_pending || orb == NULL) return;
    /* Native top-layer screens can otherwise cover a received alert. These
     * objects remain passive, so Watcher's own touch controls still work. */
    lv_obj_move_foreground(orb);
    lv_obj_move_foreground(card);
#if LV_USE_QRCODE
    if (approval_qr != NULL && !lv_obj_has_flag(approval_qr, LV_OBJ_FLAG_HIDDEN))
        lv_obj_move_foreground(approval_qr);
#endif
}

static void frame_cb(lv_timer_t *timer)
{
    (void)timer;
    uint32_t now = lv_tick_get();
    if (orb == NULL || !lv_obj_is_visible(orb)) return;
    if (lv_tick_elaps(activity_at) >= 2000) {
        raise_attention();
        lv_disp_trig_activity(NULL);
        activity_at = now;
    }
    uint32_t age = lv_tick_elaps(changed_at);
    int32_t progress = age >= TRANSITION_MS ? 255 : age * 255 / TRANSITION_MS;
    /* Smoothstep gives the face and color the same gentle transition. */
    morph = progress * progress / 255 * (765 - 2 * progress) / 255;
    accent = lv_color_mix(lv_color_hex(expressions[emotion].accent), source_accent, morph);
    if (emotion != OFFLINE && emotion != SLEEPING && !blinking &&
        (int32_t)(now - next_blink_at) >= 0) {
        blinking = true;
        blink_at = now;
        next_blink_at = now + 2500 + (blink_count++ % 3) * 450;
    }
    if (blinking && lv_tick_elaps(blink_at) >= 260) blinking = false;
    lv_obj_invalidate(orb);
}

static void passive(lv_obj_t *obj)
{
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    passive(label);
    return label;
}

void view_hermes_init(void)
{
    if (orb != NULL) return;
    emotion = OFFLINE;
    accent = source_accent = lv_color_hex(expressions[emotion].accent);
    started_at = changed_at = activity_at = lv_tick_get();
    next_blink_at = started_at + 2500;

    orb = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(orb);
    lv_obj_set_size(orb, ORB_SIZE, ORB_SIZE);
    lv_obj_align(orb, LV_ALIGN_TOP_MID, 0, ORB_Y);
    passive(orb);
    lv_obj_add_event_cb(orb, orb_draw_cb, LV_EVENT_DRAW_MAIN, NULL);

    card = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, CARD_WIDTH, 62);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, CARD_Y);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x111B2A), 0);
    lv_obj_set_style_bg_opa(card, CARD_OPACITY, 0);
    lv_obj_set_style_radius(card, 18, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_opa(card, 110, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    passive(card);
    status_dot = lv_obj_create(card);
    lv_obj_remove_style_all(status_dot);
    lv_obj_set_size(status_dot, 4, 4);
    lv_obj_set_style_radius(status_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(status_dot, LV_OPA_COVER, 0);
    lv_obj_align(status_dot, LV_ALIGN_TOP_MID, 0, 7);
    passive(status_dot);
    title_label = make_label(card, &lv_font_montserrat_18, lv_color_hex(0xF4F8FF));
    lv_obj_set_width(title_label, CARD_WIDTH - 20);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 17);
    detail_label = make_label(card, &lv_font_montserrat_14, lv_color_hex(0xDDE5F3));
    lv_obj_set_size(detail_label, CARD_WIDTH - 28, 18);
    lv_obj_align(detail_label, LV_ALIGN_TOP_MID, 0, 42);
    hint_label = make_label(card, &lv_font_montserrat_14, accent);
    lv_obj_set_width(hint_label, CARD_WIDTH - 16);
    lv_obj_align(hint_label, LV_ALIGN_BOTTOM_MID, 0, -7);

    /* Optional QR appears only when the sender supplies a real approval URL. */
#if LV_USE_QRCODE
    approval_qr = lv_qrcode_create(lv_layer_top(), 104,
                                    lv_color_hex(0x101927), lv_color_hex(0xFFFFFF));
    if (approval_qr != NULL) {
        lv_obj_align(approval_qr, LV_ALIGN_TOP_MID, 0, 286);
        /* The bundled encoder needs an explicit white quiet zone for scanning. */
        lv_obj_set_style_outline_color(approval_qr, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_outline_width(approval_qr, 8, 0);
        lv_obj_set_style_outline_pad(approval_qr, 0, 0);
        lv_obj_set_style_outline_opa(approval_qr, LV_OPA_COVER, 0);
        passive(approval_qr);
        lv_obj_add_flag(approval_qr, LV_OBJ_FLAG_HIDDEN);
    }
#endif
    copy_text(detail_text, sizeof(detail_text), "initializing");
    set_display(OFFLINE, "");
    lv_timer_create(frame_cb, FRAME_MS, NULL);
}

void view_hermes_set_connected(bool is_connected)
{
    lvgl_port_lock(0);
    connected = is_connected;
    if (orb != NULL) {
        if (approval_pending) update_hint();
        else if (!connected) set_display(OFFLINE, "");
        /* A broker connection alone does not mean the Hermes agent is online. */
    }
    lvgl_port_unlock();
}

static void request_approval_locked(const char *request_id, const char *detail,
                                    const char *url, bool is_input)
{
    if (request_id != NULL && strlen(request_id) >= sizeof(approval_id)) return;
    if (orb != NULL) {
        /* Legacy heartbeats cannot replace a request with a known identity. */
        if (approval_pending && approval_id[0] != '\0' &&
            (request_id == NULL || request_id[0] == '\0')) return;
        bool new_request = !approval_pending || input_pending != is_input ||
                           strcmp(approval_id, request_id != NULL ? request_id : "") != 0;
        approval_pending = true;
        input_pending = is_input;
        copy_text(approval_id, sizeof(approval_id), request_id);
        /* Avoid truncating a link into a different, unusable destination. */
        bool valid_url = url != NULL && strlen(url) < sizeof(approval_url) &&
                         (strncmp(url, "https://", 8) == 0 || strncmp(url, "http://", 7) == 0);
        const char *new_url = valid_url ? url : "";
#if LV_USE_QRCODE
        bool url_changed = strcmp(approval_url, new_url) != 0;
#endif
        copy_text(approval_url, sizeof(approval_url), new_url);
#if LV_USE_QRCODE
        if (approval_qr != NULL && url_changed) {
            lv_obj_add_flag(approval_qr, LV_OBJ_FLAG_HIDDEN);
            if (approval_url[0] != '\0' &&
                lv_qrcode_update(approval_qr, approval_url, strlen(approval_url)) == LV_RES_OK) {
                lv_obj_clear_flag(approval_qr, LV_OBJ_FLAG_HIDDEN);
            }
        }
#endif
        set_display(APPROVAL, detail != NULL && detail[0] != '\0' ? detail :
                    is_input ? "Waiting for your choice" : "Hermes is waiting for your permission");
        update_hint();
        raise_attention();
        if (new_request) app_hermes_sound_request(is_input ? HERMES_SOUND_CHOICE : HERMES_SOUND_APPROVAL);
        lv_disp_trig_activity(NULL);
    }
}

void view_hermes_request_approval(const char *request_id, const char *detail,
                                 const char *url)
{
    lvgl_port_lock(0);
    request_approval_locked(request_id, detail, url, false);
    lvgl_port_unlock();
}

void view_hermes_request_input(const char *request_id, const char *detail)
{
    lvgl_port_lock(0);
    request_approval_locked(request_id, detail, NULL, true);
    lvgl_port_unlock();
}

bool view_hermes_approval_visible(const char *request_id)
{
    lvgl_port_lock(0);
    bool visible = approval_pending && card != NULL && lv_obj_is_visible(card) &&
                   strcmp(approval_id, request_id != NULL ? request_id : "") == 0;
    lvgl_port_unlock();
    return visible;
}

void view_hermes_resolve_approval(const char *request_id, const char *outcome)
{
    lvgl_port_lock(0);
    bool numbered = request_id != NULL && request_id[0] != '\0';
    if (orb != NULL && approval_pending &&
        (numbered ? strcmp(request_id, approval_id) == 0 : approval_id[0] == '\0')) {
        bool was_input = input_pending;
        approval_pending = input_pending = false;
        approval_id[0] = approval_url[0] = '\0';
#if LV_USE_QRCODE
        if (approval_qr != NULL) lv_obj_add_flag(approval_qr, LV_OBJ_FLAG_HIDDEN);
#endif
        char normalized_outcome[16];
        normalize_status(normalized_outcome, sizeof(normalized_outcome), outcome);
        emotion_t next = was_input && strcmp(normalized_outcome, "APPROVED") == 0 ?
                         THINKING : outcome != NULL ? parse_emotion(outcome) : ONLINE;
        if (next == APPROVAL) next = ONLINE;
        set_display(connected ? next : OFFLINE, was_input && next == THINKING ? "Continuing your task" : outcome);
    }
    lvgl_port_unlock();
}

void view_hermes_set_state(const char *state, const char *detail)
{
    if (state == NULL) return;
    emotion_t next = parse_emotion(state);
    lvgl_port_lock(0);
    if (view_hermes_is_approval_state(state, detail)) {
        /* A repeated status must preserve a structured request's ID and QR. */
        if (!approval_pending) request_approval_locked(NULL, detail, NULL, false);
    } else {
        /* Heartbeats/task updates cannot dismiss an unresolved request. */
        if (orb != NULL && !approval_pending) set_display(next, detail);
    }
    lvgl_port_unlock();
}

void view_hermes_set_status(const char *text)
{
    if (text == NULL) return;
    /* Keep the existing plain-text publisher working. Details may follow ':'. */
    const char *detail = strchr(text, ':');
    if (detail != NULL) {
        ++detail;
        while (*detail == ' ') ++detail;
    }
    view_hermes_set_state(text, detail);
}
