#include "toniebox_state.h"
#include "settings.h"
#include "server_helpers.h"

static toniebox_state_t Box_State_Overlay[MAX_OVERLAYS];

void toniebox_state_init()
{
    for (size_t i = 0; i < MAX_OVERLAYS; i++)
    {
        osMemset(&Box_State_Overlay[i], 0, sizeof(toniebox_state_t));
    }
}

toniebox_state_t *get_toniebox_state()
{
    return get_toniebox_state_id(0);
}
toniebox_state_t *get_toniebox_state_id(uint8_t id)
{
    return &Box_State_Overlay[id];
}

void tbs_tag_placed(client_ctx_t *client_ctx, uint64_t uid, bool valid)
{
    client_ctx->state->tag.uid = uid;
    client_ctx->state->tag.valid = valid;
    if (valid)
    {
        setLastUid(client_ctx->state->tag.uid, client_ctx->settingsNoOverlay);
    }

    char cuid[16 + 1];
    osSprintf((char *)cuid, "%016" PRIX64 "", (int64_t)uid);

    sse_sendEvent(valid ? "TagValid" : "TagInvalid", cuid, true);

    mqtt_sendBoxEvent(valid ? "TagValid" : "TagInvalid", cuid, client_ctx);
    mqtt_sendBoxEvent(!valid ? "TagValid" : "TagInvalid", "", client_ctx);
}

void tbs_tag_unplaced(client_ctx_t *client_ctx, uint64_t uid, bool valid)
{
    client_ctx->state->tag.uid = uid;
    client_ctx->state->tag.valid = valid;
    if (valid)
    {
        setLastUid(client_ctx->state->tag.uid, client_ctx->settingsNoOverlay);
    }

    char cuid[16 + 2];
    osSprintf((char *)cuid, "-%016" PRIX64 "", (int64_t)uid);

    mqtt_sendBoxEvent(valid ? "TagValid" : "TagInvalid", cuid, client_ctx);
    mqtt_sendBoxEvent(!valid ? "TagValid" : "TagInvalid", "-", client_ctx);
}

void tbs_knock(client_ctx_t *client_ctx, bool forward)
{
    sse_sendEvent("knock", forward ? "forward" : "backward", true);
    mqtt_sendBoxEvent(forward ? "KnockForward" : "KnockBackward", "{\"event_type\": \"triggered\"}", client_ctx);
}
void tbs_tilt(client_ctx_t *client_ctx, bool forward)
{
    sse_sendEvent("tilt", forward ? "forward" : "backward", true);
    mqtt_sendBoxEvent(forward ? "TiltForward" : "TiltBackward", "{\"event_type\": \"triggered\"}", client_ctx);
}

void tbs_playback(client_ctx_t *client_ctx, toniebox_state_playback_t playback)
{
    switch (playback)
    {
    case TBS_PLAYBACK_STARTING:
        sse_sendEvent("playback", "starting", true);
        mqtt_sendBoxEvent("Playback", "OFF", client_ctx);
    case TBS_PLAYBACK_STARTED:
        sse_sendEvent("playback", "started", true);
        mqtt_sendBoxEvent("Playback", "ON", client_ctx);
        break;
    case TBS_PLAYBACK_STOPPED:
        if (client_ctx->state->box.stream_ctx.stop_on_playback_stop && !client_ctx->state->box.stream_ctx.quit && client_ctx->state->tag.valid)
        {
            client_ctx->state->box.stream_ctx.active = false;
        }
        client_ctx->state->tag.audio_id = 0;
        client_ctx->state->tag.valid = false;
        client_ctx->state->tag.uid = 0;

        sse_sendEvent("playback", "stopped", true);
        mqtt_sendBoxEvent("Playback", "OFF", client_ctx);
        mqtt_sendBoxEvent("ContentAudioId", "", client_ctx);
        mqtt_sendBoxEvent("ContentTitle", "", client_ctx);
        char *url = custom_asprintf("%s/img_empty.png", settings_get_string("core.host_url"));
        mqtt_sendBoxEvent("ContentPicture", url, client_ctx);
        osFreeMem(url);
        break;
    default:
        break;
    }
}

void tbs_playback_file(client_ctx_t *client_ctx, char *filepath)
{
    // filepath: content/00000000/00000012
    // Get the first part ("content")
    char *content = strtok(filepath, "/");
    if (content == NULL)
    {
        return;
    }
    // Get the second part ("00000000")
    char *dir = strtok(NULL, "/");
    if (dir == NULL || strlen(dir) != 8)
    {
        return;
    }
    // Get the third part ("00000012")
    char *file = strtok(NULL, "/");
    if (file == NULL || strlen(file) != 8)
    {
        return;
    }

    if (strncmp(dir, "0000000", 7) == 0)
    {
        if (strncmp(file, "000000", 6) == 0)
        {
            toniebox_state_system_sound_t language = (toniebox_state_system_sound_lang_t)strtol(dir, NULL, 16);
            toniebox_state_system_sound_t sound = (toniebox_state_system_sound_t)strtol(file, NULL, 16);
            tbs_playback_system_sound(client_ctx, language, sound);
        }
    }
}
void tbs_playback_system_sound(client_ctx_t *client_ctx, toniebox_state_system_sound_lang_t language, toniebox_state_system_sound_t system_sound)
{
    tbs_playback(client_ctx, TBS_PLAYBACK_STOPPED);
}