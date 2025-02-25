#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "toniefile.h"

#include "toniebox_state_type.h"
#include "handler_sse.h"
#include "mqtt.h"

void toniebox_state_init();
toniebox_state_t *get_toniebox_state();
toniebox_state_t *get_toniebox_state_id(uint8_t id);

void tbs_tag_placed(client_ctx_t *client_ctx, uint64_t uid, bool valid);
void tbs_tag_unplaced(client_ctx_t *client_ctx, uint64_t uid, bool valid);
void tbs_tag_removed(client_ctx_t *client_ctx);
void tbs_knock(client_ctx_t *client_ctx, bool forward);
void tbs_tilt(client_ctx_t *client_ctx, bool forward);
void tbs_playback(client_ctx_t *client_ctx, toniebox_state_playback_t playback);
void tbs_playback_stop(client_ctx_t *client_ctx);
void tbs_playback_file(client_ctx_t *client_ctx, char *filepath);
void tbs_playback_system_sound(client_ctx_t *client_ctx, toniebox_state_system_sound_lang_t language, toniebox_state_system_sound_t system_sound);