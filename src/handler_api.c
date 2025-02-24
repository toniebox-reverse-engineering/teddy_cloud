
#include <sys/types.h>
#include <time.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "path.h"
#include "path_ext.h"
#include "server_helpers.h"
#include "fs_port.h"
#include "handler.h"
#include "handler_api.h"
#include "handler_cloud.h"
#include "settings.h"
#include "stats.h"
#include "returncodes.h"
#include "cJSON.h"
#include "toniefile.h"
#include "toniesJson.h"
#include "fs_ext.h"
#include "os_ext.h"
#include "cert.h"
#include "esp32.h"
#include "cache.h"

error_t parsePostData(HttpConnection *connection, char_t *post_data, size_t buffer_size)
{
    error_t error = NO_ERROR;
    osMemset(post_data, 0, buffer_size);
    size_t size;
    if (buffer_size > 0 && buffer_size <= connection->request.byteCount)
    {
        TRACE_ERROR("Body size %" PRIuSIZE " bigger than buffer size %" PRIuSIZE " bytes\r\n", connection->request.byteCount, buffer_size);
        return ERROR_BUFFER_OVERFLOW;
    }
    error = httpReceive(connection, post_data, buffer_size, &size, 0x00);
    if (error != NO_ERROR)
    {
        TRACE_ERROR("Could not read post data\r\n");
        return error;
    }
    return error;
}

/* sanitizes the path - needs two additional characters in worst case, so make sure 'path' has enough space */
void sanitizePath(char *path, bool isDir)
{
    size_t i, j;
    bool slash = false;

    pathCanonicalize(path);

    /* Merge all double (or more) slashes // */
    for (i = 0, j = 0; path[i]; ++i)
    {
        if (path[i] == PATH_SEPARATOR)
        {
            if (slash)
                continue;
            slash = true;
        }
        else
        {
            slash = false;
        }
        path[j++] = path[i];
    }

    /* Make sure the path doesn't end with a '/' unless it's the root directory. */
    if (j > 1 && path[j - 1] == PATH_SEPARATOR)
        j--;

    /* Null terminate the sanitized path */
    path[j] = '\0';

#ifndef WIN32
    /* If path doesn't start with '/', shift right and add '/' */
    if (path[0] != PATH_SEPARATOR)
    {
        memmove(&path[1], &path[0], j + 1); // Shift right
        path[0] = PATH_SEPARATOR;           // Add '/' at the beginning
        j++;
    }
#endif

    /* If path doesn't end with '/', add '/' at the end */
    if (isDir)
    {
        if (path[j - 1] != PATH_SEPARATOR)
        {
            path[j] = PATH_SEPARATOR; // Add '/' at the end
            path[j + 1] = '\0';       // Null terminate
        }
    }
}

error_t queryPrepare(const char *queryString, const char **rootPath, char *overlay, size_t overlay_size, settings_t **settings)
{
    char special[16];

    osStrcpy(special, "");

    if (overlay)
    {
        osStrcpy(overlay, "");
        if (queryGet(queryString, "overlay", overlay, overlay_size))
        {
            TRACE_DEBUG("got overlay '%s'\r\n", overlay);

            *settings = get_settings_ovl(overlay);
        }
    }

    *rootPath = settings_get_string_ovl("internal.contentdirfull", overlay);

    if (*rootPath == NULL || !fsDirExists(*rootPath))
    {
        TRACE_ERROR("internal.contentdirfull not set to a valid path: '%s'\r\n", *rootPath);
        return ERROR_FAILURE;
    }

    if (queryGet(queryString, "special", special, sizeof(special)))
    {
        TRACE_DEBUG("requested index for '%s'\r\n", special);
        if (!osStrcmp(special, "library"))
        {
            *rootPath = settings_get_string_ovl("internal.librarydirfull", overlay);

            if (*rootPath == NULL || !fsDirExists(*rootPath))
            {
                TRACE_ERROR("internal.librarydirfull not set to a valid path: '%s'\r\n", *rootPath);
                return ERROR_FAILURE;
            }
        }
    }

    return NO_ERROR;
}

void addToniesJsonInfoJson(toniesJson_item_t *item, char *fallbackModel, cJSON *parent)
{
    cJSON *tracksJson = cJSON_CreateArray();
    cJSON *tonieInfoJson;
    if (parent->type == cJSON_Object)
    {
        tonieInfoJson = cJSON_AddObjectToObject(parent, "tonieInfo");
    }
    else if (parent->type == cJSON_Array)
    {
        tonieInfoJson = cJSON_CreateObject();
        cJSON_AddItemToArray(parent, tonieInfoJson);
    }
    else
    {
        return;
    }

    cJSON_AddItemToObject(tonieInfoJson, "tracks", tracksJson);
    if (item != NULL)
    {
        cJSON_AddStringToObject(tonieInfoJson, "model", item->model);
        cJSON_AddStringToObject(tonieInfoJson, "series", item->series);
        cJSON_AddStringToObject(tonieInfoJson, "episode", item->episodes);
        cJSON_AddStringToObject(tonieInfoJson, "picture", item->picture);
        cJSON_AddStringToObject(tonieInfoJson, "language", item->language);
        for (size_t i = 0; i < item->tracks_count; i++)
        {
            cJSON_AddItemToArray(tracksJson, cJSON_CreateString(item->tracks[i]));
        }
    }
    else
    {
        if (fallbackModel != NULL)
        {
            cJSON_AddStringToObject(tonieInfoJson, "model", fallbackModel);
        }
        else
        {
            cJSON_AddStringToObject(tonieInfoJson, "model", "");
        }
        cJSON_AddStringToObject(tonieInfoJson, "series", "");
        cJSON_AddStringToObject(tonieInfoJson, "episode", "");

        cJSON_AddStringToObject(tonieInfoJson, "picture", "/img_unknown.png");
    }
}

error_t handleApiAssignUnknown(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx)
{
    const char *rootPath = NULL;

    TRACE_INFO("Query: '%s'\r\n", queryString);

    char path[256];
    char overlay[16];

    osStrcpy(path, "");

    if (queryPrepare(queryString, &rootPath, overlay, sizeof(overlay), &client_ctx->settings) != NO_ERROR)
    {
        return ERROR_FAILURE;
    }

    if (queryGet(queryString, "path", path, sizeof(path)))
    {
        TRACE_INFO("got path '%s'\r\n", path);
    }

    /* important: first canonicalize path, then merge to prevent directory traversal attacks */
    pathSafeCanonicalize(path);
    char *pathAbsolute = custom_asprintf("%s%c%s", rootPath, PATH_SEPARATOR, path);
    pathSafeCanonicalize(pathAbsolute);

    TRACE_INFO("Set '%s' for next unknown request\r\n", pathAbsolute);

    settings_set_string("internal.assign_unknown", pathAbsolute);
    osFreeMem(pathAbsolute);

    return httpOkResponse(connection);
}

error_t handleApiGetIndex(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx)
{
    cJSON *json = cJSON_CreateObject();
    cJSON *jsonArray = cJSON_AddArrayToObject(json, "options");

    char overlay[16];
    osStrcpy(overlay, "");
    char internal[6];
    osStrcpy(internal, "");
    char noLevel[6];
    osStrcpy(noLevel, "");

    bool showInternal = false;
    bool isNoLevel = false;

    if (queryGet(queryString, "overlay", overlay, sizeof(overlay)))
    {
        TRACE_DEBUG("got overlay '%s'\r\n", overlay);
    }
    if (queryGet(queryString, "internal", internal, sizeof(internal)))
    {
        if (internal[0] == 't')
        {
            showInternal = true;
        }
    }
    if (queryGet(queryString, "nolevel", noLevel, sizeof(internal)))
    {
        if (noLevel[0] == 't')
        {
            isNoLevel = true;
        }
    }
    for (size_t pos = 0; pos < settings_get_size(); pos++)
    {
        setting_item_t *opt = settings_get_ovl(pos, overlay);

        if (opt->type == TYPE_TREE_DESC)
        {
            continue;
        }

        if (opt->internal && !showInternal)
        {
            continue;
        }

        settings_level user_level = get_settings_ovl(overlay)->core.settings_level;
        if (!isNoLevel && opt->level > user_level)
        {
            continue;
        }

        cJSON *jsonEntry = cJSON_CreateObject();
        cJSON_AddStringToObject(jsonEntry, "ID", opt->option_name);
        cJSON_AddStringToObject(jsonEntry, "shortname", opt->option_name);
        cJSON_AddStringToObject(jsonEntry, "description", opt->description);
        cJSON_AddStringToObject(jsonEntry, "label", opt->label);
        cJSON_AddBoolToObject(jsonEntry, "overlayed", opt->overlayed);
        cJSON_AddBoolToObject(jsonEntry, "internal", opt->internal);
        cJSON_AddNumberToObject(jsonEntry, "level", opt->level);

        switch (opt->type)
        {
        case TYPE_BOOL:
            cJSON_AddStringToObject(jsonEntry, "type", "bool");
            cJSON_AddBoolToObject(jsonEntry, "value", settings_get_bool_ovl(opt->option_name, overlay));
            cJSON_AddBoolToObject(jsonEntry, "valueInit", opt->init.bool_value);
            break;
        case TYPE_UNSIGNED:
            cJSON_AddStringToObject(jsonEntry, "type", "uint");
            cJSON_AddNumberToObject(jsonEntry, "value", settings_get_unsigned_ovl(opt->option_name, overlay));
            cJSON_AddNumberToObject(jsonEntry, "valueInit", opt->init.unsigned_value);
            cJSON_AddNumberToObject(jsonEntry, "min", opt->min.unsigned_value);
            cJSON_AddNumberToObject(jsonEntry, "max", opt->max.unsigned_value);
            break;
        case TYPE_SIGNED:
            cJSON_AddStringToObject(jsonEntry, "type", "int");
            cJSON_AddNumberToObject(jsonEntry, "value", settings_get_signed_ovl(opt->option_name, overlay));
            cJSON_AddNumberToObject(jsonEntry, "valueInit", opt->init.signed_value);
            cJSON_AddNumberToObject(jsonEntry, "min", opt->min.signed_value);
            cJSON_AddNumberToObject(jsonEntry, "max", opt->max.signed_value);
            break;
        case TYPE_HEX:
            cJSON_AddStringToObject(jsonEntry, "type", "hex");
            cJSON_AddNumberToObject(jsonEntry, "value", settings_get_unsigned_ovl(opt->option_name, overlay));
            cJSON_AddNumberToObject(jsonEntry, "valueInit", opt->init.unsigned_value);
            cJSON_AddNumberToObject(jsonEntry, "min", opt->min.unsigned_value);
            cJSON_AddNumberToObject(jsonEntry, "max", opt->max.unsigned_value);
            break;
        case TYPE_STRING:
            cJSON_AddStringToObject(jsonEntry, "type", "string");
            cJSON_AddStringToObject(jsonEntry, "value", settings_get_string_ovl(opt->option_name, overlay));
            cJSON_AddStringToObject(jsonEntry, "valueInit", opt->init.string_value);
            break;
        case TYPE_FLOAT:
            cJSON_AddStringToObject(jsonEntry, "type", "float");
            cJSON_AddNumberToObject(jsonEntry, "value", settings_get_float_ovl(opt->option_name, overlay));
            cJSON_AddNumberToObject(jsonEntry, "valueInit", opt->init.float_value);
            cJSON_AddNumberToObject(jsonEntry, "min", opt->min.float_value);
            cJSON_AddNumberToObject(jsonEntry, "max", opt->max.float_value);
            break;
        case TYPE_TREE_DESC:
            cJSON_AddStringToObject(jsonEntry, "type", "desc");
            break;
        default:
            break;
        }

        cJSON_AddItemToArray(jsonArray, jsonEntry);
    }

    char *jsonString = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);

    httpInitResponseHeader(connection);
    connection->response.contentType = "text/json";
    connection->response.contentLength = osStrlen(jsonString);

    return httpWriteResponse(connection, jsonString, connection->response.contentLength, true);
}

error_t handleApiGetBoxes(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx)
{
    cJSON *json = cJSON_CreateObject();
    cJSON *jsonArray = cJSON_AddArrayToObject(json, "boxes");

    for (size_t i = 1; i < MAX_OVERLAYS; i++)
    {
        settings_t *settings = get_settings_id(i);
        if (!settings->internal.config_used)
        {
            continue;
        }

        cJSON *jsonEntry = cJSON_CreateObject();
        cJSON_AddStringToObject(jsonEntry, "ID", settings->internal.overlayUniqueId);
        cJSON_AddStringToObject(jsonEntry, "commonName", settings->commonName);
        cJSON_AddStringToObject(jsonEntry, "boxName", settings->boxName);
        cJSON_AddStringToObject(jsonEntry, "boxModel", settings->boxModel); // TODO add color name + url

        cJSON_AddItemToArray(jsonArray, jsonEntry);
    }

    char *jsonString = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);

    httpInitResponseHeader(connection);
    connection->response.contentType = "text/json";
    connection->response.contentLength = osStrlen(jsonString);

    return httpWriteResponse(connection, jsonString, connection->response.contentLength, true);
}

error_t handleApiTrigger(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx)
{
    const char *item = &uri[5];
    char response[256];

    osSprintf(response, "FAILED");

    if (!strcmp(item, "triggerExit"))
    {
        TRACE_INFO("Triggered Exit\r\n");
        settings_set_bool("internal.exit", TRUE);
        settings_set_signed("internal.returncode", RETURNCODE_USER_QUIT);
        osSprintf(response, "OK");
    }
    else if (!strcmp(item, "triggerRestart"))
    {
        TRACE_INFO("Triggered Restart\r\n");
        settings_set_bool("internal.exit", TRUE);
        settings_set_signed("internal.returncode", RETURNCODE_USER_RESTART);
        osSprintf(response, "OK");
    }
    else if (!strcmp(item, "triggerReloadConfig"))
    {
        TRACE_INFO("Triggered ReloadConfig\r\n");
        osSprintf(response, "OK");
        settings_load();
    }
    else if (!strcmp(item, "triggerWriteConfig"))
    {
        TRACE_INFO("Triggered WriteConfig\r\n");
        osSprintf(response, "OK");
        settings_save();
    }

    httpInitResponseHeader(connection);
    connection->response.contentType = "text/plain";
    connection->response.contentLength = osStrlen(response);

    return httpWriteResponse(connection, response, connection->response.contentLength, false);
}

error_t handleApiSettingsGet(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx)
{
    const char *item = &uri[18];

    char response[32];
    osStrcpy(response, "ERROR");
    const char *response_ptr = response;

    char overlay[16];
    osStrcpy(overlay, "");
    if (queryGet(queryString, "overlay", overlay, sizeof(overlay)))
    {
        TRACE_DEBUG("got overlay '%s'\r\n", overlay);
    }
    setting_item_t *opt = settings_get_by_name_ovl(item, overlay);

    if (opt == NULL)
    {
        return ERROR_NOT_FOUND;
    }

    if (opt->level != LEVEL_SECRET)
    {
        switch (opt->type)
        {
        case TYPE_BOOL:
            osSprintf(response, "%s", settings_get_bool_ovl(item, overlay) ? "true" : "false");
            break;
        case TYPE_HEX:
        case TYPE_UNSIGNED:
            osSprintf(response, "%u", settings_get_unsigned_ovl(item, overlay));
            break;
        case TYPE_SIGNED:
            osSprintf(response, "%d", settings_get_signed_ovl(item, overlay));
            break;
        case TYPE_STRING:
            response_ptr = settings_get_string_ovl(item, overlay);
            break;
        case TYPE_FLOAT:
            osSprintf(response, "%f", settings_get_float_ovl(item, overlay));
            break;
        default:
            break;
        }
    }

    httpInitResponseHeader(connection);
    connection->response.contentType = "text/plain";
    connection->response.contentLength = osStrlen(response_ptr);

    return httpWriteResponse(connection, (char_t *)response_ptr, connection->response.contentLength, false);
}

error_t handleApiSettingsSet(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx)
{
    char response[256];
    osSprintf(response, "ERROR");
    const char *item = &uri[18];

    char_t data[BODY_BUFFER_SIZE];
    size_t size;
    if (BODY_BUFFER_SIZE <= connection->request.byteCount)
    {
        TRACE_ERROR("Body size for setting '%s' %zu bigger than buffer size %i bytes\r\n", item, connection->request.byteCount, BODY_BUFFER_SIZE);
    }
    else
    {
        if (connection->request.byteCount > 0)
        {
            error_t error = httpReceive(connection, &data, BODY_BUFFER_SIZE, &size, 0x00);
            if (error != NO_ERROR)
            {
                TRACE_ERROR("httpReceive failed!\r\n");
                return error;
            }
        }
        else
        {
            size = 0;
        }
        data[size] = '\0';

        TRACE_INFO("Setting: '%s' to '%s'\r\n", item, data);

        char overlay[16];
        osStrcpy(overlay, "");
        if (queryGet(queryString, "overlay", overlay, sizeof(overlay)))
        {
            TRACE_DEBUG("got overlay '%s'\r\n", overlay);
        }

        bool success = false;
        if (size > 0)
        {
            success = settings_set_by_string_ovl(item, data, overlay);
        }

        if (success)
        {
            osStrcpy(response, "OK");
        }
    }

    httpPrepareHeader(connection, "text/plain; charset=utf-8", 0);
    return httpWriteResponseString(connection, response, false);
}
error_t handleApiSettingsReset(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx)
{
    char response[256];
    osSprintf(response, "ERROR");
    const char *item = &uri[20];
    char overlay[16];
    osStrcpy(overlay, "");
    if (queryGet(queryString, "overlay", overlay, sizeof(overlay)))
    {
        TRACE_DEBUG("got overlay '%s'\r\n", overlay);
    }
    setting_item_t *opt = settings_get_by_name_ovl(item, overlay);
    setting_item_t *opt_src = settings_get_by_name(item);
    bool success = false;

    if (opt && opt_src)
    {
        if (opt->overlayed || opt == opt_src)
        {
            overlay_settings_init_opt(opt, opt_src);
            if (opt == opt_src)
            {
                TRACE_INFO("Setting: '%s' reset to default\r\n", item);
            }
            else
            {
                TRACE_INFO("Setting: '%s' overlay removed\r\n", item);
            }
            success = true;
        }
        else
        {
            TRACE_WARNING("Setting '%s' is not overlayed\r\n", item);
        }
    }
    else
    {
        TRACE_ERROR("Setting '%s' is unknown\r\n", item);
    }

    if (success)
    {
        osStrcpy(response, "OK");
    }

    httpPrepareHeader(connection, "text/plain; charset=utf-8", osStrlen(response));
    return httpWriteResponseString(connection, response, false);
}

error_t handleApiFileIndexV2(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx)
{
    char overlay[16];
    const char *rootPath = NULL;

    if (queryPrepare(queryString, &rootPath, overlay, sizeof(overlay), &client_ctx->settings) != NO_ERROR)
    {
        return ERROR_FAILURE;
    }

    char path[128];

    if (!queryGet(queryString, "path", path, sizeof(path)))
    {
        osStrcpy(path, "/");
    }

    /* first canonicalize path, then merge to prevent directory traversal bugs */
    pathSafeCanonicalize(path);
    char *pathAbsolute = custom_asprintf("%s%c%s", rootPath, PATH_SEPARATOR, path);
    pathSafeCanonicalize(pathAbsolute);

    FsDir *dir = fsOpenDir(pathAbsolute);
    if (dir == NULL)
    {
        TRACE_ERROR("Failed to open dir '%s'\r\n", pathAbsolute);
        osFreeMem(pathAbsolute);
        return ERROR_FAILURE;
    }

    cJSON *json = cJSON_CreateObject();
    cJSON *jsonArray = cJSON_AddArrayToObject(json, "files");

    while (true)
    {
        FsDirEntry entry;

        if (fsReadDir(dir, &entry) != NO_ERROR)
        {
            fsCloseDir(dir);
            break;
        }

        if (!osStrcmp(entry.name, "."))
        {
            continue;
        }
        if (!osStrcmp(entry.name, "..") && path[0] == '\0')
        {
            continue;
        }
        bool isDir = (entry.attributes & FS_FILE_ATTR_DIRECTORY);
        char *filePathAbsolute = custom_asprintf("%s%c%s", pathAbsolute, PATH_SEPARATOR, entry.name);
        pathSafeCanonicalize(filePathAbsolute);

        cJSON *jsonEntry = cJSON_CreateObject();
        cJSON_AddStringToObject(jsonEntry, "name", entry.name);
        cJSON_AddNumberToObject(jsonEntry, "date", convertDateToUnixTime(&entry.modified));
        cJSON_AddNumberToObject(jsonEntry, "size", entry.size);
        cJSON_AddBoolToObject(jsonEntry, "isDir", isDir);

        tonie_info_t *tafInfo = getTonieInfo(filePathAbsolute, false, client_ctx->settings);
        toniesJson_item_t *item = NULL;
        if (tafInfo->valid)
        {
            cJSON *tafHeaderEntry = cJSON_AddObjectToObject(jsonEntry, "tafHeader");
            cJSON_AddNumberToObject(tafHeaderEntry, "audioId", tafInfo->tafHeader->audio_id);
            char sha1Hash[41];
            sha1Hash[0] = '\0';
            for (int pos = 0; pos < tafInfo->tafHeader->sha1_hash.len; pos++)
            {
                char tmp[3];
                osSprintf(tmp, "%02x", tafInfo->tafHeader->sha1_hash.data[pos]);
                osStrcat(sha1Hash, tmp);
            }
            cJSON_AddStringToObject(tafHeaderEntry, "sha1Hash", sha1Hash);
            cJSON_AddNumberToObject(tafHeaderEntry, "size", tafInfo->tafHeader->num_bytes);
            cJSON_AddBoolToObject(tafHeaderEntry, "valid", tafInfo->valid);
            cJSON *tracksArray = cJSON_AddArrayToObject(tafHeaderEntry, "trackSeconds");
            for (size_t i = 0; i < tafInfo->additional.track_positions.count; i++)
            {
                cJSON_AddItemToArray(tracksArray, cJSON_CreateNumber(tafInfo->additional.track_positions.pos[i]));
            }

            item = tonies_byAudioIdHashModel(tafInfo->tafHeader->audio_id, tafInfo->tafHeader->sha1_hash.data, tafInfo->json.tonie_model);
        }
        else
        {
            char *json_extension = NULL;
            if (isDir)
            {
                char *filePathAbsoluteSub = NULL;
                FsDir *subdir = fsOpenDir(filePathAbsolute);
                FsDirEntry subentry;
                if (subdir != NULL)
                {
                    while (true)
                    {
                        if (fsReadDir(subdir, &subentry) != NO_ERROR || item != NULL)
                        {
                            fsCloseDir(subdir);
                            break;
                        }
                        filePathAbsoluteSub = custom_asprintf("%s%c%s", filePathAbsolute, PATH_SEPARATOR, subentry.name);

                        json_extension = osStrstr(filePathAbsoluteSub, ".json");
                        if (json_extension != NULL)
                        {
                            *json_extension = '\0';
                        }

                        contentJson_t contentJson = {0};
                        load_content_json(filePathAbsoluteSub, &contentJson, false, client_ctx->settings);
                        item = tonies_byModel(contentJson.tonie_model);
                        osFreeMem(filePathAbsoluteSub);
                        free_content_json(&contentJson);
                    }
                }
            }
            else
            {
                json_extension = osStrstr(filePathAbsolute, ".json");
                if (json_extension != NULL)
                {
                    *json_extension = '\0';
                }
                contentJson_t contentJson = {0};
                load_content_json(filePathAbsolute, &contentJson, false, client_ctx->settings);
                item = tonies_byModel(contentJson.tonie_model);

                cJSON_AddBoolToObject(jsonEntry, "hide", contentJson.hide);
                if (contentJson._has_cloud_auth)
                {
                    cJSON_AddBoolToObject(jsonEntry, "has_cloud_auth", true);
                }
                free_content_json(&contentJson);
            }
        }
        if (item != NULL)
        {
            addToniesJsonInfoJson(item, NULL, jsonEntry);
        }
        freeTonieInfo(tafInfo);

        osFreeMem(filePathAbsolute);
        cJSON_AddItemToArray(jsonArray, jsonEntry);
    }

    osFreeMem(pathAbsolute);
    char *jsonString = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);

    httpInitResponseHeader(connection);
    connection->response.contentType = "text/json";
    connection->response.contentLength = osStrlen(jsonString);

    return httpWriteResponse(connection, jsonString, connection->response.contentLength, true);
}
error_t handleApiFileIndex(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx)
{
    char *jsonString = strdup("{\"files\":[]}"); // Make warning go away

    do
    {
        char overlay[16];
        const char *rootPath = NULL;

        if (queryPrepare(queryString, &rootPath, overlay, sizeof(overlay), &client_ctx->settings) != NO_ERROR)
        {
            osFreeMem(jsonString);
            return ERROR_FAILURE;
        }

        char path[128];

        if (!queryGet(queryString, "path", path, sizeof(path)))
        {
            osStrcpy(path, "/");
        }

        /* first canonicalize path, then merge to prevent directory traversal bugs */
        pathSafeCanonicalize(path);
        char *pathAbsolute = custom_asprintf("%s%c%s", rootPath, PATH_SEPARATOR, path);
        pathSafeCanonicalize(pathAbsolute);

        int pos = 0;
        FsDir *dir = fsOpenDir(pathAbsolute);
        if (dir == NULL)
        {
            TRACE_ERROR("Failed to open dir '%s'\r\n", pathAbsolute);
            osFreeMem(pathAbsolute);
            break;
        }

        cJSON *json = cJSON_CreateObject();
        cJSON *jsonArray = cJSON_AddArrayToObject(json, "files");

        while (true)
        {
            FsDirEntry entry;

            if (fsReadDir(dir, &entry) != NO_ERROR)
            {
                fsCloseDir(dir);
                break;
            }

            if (!osStrcmp(entry.name, ".") || !osStrcmp(entry.name, ".."))
            {
                continue;
            }
            bool isDir = (entry.attributes & FS_FILE_ATTR_DIRECTORY);
            char dateString[64];

            osSnprintf(dateString, sizeof(dateString), " %04" PRIu16 "-%02" PRIu8 "-%02" PRIu8 ",  %02" PRIu8 ":%02" PRIu8 ":%02" PRIu8,
                       entry.modified.year, entry.modified.month, entry.modified.day,
                       entry.modified.hours, entry.modified.minutes, entry.modified.seconds);

            char *filePathAbsolute = custom_asprintf("%s%c%s", pathAbsolute, PATH_SEPARATOR, entry.name);
            pathSafeCanonicalize(filePathAbsolute);

            cJSON *jsonEntry = cJSON_CreateObject();
            cJSON_AddStringToObject(jsonEntry, "name", entry.name);
            cJSON_AddStringToObject(jsonEntry, "date", dateString);
            cJSON_AddNumberToObject(jsonEntry, "size", entry.size);
            cJSON_AddBoolToObject(jsonEntry, "isDirectory", isDir);

            char desc[3 + 1 + 8 + 1 + 40 + 1 + 64 + 1 + 64];
            desc[0] = 0;
            tonie_info_t *tafInfo = getTonieInfo(filePathAbsolute, false, client_ctx->settings);
            toniesJson_item_t *item = NULL;
            if (tafInfo->valid)
            {
                osSnprintf(desc, sizeof(desc), "TAF:%08X:", tafInfo->tafHeader->audio_id);
                for (int hash_pos = 0; hash_pos < tafInfo->tafHeader->sha1_hash.len; hash_pos++)
                {
                    char tmp[3];
                    osSprintf(tmp, "%02x", tafInfo->tafHeader->sha1_hash.data[hash_pos]);
                    osStrcat(desc, tmp);
                }
                char extraDesc[1 + 64 + 1 + 64];
                osSnprintf(extraDesc, sizeof(extraDesc), ":%" PRIu64 ":%" PRIuSIZE, tafInfo->tafHeader->num_bytes, tafInfo->tafHeader->n_track_page_nums);
                osStrcat(desc, extraDesc);

                item = tonies_byAudioIdHashModel(tafInfo->tafHeader->audio_id, tafInfo->tafHeader->sha1_hash.data, tafInfo->json.tonie_model);
            }
            else
            {
                char *json_extension = NULL;
                if (isDir)
                {
                    char *filePathAbsoluteSub = NULL;
                    FsDir *subdir = fsOpenDir(filePathAbsolute);
                    FsDirEntry subentry;
                    if (subdir != NULL)
                    {
                        while (true)
                        {
                            if (fsReadDir(subdir, &subentry) != NO_ERROR || item != NULL)
                            {
                                fsCloseDir(subdir);
                                break;
                            }
                            filePathAbsoluteSub = custom_asprintf("%s%c%s", filePathAbsolute, PATH_SEPARATOR, subentry.name);

                            json_extension = osStrstr(filePathAbsoluteSub, ".json");
                            if (json_extension != NULL)
                            {
                                *json_extension = '\0';
                            }

                            contentJson_t contentJson = {0};
                            load_content_json(filePathAbsoluteSub, &contentJson, false, client_ctx->settings);
                            item = tonies_byModel(contentJson.tonie_model);
                            osFreeMem(filePathAbsoluteSub);
                            free_content_json(&contentJson);
                        }
                    }
                }
                else
                {
                    json_extension = osStrstr(filePathAbsolute, ".json");
                    if (json_extension != NULL)
                    {
                        *json_extension = '\0';
                    }
                    contentJson_t contentJson = {0};
                    load_content_json(filePathAbsolute, &contentJson, false, client_ctx->settings);
                    item = tonies_byModel(contentJson.tonie_model);

                    if (contentJson._has_cloud_auth)
                    {
                        cJSON_AddBoolToObject(jsonEntry, "has_cloud_auth", true);
                    }
                    free_content_json(&contentJson);
                }
            }
            if (item != NULL)
            {
                addToniesJsonInfoJson(item, NULL, jsonEntry);
            }

            freeTonieInfo(tafInfo);
            osFreeMem(filePathAbsolute);
            cJSON_AddStringToObject(jsonEntry, "desc", desc);

            cJSON_AddItemToArray(jsonArray, jsonEntry);

            pos++;
        }

        osFreeMem(pathAbsolute);
        osFreeMem(jsonString);
        jsonString = cJSON_PrintUnformatted(json);
        cJSON_Delete(json);
    } while (0);

    httpInitResponseHeader(connection);
    connection->response.contentType = "text/json";
    connection->response.contentLength = osStrlen(jsonString);

    return httpWriteResponse(connection, jsonString, connection->response.contentLength, true);
}

error_t handleApiStats(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx)
{
    cJSON *json = cJSON_CreateObject();
    cJSON *jsonArray = cJSON_AddArrayToObject(json, "stats");
    int pos = 0;

    while (true)
    {
        stat_t *stat = stats_get(pos);

        if (!stat)
        {
            break;
        }
        cJSON *jsonEntry = cJSON_CreateObject();
        cJSON_AddStringToObject(jsonEntry, "ID", stat->name);
        cJSON_AddStringToObject(jsonEntry, "description", stat->description);
        cJSON_AddNumberToObject(jsonEntry, "value", stat->value);
        cJSON_AddItemToArray(jsonArray, jsonEntry);

        pos++;
    }

    char *jsonString = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);

    httpInitResponseHeader(connection);
    connection->response.contentType = "text/json";
    connection->response.contentLength = osStrlen(jsonString);

    return httpWriteResponse(connection, jsonString, connection->response.contentLength, true);
}

error_t file_save_start(void *in_ctx, const char *name, const char *filename)
{
    file_save_ctx *ctx = (file_save_ctx *)in_ctx;

    if (strchr(filename, '\\') || strchr(filename, '/'))
    {
        TRACE_ERROR("Filename '%s' contains directory separators!\r\n", filename);
        return ERROR_DIRECTORY_NOT_FOUND;
    }

    /* first canonicalize path, then merge to prevent directory traversal bugs */
    ctx->filename = custom_asprintf("%s%c%s", ctx->root_path, PATH_SEPARATOR, filename);
    sanitizePath(ctx->filename, false);

    if (fsFileExists(ctx->filename))
    {
        TRACE_INFO("Filename '%s' already exists, overwriting\r\n", ctx->filename);
    }
    else
    {
        TRACE_INFO("Writing to '%s'\r\n", ctx->filename);
    }

    ctx->file = fsOpenFile(ctx->filename, FS_FILE_MODE_WRITE | FS_FILE_MODE_CREATE | FS_FILE_MODE_TRUNC);

    if (ctx->file == NULL)
    {
        return ERROR_FILE_OPENING_FAILED;
    }

    return NO_ERROR;
}

error_t file_save_add(void *in_ctx, void *data, size_t length)
{
    file_save_ctx *ctx = (file_save_ctx *)in_ctx;

    if (!ctx->file)
    {
        return ERROR_FAILURE;
    }

    if (fsWriteFile(ctx->file, data, length) != NO_ERROR)
    {
        return ERROR_FAILURE;
    }

    return NO_ERROR;
}

error_t file_save_end(void *in_ctx)
{
    file_save_ctx *ctx = (file_save_ctx *)in_ctx;

    if (!ctx->file)
    {
        return ERROR_FAILURE;
    }
    fsCloseFile(ctx->file);
    osFreeMem(ctx->filename);
    ctx->file = NULL;

    return NO_ERROR;
}

error_t file_save_end_cert(void *in_ctx)
{
    file_save_ctx *ctx = (file_save_ctx *)in_ctx;

    if (!ctx->file)
    {
        return ERROR_FAILURE;
    }
    fsCloseFile(ctx->file);
    ctx->file = NULL;

    /* file was uploaded, this is the cert-specific handler */
    if (!osStrcasecmp(ctx->filename, "ca.der"))
    {
        TRACE_INFO("Set ca.der to %s\r\n", ctx->filename);
        settings_set_string_ovl("core.client_cert.file.ca", ctx->filename, ctx->overlay);
    }
    else if (!osStrcasecmp(ctx->filename, "client.der"))
    {
        TRACE_INFO("Set client.der to %s\r\n", ctx->filename);
        settings_set_string_ovl("core.client_cert.file.crt", ctx->filename, ctx->overlay);
    }
    else if (!osStrcasecmp(ctx->filename, "private.der"))
    {
        TRACE_INFO("Set private.der to %s\r\n", ctx->filename);
        settings_set_string_ovl("core.client_cert.file.key", ctx->filename, ctx->overlay);
    }
    else
    {
        TRACE_INFO("Unknown file type %s\r\n", ctx->filename);
    }

    return NO_ERROR;
}

error_t handleApiUploadCert(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx)
{
    uint_t statusCode = 500;
    char message[128] = {0};
    char overlay[16] = {0};

    if (queryGet(queryString, "overlay", overlay, sizeof(overlay)))
    {
        TRACE_DEBUG("got overlay '%s'\r\n", overlay);
    }
    const char *rootPath = settings_get_string_ovl("internal.certdirfull", overlay);

    if (rootPath == NULL)
    {
        statusCode = 500;
        osSnprintf(message, sizeof(message), "internal.certdirfull not set to a valid path");
        TRACE_ERROR("internal.certdirfull not set to a valid path\r\n");
    }
    else if (!fsDirExists(rootPath))
    {
        error_t error = fsCreateDirEx(rootPath, true);
        if (error != NO_ERROR || !fsDirExists(rootPath))
        {
            osSnprintf(message, sizeof(message), "internal.certdirfull '%s' does not exist and could not be created. Error: %s", rootPath, error2text(error));
            TRACE_ERROR("internal.certdirfull '%s' does not exist and could not be created. Error: %s\r\n", rootPath, error2text(error));
        }
    }
    else
    {
        multipart_cbr_t cbr;
        file_save_ctx ctx;

        osMemset(&cbr, 0x00, sizeof(cbr));
        osMemset(&ctx, 0x00, sizeof(ctx));

        cbr.multipart_start = &file_save_start;
        cbr.multipart_add = &file_save_add;
        cbr.multipart_end = &file_save_end_cert;

        ctx.root_path = rootPath;
        ctx.overlay = overlay;

        switch (multipart_handle(connection, &cbr, &ctx))
        {
        case NO_ERROR:
            statusCode = 200;
            osSnprintf(message, sizeof(message), "OK");
            break;
        default:
            statusCode = 500;
            break;
        }
    }

    httpPrepareHeader(connection, "text/plain; charset=utf-8", osStrlen(message));
    connection->response.statusCode = statusCode;

    return httpWriteResponseString(connection, message, false);
}

error_t file_save_start_suffix(void *in_ctx, const char *name, const char *filename)
{
    file_save_ctx *ctx = (file_save_ctx *)in_ctx;

    if (strchr(filename, '\\') || strchr(filename, '/'))
    {
        TRACE_ERROR("Filename '%s' contains directory separators!\r\n", filename);
        return ERROR_DIRECTORY_NOT_FOUND;
    }

    /* first canonicalize path, then merge to prevent directory traversal bugs */
    for (int suffix = 0; suffix < 100; suffix++)
    {
        if (suffix)
        {
            ctx->filename = custom_asprintf("%s/%s_%d.bin", ctx->root_path, filename, suffix);
        }
        else
        {
            ctx->filename = custom_asprintf("%s/%s.bin", ctx->root_path, filename);
        }
        sanitizePath(ctx->filename, false);

        if (fsFileExists(ctx->filename))
        {
            osFreeMem(ctx->filename);
            continue;
        }
        else
        {
            TRACE_INFO("Writing to '%s'\r\n", ctx->filename);
            break;
        }
    }

    ctx->file = fsOpenFile(ctx->filename, FS_FILE_MODE_WRITE | FS_FILE_MODE_CREATE | FS_FILE_MODE_TRUNC);

    if (ctx->file == NULL)
    {
        return ERROR_FILE_OPENING_FAILED;
    }

    return NO_ERROR;
}

error_t file_save_end_suffix(void *in_ctx)
{
    file_save_ctx *ctx = (file_save_ctx *)in_ctx;

    if (!ctx->file)
    {
        return ERROR_FAILURE;
    }
    fsCloseFile(ctx->file);
    ctx->file = NULL;

    return NO_ERROR;
}

error_t handleApiGetCaDer(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx)
{
    settings_t *settings = get_settings();
    char *ca_path = custom_asprintf("%s%c%s", settings->internal.basedirfull, PATH_SEPARATOR, settings->core.server_cert.file.ca_der);

    error_t err = httpSendResponseUnsafe(connection, uri, ca_path);
    osFreeMem(ca_path);
    return err;
}

error_t handleApiESP32UploadFirmware(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx)
{
    uint_t statusCode = 500;
    char message[128];
    char overlay[16];

    const char *rootPath = get_settings()->internal.firmwaredirfull;

    if (rootPath == NULL || !fsDirExists(rootPath))
    {
        statusCode = 500;
        osSnprintf(message, sizeof(message), "core.firmwaredir not set to a valid path: '%s'", rootPath);
        TRACE_ERROR("%s\r\n", message);
    }
    else
    {
        multipart_cbr_t cbr;
        file_save_ctx ctx;

        osMemset(&cbr, 0x00, sizeof(cbr));
        osMemset(&ctx, 0x00, sizeof(ctx));

        cbr.multipart_start = &file_save_start_suffix;
        cbr.multipart_add = &file_save_add;
        cbr.multipart_end = &file_save_end_suffix;

        ctx.root_path = rootPath;
        ctx.overlay = overlay;
        ctx.filename = NULL;

        switch (multipart_handle(connection, &cbr, &ctx))
        {
        case NO_ERROR:
            statusCode = 200;
            TRACE_INFO("Received new file:\r\n");
            TRACE_INFO("  '%s'\r\n", ctx.filename);
            osSnprintf(message, sizeof(message), "%s", &ctx.filename[strlen(ctx.root_path) + 1]);
            break;
        default:
            statusCode = 500;
            break;
        }

        osFreeMem(ctx.filename);
    }

    httpPrepareHeader(connection, "text/plain; charset=utf-8", osStrlen(message));
    connection->response.statusCode = statusCode;

    return httpWriteResponseString(connection, message, false);
}

bool_t move_cert_file(const char *type, char *from, char *to, char message[1024], uint_t *statusCode, bool overwrite)
{
    size_t message_size = 1024;
    error_t error = fsCompareFiles(from, to, NULL);
    if (error == ERROR_FILE_NOT_FOUND || overwrite)
    {
        error = fsMoveFile(from, to, true);
        if (error != NO_ERROR)
        {
            osSnprintf(message, message_size, "Moving %s from %s to %s failed with error %s\r\n", type, from, to, error2text(error));
            return false;
        }
    }
    else if (error == ERROR_ABORTED)
    {
        *statusCode = 409; // Conflict
        osSnprintf(message, message_size, "Different %s already exists at %s\r\n", type, to);
        return false;
    }
    else if (error == NO_ERROR)
    {
        TRACE_INFO("Skipped identical %s", type);
    }
    return true;
}

error_t handleApiESP32ExtractCerts(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx)
{
    uint_t statusCode = 500;
    char message[1024];

    error_t error = NO_ERROR;
    const char *firmwareRootPath = get_settings()->internal.firmwaredirfull;
    const char *certRootPath = get_settings()->internal.certdirfull;
    char filename[255] = {0};
    char overwrite_s[16] = {0};
    char mac[13] = {0};

    if (!queryGet(queryString, "filename", filename, sizeof(filename)))
    {
        return ERROR_FAILURE;
    }

    bool overwrite = false;
    bool overwriteBase = false;
    if (queryGet(queryString, "overwrite", overwrite_s, sizeof(overwrite_s)))
    {
        if (overwrite_s[0] == 't')
        {
            overwrite = true;
        }
    }

    const char *sep = osStrchr(filename, '_');
    if (!sep || strlen(&sep[1]) < 12)
    {
        TRACE_ERROR("Invalid file pattern '%s'\r\n", filename);
        return ERROR_NOT_FOUND;
    }
    osStrncpy(mac, &sep[1], 12);
    mac[12] = 0;
    osStringToLower(mac);

    char *file_path = custom_asprintf("%s%c%s", firmwareRootPath, PATH_SEPARATOR, filename);
    char *target_dir = custom_asprintf("%s%c%s", certRootPath, PATH_SEPARATOR, mac);

    char *ca_file = custom_asprintf("%s%c%s", target_dir, PATH_SEPARATOR, "CA.DER");
    char *client_file = custom_asprintf("%s%c%s", target_dir, PATH_SEPARATOR, "CLIENT.DER");
    char *private_file = custom_asprintf("%s%c%s", target_dir, PATH_SEPARATOR, "PRIVATE.DER");

    char *ca_target_file = custom_asprintf("%s%c%s", target_dir, PATH_SEPARATOR, "ca.der");
    char *client_target_file = custom_asprintf("%s%c%s", target_dir, PATH_SEPARATOR, "client.der");
    char *private_target_file = custom_asprintf("%s%c%s", target_dir, PATH_SEPARATOR, "private.der");

    char *ca_global_file = custom_asprintf("%s%c%s", certRootPath, PATH_SEPARATOR, "ca.der");
    char *client_global_file = custom_asprintf("%s%c%s", certRootPath, PATH_SEPARATOR, "client.der");
    char *private_global_file = custom_asprintf("%s%c%s", certRootPath, PATH_SEPARATOR, "private.der");

    do
    {
        if (!fsDirExists(target_dir))
        {
            error = fsCreateDir(target_dir);
            if (error != NO_ERROR)
            {
                osSnprintf(message, sizeof(message), "Failed to create directory '%s' with error %s\r\n", target_dir, error2text(error));
                break;
            }
        }
        error = esp32_fat_extract(file_path, "CERT", target_dir);
        if (error != NO_ERROR)
        {
            osSnprintf(message, sizeof(message), "esp32_fat_extract failed with error %s\r\n", error2text(error));
            break;
        }

        if (!move_cert_file("CA", ca_file, ca_target_file, message, &statusCode, overwrite))
        {
            break;
        }
        if (!move_cert_file("CLIENT", client_file, client_target_file, message, &statusCode, overwrite))
        {
            break;
        }
        if (!move_cert_file("PRIVATE", private_file, private_target_file, message, &statusCode, overwrite))
        {
            break;
        }

        if (!move_cert_file("CA", ca_target_file, ca_global_file, message, &statusCode, overwriteBase))
        {
        }
        if (!move_cert_file("CLIENT", client_target_file, client_global_file, message, &statusCode, overwriteBase))
        {
        }
        if (!move_cert_file("PRIVATE", private_target_file, private_global_file, message, &statusCode, overwriteBase))
        {
        }

        osSnprintf(message, sizeof(message), "OK");
        statusCode = 200;
    } while (false);

    if (statusCode != 200)
    {
        TRACE_ERROR("%s\r\n", message);
    }

    fsDeleteFile(ca_file);
    fsDeleteFile(client_file);
    fsDeleteFile(private_file);

    osFreeMem(file_path);
    osFreeMem(target_dir);

    osFreeMem(ca_file);
    osFreeMem(client_file);
    osFreeMem(private_file);

    osFreeMem(ca_target_file);
    osFreeMem(client_target_file);
    osFreeMem(private_target_file);

    osFreeMem(ca_global_file);
    osFreeMem(client_global_file);
    osFreeMem(private_global_file);

    httpPrepareHeader(connection, "text/plain; charset=utf-8", osStrlen(message));
    connection->response.statusCode = statusCode;

    return httpWriteResponseString(connection, message, false);
}
error_t handleApiESP32PatchFirmware(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx)
{
    const char *rootPath = get_settings()->internal.firmwaredirfull;

    TRACE_INFO("Patch firmware\r\n");
    TRACE_DEBUG("Query: '%s'\r\n", queryString);

    bool generate_certs = false;
    bool inject_ca = true;
    char patch_host[32] = {0};
    char wifi_ssid[64] = {0};
    char wifi_pass[64] = {0};
    char filename[255] = {0};
    char mac[13] = {0};
    osStrcpy(patch_host, "");
    osStrcpy(filename, "");
    osStrcpy(mac, "");

    if (!queryGet(queryString, "filename", filename, sizeof(filename)))
    {
        return ERROR_FAILURE;
    }

    if (queryGet(queryString, "hostname", patch_host, sizeof(patch_host)))
    {
        TRACE_INFO("Patch hostnames '%s'\r\n", patch_host);
    }

    if (queryGet(queryString, "wifi_ssid", wifi_ssid, sizeof(wifi_ssid)))
    {
        TRACE_INFO("wifi ssid '%s'\r\n", wifi_ssid);
    }

    if (queryGet(queryString, "wifi_pass", wifi_pass, sizeof(wifi_pass)))
    {
        TRACE_INFO("wifi pass '%s'\r\n", wifi_pass);
    }

    const char *sep = osStrchr(filename, '_');
    if (!sep || strlen(&sep[1]) < 12)
    {
        TRACE_ERROR("Invalid file pattern '%s'\r\n", filename);
        return ERROR_NOT_FOUND;
    }
    osStrncpy(mac, &sep[1], 12);
    mac[12] = 0;

    char *file_path = custom_asprintf("%s%c%s", rootPath, PATH_SEPARATOR, filename);
    char *patched_path = custom_asprintf("%s%cpatched_%s.bin", rootPath, PATH_SEPARATOR, mac);

    TRACE_INFO("Request for '%s'\r\n", file_path);

    error_t error;
    size_t n;
    uint32_t length;
    FsFile *file;

    if (!fsFileExists(file_path))
    {
        TRACE_ERROR("File does not exist '%s'\r\n", file_path);
        return ERROR_NOT_FOUND;
    }
    fsCopyFile(file_path, patched_path, true);
    free(file_path);

    if (generate_certs)
    {
        if (esp32_inject_cert(rootPath, patched_path, mac) != NO_ERROR)
        {
            TRACE_ERROR("Failed to generate and inject certs\r\n");
            return ERROR_NOT_FOUND;
        }
    }

    if (inject_ca)
    {
        if (esp32_inject_ca(rootPath, patched_path, mac) != NO_ERROR)
        {
            TRACE_ERROR("Failed to generate and inject CA\r\n");
            return ERROR_NOT_FOUND;
        }
    }

    if (osStrlen(patch_host) > 0)
    {
        char *oldrtnl = "rtnl.bxcl.de";
        char *oldapi = "prod.de.tbs.toys";
        if (esp32_patch_host(patched_path, patch_host, oldrtnl, oldapi) != NO_ERROR)
        {
            TRACE_ERROR("Failed to patch hostnames\r\n");
            return ERROR_NOT_FOUND;
        }
    }

    if (osStrlen(wifi_ssid) > 0)
    {
        if (esp32_patch_wifi(patched_path, wifi_ssid, wifi_pass) != NO_ERROR)
        {
            TRACE_ERROR("Failed to patch WiFi credentials\r\n");
            return ERROR_NOT_FOUND;
        }
    }

    if (esp32_fixup(patched_path, true) != NO_ERROR)
    {
        TRACE_ERROR("Failed to fixup image\r\n");
        return ERROR_NOT_FOUND;
    }

    // Open the file for reading
    error = fsGetFileSize(patched_path, &length);
    if (error)
    {
        TRACE_ERROR("File does not exist '%s'\r\n", patched_path);
        return ERROR_NOT_FOUND;
    }

    file = fsOpenFile(patched_path, FS_FILE_MODE_READ);
    free(patched_path);

    // Failed to open the file?
    if (file == NULL)
    {
        return ERROR_NOT_FOUND;
    }

    connection->response.statusCode = 200;
    connection->response.contentLength = length;
    connection->response.contentType = "binary/octet-stream";
    connection->response.chunkedEncoding = FALSE;

    error = httpWriteHeader(connection);

    if (error)
    {
        fsCloseFile(file);
        return error;
    }

    while (length > 0)
    {
        n = MIN(length, HTTP_SERVER_BUFFER_SIZE);

        error = fsReadFile(file, connection->buffer, n, &n);
        if (error)
        {
            break;
        }

        error = httpWriteStream(connection, connection->buffer, n);
        if (error)
        {
            break;
        }

        length -= n;
    }

    fsCloseFile(file);

    if (error == NO_ERROR || error == ERROR_END_OF_FILE)
    {
        if (length == 0)
        {
            error = httpFlushStream(connection);
        }
    }

    return error;
}

error_t handleApiFileUpload(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx)
{
    char overlay[16];
    char path[256 + 1];

    osStrcpy(path, "");

    const char *rootPath = NULL;

    if (queryPrepare(queryString, &rootPath, overlay, sizeof(overlay), &client_ctx->settings) != NO_ERROR)
    {
        return ERROR_FAILURE;
    }

    if (!queryGet(queryString, "path", path, sizeof(path)))
    {
        osStrcpy(path, "/");
    }

    /* first canonicalize path, then merge to prevent directory traversal bugs */
    sanitizePath(path, true);
    char *pathAbsolute = custom_asprintf("%s%c%s", rootPath, PATH_SEPARATOR, path);
    sanitizePath(pathAbsolute, true);

    uint_t statusCode = 500;
    char message[512];

    osSnprintf(message, sizeof(message), "OK");

    if (!fsDirExists(pathAbsolute))
    {
        statusCode = 500;
        osSnprintf(message, sizeof(message), "invalid path: '%.200s'", path);
        TRACE_ERROR("invalid path: '%s' -> '%s'\r\n", path, pathAbsolute);
    }
    else
    {
        multipart_cbr_t cbr;
        file_save_ctx ctx;

        osMemset(&cbr, 0x00, sizeof(cbr));
        osMemset(&ctx, 0x00, sizeof(ctx));

        cbr.multipart_start = &file_save_start;
        cbr.multipart_add = &file_save_add;
        cbr.multipart_end = &file_save_end;

        ctx.root_path = pathAbsolute;
        ctx.overlay = overlay;

        switch (multipart_handle(connection, &cbr, &ctx))
        {
        case NO_ERROR:
            statusCode = 200;
            osSnprintf(message, sizeof(message), "OK");
            break;
        default:
            statusCode = 500;
            break;
        }
    }

    osFreeMem(pathAbsolute);
    httpPrepareHeader(connection, "text/plain; charset=utf-8", osStrlen(message));
    connection->response.statusCode = statusCode;

    return httpWriteResponseString(connection, message, false);
}

error_t handleApiContent(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx)
{
    TRACE_DEBUG("Query: '%s'\r\n", queryString);

    char ogg[16];
    char overlay[16];
    osStrcpy(ogg, "");
    osStrcpy(overlay, "");

    const char *rootPath = NULL;

    if (!queryGet(queryString, "ogg", ogg, sizeof(ogg)))
    {
        strcpy(ogg, "false");
    }

    if (queryPrepare(queryString, &rootPath, overlay, sizeof(overlay), &client_ctx->settings) != NO_ERROR)
    {
        return ERROR_FAILURE;
    }

    bool skipFileHeader = !strcmp(ogg, "true");
    size_t startOffset = skipFileHeader ? 4096 : 0;

    char *file_path = custom_asprintf("%s%s", rootPath, &uri[8]);

    TRACE_DEBUG("Request for '%s', ogg: %s\r\n", file_path, ogg);

    error_t error;
    size_t n;
    uint32_t length;
    FsFile *file;

    // Retrieve the size of the specified file
    error = fsGetFileSize(file_path, &length);

    bool_t isStream = false;
    tonie_info_t *tafInfo = getTonieInfo(file_path, false, client_ctx->settings);

    if (tafInfo->valid && tafInfo->json._source_type == CT_SOURCE_STREAM)
    {
        isStream = true;
        length = client_ctx->settings->encode.stream_max_size;
        connection->response.noCache = true;
    }

    freeTonieInfo(tafInfo);

    if (error || length < startOffset)
    {
        TRACE_ERROR("File does not exist '%s'\r\n", file_path);
        return ERROR_NOT_FOUND;
    }

    /* in case of skipped headers, also reduce the file length */
    length -= startOffset;

    // Open the file for reading
    file = fsOpenFile(file_path, FS_FILE_MODE_READ);
    free(file_path);

    // Failed to open the file?
    if (file == NULL)
    {
        return ERROR_NOT_FOUND;
    }

    char *range_hdr = NULL;

    // Format HTTP response header
    // TODO add status 416 on invalid ranges
    if (!isStream && connection->request.Range.start > 0)
    {
        connection->request.Range.size = length;
        if (connection->request.Range.end >= connection->request.Range.size || connection->request.Range.end == 0)
        {
            connection->request.Range.end = connection->request.Range.size - 1;
        }

        range_hdr = custom_asprintf("bytes %" PRIu32 "-%" PRIu32 "/%" PRIu32, connection->request.Range.start, connection->request.Range.end, connection->request.Range.size);
        connection->response.contentRange = range_hdr;
        connection->response.statusCode = 206;
        connection->response.contentLength = connection->request.Range.end - connection->request.Range.start + 1;
        TRACE_DEBUG("Added response range %s\r\n", connection->response.contentRange);
    }
    else
    {
        connection->response.statusCode = 200;
        connection->response.contentLength = length;
    }
    connection->response.contentType = "audio/ogg";
    connection->response.chunkedEncoding = FALSE;

    error = httpWriteHeader(connection);

    if (range_hdr)
    {
        osFreeMem(range_hdr);
    }

    if (error)
    {
        fsCloseFile(file);
        return error;
    }

    if (!isStream && connection->request.Range.start > 0 && connection->request.Range.start < connection->request.Range.size)
    {
        TRACE_DEBUG("Seeking file to %" PRIu32 "\r\n", connection->request.Range.start);
        fsSeekFile(file, startOffset + connection->request.Range.start, FS_SEEK_SET);
    }
    else
    {
        TRACE_DEBUG("No seeking, sending from beginning\r\n");
        fsSeekFile(file, startOffset, FS_SEEK_SET);
    }

    // Send response body
    while (length > 0)
    {
        // Limit the number of bytes to read at a time
        n = MIN(length, HTTP_SERVER_BUFFER_SIZE);

        // Read data from the specified file
        error = fsReadFile(file, connection->buffer, n, &n);
        // End of input stream?
        if (isStream && error == ERROR_END_OF_FILE && connection->running)
        {
            osDelayTask(500);
            continue;
        }
        if (error)
            break;

        // Send data to the client
        error = httpWriteStream(connection, connection->buffer, n);
        // Any error to report?
        if (error)
            break;

        // Decrement the count of remaining bytes to be transferred
        length -= n;
    }

    // Close the file
    fsCloseFile(file);

    // Successful file transfer?
    if (error == NO_ERROR || error == ERROR_END_OF_FILE)
    {
        if (length == 0)
        {
            // Properly close the output stream
            error = httpFlushStream(connection);
        }
    }

    return error;
}
error_t handleApiContentDownload(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx)
{
    // TODO Rewrite URL
    // Use RUID/Azth from content.json
    // set connection->private.authentication_token
    // TODO Remove JSON suffix

    // http://dev11.lan/content/download/3D8C0F13/500304E0.json
    // http://dev11.lan/v2/content/3d8c0f13500304e0

    char overlay[16];
    const char *rootPath = NULL;

    if (queryPrepare(queryString, &rootPath, overlay, sizeof(overlay), &client_ctx->settings) != NO_ERROR)
    {
        return ERROR_FAILURE;
    }

    if (osStrlen(uri) < 34)
    {
        return NO_ERROR;
    }

    char *json_extension = osStrstr(uri, ".json");
    if (json_extension != NULL)
    {
        *json_extension = '\0';
    }

    char *path = (char *)uri + 1 + 7 + 1 + 8 + 1;
    char *pathAbsolute = custom_asprintf("%s%c%s", rootPath, PATH_SEPARATOR, path);

    char ruid[17];

    contentJson_t contentJson;
    load_content_json(pathAbsolute, &contentJson, false, client_ctx->settings);
    osFreeMem(pathAbsolute);

    osStrncpy(ruid, path, 8);
    osStrncpy(ruid + 8, path + 9, 8);
    for (uint8_t i = 0; i < 16; i++)
    {
        ruid[i] = osTolower(ruid[i]);
    }
    ruid[16] = '\0';

    bool isSys = (ruid[0] == '0' && ruid[1] == '0' && ruid[2] == '0' && ruid[3] == '0' && ruid[4] == '0' && ruid[5] == '0' && ruid[6] == '0');

    error_t error = NO_ERROR;
    if (isSys || contentJson.nocloud)
    {
        osSprintf((char *)uri, "/v1/content/%s", ruid);
        error = handleCloudContent(connection, uri, queryString, client_ctx, true);
    }
    else
    {
        osMemcpy(connection->private.authentication_token, contentJson.cloud_auth, contentJson.cloud_auth_len);
        osSprintf((char *)uri, "/v2/content/%s", ruid);
        error = handleCloudContent(connection, uri, queryString, client_ctx, false);
    }

    free_content_json(&contentJson);
    return error;
}

typedef struct
{
    const char *overlay;
    const char *file_path;
    toniefile_t *taf;
    uint8_t remainder[4];
    int remainder_avail;
    uint32_t audio_id;
} taf_encode_ctx;

error_t taf_encode_start(void *in_ctx, const char *name, const char *filename)
{
    taf_encode_ctx *ctx = (taf_encode_ctx *)in_ctx;

    if (!ctx->taf)
    {
        TRACE_INFO("[TAF] Start encoding to %s\r\n", ctx->file_path);
        TRACE_INFO("[TAF]   first file: %s\r\n", name);

        ctx->taf = toniefile_create(ctx->file_path, ctx->audio_id, false, 0);

        if (ctx->taf == NULL)
        {
            TRACE_INFO("[TAF]   Creating TAF failed\r\n");
            return ERROR_FILE_OPENING_FAILED;
        }
    }
    else
    {
        TRACE_INFO("[TAF]   new chapter for %s\r\n", name);
        toniefile_new_chapter(ctx->taf);
    }

    return NO_ERROR;
}

error_t taf_encode_add(void *in_ctx, void *data, size_t length)
{
    taf_encode_ctx *ctx = (taf_encode_ctx *)in_ctx;
    uint8_t *byte_data = (uint8_t *)data;
    size_t byte_data_start = 0;
    size_t byte_data_length = length;

    /* we have to take into account that the packets are not 4 byte aligned */
    if (ctx->remainder_avail)
    {
        /* there a a few bytes, so first fill the buffer */
        int size = 4 - ctx->remainder_avail;
        if (size > length)
        {
            size = length;
        }
        osMemcpy(&ctx->remainder[ctx->remainder_avail], byte_data, size);

        byte_data_start += size;
        byte_data_length -= size;
        ctx->remainder_avail += size;
    }

    /* either we have a full buffer now or no more data */
    if (ctx->remainder_avail == 4)
    {
        toniefile_encode(ctx->taf, (int16_t *)ctx->remainder, 1);
        ctx->remainder_avail = 0;
    }

    int samples = byte_data_length / 4;
    int remain = byte_data_length % 4;

    if (samples)
    {
        toniefile_encode(ctx->taf, (int16_t *)&byte_data[byte_data_start], samples);
    }

    if (remain)
    {
        osMemcpy(ctx->remainder, &byte_data[byte_data_start + samples * 4], remain);
        ctx->remainder_avail = remain;
    }

    return NO_ERROR;
}

error_t taf_encode_end(void *in_ctx)
{
    TRACE_INFO("[TAF]   end of file\r\n");
    return NO_ERROR;
}

error_t handleApiEncodeFile(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx)
{
    char overlay[16];
    const char *rootPath = NULL;

    if (queryPrepare(queryString, &rootPath, overlay, sizeof(overlay), &client_ctx->settings) != NO_ERROR)
    {
        return ERROR_FAILURE;
    }

    char_t post_data[BODY_BUFFER_SIZE];
    error_t error = parsePostData(connection, post_data, POST_BUFFER_SIZE);
    if (error != NO_ERROR)
    {
        TRACE_ERROR("parsePostData failed with error %s\r\n", error2text(error));
        return error;
    }

    char multisource[99][PATH_LEN];
    uint8_t multisource_size = 0;
    char source[PATH_LEN];
    char target[PATH_LEN];

    if (!queryGet(post_data, "target", target, sizeof(target)))
    {
        TRACE_ERROR("target missing!\r\n");
        return ERROR_INVALID_REQUEST;
    }
    sanitizePath(target, false);
    char *targetAbsolute = custom_asprintf("%s%c%s", rootPath, PATH_SEPARATOR, target);
    sanitizePath(targetAbsolute, false);

    char_t message[256];
    uint_t statusCode = 200;

    if (fsFileExists(targetAbsolute))
    {
        TRACE_ERROR("File %s already exists!\r\n", targetAbsolute);
        osSnprintf(message, sizeof(message), "File %s already exists!\r\n", targetAbsolute);
        statusCode = 500;
    }
    else
    {
        while (queryGetMulti(post_data, "source", source, sizeof(source), multisource_size))
        {
            sanitizePath(source, false);
            osSprintf(multisource[multisource_size], "%s%c%s", rootPath, PATH_SEPARATOR, source);
            sanitizePath(multisource[multisource_size], false);
            // TRACE_INFO("Source %s\r\n", multisource[multisource_size]);
            if (!fsFileExists(multisource[multisource_size]))
            {
                TRACE_ERROR("Source %s does not exist!\r\n", multisource[multisource_size]);
                osFreeMem(targetAbsolute);
                return ERROR_INVALID_REQUEST;
            }
            multisource_size++;
        }
        if (multisource_size == 0)
        {
            TRACE_ERROR("Source missing!\r\n");
            osFreeMem(targetAbsolute);
            return ERROR_INVALID_REQUEST;
        }

        TRACE_INFO("Encode %" PRIu8 " files to %s\r\n", multisource_size, targetAbsolute);
        size_t current_source = 0;
        error = ffmpeg_convert(multisource, multisource_size, &current_source, targetAbsolute, 0);
        osFreeMem(targetAbsolute);
        if (error != NO_ERROR)
        {
            TRACE_ERROR("ffmpeg_convert failed with error %s\r\n", error2text(error));
            statusCode = 500;
            osSnprintf(message, sizeof(message), "ffmpeg_convert failed with error %s\r\n", error2text(error));
        }
        else
        {
            osSnprintf(message, sizeof(message), "OK\r\n");
        }
    }

    httpPrepareHeader(connection, "text/plain; charset=utf-8", osStrlen(message));
    connection->response.statusCode = statusCode;

    return httpWriteResponseString(connection, message, false);
    return error;
}
error_t handleApiPcmUpload(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx)
{
    char overlay[16];
    char name[256];
    char uid[32];
    char path[256 + 1];
    char audio_id_str[128];
    uint32_t audio_id = 0;

    osStrcpy(name, "unnamed");
    osStrcpy(uid, "");
    osStrcpy(path, "");
    osStrcpy(audio_id_str, "");

    const char *rootPath = NULL;

    if (queryPrepare(queryString, &rootPath, overlay, sizeof(overlay), &client_ctx->settings) != NO_ERROR)
    {
        return ERROR_FAILURE;
    }

    if (queryGet(queryString, "name", name, sizeof(name)))
    {
        TRACE_INFO("got name '%s'\r\n", name);
    }
    if (queryGet(queryString, "uid", uid, sizeof(uid)))
    {
        TRACE_INFO("got uid '%s'\r\n", uid);
    }
    if (queryGet(queryString, "audioId", audio_id_str, sizeof(audio_id_str)))
    {
        TRACE_INFO("got audioId '%s'\r\n", audio_id_str);
        audio_id = atol(audio_id_str);
    }
    if (!queryGet(queryString, "path", path, sizeof(path)))
    {
        osStrcpy(path, "/");
    }

    sanitizePath(name, false);

    /* first canonicalize path, then merge to prevent directory traversal bugs */
    sanitizePath(path, true);
    char *pathAbsolute = custom_asprintf("%s%c%s", rootPath, PATH_SEPARATOR, path);
    sanitizePath(pathAbsolute, true);

    uint_t statusCode = 500;
    char message[512];
    osSnprintf(message, sizeof(message), "OK");

    if (!fsDirExists(pathAbsolute))
    {
        statusCode = 500;
        osSnprintf(message, sizeof(message), "invalid path: '%.200s'", path);
        TRACE_ERROR("invalid path: '%s' -> '%s'\r\n", path, pathAbsolute);
    }
    else
    {
        char *filename = custom_asprintf("%s%c%s", pathAbsolute, PATH_SEPARATOR, name);

        if (!filename)
        {
            TRACE_ERROR("Failed to build filename\r\n");
            return ERROR_FAILURE;
        }
        sanitizePath(filename, false);

        if (fsFileExists(filename))
        {
            TRACE_INFO("Filename '%s' already exists, overwriting\r\n", filename);
        }

        multipart_cbr_t cbr;
        taf_encode_ctx ctx;

        osMemset(&cbr, 0x00, sizeof(cbr));
        osMemset(&ctx, 0x00, sizeof(ctx));

        cbr.multipart_start = &taf_encode_start;
        cbr.multipart_add = &taf_encode_add;
        cbr.multipart_end = &taf_encode_end;

        ctx.file_path = filename;
        ctx.overlay = overlay;
        ctx.audio_id = audio_id;

        switch (multipart_handle(connection, &cbr, &ctx))
        {
        case NO_ERROR:
            statusCode = 200;
            osSnprintf(message, sizeof(message), "OK");
            break;
        default:
            statusCode = 500;
            break;
        }

        if (ctx.taf)
        {
            TRACE_INFO("[TAF] Ended encoding\r\n");
            toniefile_close(ctx.taf);
        }
        osFreeMem(filename);
    }

    osFreeMem(pathAbsolute);

    httpPrepareHeader(connection, "text/plain; charset=utf-8", osStrlen(message));
    connection->response.statusCode = statusCode;

    return httpWriteResponseString(connection, message, false);
}

error_t handleApiDirectoryCreate(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx)
{
    char overlay[16];
    const char *rootPath = NULL;

    if (queryPrepare(queryString, &rootPath, overlay, sizeof(overlay), &client_ctx->settings) != NO_ERROR)
    {
        return ERROR_FAILURE;
    }

    char path[256 + 3];
    size_t size = 0;

    error_t error = httpReceive(connection, &path, sizeof(path) - 3, &size, 0x00);
    if (error != NO_ERROR)
    {
        TRACE_ERROR("httpReceive failed!\r\n");
        return error;
    }
    path[size] = 0;

    /* first canonicalize path, then merge to prevent directory traversal bugs */
    sanitizePath(path, true);
    char *pathAbsolute = custom_asprintf("%s%c%s", rootPath, PATH_SEPARATOR, path);
    sanitizePath(pathAbsolute, true);

    TRACE_INFO("Creating directory: '%s'\r\n", pathAbsolute);

    uint_t statusCode = 200;
    char message[256 + 64];

    osSnprintf(message, sizeof(message), "OK");

    error_t err = fsCreateDir(pathAbsolute);

    if (err != NO_ERROR)
    {
        statusCode = 500;
        osSnprintf(message, sizeof(message), "Error creating directory '%s', error %s", path, error2text(err));
        TRACE_ERROR("Error creating directory '%s' -> '%s', error %s\r\n", path, pathAbsolute, error2text(err));
    }
    httpPrepareHeader(connection, "text/plain; charset=utf-8", osStrlen(message));
    connection->response.statusCode = statusCode;

    osFreeMem(pathAbsolute);

    return httpWriteResponseString(connection, message, false);
}

error_t handleApiDirectoryDelete(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx)
{
    char overlay[16];
    const char *rootPath = NULL;

    if (queryPrepare(queryString, &rootPath, overlay, sizeof(overlay), &client_ctx->settings) != NO_ERROR)
    {
        return ERROR_FAILURE;
    }

    char path[256 + 3];
    size_t size = 0;

    error_t error = httpReceive(connection, &path, sizeof(path) - 3, &size, 0x00);
    if (error != NO_ERROR)
    {
        TRACE_ERROR("httpReceive failed!\r\n");
        return error;
    }
    path[size] = 0;

    /* first canonicalize path, then merge to prevent directory traversal bugs */
    sanitizePath(path, true);
    char *pathAbsolute = custom_asprintf("%s%c%s", rootPath, PATH_SEPARATOR, path);
    sanitizePath(pathAbsolute, true);

    TRACE_INFO("Deleting directory: '%s'\r\n", pathAbsolute);

    uint_t statusCode = 200;
    char message[256 + 64];

    osSnprintf(message, sizeof(message), "OK");

    error_t err = fsRemoveDir(pathAbsolute);

    if (err != NO_ERROR)
    {
        statusCode = 500;
        osSnprintf(message, sizeof(message), "Error deleting directory '%s', error %s", path, error2text(err));
        TRACE_ERROR("Error deleting directory '%s' -> '%s', error %s\r\n", path, pathAbsolute, error2text(err));
    }
    httpPrepareHeader(connection, "text/plain; charset=utf-8", osStrlen(message));
    connection->response.statusCode = statusCode;

    osFreeMem(pathAbsolute);

    return httpWriteResponseString(connection, message, false);
}

error_t handleApiFileDelete(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx)
{
    char overlay[16];
    const char *rootPath = NULL;

    if (queryPrepare(queryString, &rootPath, overlay, sizeof(overlay), &client_ctx->settings) != NO_ERROR)
    {
        return ERROR_FAILURE;
    }

    char path[256 + 3];
    size_t size = 0;

    error_t error = httpReceive(connection, &path, sizeof(path) - 3, &size, 0x00);
    if (error != NO_ERROR)
    {
        TRACE_ERROR("httpReceive failed!\r\n");
        return error;
    }
    path[size] = 0;

    /* first canonicalize path, then merge to prevent directory traversal bugs */
    sanitizePath(path, false);
    char *pathAbsolute = custom_asprintf("%s%c%s", rootPath, PATH_SEPARATOR, path);
    sanitizePath(pathAbsolute, false);

    TRACE_INFO("Deleting file: '%s'\r\n", path);

    uint_t statusCode = 200;
    char message[256 + 64];

    osSnprintf(message, sizeof(message), "OK");

    error_t err = fsDeleteFile(pathAbsolute);

    if (err != NO_ERROR)
    {
        statusCode = 500;
        osSnprintf(message, sizeof(message), "Error deleting file '%s', error %s", path, error2text(err));
        TRACE_ERROR("Error deleting file '%s' -> '%s', error %s\r\n", path, pathAbsolute, error2text(err));
    }
    httpPrepareHeader(connection, "text/plain; charset=utf-8", osStrlen(message));
    connection->response.statusCode = statusCode;

    osFreeMem(pathAbsolute);

    return httpWriteResponseString(connection, message, false);
}
error_t handleApiFileMove(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx)
{
    char overlay[16];
    const char *rootPath = NULL;

    if (queryPrepare(queryString, &rootPath, overlay, sizeof(overlay), &client_ctx->settings) != NO_ERROR)
    {
        return ERROR_FAILURE;
    }

    char_t post_data[POST_BUFFER_SIZE];
    error_t error = parsePostData(connection, post_data, POST_BUFFER_SIZE);
    if (error != NO_ERROR)
    {
        return error;
    }

    char source[256 + 3];
    char target[256 + 3];

    if (!queryGet(post_data, "source", source, sizeof(source)))
    {
        TRACE_ERROR("source missing!\r\n");
        return ERROR_INVALID_REQUEST;
    }
    if (!queryGet(post_data, "target", target, sizeof(target)))
    {
        TRACE_ERROR("target missing!\r\n");
        return ERROR_INVALID_REQUEST;
    }

    /* first canonicalize path, then merge to prevent directory traversal bugs */
    sanitizePath(source, false);
    char *sourceAbsolute = custom_asprintf("%s%c%s", rootPath, PATH_SEPARATOR, source);
    sanitizePath(sourceAbsolute, false);

    sanitizePath(target, false);
    char *targetAbsolute = custom_asprintf("%s%c%s", rootPath, PATH_SEPARATOR, target);
    sanitizePath(targetAbsolute, false);

    TRACE_INFO("Moving file: '%s' to '%s'\r\n", source, target);
    TRACE_INFO("Moving file: '%s' to '%s'\r\n", sourceAbsolute, targetAbsolute);

    uint_t statusCode = 200;
    char message[1024];

    osSnprintf(message, sizeof(message), "OK");

    error_t err = fsMoveFile(sourceAbsolute, targetAbsolute, false);

    if (err != NO_ERROR)
    {
        statusCode = 500;
        osSnprintf(message, sizeof(message), "Error moving file '%s' to '%s', error %s", source, target, error2text(err));
        TRACE_ERROR("Error moving file '%s' to '%s', error %s\r\n", sourceAbsolute, targetAbsolute, error2text(err));
    }
    httpPrepareHeader(connection, "text/plain; charset=utf-8", osStrlen(message));
    connection->response.statusCode = statusCode;

    osFreeMem(sourceAbsolute);
    osFreeMem(targetAbsolute);

    return httpWriteResponseString(connection, message, false);
}

error_t handleApiToniesJsonReload(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx)
{
    tonies_deinit();
    tonies_init();
    httpPrepareHeader(connection, "text/plain; charset=utf-8", 2);
    return httpWriteResponseString(connection, "OK", false);
}
error_t handleApiToniesJson(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx)
{
    char *tonies_path = custom_asprintf("%s%c%s", settings_get_string("internal.configdirfull"), PATH_SEPARATOR, TONIES_JSON_FILE);

    error_t err = httpSendResponseUnsafe(connection, uri, tonies_path);
    osFreeMem(tonies_path);
    return err;
}
error_t handleApiToniesJsonUpdate(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx)
{
    char *message = "Triggered tonies.json update";
    httpPrepareHeader(connection, "text/plain; charset=utf-8", osStrlen(message));
    httpWriteResponseString(connection, message, false);
    return tonies_update();
}

error_t handleApiToniesCustomJson(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx)
{
    char *tonies_custom_path = custom_asprintf("%s%c%s", settings_get_string("internal.configdirfull"), PATH_SEPARATOR, TONIES_CUSTOM_JSON_FILE);

    error_t err = httpSendResponseUnsafe(connection, uri, tonies_custom_path);
    osFreeMem(tonies_custom_path);
    return err;
}

static char *getJsonString(cJSON *jsonElement, char *name)
{
    cJSON *attr = cJSON_GetObjectItemCaseSensitive(jsonElement, name);
    if (cJSON_IsString(attr))
    {
        return strdup(attr->valuestring);
    }
    return strdup("");
}

error_t handleApiTonieboxJson(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx)
{
    char *path = custom_asprintf("%s%c%s", settings_get_string("internal.configdirfull"), PATH_SEPARATOR, TONIEBOX_JSON_FILE);

    size_t fileSize = 0;
    fsGetFileSize(path, (uint32_t *)(&fileSize));
    TRACE_INFO("Trying to read %s with size %zu\r\n", path, fileSize);

    FsFile *fsFile = fsOpenFile(path, FS_FILE_MODE_READ);
    if (fsFile == NULL)
    {
        httpWriteResponseString(connection, "Failed to open file", false);
        return ERROR_FAILURE;
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
    cJSON *inputJson = cJSON_ParseWithLengthOpts(data, fileSize, 0, 0);
    osFreeMem(data);

    /* Create a cJSON array to hold the output */
    cJSON *outputJson = cJSON_CreateArray();

    cJSON *tonieJson;
    cJSON_ArrayForEach(tonieJson, inputJson)
    {
        /* Create a cJSON object for the current item */
        cJSON *jsonObject = cJSON_CreateObject();
        if (jsonObject == NULL)
        {
            cJSON_Delete(outputJson);
            cJSON_Delete(inputJson);
            return ERROR_FAILURE;
        }

        /* Add "id" and "name" to the object */
        char *id = getJsonString(tonieJson, "id");
        char *name = getJsonString(tonieJson, "name");
        cJSON_AddStringToObject(jsonObject, "id", id);
        cJSON_AddStringToObject(jsonObject, "name", name);

        TRACE_DEBUG("Adding box '%s'\r\n", name);

        /* Handle "img_src" and caching logic */
        char *img_src = getJsonString(tonieJson, "img_src");
        if (osStrlen(img_src) > 0 && settings_get_bool("tonie_json.cache_images"))
        {
            cache_entry_t *cache = NULL;
            /* try to get existing cache entry */
            cache = cache_fetch_by_url(img_src);

            /* none existing, create one */
            if (!cache)
            {
                cache = cache_add(img_src);
            }

            /* if that failed as well, use external URL */
            if (cache)
            {
                osFreeMem(img_src);
                img_src = strdup(cache->cached_url);

                TRACE_DEBUG("Cache URL would be: '%s'\r\n", cache->cached_url);

                if (settings_get_bool("tonie_json.cache_preload"))
                {
                    /* try to download and cache the file */
                    cache_fetch_entry(cache);
                }
            }
        }
        cJSON_AddStringToObject(jsonObject, "img_src", img_src);

        /* Handle the crop array */
        cJSON *cropArray = cJSON_GetObjectItem(tonieJson, "crop");
        if (cropArray != NULL && cJSON_IsArray(cropArray))
        {
            cJSON *newCropArray = cJSON_CreateArray();
            if (newCropArray != NULL)
            {
                int cropCount = cJSON_GetArraySize(cropArray);
                for (int index = 0; index < 3; index++)
                {
                    if (index < cropCount)
                    {
                        cJSON *cropElem = cJSON_GetArrayItem(cropArray, index);
                        if (cJSON_IsNumber(cropElem))
                        {
                            cJSON_AddItemToArray(newCropArray, cJSON_CreateNumber(cropElem->valuedouble));
                        }
                        else
                        {
                            /* default value when parsing failed */
                            cJSON_AddItemToArray(newCropArray, cJSON_CreateNumber(0.0));
                        }
                    }
                    else
                    {
                        /* default value */
                        cJSON_AddItemToArray(newCropArray, cJSON_CreateNumber(0.0));
                    }
                }
                cJSON_AddItemToObject(jsonObject, "crop", newCropArray);
            }
        }

        /* Add the jsonObject to the output array */
        cJSON_AddItemToArray(outputJson, jsonObject);

        osFreeMem(id);
        osFreeMem(name);
        osFreeMem(img_src);
    }

    /* Convert the cJSON array to a JSON string */
    char *jsonString = cJSON_PrintUnformatted(outputJson);
    if (jsonString != NULL)
    {
        httpPrepareHeader(connection, "application/json; charset=utf-8", strlen(jsonString));
        httpWriteResponseString(connection, jsonString, false);
        osFreeMem(jsonString);
    }

    /* Clean up */
    cJSON_Delete(inputJson);
    cJSON_Delete(outputJson);

    return NO_ERROR;
}

error_t handleApiTonieboxCustomJson(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx)
{
    char *path = custom_asprintf("%s%c%s", settings_get_string("internal.configdirfull"), PATH_SEPARATOR, TONIEBOX_CUSTOM_JSON_FILE);

    if (!fsFileExists(path))
    {
        FsFile *file = fsOpenFile(path, FS_FILE_MODE_WRITE | FS_FILE_MODE_CREATE);
        fsWriteFile(file, "[]", 2);
        fsCloseFile(file);
    }

    error_t err = httpSendResponseUnsafe(connection, uri, path);
    osFreeMem(path);
    return err;
}

error_t handleApiToniesJsonSearch(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx)
{
    char searchModel[256];
    char searchSeries[256];
    char searchEpisode[256];
    searchModel[0] = '\0';
    searchSeries[0] = '\0';
    searchEpisode[0] = '\0';
    toniesJson_item_t *result[18];
    size_t result_size;

    queryGet(queryString, "searchModel", searchModel, sizeof(searchModel));
    queryGet(queryString, "searchSeries", searchSeries, sizeof(searchSeries));
    queryGet(queryString, "searchEpisode", searchEpisode, sizeof(searchEpisode));

    tonies_byModelSeriesEpisode(searchModel, searchSeries, searchEpisode, result, &result_size);

    cJSON *jsonArray = cJSON_CreateArray();
    for (size_t i = 0; i < result_size; i++)
    {
        addToniesJsonInfoJson(result[i], NULL, jsonArray);
    }

    char *jsonString = cJSON_PrintUnformatted(jsonArray);
    cJSON_Delete(jsonArray);
    httpInitResponseHeader(connection);
    connection->response.contentType = "text/json";
    connection->response.contentLength = osStrlen(jsonString);

    return httpWriteResponse(connection, jsonString, connection->response.contentLength, true);
}
error_t handleApiContentJson(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx)
{
    char overlay[16];
    const char *rootPath = NULL;

    if (queryPrepare(queryString, &rootPath, overlay, sizeof(overlay), &client_ctx->settings) != NO_ERROR)
    {
        return ERROR_FAILURE;
    }
    char *file_path = custom_asprintf("%s%s", rootPath, &uri[13]);
    return httpSendResponseUnsafe(connection, uri, file_path);
}

error_t handleApiContentJsonBase(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx, char **contentPath)
{
    char overlay[16];
    const char *rootPath = NULL;

    if (queryPrepare(queryString, &rootPath, overlay, sizeof(overlay), &client_ctx->settings) != NO_ERROR)
    {
        return ERROR_FAILURE;
    }
    // /content/json/get/3d8c0f13500304e0
    if (osStrlen(uri) != 34)
    {
        return ERROR_NOT_FOUND;
    }
    char ruid[17];
    osStrcpy(ruid, &uri[osStrlen(uri) - 16]);
    getContentPathFromCharRUID(ruid, contentPath, client_ctx->settings);

    return NO_ERROR;
}
error_t handleApiContentJsonGet(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx)
{
    char *contentPath;
    char *contentJsonPath;
    error_t error = handleApiContentJsonBase(connection, uri, queryString, client_ctx, &contentPath);
    if (error != NO_ERROR)
    {
        return error;
    }

    contentJsonPath = custom_asprintf("%s%s", contentPath, ".json");
    error = httpSendResponseUnsafe(connection, uri, contentJsonPath);
    osFreeMem(contentPath);
    osFreeMem(contentJsonPath);
    return error;
}

error_t handleApiContentJsonSet(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx)
{
    char *contentPath;
    error_t error = handleApiContentJsonBase(connection, uri, queryString, client_ctx, &contentPath);
    if (error != NO_ERROR)
    {
        return error;
    }

    char_t post_data[POST_BUFFER_SIZE];
    error = parsePostData(connection, post_data, POST_BUFFER_SIZE);
    if (error != NO_ERROR)
    {
        osFreeMem(contentPath);
        return error;
    }

    contentJson_t content_json;
    load_content_json(contentPath, &content_json, true, client_ctx->settings);

    char item_data[256];
    bool_t updated = false;
    if (queryGet(post_data, "source", item_data, sizeof(item_data)))
    {
        if (osStrcmp(item_data, content_json.source))
        {
            osFreeMem(content_json.source);
            content_json.source = strdup(item_data);
            updated = true;
        }
    }
    if (queryGet(post_data, "tonie_model", item_data, sizeof(item_data)))
    {
        if (osStrcmp(item_data, content_json.tonie_model))
        {
            osFreeMem(content_json.tonie_model);
            content_json.tonie_model = strdup(item_data);
            updated = true;
        }
    }
    if (queryGet(post_data, "live", item_data, sizeof(item_data)))
    {
        bool_t target_value = false;
        if (!osStrcmp(item_data, "true"))
        {
            target_value = true;
        }
        if (target_value != content_json.live)
        {
            content_json.live = target_value;
            updated = true;
        }
    }
    if (queryGet(post_data, "nocloud", item_data, sizeof(item_data)))
    {
        bool_t target_value = false;
        if (!osStrcmp(item_data, "true"))
        {
            target_value = true;
        }
        if (target_value != content_json.nocloud)
        {
            content_json.nocloud = target_value;
            updated = true;
        }
    }
    if (queryGet(post_data, "hide", item_data, sizeof(item_data)))
    {
        bool_t target_value = false;
        if (!osStrcmp(item_data, "true"))
        {
            target_value = true;
        }
        if (target_value != content_json.hide)
        {
            content_json.hide = target_value;
            updated = true;
        }
    }
    if (queryGet(post_data, "claimed", item_data, sizeof(item_data)))
    {
        bool_t target_value = false;
        if (!osStrcmp(item_data, "true"))
        {
            target_value = true;
        }
        if (target_value != content_json.claimed)
        {
            content_json.claimed = target_value;
            updated = true;
        }
    }

    if (updated)
    {
        char *json_path = custom_asprintf("%s.json", contentPath);
        error = save_content_json(json_path, &content_json);
        osFreeMem(json_path);
        if (error != NO_ERROR)
        {
            return error;
        }
        TRACE_INFO("Updated content json of %s\r\n", contentPath);
    }
    osFreeMem(contentPath);
    free_content_json(&content_json);

    return httpOkResponse(connection);
}

bool isHexString(const char *buf, size_t maxLen)
{
    bool isHex = true;

    for (size_t i = 0; i < osStrlen(buf) && i < maxLen; i++)
    {
        char letter = toupper(buf[i]);

        isHex &= (letter >= 'A' && letter <= 'F') || (letter >= '0' && letter <= '9');
    }

    return isHex;
}

error_t getTagInfoJson(char ruid[17], cJSON *jsonTarget, client_ctx_t *client_ctx)
{
    error_t error = NO_ERROR;
    /* build filename with 8 chars of the taf/json */
    char *tagPath = custom_asprintf("%.8s%c%.8s", &ruid[0], PATH_SEPARATOR, &ruid[8]);
    for (size_t i = 0; tagPath[i] != '\0'; i++)
    {
        tagPath[i] = toupper(tagPath[i]);
    }
    char *fullTagPath = custom_asprintf("%s%c%s", client_ctx->settings->internal.contentdirfull, PATH_SEPARATOR, tagPath);
    char *fullJsonPath = custom_asprintf("%s%c%s.json", client_ctx->settings->internal.contentdirfull, PATH_SEPARATOR, tagPath);

    if (fsFileExists(fullJsonPath))
    {
        contentJson_t contentJson;
        /* read TAF info - would create .json if not existing */
        // tonie_info_t *tafInfo = getTonieInfoFromRuid(ruid, true, client_ctx->settings);
        tonie_info_t *tafInfo = getTonieInfo(fullTagPath, false, client_ctx->settings);
        /* now update with updated model if found. */
        saveTonieInfo(tafInfo, true);
        contentJson = tafInfo->json;

        if (contentJson._valid)
        {
            /* only process one TAF/json per directory */
            cJSON *jsonEntry = cJSON_CreateObject();
            cJSON_AddStringToObject(jsonEntry, "ruid", ruid);

            char huid[24];
            for (size_t i = 0; i < 8; i++)
            {
                size_t hcharId = (i * 3);
                size_t rcharId = 16 - (i * 2) - 1;
                huid[hcharId + 2] = ':';
                huid[hcharId + 1] = toupper(ruid[rcharId]);
                huid[hcharId] = toupper(ruid[rcharId - 1]);
            }
            huid[23] = '\0';
            cJSON_AddStringToObject(jsonEntry, "uid", huid);

            bool isSys = !osStrncmp(ruid, "0000000", 7);
            if (isSys)
            {
                cJSON_AddStringToObject(jsonEntry, "type", "system");
            }
            else
            {
                cJSON_AddStringToObject(jsonEntry, "type", "tag");
            }
            cJSON_AddBoolToObject(jsonEntry, "valid", tafInfo->valid);
            cJSON_AddBoolToObject(jsonEntry, "exists", tafInfo->exists);
            cJSON_AddBoolToObject(jsonEntry, "live", tafInfo->json.live);
            cJSON_AddBoolToObject(jsonEntry, "nocloud", tafInfo->json.nocloud);
            cJSON_AddBoolToObject(jsonEntry, "hasCloudAuth", tafInfo->json._has_cloud_auth && !tafInfo->json.cloud_override);
            cJSON_AddBoolToObject(jsonEntry, "hide", tafInfo->json.hide);
            cJSON_AddBoolToObject(jsonEntry, "claimed", tafInfo->json.claimed);
            cJSON_AddStringToObject(jsonEntry, "source", tafInfo->json.source);

            cJSON *tracksArray = cJSON_AddArrayToObject(jsonEntry, "trackSeconds");
            for (size_t i = 0; i < tafInfo->additional.track_positions.count; i++)
            {
                cJSON_AddItemToArray(tracksArray, cJSON_CreateNumber(tafInfo->additional.track_positions.pos[i]));
            }

            char *downloadUrl = custom_asprintf("/content/download/%s?overlay=%s", tagPath, client_ctx->settings->internal.overlayUniqueId);
            char *audioUrl = custom_asprintf("%s&skip_header=true", downloadUrl);
            cJSON_AddStringToObject(jsonEntry, "audioUrl", audioUrl);
            if (!tafInfo->exists && !tafInfo->json.nocloud)
            {
                if (contentJson._has_cloud_auth || isSys)
                {
                    cJSON_AddStringToObject(jsonEntry, "downloadTriggerUrl", downloadUrl);
                }
                else
                {
                    cJSON_AddStringToObject(jsonEntry, "downloadTriggerUrl", "");
                }
            }
            osFreeMem(audioUrl);
            osFreeMem(downloadUrl);

            toniesJson_item_t *item = tonies_byModel(contentJson.tonie_model);
            addToniesJsonInfoJson(item, contentJson.tonie_model, jsonEntry);

            toniesJson_item_t *item2 = tonies_byModel(contentJson._source_model);
            if (tafInfo->exists && item != item2)
            {
                cJSON *jsonSourceInfo = cJSON_CreateObject();
                addToniesJsonInfoJson(item2, contentJson._source_model, jsonSourceInfo);
                cJSON *tonieInfoCopy = cJSON_DetachItemFromObject(jsonSourceInfo, "tonieInfo");
                cJSON_AddItemToObject(jsonEntry, "sourceInfo", tonieInfoCopy);
            }

            if (cJSON_IsArray(jsonTarget))
            {
                cJSON_AddItemToArray(jsonTarget, jsonEntry);
            }
            else
            {
                cJSON_AddItemToObject(jsonTarget, "tagInfo", jsonEntry);
            }
        }
        else
        {
            error = ERROR_NOT_FOUND;
        }
        freeTonieInfo(tafInfo);
    }
    else
    {
        error = ERROR_NOT_FOUND;
    }

    osFreeMem(tagPath);
    osFreeMem(fullTagPath);
    osFreeMem(fullJsonPath);

    return error;
}

error_t handleApiTagInfo(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx)
{
    char overlay[16];
    char ruid[17];
    const char *rootPath = NULL;

    if (queryPrepare(queryString, &rootPath, overlay, sizeof(overlay), &client_ctx->settings) != NO_ERROR)
    {
        return ERROR_FAILURE;
    }
    if (!queryGet(queryString, "ruid", ruid, sizeof(ruid)))
    {
        return ERROR_FAILURE;
    }

    cJSON *json = cJSON_CreateObject();

    error_t error = getTagInfoJson(ruid, json, client_ctx);
    if (error != NO_ERROR)
    {
        cJSON_Delete(json);
        return error;
    }

    char *jsonString = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);

    httpInitResponseHeader(connection);
    connection->response.contentType = "text/json";
    connection->response.contentLength = osStrlen(jsonString);

    return httpWriteResponse(connection, jsonString, connection->response.contentLength, true);
}
error_t handleApiTagIndex(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx)
{
    char overlay[16];
    const char *rootPath = NULL;

    if (queryPrepare(queryString, &rootPath, overlay, sizeof(overlay), &client_ctx->settings) != NO_ERROR)
    {
        return ERROR_FAILURE;
    }

    FsDir *dir = fsOpenDir(rootPath);
    if (dir == NULL)
    {
        TRACE_ERROR("Failed to open dir '%s'\r\n", rootPath);
        return ERROR_FAILURE;
    }

    cJSON *json = cJSON_CreateObject();
    cJSON *jsonArray = cJSON_AddArrayToObject(json, "tags");

    while (true)
    {
        FsDirEntry entry;
        if (fsReadDir(dir, &entry) != NO_ERROR)
        {
            fsCloseDir(dir);
            break;
        }

        if (!(entry.attributes & FS_FILE_ATTR_DIRECTORY))
        {
            continue;
        }
        if (osStrlen(entry.name) != 8)
        {
            continue;
        }

        if (!isHexString(entry.name, 8))
        {
            continue;
        }

        char ruid[17];
        osStrcpy(ruid, entry.name);

        char *subDirPath = custom_asprintf("%s/%s", rootPath, entry.name);
        FsDir *subDir = fsOpenDir(subDirPath);

        while (true)
        {
            FsDirEntry subEntry;
            if (fsReadDir(subDir, &subEntry) != NO_ERROR)
            {
                break;
            }

            /* do not process directories here */
            if ((subEntry.attributes & FS_FILE_ATTR_DIRECTORY))
            {
                continue;
            }
            /* filename must start with 8 hex characters */
            if (!isHexString(subEntry.name, 8))
            {
                continue;
            }

            /* fill rest of reverse UID */
            osStrncpy(&ruid[8], subEntry.name, 8);
            ruid[16] = '\0';
            for (size_t i = 0; ruid[i] != '\0'; i++)
            {
                ruid[i] = tolower(ruid[i]);
            }

            if (getTagInfoJson(ruid, jsonArray, client_ctx) == NO_ERROR)
            {
                break;
            }
        }
        fsCloseDir(subDir);
        osFreeMem(subDirPath);
    }

    char *jsonString = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);

    httpInitResponseHeader(connection);
    connection->response.contentType = "text/json";
    connection->response.contentLength = osStrlen(jsonString);

    return httpWriteResponse(connection, jsonString, connection->response.contentLength, true);
}

#define TEST_TOKEN "THIS_IS_A_TEST_TOKEN"

error_t handleApiAuthLogin(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx)
{
    char_t post_data[POST_BUFFER_SIZE];
    error_t error = parsePostData(connection, post_data, POST_BUFFER_SIZE);
    if (error != NO_ERROR)
    {
        return error;
    }

    char username[256];
    if (queryGet(post_data, "username", username, sizeof(username)))
    {
        char passwordHash[256];
        if (queryGet(post_data, "passwordHash", passwordHash, sizeof(passwordHash)))
        {
            if (osStrcmp("admin", username) == 0) // && osStrcmp("admin", passwordHash) == 0)
            {
                char *token = TEST_TOKEN;
                httpInitResponseHeader(connection);
                connection->response.contentType = "text/plain";
                connection->response.contentLength = osStrlen(token);

                return httpWriteResponse(connection, token, connection->response.contentLength, false);
            }
        }
    }
    httpInitResponseHeader(connection);
    connection->response.contentLength = 0;
    connection->response.statusCode = 401; // Unauthorized
    return httpWriteResponse(connection, "", connection->response.contentLength, false);
}
error_t handleApiAuthLogout(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx)
{
    httpInitResponseHeader(connection);
    connection->response.contentLength = 0;
    connection->response.statusCode = 200; // Unauthorized
    connection->response.contentType = "text/plain";
    return httpWriteResponse(connection, "", connection->response.contentLength, false);
}
error_t handleApiAuthRefreshToken(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx)
{
    char_t post_data[POST_BUFFER_SIZE];
    error_t error = parsePostData(connection, post_data, POST_BUFFER_SIZE);
    if (error != NO_ERROR)
    {
        return error;
    }

    httpInitResponseHeader(connection);
    connection->response.statusCode = 401; // Unauthorized
    connection->response.contentType = "text/plain";
    char refreshToken[256];
    refreshToken[0] = '\0';
    if (queryGet(post_data, "refreshToken", refreshToken, sizeof(refreshToken)))
    {
        if (osStrcmp(TEST_TOKEN, refreshToken) == 0)
        {
            connection->response.statusCode = 200;
        }
    }
    connection->response.contentLength = osStrlen(refreshToken);
    return httpWriteResponse(connection, refreshToken, connection->response.contentLength, false);
}

error_t handleApiMigrateContent2Lib(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx)
{
    char overlay[16];
    const char *rootPath = NULL;

    if (queryPrepare(queryString, &rootPath, overlay, sizeof(overlay), &client_ctx->settings) != NO_ERROR)
    {
        return ERROR_FAILURE;
    }

    char_t post_data[POST_BUFFER_SIZE];
    error_t error = ERROR_FILE_NOT_FOUND;
    error = parsePostData(connection, post_data, POST_BUFFER_SIZE);
    if (error != NO_ERROR)
    {
        return error;
    }
    char ruid[256];
    char libroot[256];
    bool_t lib_root = false;
    if (queryGet(post_data, "libroot", libroot, sizeof(libroot)))
    {
        if (osStrcmp("true", libroot) == 0)
        {
            lib_root = true;
        }
    }

    if (queryGet(post_data, "ruid", ruid, sizeof(ruid)) && osStrlen(ruid) == 16)
    {
        tonie_info_t *tonieInfo;
        tonieInfo = getTonieInfoFromRuid(ruid, false, client_ctx->settings);

        if (tonieInfo->valid)
        {
            if (tonieInfo->json._source_type != CT_SOURCE_NONE)
            {
                error = ERROR_FILE_NOT_FOUND;
                TRACE_WARNING("Source already set, cannot migrate %s\n", ruid);
            }
            else
            {
                error = moveTAF2Lib(tonieInfo, client_ctx->settings, lib_root);
            }
        }
        else
        {
            error = ERROR_FILE_NOT_FOUND;
        }
        freeTonieInfo(tonieInfo);
    }
    if (error != NO_ERROR)
    {
        return ERROR_FILE_NOT_FOUND;
    }
    return httpOkResponse(connection);
}

error_t handleDeleteOverlay(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx)
{
    char overlay[16];
    const char *rootPath = NULL;

    if (queryPrepare(queryString, &rootPath, overlay, sizeof(overlay), &client_ctx->settings) != NO_ERROR)
    {
        return ERROR_FAILURE;
    }
    if (get_overlay_id(overlay) == 0)
    {
        TRACE_ERROR("No overlay detected %s\n", overlay);
        return ERROR_FAILURE;
    }
    get_settings_cn(overlay)->internal.config_used = false;
    settings_save();
    TRACE_INFO("Removed overlay %s\n", overlay);

    return httpOkResponse(connection);
}

error_t handleApiCacheFlush(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx)
{
    /* RESTful API-based cache flush request */
    uint32_t deleted = cache_flush();

    char json_resp[128];
    snprintf(json_resp, sizeof(json_resp), "{\"message\": \"Cache successfully flushed.\", \"deleted_files\": %u}", deleted);

    httpPrepareHeader(connection, "application/json; charset=utf-8", strlen(json_resp));
    return httpWriteResponseString(connection, json_resp, false);
}

error_t handleApiCacheStats(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx)
{
    cache_stats_t stats;
    cache_stats(&stats);

    char stats_json[512];
    snprintf(stats_json, sizeof(stats_json),
             "{"
             "\"total_entries\": %zu,"
             "\"exists_entries\": %zu,"
             "\"total_files\": %zu,"
             "\"total_size\": %zu,"
             "\"memory_used\": %zu"
             "}",
             stats.total_entries,
             stats.exists_entries,
             stats.total_files,
             stats.total_size,
             stats.memory_used);

    httpPrepareHeader(connection, "application/json; charset=utf-8", osStrlen(stats_json));
    return httpWriteResponseString(connection, stats_json, false);
}