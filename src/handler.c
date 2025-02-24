#include "handler.h"
#include "toniesJson.h"
#include "server_helpers.h"
#include "fs_ext.h"
#include "mutex_manager.h"

void fillBaseCtx(HttpConnection *connection, const char_t *uri, const char_t *queryString, cloudapi_t api, cbr_ctx_t *ctx, client_ctx_t *client_ctx)
{
    osMemset(ctx, 0, sizeof(cbr_ctx_t));

    ctx->uri = uri;
    ctx->queryString = queryString;
    ctx->api = api;
    ctx->status = PROX_STATUS_IDLE;
    ctx->connection = connection;
    ctx->client_ctx = client_ctx;

    if (connection && connection->private.client_ctx.settings->internal.overlayNumber > 0)
    {
        ctx->user_agent = connection->request.userAgent;
    }
}

req_cbr_t getCloudOtaCbr(HttpConnection *connection, const char_t *uri, const char_t *queryString, cbr_ctx_t *ctx, client_ctx_t *client_ctx)
{
    fillBaseCtx(connection, uri, queryString, V1_OTA, ctx, client_ctx);

    req_cbr_t cbr = {
        .ctx = ctx,
        .response = NULL,
        .header = &cbrCloudOtaHeader,
        .body = &cbrCloudOtaBody,
        .disconnect = NULL};

    return cbr;
}

void cbrCloudOtaHeader(void *src_ctx, HttpClientContext *cloud_ctx, const char *header, const char *value)
{
    cbr_ctx_t *ctx = (cbr_ctx_t *)src_ctx;
    HttpClientContext *httpClientContext = (HttpClientContext *)cloud_ctx;

    if (ctx->client_ctx->settings->cloud.cacheOta)
    {
        if (httpClientContext->statusCode == 200)
        {
            if (header)
            {
                if (osStrcmp(header, "Content-Length") == 0)
                {
                    ctx->customDataLen = atoi(value);
                }
                else if (osStrcmp(header, "Content-Disposition") == 0)
                {
                    ota_ctx_t *ota_ctx = (ota_ctx_t *)ctx->customData;
                    char *prefix = "attachment;filename=";
                    if (osStrncmp(value, prefix, osStrlen(prefix)) == 0)
                    {
                        const char *filename = &value[osStrlen(prefix)];
                        // search for PATH_SERPERATOR_LINUX / PATH_SERPERATOR_LINUX and replace with null
                        char *p1 = osStrchr(filename, PATH_SEPARATOR_LINUX);
                        char *p2 = osStrchr(filename, PATH_SEPARATOR_WINDOWS);
                        if (p1)
                        {
                            *p1 = '\0';
                        }
                        if (p2)
                        {
                            *p2 = '\0';
                        }

                        char *folder;
                        switch (ctx->client_ctx->settings->internal.toniebox_firmware.boxIC)
                        {
                        case BOX_CC3200:
                            folder = custom_asprintf("cc3200%c", PATH_SEPARATOR);
                            break;
                        case BOX_CC3235:
                            folder = custom_asprintf("cc3235%c", PATH_SEPARATOR);
                            break;
                        case BOX_ESP32:
                            folder = custom_asprintf("esp32%c", PATH_SEPARATOR);
                            break;
                        default:
                            folder = strdup("");
                            break;
                        }
                        char *local_dir = custom_asprintf("%s%cota%c%s%" PRIu8 "%c", ctx->client_ctx->settings->internal.firmwaredirfull, PATH_SEPARATOR, PATH_SEPARATOR, folder, ota_ctx->fileId, PATH_SEPARATOR);
                        char *local_filename = custom_asprintf("%s%s", local_dir, filename);
                        char *local_filename_tmp = custom_asprintf("%s.tmp", local_filename);

                        osFreeMem(folder);

                        fsCreateDirEx(local_dir, true);
                        if (!fsFileExists(local_filename))
                        {
                            ctx->file = fsOpenFile(local_filename_tmp, FS_FILE_MODE_WRITE);
                            if (ctx->file == NULL)
                            {
                                TRACE_ERROR(">> Could not open file %s\r\n", local_filename_tmp);
                            }
                            else
                            {
                                ctx->customData = strdup(local_filename);
                            }
                        }
                        else
                        {
                            TRACE_WARNING(">> File %s already exists, no ota caching\r\n", local_filename);
                        }
                        osFreeMem(local_dir);
                        osFreeMem(local_filename);
                        osFreeMem(local_filename_tmp);
                    }
                }
            }
        }
    }

    ctx->status = PROX_STATUS_HEAD;
}
void cbrCloudOtaBody(void *src_ctx, HttpClientContext *cloud_ctx, const char *payload, size_t length, error_t error)
{
    cbr_ctx_t *ctx = (cbr_ctx_t *)src_ctx;
    error_t ferror = NO_ERROR;

    if (ctx->file != NULL)
    {
        if (length > 0)
        {
            ferror = fsWriteFile(ctx->file, (void *)payload, length);
            if (ferror)
                TRACE_ERROR(">> fsWriteFile Error: %s\r\n", error2text(ferror));
        }
        if (error == ERROR_END_OF_STREAM)
        {
            fsCloseFile(ctx->file);
            char *local_filename = (char *)ctx->customData;
            char *local_filename_tmp = custom_asprintf("%s.tmp", local_filename);
            uint32_t fileSize = 0;
            fsGetFileSize(local_filename_tmp, &fileSize);
            if (fileSize > 0 && fileSize == ctx->customDataLen)
            {
                ferror = fsRenameFile(local_filename_tmp, local_filename);

                if (ferror == NO_ERROR)
                {
                    TRACE_INFO(">> Successfully cached %s\r\n", local_filename);
                }
                else
                {
                    TRACE_ERROR(">> Error caching %s, %s\r\n", local_filename, error2text(ferror));
                }
            }
            else
            {
                TRACE_ERROR(">> File %s has wrong size %" PRIu32 " != %zu\r\n", local_filename, fileSize, ctx->customDataLen);
            }
            osFreeMem(local_filename_tmp);
        }
    }

    ctx->status = PROX_STATUS_BODY;
}

req_cbr_t getCloudCbr(HttpConnection *connection, const char_t *uri, const char_t *queryString, cloudapi_t api, cbr_ctx_t *ctx, client_ctx_t *client_ctx)
{
    fillBaseCtx(connection, uri, queryString, api, ctx, client_ctx);

    req_cbr_t cbr = {
        .ctx = ctx,
        .response = &cbrCloudResponsePassthrough,
        .header = &cbrCloudHeaderPassthrough,
        .body = &cbrCloudBodyPassthrough,
        .disconnect = &cbrCloudServerDiscoPassthrough};

    return cbr;
}
void cbrCloudResponsePassthrough(void *src_ctx, HttpClientContext *cloud_ctx)
{
    cbr_ctx_t *ctx = (cbr_ctx_t *)src_ctx;
    char line[128];

    // This is fine: https://www.youtube.com/watch?v=0oBx7Jg4m-o
    const char *statusText = httpStatusCodeText(cloud_ctx->statusCode);

    osSprintf(line, "HTTP/%d.%d %u %s\r\n", MSB(cloud_ctx->version), LSB(cloud_ctx->version), cloud_ctx->statusCode, statusText);
    httpSend(ctx->connection, line, osStrlen(line), HTTP_FLAG_DELAY);
    ctx->status = PROX_STATUS_CONN;
}

void cbrCloudHeaderPassthrough(void *src_ctx, HttpClientContext *cloud_ctx, const char *header, const char *value)
{
    cbr_ctx_t *ctx = (cbr_ctx_t *)src_ctx;
    char line[256];
    bool passthrough = true;

    if (ctx->status != PROX_STATUS_HEAD) // Only once
    {
        char_t *allowOrigin = ctx->connection->serverContext->settings.allowOrigin;
        if (allowOrigin != NULL && osStrlen(allowOrigin) > 0)
        {
            osSprintf(line, "Access-Control-Allow-Origin: %s\r\n", allowOrigin);
            httpSend(ctx->connection, line, osStrlen(line), HTTP_FLAG_DELAY);
            line[0] = '\0';
        }
    }
    switch (ctx->api)
    {
    case V1_FRESHNESS_CHECK:
        if (!header || osStrcmp(header, "Content-Length") == 0) // Skip empty line at the and + contentlen
        {
            passthrough = false;
        }
        break;
    default:
        break;
    }
    if (passthrough)
    {
        if (header)
        {
            if (osStrcmp(header, "Access-Control-Allow-Origin") != 0)
            {
                TRACE_DEBUG(">> cbrCloudHeaderPassthrough: %s = %s\r\n", header, value);
                osSprintf(line, "%s: %s\r\n", header, value);
            }
        }
        else
        {
            TRACE_DEBUG(">> cbrCloudHeaderPassthrough: NULL\r\n");
            osStrcpy(line, "\r\n");
        }

        httpSend(ctx->connection, line, osStrlen(line), HTTP_FLAG_DELAY);
    }

    ctx->status = PROX_STATUS_HEAD;
}

bool fillCbrBodyCache(cbr_ctx_t *ctx, HttpClientContext *httpClientContext, const char *payload, size_t length)
{
    if (ctx->bufferPos == 0)
    {
        ctx->bufferLen = httpClientContext->bodyLen; // ctx->connection->response.contentLength;
        ctx->buffer = osAllocMem(ctx->bufferLen);
    }
    osMemcpy(&ctx->buffer[ctx->bufferPos], payload, length);
    ctx->bufferPos += length;
    return (ctx->bufferPos == ctx->bufferLen);
}

void cbrCloudBodyPassthrough(void *src_ctx, HttpClientContext *cloud_ctx, const char *payload, size_t length, error_t error)
{
    cbr_ctx_t *ctx = (cbr_ctx_t *)src_ctx;
    HttpClientContext *httpClientContext = (HttpClientContext *)cloud_ctx;

    // TRACE_INFO(">> cbrCloudBodyPassthrough: %lu received\r\n", length);
    switch (ctx->api)
    {
    case V2_CONTENT: // Also handles V1_CONTENT
        if (ctx->client_ctx->settings->cloud.cacheContent && httpClientContext->statusCode == 200)
        {
            // TRACE_INFO(">> cbrCloudBodyPassthrough: %lu received\r\n", length);
            // TRACE_INFO(">> %s\r\n", ctx->uri);
            if (ctx->status == PROX_STATUS_HEAD)
            {
                /* URI is always "/v2/content/xxxxxxxxxx0304E0" where the x's are hex digits. length has to be fixed */
                TRACE_INFO(">> Start caching uri=%s\r\n", ctx->uri);
                // TODO detect partial downloads
                if (strlen(ctx->uri) < 28)
                {
                    TRACE_ERROR(">> ctx->uri is too short\r\n");
                    return;
                }
                char *tmpPath = custom_asprintf("%s.tmp", ctx->tonieInfo->contentPath);

                char *dir = strdup(ctx->tonieInfo->contentPath);
                dir[osStrlen(dir) - 8] = '\0';
                fsCreateDir(dir);

                ctx->file = fsOpenFile(tmpPath, FS_FILE_MODE_WRITE | FS_FILE_MODE_TRUNC);

                if (ctx->file == NULL)
                {
                    TRACE_ERROR(">> Could not open file %s\r\n", tmpPath);
                }
                free(tmpPath);
                free(dir);
            }
            if (length > 0 && ctx->file != NULL)
            {
                error = fsWriteFile(ctx->file, (void *)payload, length);
                if (error)
                {
                    TRACE_ERROR(">> fsWriteFile Error: %s\r\n", error2text(error));
                }
            }
            if (error == ERROR_END_OF_STREAM)
            {
                fsCloseFile(ctx->file);
                char *tmpPath = custom_asprintf("%s.tmp", ctx->tonieInfo->contentPath);

                fsDeleteFile(ctx->tonieInfo->contentPath);
                fsRenameFile(tmpPath, ctx->tonieInfo->contentPath);
                if (fsFileExists(ctx->tonieInfo->contentPath))
                {
                    TRACE_INFO(">> Successfully cached %s\r\n", ctx->tonieInfo->contentPath);

                    if (ctx->client_ctx->settings->cloud.cacheToLibrary)
                    {
                        tonie_info_t *tonieInfo = getTonieInfo(ctx->tonieInfo->contentPath, true, ctx->client_ctx->settings);
                        moveTAF2Lib(tonieInfo, ctx->client_ctx->settings, false);
                        freeTonieInfo(tonieInfo);
                    }
                }
                else
                {
                    TRACE_ERROR(">> Error caching %s\r\n", ctx->tonieInfo->contentPath);
                }
                free(tmpPath);
            }
        }
        httpSend(ctx->connection, payload, length, HTTP_FLAG_DELAY);
        break;
    case V1_FRESHNESS_CHECK:
        if (length > 0 && fillCbrBodyCache(ctx, httpClientContext, payload, length))
        {
            TonieFreshnessCheckResponse *freshResp = (TonieFreshnessCheckResponse *)ctx->customData;
            TonieFreshnessCheckResponse *freshRespCloud = tonie_freshness_check_response__unpack(NULL, ctx->bufferLen, (const uint8_t *)ctx->buffer);

            if (ctx->client_ctx->settings->toniebox.overrideCloud)
            {
                setTonieboxSettings(freshResp, ctx->client_ctx->settings);
            }
            else
            {
                freshResp->max_vol_spk = freshRespCloud->max_vol_spk;
                freshResp->max_vol_hdp = freshRespCloud->max_vol_hdp;
                freshResp->slap_en = freshRespCloud->slap_en;
                freshResp->slap_dir = freshRespCloud->slap_dir;
                freshResp->led = freshRespCloud->led;
                freshResp->field2 = freshRespCloud->field2;
                freshResp->field6 = freshRespCloud->field6;
            }
            TRACE_INFO("FreshnessCheck data from cloud / to box\r\n");
            TRACE_INFO(" max_vol_spk: %" PRIu32 " / %" PRIu32 "\r\n", freshRespCloud->max_vol_spk, freshResp->max_vol_spk);
            TRACE_INFO(" max_vol_hdp: %" PRIu32 " / %" PRIu32 "\r\n", freshRespCloud->max_vol_hdp, freshResp->max_vol_hdp);
            TRACE_INFO(" slap_en: %" PRIu32 " / %" PRIu32 "\r\n", freshRespCloud->slap_en, freshResp->slap_en);
            TRACE_INFO(" slap_dir: %" PRIu32 " / %" PRIu32 "\r\n", freshRespCloud->slap_dir, freshResp->slap_dir);
            TRACE_INFO(" led: %" PRIu32 " / %" PRIu32 "\r\n", freshRespCloud->led, freshResp->led);
            TRACE_INFO(" field2: %" PRIu32 " / %" PRIu32 "\r\n", freshRespCloud->field2, freshResp->field2);
            TRACE_INFO(" field6: %" PRIu32 " / %" PRIu32 "\r\n", freshRespCloud->field6, freshResp->field6);

            if (freshRespCloud->field2 != 7)
            {
                TRACE_WARNING("Field 2 has not the expected value 7. Value=%" PRIu32 "\r\n", freshRespCloud->field2);
            }
            if (freshRespCloud->field6 != 1)
            {
                TRACE_WARNING("Field 6 has not the expected value 1. Value=%" PRIu32 "\r\n", freshRespCloud->field6);
            }

            for (size_t i = 0; i < freshRespCloud->n_tonie_marked; i++)
            {
                bool found = false;
                for (size_t j = 0; j < freshResp->n_tonie_marked; j++)
                {
                    if (freshRespCloud->tonie_marked[i] == freshResp->tonie_marked[j])
                    {
                        found = true;
                        break;
                    }
                }
                if (!found)
                {
                    // handleCloudFreshnessCheck allocated space for all in freshResp.tonie_marked
                    if (ctx->customDataLen > freshResp->n_tonie_marked)
                    {
                        freshResp->tonie_marked[freshResp->n_tonie_marked++] = freshRespCloud->tonie_marked[i];
                        TRACE_INFO("Marked UID %016" PRIX64 " as updated from cloud\r\n", freshRespCloud->tonie_marked[i]);
                    }
                    else
                    {
                        TRACE_WARNING("Could not add UID %016" PRIX64 " to freshnessCheck response, as not enough slots allocated!\r\n", freshRespCloud->tonie_marked[i]);
                    }
                }
            }
            tonie_freshness_check_response__free_unpacked(freshRespCloud, NULL);

            size_t packSize = tonie_freshness_check_response__get_packed_size(freshResp);
            if (ctx->bufferLen < packSize)
            {
                osFreeMem(ctx->buffer);
                ctx->bufferLen = packSize;
                ctx->buffer = osAllocMem(ctx->bufferLen);
            }
            tonie_freshness_check_response__pack(freshResp, (uint8_t *)ctx->buffer);

            settings_set_u64_array_id("internal.freshnessCache", freshResp->tonie_marked, freshResp->n_tonie_marked, ctx->client_ctx->settings->internal.overlayNumber);

            char line[128];
            osSnprintf(line, 128, "Content-Length: %zu\r\n\r\n", ctx->bufferLen);
            httpSend(ctx->connection, line, osStrlen(line), HTTP_FLAG_DELAY);

            httpSend(ctx->connection, ctx->buffer, ctx->bufferLen, HTTP_FLAG_DELAY);
            osFreeMem(ctx->buffer);
        }
        else
        {
            httpSend(ctx->connection, payload, length, HTTP_FLAG_DELAY);
        }
        break;
    default:
        httpSend(ctx->connection, payload, length, HTTP_FLAG_DELAY);
        break;
    }
    ctx->status = PROX_STATUS_BODY;
}

void cbrCloudServerDiscoPassthrough(void *src_ctx, HttpClientContext *cloud_ctx)
{
    cbr_ctx_t *ctx = (cbr_ctx_t *)src_ctx;
    TRACE_DEBUG(">> cbrCloudServerDiscoPassthrough\r\n");
    httpFlushStream(ctx->connection);
    ctx->status = PROX_STATUS_DONE;
}

char *strupr(char input[])
{
    for (uint16_t i = 0; input[i]; i++)
    {
        input[i] = toupper(input[i]);
    }
    return input;
}

void getContentPathFromCharRUID(char ruid[17], char **pcontentPath, settings_t *settings)
{
    char filePath[18];
    osSprintf(filePath, "%.8s/%.8s", ruid, &ruid[8]);
    strupr(filePath);

    *pcontentPath = custom_asprintf("%s%c%s", settings->internal.contentdirfull, PATH_SEPARATOR, filePath);
}

void getContentPathFromUID(uint64_t uid, char **pcontentPath, settings_t *settings)
{
    uint16_t cuid[9];
    osSprintf((char *)cuid, "%016" PRIX64 "", uid);
    uint16_t cruid[9];
    for (uint8_t i = 0; i < 8; i++)
    {
        cruid[i] = cuid[7 - i];
    }
    cruid[8] = 0;
    getContentPathFromCharRUID((char *)cruid, pcontentPath, settings);
}

void setTonieboxSettings(TonieFreshnessCheckResponse *freshResp, settings_t *settings)
{
    freshResp->max_vol_spk = settings->toniebox.max_vol_spk;
    freshResp->max_vol_hdp = settings->toniebox.max_vol_hdp;
    freshResp->slap_en = settings->toniebox.slap_enabled;
    freshResp->slap_dir = settings->toniebox.slap_back_left;
    freshResp->led = settings->toniebox.led;
    if (settings->toniebox.overrideFields) {
        freshResp->field2 = settings->toniebox.field2;
        freshResp->field6 = settings->toniebox.field6;
    }
}

bool_t isValidTaf(const char *contentPath, bool checkHashAndSize)
{
    bool_t valid = false;
    FsFile *file = fsOpenFile(contentPath, FS_FILE_MODE_READ);
    if (file)
    {
        uint8_t headerBuffer[TAF_HEADER_SIZE];
        size_t read_length;
        fsReadFile(file, headerBuffer, 4, &read_length);
        if (read_length == 4)
        {
            uint32_t protobufSize = (uint32_t)((headerBuffer[0] << 24) | (headerBuffer[1] << 16) | (headerBuffer[2] << 8) | headerBuffer[3]);
            if (protobufSize <= TAF_HEADER_SIZE)
            {
                fsReadFile(file, headerBuffer, protobufSize, &read_length);
                if (read_length == protobufSize)
                {
                    TonieboxAudioFileHeader *tafHeader = toniebox_audio_file_header__unpack(NULL, protobufSize, (const uint8_t *)headerBuffer);
                    if (tafHeader)
                    {
                        if (tafHeader->sha1_hash.len == 20)
                        {
                            if (checkHashAndSize)
                            {
                                Sha1Context sha1Ctx;
                                size_t audio_length = 0;
                                sha1Init(&sha1Ctx);
                                char buffer[TONIEFILE_FRAME_SIZE];
                                uint8_t sha1[SHA1_DIGEST_SIZE];
                                while (true)
                                {
                                    error_t error = fsReadFile(file, buffer, TONIEFILE_FRAME_SIZE, &read_length);
                                    if (error != NO_ERROR && error != ERROR_END_OF_FILE)
                                    {
                                        TRACE_ERROR("Cannot read file, error=%" PRIu16 "\n", error);
                                        break;
                                    }
                                    if (read_length == 0)
                                    {
                                        break;
                                    }
                                    audio_length += read_length;
                                    sha1Update(&sha1Ctx, buffer, read_length);
                                }
                                sha1Final(&sha1Ctx, sha1);
                                if (osMemcmp(tafHeader->sha1_hash.data, sha1, SHA1_DIGEST_SIZE) == 0)
                                {
                                    if (audio_length == tafHeader->num_bytes)
                                    {
                                        valid = true;
                                    }
                                }
                            }
                            else
                            {
                                valid = true;
                            }
                        }
                        toniebox_audio_file_header__free_unpacked(tafHeader, NULL);
                    }
                }
            }
        }
        fsCloseFile(file);
    }
    return valid;
}

void readTrackPositions(tonie_info_t *tonieInfo, FsFile *file)
{
    bool hasError = false;
    track_positions_t *trackPos = &tonieInfo->additional.track_positions;
    TonieboxAudioFileHeader *tafHeader = tonieInfo->tafHeader;
    trackPos->count = tafHeader->n_track_page_nums;
    if (trackPos->count > 0)
    {
        trackPos->pos = osAllocMem(trackPos->count * sizeof(uint32_t));
        uint64_t correction = 0;
        for (size_t i = 0; i < trackPos->count; i++)
        {
            uint8_t buffer[14];
            size_t readBytes = 0;
            uint32_t trackPageNum = tafHeader->track_page_nums[i];
            size_t filePos = 4096 + 4096 * trackPageNum;
            if (i == 0)
            {
                filePos += 0x200;
            }

            error_t error = fsSeekFile(file, filePos, SEEK_SET);
            if (error != NO_ERROR)
            {
                hasError = true;
                TRACE_ERROR("Failed to seek track position at %" PRIuSIZE " with error %s, %s\r\n", filePos, error2text(error), tonieInfo->contentPath);
                break;
            }
            error = fsReadFile(file, buffer, sizeof(buffer), &readBytes);
            if (error != NO_ERROR)
            {
                hasError = true;
                TRACE_ERROR("Failed to read track position at %" PRIuSIZE " with error %s, %s\r\n", filePos, error2text(error), tonieInfo->contentPath);
                break;
            }

            if (!osMemcmp(buffer, "OggS", 4) == 0)
            {
                hasError = true;
                TRACE_ERROR("Invalid OggS header at %" PRIuSIZE ", %s\r\n", filePos, tonieInfo->contentPath);
                break;
            }
            if (buffer[4] != 0)
            { // Opus Version
                hasError = true;
                TRACE_ERROR("Invalid Opus Version %" PRIu8 " at %" PRIuSIZE ", %s\r\n", buffer[4], filePos, tonieInfo->contentPath);
                break;
            }
            if (buffer[5] != 0)
            { // Header Type
                hasError = true;
                TRACE_ERROR("Invalid Header Type %" PRIu8 " at %" PRIuSIZE ", %s\r\n", buffer[5], filePos, tonieInfo->contentPath);
                break;
            }
            uint64_t granulePosition = 0;
            osMemcpy(&granulePosition, &buffer[6], 8);
            trackPos->pos[i] = (uint32_t)((granulePosition - correction) / 48000); // 48000 samples per second
            TRACE_VERBOSE("Track position %" PRIu32 "\r\n", trackPos->pos[i]);

            if (i == 0)
            {
                correction = granulePosition;
            }
        }
        if (hasError)
        {
            trackPos->count = 0;
            osFreeMem(trackPos->pos);
            trackPos->pos = NULL;

            if (get_settings()->core.track_pos_taf_validation)
            {
                if (!isValidTaf(tonieInfo->contentPath, true))
                {
                    TRACE_ERROR("SHA1 not valid or length different for TAF %s\r\n", tonieInfo->contentPath);
                }
            }
        }
    }
}

tonie_info_t *getTonieInfoFromUid(uint64_t uid, bool lock, settings_t *settings)
{
    char *contentPath;
    getContentPathFromUID(uid, &contentPath, settings);
    tonie_info_t *tonieInfo = getTonieInfo(contentPath, lock, settings);
    osFreeMem(contentPath);
    return tonieInfo;
}
tonie_info_t *getTonieInfoFromRuid(char ruid[17], bool lock, settings_t *settings)
{
    char *contentPath;
    getContentPathFromCharRUID(ruid, &contentPath, settings);
    tonie_info_t *tonieInfo = getTonieInfo(contentPath, lock, settings);
    osFreeMem(contentPath);
    return tonieInfo;
}
tonie_info_t *getTonieInfo(const char *contentPath, bool lock, settings_t *settings)
{
    tonie_info_t *tonieInfo;
    tonieInfo = osAllocMem(sizeof(tonie_info_t));

    tonieInfo->valid = false;
    tonieInfo->updated = false;
    tonieInfo->tafHeader = NULL;
    tonieInfo->contentPath = strdup(contentPath);
    tonieInfo->jsonPath = custom_asprintf("%s.json", contentPath);
    tonieInfo->exists = false;
    tonieInfo->locked = false;
    osMemset(&tonieInfo->json, 0, sizeof(contentJson_t));
    osMemset(&tonieInfo->additional, 0, sizeof(tonie_info_additional_t));

    if (osStrstr(contentPath, ".json") == NULL)
    {
        if (lock)
        {
            tonieInfo->locked = true;
            mutex_lock_id(tonieInfo->jsonPath);
        }
        if (osStrstr(contentPath, settings->internal.contentdirfull) == contentPath &&
            (contentPath[osStrlen(settings->internal.contentdirfull)] == '/' || contentPath[osStrlen(settings->internal.contentdirfull)] == '\\') &&
            osStrlen(contentPath) - 18 == osStrlen(settings->internal.contentdirfull))
        {
            // TODO: Nice checking if valid tonie path
            load_content_json(contentPath, &tonieInfo->json, true, settings);
        }

        if (tonieInfo->json._source_type == CT_SOURCE_TAF || tonieInfo->json._source_type == CT_SOURCE_TAP_CACHED)
        {
            osFreeMem(tonieInfo->contentPath);
            tonieInfo->contentPath = strdup(tonieInfo->json._source_resolved);
        }
        else if (tonieInfo->json._source_type == CT_SOURCE_TAP_STREAM)
        {
            osFreeMem(tonieInfo->contentPath);
            tonieInfo->contentPath = custom_asprintf("%s.tmp", tonieInfo->json._source_resolved);
        }
        tonieInfo->exists = fsFileExists(tonieInfo->contentPath);

        FsFile *file = fsOpenFile(tonieInfo->contentPath, FS_FILE_MODE_READ);
        if (file)
        {
            uint8_t headerBuffer[TAF_HEADER_SIZE];
            size_t read_length;
            fsReadFile(file, headerBuffer, 4, &read_length);
            if (read_length == 4)
            {
                uint32_t protobufSize = (uint32_t)((headerBuffer[0] << 24) | (headerBuffer[1] << 16) | (headerBuffer[2] << 8) | headerBuffer[3]);
                if (protobufSize <= TAF_HEADER_SIZE)
                {
                    fsReadFile(file, headerBuffer, protobufSize, &read_length);
                    if (read_length == protobufSize)
                    {
                        tonieInfo->tafHeader = toniebox_audio_file_header__unpack(NULL, protobufSize, (const uint8_t *)headerBuffer);
                        if (tonieInfo->tafHeader)
                        {
                            if (tonieInfo->tafHeader->sha1_hash.len == 20)
                            {
                                tonieInfo->valid = isValidTaf(tonieInfo->contentPath, settings->core.full_taf_validation);
                                readTrackPositions(tonieInfo, file);
                                if (tonieInfo->tafHeader->num_bytes == get_settings()->encode.stream_max_size)
                                {
                                    tonieInfo->json._source_type = CT_SOURCE_TAF_INCOMPLETE;
                                }
                                else if ((tonieInfo->json.tonie_model == NULL || tonieInfo->json.tonie_model[0] == '\0') && (tonieInfo->json._source_type == CT_SOURCE_NONE || tonieInfo->json._source_type == CT_SOURCE_TAF)) // TAF beside the content json
                                {
                                    content_json_update_model(&tonieInfo->json, tonieInfo->tafHeader->audio_id, tonieInfo->tafHeader->sha1_hash.data);
                                }
                                toniesJson_item_t *toniesJson = tonies_byAudioIdHash(tonieInfo->tafHeader->audio_id, tonieInfo->tafHeader->sha1_hash.data);
                                if (toniesJson != NULL)
                                {
                                    tonieInfo->json._source_model = strdup(toniesJson->model);
                                }
                            }
                            else
                            {
                                TRACE_WARNING("Invalid TAF-header on %s, sha1_hash.len=%zu != 20\r\n", tonieInfo->contentPath, tonieInfo->tafHeader->sha1_hash.len);
                            }
                        }
                    }
                    else
                    {
                        TRACE_WARNING("Invalid TAF-header on %s, read_length=%zu != protobufSize=%" PRIu32 "\r\n", tonieInfo->contentPath, read_length, protobufSize);
                    }
                }
                else
                {
                    TRACE_VERBOSE("Invalid TAF-header on %s, protobufSize=%" PRIu32 " >= TAF_HEADER_SIZE=%u\r\n", tonieInfo->contentPath, protobufSize, TAF_HEADER_SIZE);
                }
            }
            else if (read_length == 0)
            {
                // TODO don't send invalid TAF files via API
                TRACE_VERBOSE("Invalid TAF-header, file %s is empty!", tonieInfo->contentPath);
            }
            else
            {
                TRACE_WARNING("Invalid TAF-header on %s, Could not read 4 bytes, read_length=%zu\r\n", tonieInfo->contentPath, read_length);
            }
            fsCloseFile(file);
        }
    }
    return tonieInfo;
}

void saveTonieInfo(tonie_info_t *tonieInfo, bool unlock)
{
    if (tonieInfo->json._updated && tonieInfo->json._create_if_missing)
    {
        save_content_json(tonieInfo->jsonPath, &tonieInfo->json);
    }
    if (tonieInfo->locked && unlock)
    {
        mutex_unlock_id(tonieInfo->jsonPath);
        tonieInfo->locked = false;
    }
}
void freeTonieInfo(tonie_info_t *tonieInfo)
{
    saveTonieInfo(tonieInfo, true);
    if (tonieInfo->tafHeader)
    {
        toniebox_audio_file_header__free_unpacked(tonieInfo->tafHeader, NULL);
        tonieInfo->tafHeader = NULL;
    }
    if (tonieInfo->contentPath)
    {
        osFreeMem(tonieInfo->contentPath);
        tonieInfo->contentPath = NULL;
    }
    if (tonieInfo->jsonPath)
    {
        osFreeMem(tonieInfo->jsonPath);
        tonieInfo->jsonPath = NULL;
    }
    if (tonieInfo->additional.track_positions.pos)
    {
        osFreeMem(tonieInfo->additional.track_positions.pos);
        tonieInfo->additional.track_positions.pos = NULL;
        tonieInfo->additional.track_positions.count = 0;
    }

    free_content_json(&tonieInfo->json);
    free(tonieInfo);
}

void httpPrepareHeader(HttpConnection *connection, const void *contentType, size_t contentLength)
{
    httpInitResponseHeader(connection);
    connection->response.keepAlive = true;
    connection->response.chunkedEncoding = false;
    connection->response.contentType = contentType;
    connection->response.contentLength = contentLength;
}
error_t httpWriteResponseString(HttpConnection *connection, char_t *data, bool_t freeMemory)
{
    return httpWriteResponse(connection, data, osStrlen(data), freeMemory);
}
error_t httpWriteResponse(HttpConnection *connection, void *data, size_t size, bool_t freeMemory)
{
    error_t error = httpWriteHeader(connection);
    if (error != NO_ERROR)
    {
        if (freeMemory)
            osFreeMem(data);
        TRACE_ERROR("Failed to send header\r\n");
        return error;
    }

    error = httpWriteStream(connection, data, size);
    if (freeMemory)
        osFreeMem(data);
    if (error != NO_ERROR)
    {
        TRACE_ERROR("Failed to send payload: %s\r\n", error2text(error));
        return error;
    }

    // can fail, when stream is already closed
    httpFlushStream(connection);
    /*
    if (error != NO_ERROR)
    {
        TRACE_ERROR("Failed to close: %s\r\n", error2text(error));
        return error;
    }
    */

    return error;
}

error_t httpWriteString(HttpConnection *connection, const char_t *content)
{
    return httpWriteStream(connection, content, osStrlen(content));
}

error_t httpFlushStream(HttpConnection *connection)
{
    return httpCloseStream(connection);
}

error_t httpOkResponse(HttpConnection *connection)
{
    httpInitResponseHeader(connection);
    connection->response.contentLength = 2;
    return httpWriteResponse(connection, "OK", connection->response.contentLength, false);
}

void setLastUid(uint64_t uid, settings_t *settings)
{
    uint16_t cuid[9];
    osSprintf((char *)cuid, "%016" PRIX64 "", uid);
    uint16_t cruid[9];
    for (uint8_t i = 0; i < 8; i++)
    {
        cruid[i] = cuid[7 - i];
    }
    cruid[8] = 0;

    setLastRuid((char *)cruid, settings);
}
void setLastRuid(char ruid[17], settings_t *settings)
{
    if (get_settings() != settings)
    {
        if (osStrcmp(settings->internal.last_ruid, ruid) != 0)
        {
            for (size_t i = 0; ruid[i] != '\0'; i++)
            {
                ruid[i] = tolower(ruid[i]);
            }
            settings_set_string_id("internal.last_ruid", ruid, settings->internal.overlayNumber);
            settings_set_unsigned_id("internal.last_ruid_time", time(NULL), settings->internal.overlayNumber);
        }
    }
    tonie_info_t *tonieInfo = getTonieInfoFromRuid(ruid, true, settings);
    if (tonieInfo->json.hide)
    {
        tonieInfo->json.hide = false;
        tonieInfo->json._updated = true;
    }
    saveTonieInfo(tonieInfo, true);
    freeTonieInfo(tonieInfo);
}

char *getLibraryCachePath(settings_t *settings, uint32_t audioId, bool_t shortPath)
{
    char *libraryPathPrefix;
    if (shortPath)
    {
        libraryPathPrefix = "lib:/";
    }
    else
    {
        libraryPathPrefix = settings->internal.librarydirfull;
    }
    char *libraryByPath = custom_asprintf("%s/by", libraryPathPrefix);
    char *libraryBasePath = custom_asprintf("%s/audioID", libraryByPath);
    char *libraryPath = custom_asprintf("%s/%" PRIu32 ".taf", libraryBasePath, audioId);

    fsCreateDir(libraryByPath);
    fsCreateDir(libraryBasePath);

    osFreeMem(libraryByPath);
    osFreeMem(libraryBasePath);

    return libraryPath;
}

error_t moveTAF2Lib(tonie_info_t *tonieInfo, settings_t *settings, bool_t rootDir)
{
    error_t error = NO_ERROR;
    if (tonieInfo->valid)
    {
        char *libraryPath = NULL;
        char *libraryShortPath = NULL;
        uint32_t audioId = tonieInfo->tafHeader->audio_id;
        if (rootDir)
        {
            libraryPath = custom_asprintf("%s/%" PRIu32 ".taf", settings->internal.librarydirfull, audioId);
            libraryShortPath = custom_asprintf("%s/%" PRIu32 ".taf", "lib:/", audioId);
        }
        else
        {
            if (audioId <= 1)
            {
                TRACE_WARNING(">> Audio ID is %" PRIu32 ", not moving to library\r\n", audioId);
                error = ERROR_INVALID_FILE;
            }
            else
            {
                libraryPath = getLibraryCachePath(settings, audioId, false);
                libraryShortPath = getLibraryCachePath(settings, audioId, true);
            }
        }
        if (libraryPath)
        {
            tonie_info_t *tonieInfoLib = getTonieInfo(libraryPath, false, settings);
            bool moveToLibrary = true;
            bool skipMove = false;
            if (tonieInfoLib->valid)
            {
                if (!osMemcmp(tonieInfoLib->tafHeader->sha1_hash.data, tonieInfo->tafHeader->sha1_hash.data, tonieInfoLib->tafHeader->sha1_hash.len))
                {
                    TRACE_WARNING(">> SHA1 Hash for Audio ID %" PRIu32 ", already in library, deleting source file\r\n", audioId);
                    fsDeleteFile(tonieInfo->contentPath);
                    skipMove = true;
                }
                else
                {
                    TRACE_WARNING(">> SHA1 Hash forAudio ID %" PRIu32 ", of source file is different to library, not moving to library\r\n", audioId);
                    moveToLibrary = false;
                    error = ERROR_INVALID_FILE;
                }
            }
            if (moveToLibrary)
            {
                if (!skipMove)
                {
                    error = fsMoveFile(tonieInfo->contentPath, libraryPath, false);
                }
                if (error == NO_ERROR)
                {

                    free(tonieInfo->json.source);
                    tonieInfo->json.source = strdup(libraryShortPath);

                    save_content_json(tonieInfo->jsonPath, &tonieInfo->json);
                    TRACE_INFO(">> Successfully set to library %s\r\n", libraryShortPath);
                }
                else
                {
                    TRACE_ERROR(">> Failed to move %s to library %s, error=%s\r\n", tonieInfo->contentPath, libraryPath, error2text(error));
                    error = ERROR_INVALID_FILE;
                }
            }

            osFreeMem(libraryPath);
            osFreeMem(libraryShortPath);
            freeTonieInfo(tonieInfoLib);
        }
    }
    else
    {
        TRACE_ERROR(">> Invalid TAF, not moving to library\r\n");
        error = ERROR_INVALID_FILE;
    }
    return error;
}