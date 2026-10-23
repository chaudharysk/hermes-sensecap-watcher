#ifndef VIEW_HERMES_H
#define VIEW_HERMES_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Called by view_init with the LVGL port locked. */
void view_hermes_init(void);

/* These entry points acquire the LVGL port lock themselves. */
void view_hermes_set_status(const char *text);
void view_hermes_set_state(const char *state, const char *detail);
/* Shared classification for plain statuses and structured MQTT events. */
bool view_hermes_is_approval_state(const char *state, const char *detail);
void view_hermes_set_connected(bool connected);
void view_hermes_request_approval(const char *request_id, const char *detail,
                                 const char *approval_url);
/* A choice/question needs an answer in Telegram, without granting permission. */
void view_hermes_request_input(const char *request_id, const char *detail);
/* Confirms that the matching request has been applied to the visible UI. */
bool view_hermes_approval_visible(const char *request_id);
void view_hermes_resolve_approval(const char *request_id, const char *outcome);

#ifdef __cplusplus
}
#endif

#endif
