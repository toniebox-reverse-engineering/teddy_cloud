#include "tonie_audio_playlist.h"

#include "fs_port.h"
#include "toniefile.h"
#include "server_helpers.h"
#include "cJSON.h"
#include "json_helper.h"
#include "handler.h"

bool_t is_valid_tap_file(char *filename)
{

    tonie_audio_playlist_t tap;
    osMemset(&tap, 0, sizeof(tap));
    tap_load(filename, &tap);
    if (tap._valid)
    {
        tap_free(&tap);
        return true;
    }
    return false;
}

error_t tap_load(char *filename, tonie_audio_playlist_t *tap)
{
    size_t fileSize = 0;
    error_t error = fsGetFileSize(filename, (uint32_t *)(&fileSize));
    tap->_cached = false;
    tap->_valid = false;

    if (error != NO_ERROR)
    {
        return error;
    }

    FsFile *fsFile = fsOpenFile(filename, FS_FILE_MODE_READ);
    if (fsFile == NULL)
    {
        return ERROR_FILE_OPENING_FAILED;
    }

    size_t sizeRead;
    char *data = osAllocMem(fileSize);
    size_t pos = 0;

    while (pos < fileSize)
    {
        fsReadFile(fsFile, &data[pos], fileSize - pos, &sizeRead);
        pos += sizeRead;
    }
    fsCloseFile(fsFile);

    cJSON *tapJson = cJSON_ParseWithLengthOpts(data, fileSize, 0, 0);
    osFreeMem(data);
    if (tapJson == NULL)
    {
        // const char *error_ptr = cJSON_GetErrorPtr();
        TRACE_ERROR("Json parse error\r\n");
        error = ERROR_INVALID_FILE;
    }
    else
    {
        tap->type = jsonGetString(tapJson, "type");
        if (osStrcmp(tap->type, "tap") == 0)
        {
            tap->audio_id = jsonGetUInt32(tapJson, "audio_id");
            tap->filepath = jsonGetString(tapJson, "filepath");

            tap->_filepath_resolved = strdup(tap->filepath);
            resolveSpecialPathPrefix(&tap->_filepath_resolved, get_settings());

            tap->name = jsonGetString(tapJson, "name");
            const cJSON *filesJson = cJSON_GetObjectItemCaseSensitive(tapJson, "files");
            tap->filesCount = cJSON_GetArraySize(filesJson);
            if (tap->filesCount > 0)
            {
                tap->files = osAllocMem(tap->filesCount * sizeof(tap_file_t));
                uint8_t i = 0;
                cJSON *fileJson;
                cJSON_ArrayForEach(fileJson, filesJson)
                {
                    tap->files[i].filepath = jsonGetString(fileJson, "filepath");
                    tap->files[i]._filepath_resolved = strdup(tap->files[i].filepath);
                    resolveSpecialPathPrefix(&tap->files[i]._filepath_resolved, get_settings());
                    tap->files[i].name = jsonGetString(fileJson, "name");
                    i++;
                }
            }
        }
        else
        {
            error = ERROR_INVALID_FILE;
        }
    }

    cJSON_Delete(tapJson);
    if (error == NO_ERROR)
    {
        if (isValidTaf(tap->_filepath_resolved, get_settings()->core.tap_taf_validation))
        {
            tap->_cached = true;
            // TODO check audio id if different and check settings.
        }
        tap->_valid = true;
    }
    return error;
}
error_t tap_save(char *filename, tonie_audio_playlist_t *tap)
{

    cJSON *tapJson = cJSON_CreateObject();
    error_t error = NO_ERROR;

    cJSON_AddStringToObject(tapJson, "type", TAP_TYPE_TAP);
    cJSON_AddNumberToObject(tapJson, "audio_id", tap->audio_id);
    cJSON_AddStringToObject(tapJson, "filepath", tap->filepath);
    cJSON_AddStringToObject(tapJson, "name", tap->name);
    if (tap->files != NULL)
    {
        cJSON *filesJson = cJSON_AddArrayToObject(tapJson, "files");
        for (size_t i = 0; i < tap->filesCount; i++)
        {
            cJSON *fileJson = cJSON_CreateObject();
            cJSON_AddStringToObject(fileJson, "filepath", tap->files[i].filepath);
            cJSON_AddStringToObject(fileJson, "name", tap->files[i].name);
            cJSON_AddItemToArray(filesJson, fileJson);
        }
    }
    char *jsonRaw = cJSON_Print(tapJson);

    FsFile *fsFile = fsOpenFile(filename, FS_FILE_MODE_WRITE);
    if (fsFile == NULL)
    {
        error = ERROR_FILE_OPENING_FAILED;
    }
    else
    {
        error = fsWriteFile(fsFile, jsonRaw, osStrlen(jsonRaw));
        fsCloseFile(fsFile);
    }

    cJSON_Delete(tapJson);
    osFreeMem(jsonRaw);
    return error;
}

void tap_free(tonie_audio_playlist_t *tap)
{
    for (size_t i = 0; i < tap->filesCount; i++)
    {
        osFreeMem(tap->files[i].filepath);
        osFreeMem(tap->files[i].name);
    }
    if (tap->filesCount > 0)
    {
        tap->filesCount = 0;
        osFreeMem(tap->files);
    }
    if (tap->type != NULL)
    {
        osFreeMem(tap->type);
    }
    if (tap->filepath != NULL)
    {
        osFreeMem(tap->filepath);
    }
    if (tap->_filepath_resolved != NULL)
    {
        osFreeMem(tap->_filepath_resolved);
    }
    if (tap->name != NULL)
    {
        osFreeMem(tap->name);
    }

    osMemset(tap, 0, sizeof(tonie_audio_playlist_t));
}

error_t tap_generate_taf(tonie_audio_playlist_t *tap, size_t *current_source, bool_t *active, bool_t force)
{
    error_t error = NO_ERROR;
    bool_t sweep = false;
    tonie_info_t *tonieInfo = getTonieInfo(tap->_filepath_resolved, false, get_settings());

    // TODO custom audio id resolving
    if (force || !tonieInfo->valid || tonieInfo->tafHeader->audio_id != tap->audio_id)
    {
        char *tmp_taf = custom_asprintf("%s.tmp", tap->_filepath_resolved);
        char source[99][PATH_LEN];
        if (tap->filesCount == 0)
        {
            osFreeMem(tmp_taf);
            freeTonieInfo(tonieInfo);
            return ERROR_INVALID_FILE;
        }

        for (size_t i = 0; i < tap->filesCount; i++)
        {
            osStrcpy(source[i], tap->files[i]._filepath_resolved);
        }
        // toniefile_t *taf = toniefile_create(tmp_taf, tap->audio_id, false, 0);
        error = ffmpeg_stream(source, tap->filesCount, current_source, tmp_taf, 0, active, &sweep, false, false);
        // toniefile_close(taf);
        if (error != NO_ERROR)
        {
            fsDeleteFile(tmp_taf);
        }
        else
        {
            error = fsMoveFile(tmp_taf, tap->_filepath_resolved, true);
        }
        osFreeMem(tmp_taf);
        freeTonieInfo(tonieInfo);
    }
    return error;
}

void tap_generate_task(void *param)
{
    stream_ctx_t *stream_ctx = (stream_ctx_t *)param;
    tap_generate_param_t *tap_ctx = (tap_generate_param_t *)stream_ctx->ctx;

    stream_ctx->error = tap_generate_taf(tap_ctx->tap, &stream_ctx->current_source, &stream_ctx->active, tap_ctx->force);
    stream_ctx->quit = true;
    osDeleteTask(OS_SELF_TASK_ID);
}