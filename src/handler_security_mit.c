#include "handler_security_mit.h"
#include "server_helpers.h"
#include "hash/sha1.h"

bool isSecMitIncident(HttpConnection *connection)
{
    settings_t *settings = get_settings();

    bool isSecurityIncident = false;
    if (settings->security_mit.httpsOnly && !connection->settings->isHttps)
    {
        return false;
    }
    if (settings->security_mit.onBlacklistDomain && (settings->internal.security_mit.blacklisted_domain_access > 0))
    {
        isSecurityIncident = true;
        TRACE_WARNING("Blacklisted domain access detected\r\n");
    }
    if (settings->security_mit.onCrawler && (settings->internal.security_mit.crawler_access > 0))
    {
        isSecurityIncident = true;
        TRACE_WARNING("Crawler access detected\r\n");
    }
    if (settings->security_mit.onExternal && (settings->internal.security_mit.external_access > 0))
    {
        isSecurityIncident = true;
        TRACE_WARNING("External access detected\r\n");
    }
    if (settings->security_mit.onRobotsTxt && (settings->internal.security_mit.robots_txt_access > 0))
    {
        isSecurityIncident = true;
        TRACE_WARNING("robots.txt access detected\r\n");
    }

    if (isSecurityIncident)
    {
        TRACE_WARNING("Security incident detected, there is information, that you are hosting teddyCloud on a public instance\r\n");
        TRACE_WARNING("Anybody could extract your box certificates and tonies. This could render your tonies and/or box useless\r\n");
        TRACE_WARNING("Feel free to ask for help on https://forum.revvox.de or https://t.me/toniebox_reverse_engineering\r\n");
        settings->internal.security_mit.incident = true;
    }

    return isSecurityIncident;
}

error_t checkSecMitHandlers(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx)
{
    settings_t *settings = get_settings();
    if (settings->security_mit.httpsOnly && !connection->settings->isHttps)
    {
        return NO_ERROR;
    }
    if (!settings->security_mit.warnAccess && !settings->security_mit.lockAccess)
    {
        return NO_ERROR;
    }
    else if (settings->security_mit.onBlacklistDomain)
    {
        return handleSecMitDomain(connection, uri, queryString, client_ctx);
    }
    else if (settings->security_mit.onCrawler)
    {
        return handleSecMitCrawler(connection, uri, queryString, client_ctx);
    }

    return NO_ERROR;
}

error_t handleSecMitDomain(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx)
{
    settings_t *settings = get_settings();

    char *hashes[] = {
        "863a5a96d45d2e8736d1c6a6e1d8b615b6436737", // fbx
        //"b26f5cd0a1a9845fd5bb8fba7a48b0b3d011ac07"  // dev
    };
    size_t numHashes = sizeof(hashes) / sizeof(hashes[0]);
    char *host = connection->request.host;

    Sha1Context sha1Ctx;
    sha1Init(&sha1Ctx);
    sha1Update(&sha1Ctx, host, osStrlen(host));

    uint8_t *hash_data = osAllocMem(SHA1_DIGEST_SIZE);
    sha1Final(&sha1Ctx, hash_data);

    char hash[SHA1_DIGEST_SIZE * 2 + 1];
    for (size_t i = 0; i < SHA1_DIGEST_SIZE; i++)
    {
        osSprintf(&hash[i * 2], "%02" PRIx8, hash_data[i]);
    }
    hash[SHA1_DIGEST_SIZE * 2] = '\0';
    osFreeMem(hash_data);
    for (size_t i = 0; i < numHashes; i++)
    {
        if (osStrcmp(hash, hashes[i]) == 0)
        {
            settings->internal.security_mit.blacklisted_domain_access = time(NULL);
            TRACE_WARNING("Blacklisted domain access detected: %s with hash %s\r\n", host, hash);
            TRACE_WARNING("Your domain hash is on list because we found out that your teddyCloud instance is hosted in a way that anybody could access it!\r\n");
            break;
        }
    }

    return NO_ERROR;
}
error_t handleSecMitCrawler(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx)
{
    settings_t *settings = get_settings();

    char *ua = connection->request.userAgent;
    if (ua == NULL)
    {
        return NO_ERROR;
    }
    char *crawlers[] = {
        "Googlebot", "bingbot", "yandexbot", "Baiduspider", "Sogou",
        "Exabot", "ia_archiver", "facebookexternalhit", "Twitterbot",
        "LinkedInBot", "Embedly", "Quora Link Preview", "showyoubot",
        "outbrain", "pinterest", "developers.google.com",
        "Shodan", "Censys", "ZoomEye", "Masscan", "Nmap", "Nuclei", "Nessus",
        "Acunetix", "Qualys", "Nikto", "Arachni", "Burp Suite", "Netsparker",
        "w3af", "Zmap", "Digincore", "Netcraft", "SecurityTrails"};

    size_t numCrawlers = sizeof(crawlers) / sizeof(crawlers[0]);
    for (size_t i = 0; i < numCrawlers; i++)
    {
        if (osStrstr(ua, crawlers[i]) != NULL)
        {
            settings->internal.security_mit.crawler_access = time(NULL);
            TRACE_WARNING("Crawler %s detected: %s\r\n", crawlers[i], ua);
            break;
        }
    }

    return NO_ERROR;
}
error_t handleSecMitRobotsTxt(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx)
{
    settings_t *settings = get_settings();
    settings->internal.security_mit.robots_txt_access = time(NULL);

    TRACE_WARNING("robots.txt access detected with User-Agent %s\r\n", connection->request.userAgent);

    char_t *newUri = custom_asprintf("%s%s", client_ctx->settings->core.wwwdir, uri);

    error_t error = httpSendResponse(connection, newUri);
    free(newUri);

    return error;
}

error_t handleSecMitLock(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx)
{
    char *response = "TeddyCloud has been locked to mitigate security risks! Please check the logs for more information!";
    httpInitResponseHeader(connection);
    connection->response.contentType = "text/plain";
    connection->response.contentLength = osStrlen(response);

    TRACE_WARNING("TeddyCloud has been locked to mitigate security risks!\r\n");

    return httpWriteResponse(connection, (char_t *)response, connection->response.contentLength, false);
}

error_t handleSecMitWarn(HttpConnection *connection, const char_t *uri, const char_t *queryString, client_ctx_t *client_ctx)
{
    char *response = "TeddyCloud has detected security risks! Please check the logs for more information!";
    httpInitResponseHeader(connection);
    connection->response.contentType = "text/plain";
    connection->response.contentLength = osStrlen(response);

    TRACE_WARNING("TeddyCloud has detected security risks!\r\n");

    return httpWriteResponse(connection, (char_t *)response, connection->response.contentLength, false);
}