#ifndef _HANDLER_API_H
#define _HANDLER_API_H

#include "debug.h"

#include "core/net.h"
#include "core/ethernet.h"
#include "core/ip.h"
#include "core/tcp.h"
#include "http/http_server.h"
#include "http/http_server_misc.h"

#include "handler.h"

typedef struct
{
    const char *overlay;
    const char *root_path;
    char *filename;
    FsFile *file;
} file_save_ctx;

#define POST_BUFFER_SIZE 1024 * 32

void stats_update(const char *item, int count);

error_t handleApiUploadCert(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx);
error_t handleApiGetCaDer(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx);
error_t handleApiESP32UploadFirmware(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx);
error_t handleApiESP32ExtractCerts(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx);
error_t handleApiESP32PatchFirmware(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx);
error_t handleApiStats(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx);
error_t handleApiGetIndex(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx);
error_t handleApiGetBoxes(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx);
error_t handleApiSettingsGet(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx);
error_t handleApiSettingsSet(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx);
error_t handleApiSettingsReset(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx);
error_t handleApiTrigger(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx);
error_t handleApiFileIndex(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx);
error_t handleApiFileIndexV2(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx);
error_t handleApiFileUpload(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx);
error_t handleApiDirectoryCreate(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx);
error_t handleApiFileDelete(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx);
error_t handleApiFileMove(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx);
error_t handleApiDirectoryDelete(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx);
error_t handleApiAssignUnknown(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx);
error_t handleApiEncodeFile(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx);
error_t handleApiPcmUpload(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx);
error_t handleApiContent(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx);
error_t handleApiContentDownload(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx);
error_t handleApiToniesJsonReload(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx);
error_t handleApiToniesJson(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx);
error_t handleApiToniesJsonUpdate(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx);
error_t handleApiToniesCustomJson(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx);
error_t handleApiTonieboxJson(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx);
error_t handleApiTonieboxCustomJson(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx);
error_t handleApiToniesJsonSearch(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx);
error_t handleApiContentJson(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx);
error_t handleApiContentJsonGet(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx);
error_t handleApiContentJsonSet(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx);
error_t handleApiTagIndex(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx);
error_t handleApiTagInfo(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx);

error_t handleApiAuthLogin(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx);
error_t handleApiAuthLogout(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx);
error_t handleApiAuthRefreshToken(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx);

error_t handleApiMigrateContent2Lib(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx);
error_t handleDeleteOverlay(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx);

error_t handleApiCacheFlush(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx);
error_t handleApiCacheStats(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx);

#endif