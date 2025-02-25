
#include <sys/types.h>
#include <time.h>
#include <stdbool.h>
#include <stdint.h>

#include "version.h"
#include "debug.h"
#include "settings.h"
#include "mutex_manager.h"
#include "tls_adapter.h"

#include "fs_port.h"
#include "fs_ext.h"
#include "os_ext.h"
#include "server_helpers.h"
#include "cert.h"

/* static functions*/
static void settings_init_opt(setting_item_t *opt);
static void settings_deinit_ovl(uint8_t overlayNumber);
static void overlay_settings_init();
static void settings_generate_internal_dirs(settings_t *settings);
static void settings_changed();
static error_t settings_save_ovl(bool overlay);
static error_t settings_load_ovl(bool overlay);
static setting_item_t *settings_get_by_name_id(const char *item, uint8_t settingsId);
static char *settings_sanitize_box_id(const char *input_id);

/* macros */
#define ERR_RETURN(command)    \
    do                         \
    {                          \
        error_t err = command; \
        if (err != NO_ERROR)   \
        {                      \
            return err;        \
        }                      \
    } while (0)

#define SETTINGS_LOAD_BUFFER_LEN 256
#define OVERLAY_CONFIG_PREFIX "overlay."
static settings_t Settings_Overlay[MAX_OVERLAYS];
static setting_item_t *Option_Map_Overlay[MAX_OVERLAYS];
static uint16_t settings_size = 0;
static char *config_file_path = NULL;
static char *config_overlay_file_path = NULL;
DateTime settings_last_load;
DateTime settings_last_load_ovl;

static void option_map_init(uint8_t settingsId)
{
    settings_t *settings = &Settings_Overlay[settingsId];

    OPTION_START()

    OPTION_INTERNAL_UNSIGNED("configVersion", &settings->configVersion, 0, 0, 255, "Config version", LEVEL_NONE)
    OPTION_INTERNAL_STRING("commonName", &settings->commonName, "default", "common name of the certificate (for overlays)", LEVEL_NONE)
    OPTION_INTERNAL_STRING("boxName", &settings->boxName, "Toniebox", "Name of the box", LEVEL_NONE)
    OPTION_INTERNAL_STRING("boxModel", &settings->boxModel, "", "Model of the box", LEVEL_NONE)

    OPTION_TREE_DESC("log", "Logging", LEVEL_DETAIL)
    OPTION_UNSIGNED("log.level", &settings->log.level, 4, 0, 6, "Loglevel", "0=off - 6=verbose", LEVEL_DETAIL)
    OPTION_BOOL("log.color", &settings->log.color, TRUE, "Colored log", "Colored log", LEVEL_DETAIL)
    OPTION_BOOL("log.logFullAuth", &settings->log.logFullAuth, FALSE, "Log auth", "Log full authentication of tags", LEVEL_DETAIL)

    /* settings for HTTPS server */
    OPTION_TREE_DESC("core.server", "Server ports", LEVEL_EXPERT)
    OPTION_UNSIGNED("core.server.http_port", &settings->core.http_port, 80, 1, 65535, "HTTP port", "HTTP port for the webinterface", LEVEL_EXPERT)
    OPTION_UNSIGNED("core.server.https_web_port", &settings->core.https_web_port, 8443, 1, 65535, "HTTPS Web port", "HTTPS port for the webinterface", LEVEL_EXPERT)
    OPTION_UNSIGNED("core.server.https_api_port", &settings->core.https_api_port, 443, 1, 65535, "HTTPS API port", "HTTPS port for the Toniebox API", LEVEL_EXPERT)
    OPTION_STRING("core.server.bind_ip", &settings->core.bind_ip, "", "Bind IP", "ip for binding the http ports to", LEVEL_EXPERT)

    OPTION_TREE_DESC("core.server", "HTTP server", LEVEL_BASIC)
    OPTION_STRING("core.host_url", &settings->core.host_url, "http://localhost", "Host URL", "URL to teddyCloud server", LEVEL_BASIC)
    OPTION_STRING("core.certdir", &settings->core.certdir, "certs/client", "Cert dir", "Directory to upload genuine client certificates", LEVEL_EXPERT)
    OPTION_INTERNAL_STRING("core.configdir", &settings->core.configdir, CONFIG_BASE_PATH, "Configuration dir", LEVEL_EXPERT)
    OPTION_STRING("core.contentdir", &settings->core.contentdir, "default", "Content dir", "Directory for placing cloud content", LEVEL_DETAIL)
    OPTION_STRING("core.librarydir", &settings->core.librarydir, "library", "Library dir", "Directory of the audio library", LEVEL_DETAIL)
    OPTION_STRING("core.datadir", &settings->core.datadir, "data", "Data dir", "Base directory for 'contentdir', 'firmwaredir', 'cachedir' and 'wwwdir' when they are relative", LEVEL_EXPERT)
    OPTION_INTERNAL_STRING("core.wwwdir", &settings->core.wwwdir, "www", "WWW dir", LEVEL_NONE)
    OPTION_STRING("core.firmwaredir", &settings->core.firmwaredir, "firmware", "Firmware dir", "Directory to upload original firmware", LEVEL_DETAIL)
    OPTION_STRING("core.cachedir", &settings->core.cachedir, "cache", "Cache dir", "Directory where to cache files downloaded from internet", LEVEL_DETAIL)
    OPTION_STRING("core.sslkeylogfile", &settings->core.sslkeylogfile, "", "SSL-key logfile", "SSL/TLS key log filename", LEVEL_EXPERT)
    OPTION_BOOL("core.new_webgui_as_default", &settings->core.new_webgui_as_default, TRUE, "New WebGUI", "Use new WebGUI as default", LEVEL_EXPERT)

    OPTION_TREE_DESC("core.server_cert", "HTTPS server certificates", LEVEL_EXPERT)
    OPTION_TREE_DESC("core.client_cert.file", "File certificates", LEVEL_EXPERT)
    OPTION_STRING("core.server_cert.file.ca", &settings->core.server_cert.file.ca, "certs/server/ca-root.pem", "CA certificate", "CA certificate", LEVEL_EXPERT)
    OPTION_STRING("core.server_cert.file.ca_der", &settings->core.server_cert.file.ca_der, "certs/server/ca.der", "CA certificate as DER", "CA certificate as DER", LEVEL_EXPERT)
    OPTION_STRING("core.server_cert.file.ca_key", &settings->core.server_cert.file.ca_key, "certs/server/ca-key.pem", "CA key", "CA key", LEVEL_EXPERT)
    OPTION_STRING("core.server_cert.file.crt", &settings->core.server_cert.file.crt, "certs/server/teddy-cert.pem", "Server certificate", "Server certificate", LEVEL_EXPERT)
    OPTION_STRING("core.server_cert.file.key", &settings->core.server_cert.file.key, "certs/server/teddy-key.pem", "Server key", "Server key", LEVEL_EXPERT)
    OPTION_TREE_DESC("core.server_cert.data", "Raw certificates", LEVEL_EXPERT)
    OPTION_STRING("core.server_cert.data.ca", &settings->core.server_cert.data.ca, "", "CA certificate data", "CA certificate data", LEVEL_EXPERT)
    OPTION_INTERNAL_STRING("core.server_cert.data.ca_key", &settings->core.server_cert.data.ca_key, "", "CA key data", LEVEL_EXPERT)
    OPTION_INTERNAL_STRING("core.server_cert.data.crt", &settings->core.server_cert.data.crt, "", "Server certificate data", LEVEL_EXPERT)
    OPTION_INTERNAL_STRING("core.server_cert.data.key", &settings->core.server_cert.data.key, "", "Server key data", LEVEL_EXPERT)

    /* settings for HTTPS/cloud client */
    OPTION_TREE_DESC("core.client_cert", "Cloud client certificates", LEVEL_DETAIL)
    OPTION_TREE_DESC("core.client_cert.file", "File certificates", LEVEL_DETAIL)
    OPTION_STRING("core.client_cert.file.ca", &settings->core.client_cert.file.ca, "certs/client/ca.der", "Client CA", "Client Certificate Authority", LEVEL_DETAIL)
    OPTION_STRING("core.client_cert.file.crt", &settings->core.client_cert.file.crt, "certs/client/client.der", "Client certificate", "Client certificate", LEVEL_DETAIL)
    OPTION_STRING("core.client_cert.file.key", &settings->core.client_cert.file.key, "certs/client/private.der", "Client key", "Client key", LEVEL_DETAIL)
    OPTION_TREE_DESC("core.client_cert.data", "Raw certificates", LEVEL_SECRET)
    OPTION_INTERNAL_STRING("core.client_cert.data.ca", &settings->core.client_cert.data.ca, "", "Client Certificate Authority", LEVEL_EXPERT)
    OPTION_INTERNAL_STRING("core.client_cert.data.crt", &settings->core.client_cert.data.crt, "", "Client certificate data", LEVEL_EXPERT)
    OPTION_INTERNAL_STRING("core.client_cert.data.key", &settings->core.client_cert.data.key, "", "Client key data", LEVEL_EXPERT)

    OPTION_STRING("core.allowOrigin", &settings->core.allowOrigin, "", "CORS Allow-Origin", "Set CORS Access-Control-Allow-Origin header", LEVEL_EXPERT)
    OPTION_BOOL("core.boxCertAuth", &settings->core.boxCertAuth, TRUE, "HTTPS box cert auth", "Client certificates are required for access to the HTTPS API for the boxes", LEVEL_EXPERT)
    OPTION_BOOL("core.allowNewBox", &settings->core.allowNewBox, TRUE, "Allow new boxes", "Allow new boxes to be added, if they try to connect", LEVEL_BASIC)

    OPTION_BOOL("core.flex_enabled", &settings->core.flex_enabled, TRUE, "Enable Flex-Tonie", "When enabled this UID always gets assigned the audio selected from web interface", LEVEL_DETAIL)
    OPTION_STRING("core.flex_uid", &settings->core.flex_uid, "", "Flex-Tonie UID", "UID which shall get selected audio files assigned", LEVEL_DETAIL)
    OPTION_UNSIGNED("core.settings_level", &settings->core.settings_level, 1, 1, 3, "Settings level", "1: Basic, 2: Detail, 3: Expert", LEVEL_BASIC)
    OPTION_BOOL("core.tonies_json_auto_update", &settings->core.tonies_json_auto_update, TRUE, "Auto-Update tonies.json", "Auto-Update tonies.json for Tonies information and images.", LEVEL_DETAIL)
    OPTION_BOOL("core.full_taf_validation", &settings->core.full_taf_validation, FALSE, "Full TAF validation", "Validate TAFs by checking the audio length and the SHA1 hash. (may be slow, as file needs to be fully read!)", LEVEL_EXPERT)
    OPTION_BOOL("core.tap_taf_validation", &settings->core.tap_taf_validation, FALSE, "TAP TAF validation", "Validate TAFs of TAPs by checking the audio length and the SHA1 hash. (may be slow, as file needs to be fully read!)", LEVEL_EXPERT)
    OPTION_BOOL("core.track_pos_taf_validation", &settings->core.track_pos_taf_validation, TRUE, "Track position error TAF validation", "Validate TAFs when track position error occurs by checking the audio length and the SHA1 hash. (may be slow, as file needs to be fully read!)", LEVEL_DETAIL)

    OPTION_TREE_DESC("security_mit", "Security mitigation", LEVEL_EXPERT)
    OPTION_BOOL("security_mit.warnAccess", &settings->security_mit.warnAccess, TRUE, "Warning on unwanted access", "If teddyCloud detects unusal access, warn on frontend until restart. (See on*)", LEVEL_EXPERT)
    OPTION_BOOL("security_mit.lockAccess", &settings->security_mit.lockAccess, TRUE, "Lock on unwanted access", "If teddyCloud detects a unusal access, lock frontend until restart. (See on*)", LEVEL_EXPERT)
    OPTION_BOOL("security_mit.httpsOnly", &settings->security_mit.httpsOnly, TRUE, "On HTTPS only", "Lock/Warn on HTTPS port only.", LEVEL_EXPERT)
    OPTION_BOOL("security_mit.onBlacklistDomain", &settings->security_mit.onBlacklistDomain, TRUE, "Detect blacklist domains", "Lock/Warn, if domain is known to be public.", LEVEL_EXPERT)
    OPTION_BOOL("security_mit.onCrawler", &settings->security_mit.onCrawler, TRUE, "Detect crawlers", "Lock/Warn, if crawler is detected (User-Agent).", LEVEL_EXPERT)
    // OPTION_BOOL("security_mit.onExternal", &settings->security_mit.onExternal, TRUE, "Detect external access", "Lock/Warn, if external access is detected.", LEVEL_EXPERT)
    OPTION_BOOL("security_mit.onRobotsTxt", &settings->security_mit.onRobotsTxt, TRUE, "Detect robots.txt", "Lock/Warn, if robots.txt is accessed.", LEVEL_EXPERT)
    OPTION_BOOL("security_mit.hardLock", &settings->security_mit.hardLock, FALSE, "Disable teddyCloud", "Disable teddyCloud due to ignored security warnings.", LEVEL_SECRET)

    OPTION_TREE_DESC("internal", "Internal", LEVEL_NONE)
    OPTION_INTERNAL_STRING("internal.server.ca", &settings->internal.server.ca, "", "CA certificate data", LEVEL_SECRET)
    OPTION_INTERNAL_STRING("internal.server.ca_key", &settings->internal.server.ca_key, "", "Server CA key data", LEVEL_SECRET)
    OPTION_INTERNAL_STRING("internal.server.crt", &settings->internal.server.crt, "", "Server certificate data", LEVEL_SECRET)
    OPTION_INTERNAL_STRING("internal.server.key", &settings->internal.server.key, "", "Server key data", LEVEL_SECRET)
    OPTION_INTERNAL_STRING("internal.server.cert_chain", &settings->internal.server.cert_chain, "", "TLS certificate chain", LEVEL_SECRET)
    OPTION_INTERNAL_STRING("internal.client.ca", &settings->internal.client.ca, "", "Client CA", LEVEL_SECRET)
    OPTION_INTERNAL_STRING("internal.client.crt", &settings->internal.client.crt, "", "Client certificate data", LEVEL_SECRET)
    OPTION_INTERNAL_STRING("internal.client.key", &settings->internal.client.key, "", "Client key data", LEVEL_SECRET)
    OPTION_INTERNAL_BOOL("internal.autogen_certs", &settings->internal.autogen_certs, TRUE, "Generate certificates if missing", LEVEL_NONE)

    OPTION_INTERNAL_BOOL("internal.exit", &settings->internal.exit, FALSE, "Exit the server", LEVEL_NONE)
    OPTION_INTERNAL_SIGNED("internal.returncode", &settings->internal.returncode, 0, -128, 127, "Returncode when exiting", LEVEL_NONE)
    OPTION_INTERNAL_BOOL("internal.config_init", &settings->internal.config_init, FALSE, "Config initialized?", LEVEL_NONE)
    OPTION_INTERNAL_BOOL("internal.config_used", &settings->internal.config_used, FALSE, "Config used?", LEVEL_NONE)
    OPTION_INTERNAL_BOOL("internal.config_changed", &settings->internal.config_changed, FALSE, "Config changed and unsaved?", LEVEL_NONE)
    OPTION_INTERNAL_BOOL("internal.logColorSupport", &settings->internal.logColorSupport, FALSE, "Terminal supports color (log)", LEVEL_BASIC)
    OPTION_INTERNAL_STRING("internal.basedir", &settings->internal.basedir, BASE_PATH, "basedir", LEVEL_NONE)
    OPTION_INTERNAL_STRING("internal.basedirfull", &settings->internal.basedirfull, "", "basedirfull", LEVEL_NONE)
    OPTION_INTERNAL_STRING("internal.cwd", &settings->internal.cwd, "", "current working dir (cwd)", LEVEL_NONE)
    OPTION_INTERNAL_STRING("internal.certdirfull", &settings->internal.certdirfull, "", "Directory where the certs are placed (absolute)", LEVEL_NONE)
    OPTION_INTERNAL_STRING("internal.configdirfull", &settings->internal.configdirfull, "", "Directory where the config is placed (absolute)", LEVEL_NONE)
    OPTION_INTERNAL_STRING("internal.contentdirrel", &settings->internal.contentdirrel, "", "Directory where cloud content is placed (relative)", LEVEL_NONE)
    OPTION_INTERNAL_STRING("internal.contentdirfull", &settings->internal.contentdirfull, "", "Directory where cloud content is placed (absolute)", LEVEL_NONE)
    OPTION_INTERNAL_STRING("internal.librarydirfull", &settings->internal.librarydirfull, "", "Directory of the audio library (absolute)", LEVEL_NONE)
    OPTION_INTERNAL_STRING("internal.datadirfull", &settings->internal.datadirfull, "", "Directory where data is placed (absolute)", LEVEL_NONE)
    OPTION_INTERNAL_STRING("internal.wwwdirfull", &settings->internal.wwwdirfull, "", "Directory where web content is placed (absolute)", LEVEL_NONE)
    OPTION_INTERNAL_STRING("internal.firmwaredirfull", &settings->internal.firmwaredirfull, "", "Directory where firmwares are placed (absolute)", LEVEL_NONE)
    OPTION_INTERNAL_STRING("internal.cachedirfull", &settings->internal.cachedirfull, "", "Directory where cached files are placed (absolute)", LEVEL_NONE)
    OPTION_INTERNAL_STRING("internal.overlayUniqueId", &settings->internal.overlayUniqueId, "", "Unique Id of the overlay", LEVEL_NONE)
    OPTION_INTERNAL_UNSIGNED("internal.overlayNumber", &settings->internal.overlayNumber, 0, 0, MAX_OVERLAYS, "Id of the overlay", LEVEL_NONE)
    OPTION_INTERNAL_STRING("internal.assign_unknown", &settings->internal.assign_unknown, "", "TAF file to assign to the next unknown tag", LEVEL_NONE)

    OPTION_INTERNAL_UNSIGNED("internal.rtnl.lastEarId", &settings->internal.rtnl.lastEarId, EAR_NONE, EAR_BIG, EAR_NONE, "Id of the last pressed id", LEVEL_NONE)
    OPTION_INTERNAL_UNSIGNED("internal.rtnl.lastEarpress", &settings->internal.rtnl.lastEarpress, 0, 0, UINT64_MAX, "Timestamp of the last pressed ear", LEVEL_NONE)
    OPTION_INTERNAL_BOOL("internal.rtnl.wasDoubleEarpress", &settings->internal.rtnl.wasDoubleEarpress, FALSE, "Was double Earpress?", LEVEL_NONE)
    OPTION_INTERNAL_UNSIGNED("internal.rtnl.multipressTime", &settings->internal.rtnl.multipressTime, 300, 0, UINT16_MAX, "Multipress time", LEVEL_NONE)
    OPTION_INTERNAL_STRING("internal.rtnl.prodDomain", &settings->internal.rtnl.prodDomain, "", "prod domain sent by the box", LEVEL_NONE)
    OPTION_INTERNAL_STRING("internal.rtnl.rtnlDomain", &settings->internal.rtnl.rtnlDomain, "", "rtnl domain sent by the box", LEVEL_NONE)

    OPTION_INTERNAL_STRING("internal.version.id", &settings->internal.version.id, "", "Version id", LEVEL_NONE)
    OPTION_INTERNAL_STRING("internal.version.git_sha_short", &settings->internal.version.git_sha_short, "", "Short Git SHA-1 hash of the build version", LEVEL_NONE)
    OPTION_INTERNAL_STRING("internal.version.git_sha", &settings->internal.version.git_sha, "", "Full Git SHA-1 hash of the build version", LEVEL_NONE)
    OPTION_INTERNAL_BOOL("internal.version.dirty", &settings->internal.version.dirty, FALSE, "Indicates if the build was made from a modified (dirty) git tree", LEVEL_NONE)
    OPTION_INTERNAL_STRING("internal.version.datetime", &settings->internal.version.datetime, "", "Datetime of the build or git commit", LEVEL_NONE)
    OPTION_INTERNAL_STRING("internal.version.platform", &settings->internal.version.platform, "", "Platform on which the software was built", LEVEL_NONE)
    OPTION_INTERNAL_STRING("internal.version.os", &settings->internal.version.os, "", "Operating System used for the build", LEVEL_NONE)
    OPTION_INTERNAL_STRING("internal.version.architecture", &settings->internal.version.architecture, "", "System architecture for the build", LEVEL_NONE)
    OPTION_INTERNAL_STRING("internal.version.v_short", &settings->internal.version.v_short, "", "Concise version descriptor", LEVEL_NONE)
    OPTION_INTERNAL_STRING("internal.version.v_long", &settings->internal.version.v_long, "", "Detailed version descriptor", LEVEL_NONE)
    OPTION_INTERNAL_STRING("internal.version.v_full", &settings->internal.version.v_full, "", "Complete version descriptor with all details", LEVEL_NONE)

    OPTION_INTERNAL_STRING("internal.version_web.id", &settings->internal.version_web.id, "", "Web version id", LEVEL_NONE)
    OPTION_INTERNAL_STRING("internal.version_web.git_sha_short", &settings->internal.version_web.git_sha_short, "", "Short Git SHA-1 hash of the web version", LEVEL_NONE)
    OPTION_INTERNAL_STRING("internal.version_web.git_sha", &settings->internal.version_web.git_sha, "", "Full Git SHA-1 hash of the web version", LEVEL_NONE)
    OPTION_INTERNAL_BOOL("internal.version_web.dirty", &settings->internal.version_web.dirty, FALSE, "Indicates if the web was made from a modified (dirty) git tree", LEVEL_NONE)
    OPTION_INTERNAL_STRING("internal.version_web.datetime", &settings->internal.version_web.datetime, "", "Datetime of the web or git commit", LEVEL_NONE)
    OPTION_INTERNAL_STRING("internal.version_web.v_short", &settings->internal.version_web.v_short, "", "Concise web version descriptor", LEVEL_NONE)
    OPTION_INTERNAL_STRING("internal.version_web.v_long", &settings->internal.version_web.v_long, "", "Detailed web version descriptor", LEVEL_NONE)
    OPTION_INTERNAL_STRING("internal.version_web.v_full", &settings->internal.version_web.v_full, "", "Complete web version descriptor with all details", LEVEL_NONE)

    OPTION_INTERNAL_UNSIGNED("internal.toniebox_firmware.boxIC", &settings->internal.toniebox_firmware.boxIC, 0, 0, UINT64_MAX, "Box IC from User Agent", LEVEL_NONE)
    OPTION_INTERNAL_UNSIGNED("internal.toniebox_firmware.uaVersionFirmware", &settings->internal.toniebox_firmware.uaVersionFirmware, 0, 0, UINT64_MAX, "Firmware version from User Agent", LEVEL_NONE)
    OPTION_INTERNAL_UNSIGNED("internal.toniebox_firmware.uaVersionServicePack", &settings->internal.toniebox_firmware.uaVersionServicePack, 0, 0, UINT64_MAX, "Service Pack version from User Agent", LEVEL_NONE)
    OPTION_INTERNAL_UNSIGNED("internal.toniebox_firmware.uaVersionHardware", &settings->internal.toniebox_firmware.uaVersionHardware, 0, 0, UINT64_MAX, "Hardware version from User Agent", LEVEL_NONE)
    OPTION_INTERNAL_STRING("internal.toniebox_firmware.uaEsp32Firmware", &settings->internal.toniebox_firmware.uaEsp32Firmware, "", "Firmware version from User Agent (esp32)", LEVEL_NONE)
    OPTION_INTERNAL_STRING("internal.toniebox_firmware.rtnlVersion", &settings->internal.toniebox_firmware.rtnlVersion, "", "Firmware version from RTNL", LEVEL_NONE)
    OPTION_INTERNAL_STRING("internal.toniebox_firmware.rtnlFullVersion", &settings->internal.toniebox_firmware.rtnlFullVersion, "", "Firmware full version from RTNL", LEVEL_NONE)
    OPTION_INTERNAL_STRING("internal.toniebox_firmware.rtnlDetail", &settings->internal.toniebox_firmware.rtnlDetail, "", "Firmware detail information", LEVEL_NONE)
    OPTION_INTERNAL_STRING("internal.toniebox_firmware.rtnlRegion", &settings->internal.toniebox_firmware.rtnlRegion, "", "Firmware region", LEVEL_NONE)
    OPTION_INTERNAL_UNSIGNED("internal.toniebox_firmware.otaVersionSfx", &settings->internal.toniebox_firmware.otaVersionSfx, 0, 0, UINT64_MAX, " ota version", LEVEL_NONE)
    OPTION_INTERNAL_UNSIGNED("internal.toniebox_firmware.otaVersionServicePack", &settings->internal.toniebox_firmware.otaVersionServicePack, 0, 0, UINT64_MAX, " ota version", LEVEL_NONE)
    OPTION_INTERNAL_UNSIGNED("internal.toniebox_firmware.otaVersionHtml", &settings->internal.toniebox_firmware.otaVersionHtml, 0, 0, UINT64_MAX, "Html ota version", LEVEL_NONE)
    OPTION_INTERNAL_UNSIGNED("internal.toniebox_firmware.otaVersionEu", &settings->internal.toniebox_firmware.otaVersionEu, 0, 0, UINT64_MAX, "Firmware EU ota version", LEVEL_NONE)
    OPTION_INTERNAL_UNSIGNED("internal.toniebox_firmware.otaVersionPd", &settings->internal.toniebox_firmware.otaVersionPd, 0, 0, UINT64_MAX, "Firmware PD ota version", LEVEL_NONE)

    OPTION_INTERNAL_U64_ARRAY("internal.freshnessCache", &settings->internal.freshnessCache, 0, "Cache for freshnessCheck", LEVEL_NONE)

    OPTION_INTERNAL_UNSIGNED("internal.last_connection", &settings->internal.last_connection, 0, 0, UINT64_MAX, "Last connection timestamp", LEVEL_NONE)
    OPTION_INTERNAL_STRING("internal.last_ruid", &settings->internal.last_ruid, "ffffffffffffffff", "Last rUID", LEVEL_NONE)
    OPTION_INTERNAL_UNSIGNED("internal.last_ruid_time", &settings->internal.last_ruid_time, 0, 0, UINT64_MAX, "Last rUID (unixtime)", LEVEL_NONE)
    OPTION_INTERNAL_STRING("internal.ip", &settings->internal.ip, "", "IP", LEVEL_NONE)
    OPTION_INTERNAL_BOOL("internal.online", &settings->internal.online, FALSE, "Check if box is online", LEVEL_NONE)

    OPTION_INTERNAL_BOOL("internal.security_mit.incident", &settings->internal.security_mit.incident, FALSE, "We had a security incident", LEVEL_NONE)
    OPTION_INTERNAL_UNSIGNED("internal.security_mit.blacklisted_domain_access", &settings->internal.security_mit.blacklisted_domain_access, 0, 0, 0, "Check accessed via blacklisted domain", LEVEL_NONE)
    OPTION_INTERNAL_UNSIGNED("internal.security_mit.crawler_access", &settings->internal.security_mit.crawler_access, 0, 0, UINT64_MAX, "Last access via crawler", LEVEL_NONE)
    OPTION_INTERNAL_UNSIGNED("internal.security_mit.external_access", &settings->internal.security_mit.external_access, 0, 0, UINT64_MAX, "Last external access", LEVEL_NONE)
    OPTION_INTERNAL_UNSIGNED("internal.security_mit.robots_txt_access", &settings->internal.security_mit.robots_txt_access, 0, 0, UINT64_MAX, "Last access onto the robots.txt", LEVEL_NONE)

    OPTION_TREE_DESC("cloud", "Cloud", LEVEL_BASIC)
    OPTION_BOOL("cloud.enabled", &settings->cloud.enabled, FALSE, "Cloud enabled", "Generally enable cloud operation", LEVEL_BASIC)
    OPTION_STRING("cloud.remote_hostname", &settings->cloud.remote_hostname, "prod.de.tbs.toys", "Cloud hostname", "Hostname of remote cloud server", LEVEL_EXPERT)
    OPTION_UNSIGNED("cloud.remote_port", &settings->cloud.remote_port, 443, 1, 65535, "Cloud port", "Port of remote cloud server", LEVEL_EXPERT)
    OPTION_BOOL("cloud.enableV1Claim", &settings->cloud.enableV1Claim, TRUE, "Forward 'claim'", "Forward 'claim' queries to claim tonies in the household in the tonies cloud", LEVEL_BASIC)
    OPTION_BOOL("cloud.enableV1CloudReset", &settings->cloud.enableV1CloudReset, FALSE, "Forward 'cloudReset'", "Forward 'cloudReset' queries to tonies cloud", LEVEL_DETAIL)
    OPTION_BOOL("cloud.enableV1FreshnessCheck", &settings->cloud.enableV1FreshnessCheck, TRUE, "Forward 'freshnessCheck'", "Forward 'freshnessCheck' queries to mark new content as updated to tonies cloud", LEVEL_DETAIL)
    OPTION_BOOL("cloud.enableV1Log", &settings->cloud.enableV1Log, FALSE, "Forward 'log'", "Forward 'log' queries to tonies cloud", LEVEL_EXPERT)
    OPTION_BOOL("cloud.enableV1Time", &settings->cloud.enableV1Time, FALSE, "Forward 'time'", "Forward 'time' queries to tonies cloud", LEVEL_EXPERT)
    OPTION_BOOL("cloud.enableV1Ota", &settings->cloud.enableV1Ota, FALSE, "Forward 'ota'", "Forward 'ota' queries to tonies cloud", LEVEL_EXPERT)
    OPTION_BOOL("cloud.enableV2Content", &settings->cloud.enableV2Content, TRUE, "Forward 'content'", "Forward 'content' queries to download content from the tonies cloud", LEVEL_BASIC)
    OPTION_BOOL("cloud.cacheOta", &settings->cloud.cacheOta, TRUE, "Cache OTA", "Cache OTA files in firmware dir of local server (this still blocks OTA if local OTA delivery is disabled)", LEVEL_EXPERT)
    OPTION_BOOL("cloud.localOta", &settings->cloud.localOta, FALSE, "Local OTA delivery", "Send local OTA files in firmware dir", LEVEL_EXPERT)
    OPTION_BOOL("cloud.cacheContent", &settings->cloud.cacheContent, TRUE, "Cache content", "Cache cloud content on local server", LEVEL_DETAIL)
    OPTION_BOOL("cloud.cacheToLibrary", &settings->cloud.cacheToLibrary, TRUE, "Cache to library", "Cache cloud content to library", LEVEL_DETAIL)
    OPTION_BOOL("cloud.markCustomTagByPass", &settings->cloud.markCustomTagByPass, TRUE, "Autodetect custom tags", "Automatically mark custom tags by password", LEVEL_EXPERT)
    OPTION_BOOL("cloud.prioCustomContent", &settings->cloud.prioCustomContent, TRUE, "Prioritize custom content", "Prioritize custom content over tonies content (force update, only if \"Update content on lower audio id\" is disabled)", LEVEL_EXPERT)
    OPTION_BOOL("cloud.updateOnLowerAudioId", &settings->cloud.updateOnLowerAudioId, TRUE, "Update content on lower audio id", "Update content on a lower audio id", LEVEL_EXPERT)
    OPTION_BOOL("cloud.dumpRuidAuthContentJson", &settings->cloud.dumpRuidAuthContentJson, TRUE, "Dump rUID/auth", "Dump the rUID and authentication into the content JSON.", LEVEL_EXPERT)

    OPTION_TREE_DESC("encode", "TAF encoding", LEVEL_EXPERT)
    OPTION_UNSIGNED("encode.bitrate", &settings->encode.bitrate, 96, 0, 256, "Opus bitrate", "Opus bitrate, tested 64, 96(default), 128, 192, 256 - be aware that this increases the TAF size!", LEVEL_EXPERT)
    OPTION_UNSIGNED("encode.ffmpeg_stream_buffer_ms", &settings->encode.ffmpeg_stream_buffer_ms, 2000, 0, 60000, "Stream buffer ms", "Stream buffer for ffmpeg based streaming.", LEVEL_EXPERT)
    OPTION_BOOL("encode.ffmpeg_stream_restart", &settings->encode.ffmpeg_stream_restart, FALSE, "Stream force restart", "If a stream is continued by the box, a new file is forced. This has the cost of a slower restart, but does not play the old buffered content and deletes the previous stream data on the box.", LEVEL_EXPERT)
    OPTION_BOOL("encode.ffmpeg_sweep_startup_buffer", &settings->encode.ffmpeg_sweep_startup_buffer, TRUE, "Sweep stream prebuffer", "Webradio streams often send several seconds as a buffer immediately. This may contain ads and will add up if you disalbe 'Stream force restart'.", LEVEL_EXPERT)
    OPTION_UNSIGNED("encode.ffmpeg_sweep_delay_ms", &settings->encode.ffmpeg_sweep_delay_ms, 2000, 0, 10000, "Sweep delay ms", "Wait x ms until sweeping is stopped and stream is started. Delays stream start, but may increase success.", LEVEL_EXPERT)
    OPTION_UNSIGNED("encode.stream_max_size", &settings->encode.stream_max_size, 1024 * 1024 * 40 * 6 - 1, 1024 * 1024 - 1, INT32_MAX, "Max stream filesize", "The box may create an empty file this length for each stream. So if you have 10 streaming tonies you use, the box may block 10*240MB. The only downside is, that the box will stop after the file is full and you'll need to replace the tag onto the box. Must not be a multiply of 4096, Default: 251.658.239, so 240MB, which means around 6h.", LEVEL_EXPERT)

    OPTION_TREE_DESC("frontend", "Frontend", LEVEL_BASIC)
    OPTION_BOOL("frontend.split_model_content", &settings->frontend.split_model_content, TRUE, "Split content / model", "If enabled, the content of the TAF will be shown beside the model of the figurine", LEVEL_DETAIL)
    OPTION_BOOL("frontend.ignore_web_version_mismatch", &settings->frontend.ignore_web_version_mismatch, FALSE, "Ignore web version mismatch", "Ignore web version mismatch and don't show the mismatch warning", LEVEL_EXPERT)
    OPTION_BOOL("frontend.confirm_audioplayer_close", &settings->frontend.confirm_audioplayer_close, TRUE, "Confirm audioplayer close", "Confirm dialog when closing the audioplayer during active playback", LEVEL_BASIC)

    OPTION_TREE_DESC("toniebox", "Toniebox", LEVEL_BASIC)
    OPTION_BOOL("toniebox.api_access", &settings->toniebox.api_access, TRUE, "API access", "Grant access to the API (default value for new boxes)", LEVEL_EXPERT)
    OPTION_BOOL("toniebox.overrideCloud", &settings->toniebox.overrideCloud, TRUE, "Override cloud settings", "Override tonies cloud settings for the toniebox with those set here", LEVEL_BASIC)
    OPTION_UNSIGNED("toniebox.max_vol_spk", &settings->toniebox.max_vol_spk, 3, 0, 3, "Limit speaker volume", "0=25%, 1=50%, 2=75%, 3=100%", LEVEL_BASIC)
    OPTION_UNSIGNED("toniebox.max_vol_hdp", &settings->toniebox.max_vol_hdp, 3, 0, 3, "Limit headphone volume", "0=25%, 1=50%, 2=75%, 3=100%", LEVEL_BASIC)
    OPTION_BOOL("toniebox.slap_enabled", &settings->toniebox.slap_enabled, TRUE, "Slap to skip", "Enable track skip via slapping gesture", LEVEL_BASIC)
    OPTION_BOOL("toniebox.slap_back_left", &settings->toniebox.slap_back_left, FALSE, "Slap direction", "Determine slap direction for skipping track: False for left-backward, True for left-forward", LEVEL_BASIC)
    OPTION_UNSIGNED("toniebox.led", &settings->toniebox.led, 0, 0, 2, "LED brightness", "0=on, 1=off, 2=dimmed", LEVEL_BASIC)
    OPTION_BOOL("toniebox.overrideFields", &settings->toniebox.overrideFields, FALSE, "Override unknown fields", "Override unknown fields 2 + 6", LEVEL_EXPERT)
    OPTION_UNSIGNED("toniebox.field2", &settings->toniebox.field2, 7, 0, UINT32_MAX, "Field 2", "Unknown Toniebox setting, default 7", LEVEL_EXPERT)
    OPTION_UNSIGNED("toniebox.field6", &settings->toniebox.field6, 1, 0, UINT32_MAX, "Field 6", "Unknown Toniebox setting, default 1", LEVEL_EXPERT)

    OPTION_TREE_DESC("rtnl", "RTNL log", LEVEL_EXPERT)
    OPTION_BOOL("rtnl.logRaw", &settings->rtnl.logRaw, FALSE, "Log RTNL (bin)", "Enable logging for raw RTNL data", LEVEL_EXPERT)
    OPTION_BOOL("rtnl.logHuman", &settings->rtnl.logHuman, FALSE, "Log RTNL (csv)", "Enable logging for human-readable RTNL data", LEVEL_EXPERT)
    OPTION_STRING("rtnl.logRawFile", &settings->rtnl.logRawFile, "config/rtnl.bin", "RTNL bin file", "Specify the filepath for raw RTNL log", LEVEL_EXPERT)
    OPTION_STRING("rtnl.logHumanFile", &settings->rtnl.logHumanFile, "config/rtnl.csv", "RTNL csv file", "Specify the filepath for human-readable RTNL log", LEVEL_EXPERT)

    OPTION_TREE_DESC("pcap", "libpcap packet log", LEVEL_EXPERT)
    OPTION_BOOL("pcap.enabled", &settings->pcap.enabled, FALSE, "Log HTTP(S) traffic", "Enable logging for HTTP(S) traffic into a .pcap file (needs restart)", LEVEL_EXPERT)
    OPTION_STRING("pcap.filename", &settings->pcap.filename, "config/traffic.pcap", "libpcap log file", "Specify the filepath for libpcap log", LEVEL_EXPERT)

    OPTION_TREE_DESC("mqtt", "MQTT", LEVEL_DETAIL)
    OPTION_BOOL("mqtt.enabled", &settings->mqtt.enabled, FALSE, "Enable MQTT", "Enable MQTT client", LEVEL_DETAIL)
    OPTION_STRING("mqtt.hostname", &settings->mqtt.hostname, "", "MQTT hostname", "IP or Hostname of the MQTT server", LEVEL_DETAIL)
    OPTION_UNSIGNED("mqtt.port", &settings->mqtt.port, 1883, 1, 65535, "MQTT port", "Port of MQTT server", LEVEL_DETAIL)
    OPTION_STRING("mqtt.username", &settings->mqtt.username, "", "Username", "MQTT Username", LEVEL_DETAIL)
    OPTION_STRING("mqtt.password", &settings->mqtt.password, "", "Password", "MQTT Password", LEVEL_DETAIL)
    OPTION_STRING("mqtt.identification", &settings->mqtt.identification, "", "Client identification", "Client identification", LEVEL_DETAIL)
    OPTION_STRING("mqtt.topic", &settings->mqtt.topic, "teddyCloud", "Topic prefix", "Topic prefix", LEVEL_DETAIL)
    OPTION_UNSIGNED("mqtt.qosLevel", &settings->mqtt.qosLevel, 0, 0, 2, "QoS level", "QoS level", LEVEL_DETAIL)
    OPTION_BOOL("mqtt.retain_will", &settings->mqtt.retain_will, TRUE, "Retain last will", "Retain last will message", LEVEL_DETAIL)
    OPTION_BOOL("mqtt.disable_on_error", &settings->mqtt.disable_on_error, FALSE, "Disable MQTT on error", "Disable MQTT if there were to many errors", LEVEL_DETAIL)

    OPTION_TREE_DESC("hass", "Home Assistant", LEVEL_DETAIL)
    OPTION_STRING("hass.name", &settings->hass.name, "teddyCloud - Server", "Home Assistant name", "Home Assistant name", LEVEL_DETAIL)
    OPTION_STRING("hass.id", &settings->hass.id, "teddyCloud_Server", "Unique ID", "Unique ID to identify this device", LEVEL_DETAIL)

    OPTION_TREE_DESC("tonie_json", "Tonie JSON", LEVEL_DETAIL)
    OPTION_BOOL("tonie_json.cache_images", &settings->tonie_json.cache_images, FALSE, "Cache images", "Cache figurine images locally", LEVEL_DETAIL)
    OPTION_BOOL("tonie_json.cache_preload", &settings->tonie_json.cache_preload, FALSE, "Preload all images", "Download all figurine images on startup. This will take several minutes the first time you start TeddyCloud.", LEVEL_DETAIL)

    OPTION_TREE_DESC("debug", "Debug", LEVEL_EXPERT)
    OPTION_BOOL("debug.web.pcm_encode_console_url", &settings->debug.web.pcm_encode_console_url, FALSE, "PCM Console URL", "Caches the PCM of the browser-side encoding and prints a download link to the browser console.", LEVEL_EXPERT)
    OPTION_END()

    settings_size = sizeof(option_map_array) / sizeof(option_map_array[0]) - 1;

    if (Option_Map_Overlay[settingsId] == NULL)
    {
        Option_Map_Overlay[settingsId] = osAllocMem(sizeof(option_map_array));
    }

    osMemcpy(Option_Map_Overlay[settingsId], option_map_array, sizeof(option_map_array));
}

static setting_item_t *get_option_map(const char *overlay)
{
    return Option_Map_Overlay[get_overlay_id(overlay)];
}

void overlay_settings_init_opt(setting_item_t *opt, setting_item_t *opt_src)
{
    if (opt == opt_src)
    {
        settings_init_opt(opt);
    }
    else
    {
        switch (opt->type)
        {
        case TYPE_BOOL:
            *((bool *)opt->ptr) = *((bool *)opt_src->ptr);
            break;
        case TYPE_SIGNED:
        case TYPE_UNSIGNED:
        case TYPE_HEX:
        case TYPE_FLOAT:
            *((uint32_t *)opt->ptr) = *((uint32_t *)opt_src->ptr);
            break;
        case TYPE_STRING:
            *((char **)opt->ptr) = strdup(*((char **)opt_src->ptr));
            break;
        case TYPE_U64_ARRAY:
            if (opt_src->size > 0)
            {
                *((uint64_t **)opt->ptr) = osAllocMem(sizeof(uint64_t *) * opt_src->size);
                osMemcpy(*((uint64_t **)opt->ptr), *((uint64_t **)opt_src->ptr), sizeof(uint64_t *) * opt_src->size);
            }
            break;
        default:
            break;
        }
        opt->overlayed = false;
    }
}

static void overlay_settings_init_field(int field, uint8_t overlay)
{
    setting_item_t *option_map = Option_Map_Overlay[overlay];
    setting_item_t *option_map_src = Option_Map_Overlay[0];

    setting_item_t *opt = &option_map[field];
    setting_item_t *opt_src = &option_map_src[field];

    overlay_settings_init_opt(opt, opt_src);
}

static void overlay_settings_init()
{
    for (uint8_t i = 1; i < MAX_OVERLAYS; i++)
    {
        settings_deinit_ovl(i);

        option_map_init(i);

        int field = 0;
        while (Option_Map_Overlay[i][field].type != TYPE_END)
        {
            overlay_settings_init_field(field, i);
            field++;
        }
        Settings_Overlay[i].internal.overlayNumber = i;
        Settings_Overlay[i].internal.config_init = true;
        Settings_Overlay[i].internal.config_used = false;
    }
}

settings_t *get_settings()
{
    return get_settings_id(0);
}

settings_t *get_settings_ovl(const char *overlay_unique_id)
{
    return get_settings_id(get_overlay_id(overlay_unique_id));
}

settings_t *get_settings_id(uint8_t settingsId)
{
    return &Settings_Overlay[settingsId];
}

settings_t *get_settings_cn(const char *commonName)
{
    mutex_lock(MUTEX_SETTINGS);
    if (commonName != NULL && osStrcmp(commonName, "") != 0)
    {
        for (size_t i = 1; i < MAX_OVERLAYS; i++)
        {
            if (osStrcmp(Settings_Overlay[i].commonName, commonName) == 0)
            {
                mutex_unlock(MUTEX_SETTINGS);
                return &Settings_Overlay[i];
            }
        }

        for (size_t i = 1; i < MAX_OVERLAYS; i++)
        {
            if (!Settings_Overlay[i].internal.config_used)
            {
                char *boxId = settings_sanitize_box_id((const char *)commonName);
                char *boxPrefix = "teddyCloud Box ";
                char *boxName = custom_asprintf("%s%s", boxPrefix, commonName);

                settings_set_string_id("commonName", boxId, i);
                settings_set_string_id("internal.overlayUniqueId", boxId, i);
                settings_set_string_id("boxName", boxName, i);
                settings_set_string_id("boxModel", "", i);
                settings_get_by_name_id("toniebox.api_access", i)->overlayed = true;
                settings_get_by_name_id("core.certdir", i)->overlayed = true;
                settings_get_by_name_id("core.client_cert.file.ca", i)->overlayed = true;
                settings_get_by_name_id("core.client_cert.file.crt", i)->overlayed = true;
                settings_get_by_name_id("core.client_cert.file.key", i)->overlayed = true;

                const char *certDir = settings_get_string_id("core.certdir", i);
                osStringToLower(boxId);
                char *customCertDir = custom_asprintf("%s%c%s", certDir, PATH_SEPARATOR, boxId);
                char *ca = custom_asprintf("%s%c%s", customCertDir, PATH_SEPARATOR, "ca.der");
                char *crt = custom_asprintf("%s%c%s", customCertDir, PATH_SEPARATOR, "client.der");
                char *key = custom_asprintf("%s%c%s", customCertDir, PATH_SEPARATOR, "private.der");
                settings_set_string_id("core.certdir", customCertDir, i);
                settings_set_string_id("core.client_cert.file.ca", ca, i);
                settings_set_string_id("core.client_cert.file.crt", crt, i);
                settings_set_string_id("core.client_cert.file.key", key, i);
                osFreeMem(customCertDir);
                osFreeMem(ca);
                osFreeMem(crt);
                osFreeMem(key);

                Settings_Overlay[i].internal.config_used = true;
                settings_save_ovl(true);
                mutex_unlock(MUTEX_SETTINGS);

                osFreeMem(boxId);
                osFreeMem(boxName);
                return &Settings_Overlay[i];
            }
        }

        TRACE_WARNING("Could not create new overlay for unknown client %s, to many overlays.\r\n", commonName);
    }
    mutex_unlock(MUTEX_SETTINGS);
    return get_settings();
}

uint8_t get_overlay_id(const char *overlay_unique_id)
{
    if (overlay_unique_id == NULL || osStrlen(overlay_unique_id) == 0)
    {
        return 0;
    }

    for (uint8_t i = 1; i < MAX_OVERLAYS; i++)
    {
        if (osStrcmp(Settings_Overlay[i].internal.overlayUniqueId, overlay_unique_id) == 0)
        {
            return i;
        }
    }
    return 0;
}

void settings_resolve_dir(char **resolvedPath, char *path, char *basePath)
{
    if (!resolvedPath || !*resolvedPath || !path)
    {
        return;
    }

    if (path[0] == PATH_SEPARATOR_LINUX || (osStrlen(path) > 1 && path[1] == ':' && path[2] == PATH_SEPARATOR_WINDOWS))
    {
        snprintf(*resolvedPath, 255, "%s", path);
    }
    else
    {
        if (!basePath)
        {
            return;
        }
        if (path[0] == '\0')
        {
            snprintf(*resolvedPath, 255, "%s", basePath);
        }
        else
        {
            snprintf(*resolvedPath, 255, "%s%c%s", basePath, PATH_SEPARATOR, path);
        }
    }
    fsFixPath(*resolvedPath);
}

static void settings_generate_internal_dirs(settings_t *settings)
{
    osFreeMem(settings->internal.basedirfull);
    osFreeMem(settings->internal.certdirfull);
    osFreeMem(settings->internal.configdirfull);
    osFreeMem(settings->internal.contentdirrel);
    osFreeMem(settings->internal.contentdirfull);
    osFreeMem(settings->internal.librarydirfull);
    osFreeMem(settings->internal.datadirfull);
    osFreeMem(settings->internal.wwwdirfull);
    osFreeMem(settings->internal.firmwaredirfull);
    osFreeMem(settings->internal.cachedirfull);

    settings->internal.basedirfull = osAllocMem(256);
    settings->internal.certdirfull = osAllocMem(256);
    settings->internal.configdirfull = osAllocMem(256);
    settings->internal.contentdirrel = osAllocMem(256);
    settings->internal.contentdirfull = osAllocMem(256);
    settings->internal.librarydirfull = osAllocMem(256);
    settings->internal.datadirfull = osAllocMem(256);
    settings->internal.wwwdirfull = osAllocMem(256);
    settings->internal.firmwaredirfull = osAllocMem(256);
    settings->internal.cachedirfull = osAllocMem(256);

    char *tmpPath = osAllocMem(256);
    settings_resolve_dir(&settings->internal.basedirfull, settings->internal.basedir, settings->internal.cwd);

    settings_resolve_dir(&settings->internal.certdirfull, settings->core.certdir, settings->internal.basedirfull);
    settings_resolve_dir(&settings->internal.datadirfull, settings->core.datadir, settings->internal.basedirfull);
    settings_resolve_dir(&settings->internal.configdirfull, settings->core.configdir, settings->internal.basedirfull);

    settings_resolve_dir(&settings->internal.wwwdirfull, settings->core.wwwdir, settings->internal.datadirfull);
    settings_resolve_dir(&settings->internal.firmwaredirfull, settings->core.firmwaredir, settings->internal.datadirfull);
    settings_resolve_dir(&settings->internal.cachedirfull, settings->core.cachedir, settings->internal.datadirfull);

    settings_resolve_dir(&tmpPath, settings->core.contentdir, "content");
    settings_resolve_dir(&settings->internal.contentdirrel, tmpPath, settings->core.datadir);
    settings_resolve_dir(&settings->internal.contentdirfull, tmpPath, settings->internal.datadirfull);
    fsCreateDir(settings->internal.contentdirfull);

    settings_resolve_dir(&settings->internal.librarydirfull, settings->core.librarydir, settings->internal.datadirfull);

    osFreeMem(tmpPath);
}

static void settings_changed()
{
    settings_changed_id(0);
}

void settings_changed_id(uint8_t settingsId)
{
    mutex_lock(MUTEX_SETTINGS);

    Settings_Overlay[settingsId].internal.config_changed = true;
    settings_generate_internal_dirs(get_settings_id((settingsId)));
    if (config_file_path != NULL)
    {
        osFreeMem(config_file_path);
    }
    if (config_overlay_file_path != NULL)
    {
        osFreeMem(config_overlay_file_path);
    }
    config_file_path = custom_asprintf("%s%c%s", settings_get_string("internal.configdirfull"), PATH_SEPARATOR, CONFIG_FILE);
    config_overlay_file_path = custom_asprintf("%s%c%s", settings_get_string("internal.configdirfull"), PATH_SEPARATOR, CONFIG_OVERLAY_FILE);

    if (settingsId == 0)
    {
        settings_load_ovl(true);
    }

    mutex_unlock(MUTEX_SETTINGS);
}

static void settings_deinit_ovl(uint8_t overlayNumber)
{
    if (overlayNumber >= MAX_OVERLAYS)
    {
        return;
    }

    if (!Settings_Overlay[overlayNumber].internal.config_init)
    {
        return;
    }

    setting_item_t *option_map = Option_Map_Overlay[overlayNumber];
    if (option_map == NULL)
    {
        return;
    }

    int pos = 0;
    while (option_map[pos].type != TYPE_END)
    {
        setting_item_t *opt = &option_map[pos];
        opt->overlayed = false;

        switch (opt->type)
        {
        case TYPE_STRING:
            if (*((char **)opt->ptr))
            {
                osFreeMem(*((char **)opt->ptr));
                *((char **)opt->ptr) = NULL;
            }
            break;
        case TYPE_U64_ARRAY:
            if (opt->size > 0)
            {
                osFreeMem(*((uint64_t **)opt->ptr));
                opt->size = 0;
            }
            break;
        default:
            break;
        }
        pos++;
    }
    Settings_Overlay[overlayNumber].internal.config_init = false;

    if (overlayNumber == 0)
    {
        osFreeMem(config_file_path);
        config_file_path = NULL;
        osFreeMem(config_overlay_file_path);
        config_overlay_file_path = NULL;
    }

    osFreeMem(Option_Map_Overlay[overlayNumber]);
    Option_Map_Overlay[overlayNumber] = NULL;
}

void settings_deinit()
{
    for (uint8_t i = 0; i < MAX_OVERLAYS; i++)
    {
        settings_deinit_ovl(i);
    }
}

static void settings_init_opt(setting_item_t *opt)
{
    switch (opt->type)
    {
    case TYPE_BOOL:
        TRACE_DEBUG("  %s = %s\r\n", opt->option_name, opt->init.bool_value ? "true" : "false");
        *((bool *)opt->ptr) = opt->init.bool_value;
        break;
    case TYPE_SIGNED:
        TRACE_DEBUG("  %s = %d\r\n", opt->option_name, opt->init.signed_value);
        *((uint32_t *)opt->ptr) = opt->init.signed_value;
        break;
    case TYPE_UNSIGNED:
    case TYPE_HEX:
        TRACE_DEBUG("  %s = %" PRIu64 "\r\n", opt->option_name, opt->init.unsigned_value);
        *((uint32_t *)opt->ptr) = opt->init.unsigned_value;
        break;
    case TYPE_FLOAT:
        TRACE_DEBUG("  %s = %f\r\n", opt->option_name, opt->init.float_value);
        *((uint32_t *)opt->ptr) = opt->init.float_value;
        break;
    case TYPE_STRING:
        TRACE_DEBUG("  %s = %s\r\n", opt->option_name, opt->init.string_value);
        *((char **)opt->ptr) = strdup(opt->init.string_value);
        break;
    case TYPE_U64_ARRAY:
        TRACE_DEBUG("  %s = size(%zu)\r\n", opt->option_name, opt->size);
        if (opt->size > 0)
        {
            *((uint64_t **)opt->ptr) = osAllocMem(sizeof(uint64_t *) * opt->size);
        }
        break;
    default:
        break;
    }
}

error_t settings_init(const char *cwd, const char *base_dir)
{
    bool autogen_certs = Settings_Overlay[0].internal.autogen_certs;
    option_map_init(0);

    Settings_Overlay[0].log.level = LOGLEVEL_INFO;

    int pos = 0;
    setting_item_t *option_map = get_option_map(NULL);
    while (option_map[pos].type != TYPE_END)
    {
        setting_item_t *opt = &option_map[pos];
        settings_init_opt(opt);
        pos++;
    }
    settings_set_string("internal.cwd", cwd);
    settings_set_string("internal.basedir", base_dir);

    settings_set_string("internal.version.id", BUILD_VERSION);
    settings_set_string("internal.version.git_sha_short", BUILD_GIT_SHORT_SHA);
    settings_set_string("internal.version.git_sha", BUILD_GIT_SHA);
    settings_set_bool("internal.version.id", BUILD_GIT_IS_DIRTY);
    settings_set_string("internal.version.datetime", BUILD_DATETIME);
    settings_set_string("internal.version.platform", BUILD_PLATFORM);
    settings_set_string("internal.version.os", BUILD_OS);
    settings_set_string("internal.version.architecture", BUILD_ARCH);
    settings_set_string("internal.version.v_short", BUILD_FULL_NAME_SHORT);
    settings_set_string("internal.version.v_long", BUILD_FULL_NAME_LONG);
    settings_set_string("internal.version.v_full", BUILD_FULL_NAME_FULL);

    settings_set_string("internal.version_web.id", WEB_VERSION);
    settings_set_string("internal.version_web.git_sha_short", WEB_GIT_SHORT_SHA);
    settings_set_string("internal.version_web.git_sha", WEB_GIT_SHA);
    settings_set_bool("internal.version_web.id", WEB_GIT_IS_DIRTY);
    settings_set_string("internal.version_web.datetime", WEB_DATETIME);
    settings_set_string("internal.version_web.v_short", WEB_FULL_NAME_SHORT);
    settings_set_string("internal.version_web.v_long", WEB_FULL_NAME_LONG);
    settings_set_string("internal.version_web.v_full", WEB_FULL_NAME_FULL);

    settings_set_bool("internal.logColorSupport", supportsAnsiColors());
    settings_set_bool("internal.autogen_certs", autogen_certs);

    Settings_Overlay[0].internal.config_init = true;
    Settings_Overlay[0].internal.config_used = true;

    settings_changed();

    return settings_load();
}

error_t settings_save()
{
    mutex_lock(MUTEX_SETTINGS);
    error_t err = NO_ERROR;

    err = settings_save_ovl(false);
    if (err == NO_ERROR)
    {
        err = settings_save_ovl(true);
    }
    mutex_unlock(MUTEX_SETTINGS);

    return err;
}

static error_t settings_save_ovl(bool overlay)
{
    char_t *config_path = (!overlay ? config_file_path : config_overlay_file_path);

    TRACE_INFO("Save settings to %s\r\n", config_path);
    FsFile *file = fsOpenFile(config_path, FS_FILE_MODE_WRITE | FS_FILE_MODE_TRUNC);
    if (file == NULL)
    {
        TRACE_ERROR("Failed to open config file for writing\r\n");
        return ERROR_DIRECTORY_NOT_FOUND;
    }

    for (size_t i = 0; i < MAX_OVERLAYS; i++)
    {
        int pos = 0;
        char *buffer = NULL;

        if (i == 0 && overlay)
        {
            i++;
        }
        else if (i > 0 && !overlay)
        {
            break;
        }
        if (!Settings_Overlay[i].internal.config_used)
        {
            continue;
        }
        Settings_Overlay[i].configVersion = CONFIG_VERSION;

        setting_item_t *option_map = Option_Map_Overlay[i];
        while (option_map[pos].type != TYPE_END)
        {
            setting_item_t *opt = &option_map[pos];
            if (!opt->internal || !osStrcmp(opt->option_name, "configVersion") || (overlay && (!osStrcmp(opt->option_name, "commonName") || !osStrcmp(opt->option_name, "boxName") || !osStrcmp(opt->option_name, "boxModel"))))
            {
                char *overlayPrefix;
                if (overlay)
                {
                    if (!opt->overlayed)
                    {
                        pos++;
                        continue; // Only write overlay settings if they were overlayed
                    }
                    overlayPrefix = custom_asprintf("overlay.%s.", Settings_Overlay[i].internal.overlayUniqueId);
                }
                else
                {
                    overlayPrefix = custom_asprintf("");
                }

                switch (opt->type)
                {
                case TYPE_BOOL:
                    buffer = custom_asprintf("%s%s=%s\n", overlayPrefix, opt->option_name, *((bool *)opt->ptr) ? "true" : "false");
                    break;
                case TYPE_SIGNED:
                    buffer = custom_asprintf("%s%s=%d\n", overlayPrefix, opt->option_name, *((int32_t *)opt->ptr));
                    break;
                case TYPE_UNSIGNED:
                case TYPE_HEX:
                    buffer = custom_asprintf("%s%s=%u\n", overlayPrefix, opt->option_name, *((uint32_t *)opt->ptr));
                    break;
                case TYPE_FLOAT:
                    buffer = custom_asprintf("%s%s=%f\n", overlayPrefix, opt->option_name, *((float *)opt->ptr));
                    break;
                case TYPE_STRING:
                    buffer = custom_asprintf("%s%s=%s\n", overlayPrefix, opt->option_name, *((char **)opt->ptr));
                    break;
                default:
                    buffer = custom_asprintf("");
                    break;
                }
                if (buffer && osStrlen(buffer) > 0)
                {
                    fsWriteFile(file, buffer, osStrlen(buffer));
                    osFreeMem(buffer);
                }
                osFreeMem(overlayPrefix);
            }
            pos++;
        }
    }
    fsCloseFile(file);
    Settings_Overlay[0].internal.config_changed = false;

    return NO_ERROR;
}

error_t settings_load()
{
    mutex_lock(MUTEX_SETTINGS);
    error_t err = NO_ERROR;

    err = settings_load_ovl(false);
    if (err == NO_ERROR)
    {
        err = settings_load_ovl(true);
    }
    mutex_unlock(MUTEX_SETTINGS);
    return err;
}

static error_t settings_load_ovl(bool overlay)
{
    char_t *config_path = (!overlay ? config_file_path : config_overlay_file_path);

    TRACE_INFO("Load settings from %s\r\n", config_path);

    if (overlay)
    {
        overlay_settings_init();
    }
    if (!fsFileExists(config_path))
    {
        TRACE_WARNING("Config file does not exist, creating it...\r\n");

        error_t err = settings_save_ovl(overlay);
        return err;
    }

    uint32_t file_size;
    error_t result = fsGetFileSize(config_path, &file_size);
    if (result != NO_ERROR)
    {
        TRACE_WARNING("Failed to get config file size\r\n");
        return ERROR_ABORTED;
    }

    FsFile *file = fsOpenFile(config_path, FS_FILE_MODE_READ);
    if (file == NULL)
    {
        TRACE_WARNING("Failed to open config file for reading\r\n");
        return ERROR_ABORTED;
    }

    // Buffer to hold the file content
    char buffer[SETTINGS_LOAD_BUFFER_LEN];
    size_t from_read;
    size_t read_length;
    bool last_line_incomplete = false;
    char *line;
    from_read = 0;
    while (fsReadFile(file, &buffer[from_read], sizeof(buffer) - from_read - 1, &read_length) == NO_ERROR || last_line_incomplete)
    {
        read_length = from_read + read_length;
        buffer[read_length] = '\0';

        // Process each line in the buffer
        line = buffer;
        char *next_line;

        while ((next_line = strchr(line, '\n')) != NULL)
        {
            *next_line = '\0'; // Terminate the line at the newline character

            // Skip empty lines or lines starting with a comment character '#'
            if (*line != '\0' && *line != '#')
            {
                // Split the line into option_name and value
                char *option_name = strtok(line, "=");
                char *value_str = &line[osStrlen(option_name) + 1];

                char *overlay_unique_id = NULL;
                char *tokenizer = NULL;
                if (overlay && osStrncmp(OVERLAY_CONFIG_PREFIX, option_name, osStrlen(OVERLAY_CONFIG_PREFIX)) == 0)
                {
                    option_name += osStrlen(OVERLAY_CONFIG_PREFIX);
                    tokenizer = strdup(option_name);
                    overlay_unique_id = settings_sanitize_box_id((const char *)strtok(tokenizer, "."));
                    option_name += osStrlen(overlay_unique_id) + 1;

                    if (get_overlay_id(overlay_unique_id) == 0)
                    {
                        for (size_t i = 1; i < MAX_OVERLAYS; i++)
                        {
                            if (!Settings_Overlay[i].internal.config_used)
                            {
                                settings_set_string_id("internal.overlayUniqueId", overlay_unique_id, i);
                                Settings_Overlay[i].internal.config_used = true;
                                break;
                            }
                        }
                    }
                }

                if (option_name != NULL && value_str != NULL)
                {
                    // Find the corresponding setting item
                    setting_item_t *opt = settings_get_by_name_ovl(option_name, overlay_unique_id);
                    if (opt != NULL)
                    {
                        // Update the setting value based on the type
                        if (overlay)
                        {
                            opt->overlayed = true;
                        }
                        switch (opt->type)
                        {
                        case TYPE_BOOL:
                            if (strcmp(value_str, "true") == 0)
                                *((bool *)opt->ptr) = true;
                            else if (strcmp(value_str, "false") == 0)
                                *((bool *)opt->ptr) = false;
                            else
                                TRACE_WARNING("Invalid boolean value '%s' for setting '%s'\r\n", value_str, option_name);
                            TRACE_DEBUG("%s=%s\r\n", opt->option_name, *((bool *)opt->ptr) ? "true" : "false");
                            break;
                        case TYPE_SIGNED:
                            *((int32_t *)opt->ptr) = atoi(value_str);
                            TRACE_DEBUG("%s=%d\r\n", opt->option_name, *((int32_t *)opt->ptr));
                            break;
                        case TYPE_UNSIGNED:
                        case TYPE_HEX:
                            *((uint32_t *)opt->ptr) = strtoul(value_str, NULL, 10);
                            TRACE_DEBUG("%s=%u\r\n", opt->option_name, *((uint32_t *)opt->ptr));
                            break;
                        case TYPE_FLOAT:
                            *((float *)opt->ptr) = strtof(value_str, NULL);
                            TRACE_DEBUG("%s=%f\r\n", opt->option_name, *((float *)opt->ptr));
                            break;
                        case TYPE_STRING:
                            osFreeMem(*((char **)opt->ptr));
                            *((char **)opt->ptr) = strdup(value_str);
                            TRACE_DEBUG("%s=%s\r\n", opt->option_name, *((char **)opt->ptr));
                            break;

                        default:
                            opt->overlayed = false;
                            break;
                        }
                    }
                    else
                    {
                        TRACE_WARNING("Setting item '%s' not found\r\n", option_name);
                    }
                }
                osFreeMem(overlay_unique_id);
                osFreeMem(tokenizer);
            }

            line = next_line + 1; // Move to the next line
        }

        if (last_line_incomplete && read_length == 0)
            break;

        // Check if the last line is incomplete (does not end with a newline character)
        last_line_incomplete = (buffer[read_length - 1] != '\n');
        if (last_line_incomplete)
        {
            if (line == buffer)
            {
                if (read_length == SETTINGS_LOAD_BUFFER_LEN - 1)
                {
                    TRACE_ERROR("Cannot read config file, line too big for buffer, skipping line %s\r\n", line);
                    while (fsReadFile(file, &buffer[0], 1, &read_length) == NO_ERROR)
                    {
                        if (line[0] == '\n')
                        {
                            break;
                        }
                    }
                    from_read = 0;
                    read_length = 0;
                }
                else
                {
                    TRACE_WARNING("Last line of config is missing a newline %s\r\n", line);
                    from_read++;
                    read_length++;
                    line[read_length - 1] = '\n';
                }
            }
            else
            {
                from_read = strlen(line);
                memmove(buffer, line, from_read);
            }
        }
        else
        {
            from_read = 0;
        }
    }
    fsCloseFile(file);
    if (overlay)
    {
        for (uint8_t i = 1; i < MAX_OVERLAYS; i++)
        {
            settings_generate_internal_dirs(&Settings_Overlay[i]);
            settings_load_certs_id(i);
            Settings_Overlay[i].internal.config_changed = false;
        }
    }
    else
    {
        settings_generate_internal_dirs(get_settings());
        settings_load_certs_id(0);

        if (Settings_Overlay[0].configVersion < CONFIG_VERSION)
        {
            for (size_t i = 0; i < MAX_OVERLAYS; i++)
            {
                if (!Settings_Overlay[i].internal.config_used)
                    continue;
                if (Settings_Overlay[i].configVersion < 12)
                {
                    Settings_Overlay[i].toniebox.api_access = true;
                    settings_get_by_name_id("toniebox.api_access", i)->overlayed = true;
                }
            }
            mutex_unlock(MUTEX_SETTINGS);
            settings_save();
            mutex_lock(MUTEX_SETTINGS);
        }
        Settings_Overlay[0].internal.config_changed = false;
    }

    FsFileStat stat;
    if (fsGetFileStat(config_path, &stat) == NO_ERROR)
    {
        if (overlay)
        {
            settings_last_load_ovl = stat.modified;
        }
        else
        {
            settings_last_load = stat.modified;
        }
    }

    return NO_ERROR;
}

uint16_t settings_get_size()
{
    return settings_size;
}

setting_item_t *settings_get(int index)
{
    return settings_get_ovl(index, NULL);
}

setting_item_t *settings_get_ovl(int index, const char *overlay_name)
{
    if (index < settings_get_size())
        return &get_option_map(overlay_name)[index];
    TRACE_WARNING("Setting item #%d not found\r\n", index);
    return NULL;
}

setting_item_t *settings_get_by_name(const char *item)
{
    return settings_get_by_name_ovl(item, NULL);
}

setting_item_t *settings_get_by_name_ovl(const char *item, const char *overlay_name)
{
    return settings_get_by_name_id(item, get_overlay_id(overlay_name));
}

static setting_item_t *settings_get_by_name_id(const char *item, uint8_t settingsId)
{
    int pos = 0;
    setting_item_t *option_map = Option_Map_Overlay[settingsId];
    if (!option_map)
    {
        TRACE_ERROR("Overlay %d not found\r\n", settingsId);
        return NULL;
    }
    while (option_map[pos].type != TYPE_END)
    {
        if (!strcmp(item, option_map[pos].option_name))
        {
            return &option_map[pos];
        }
        pos++;
    }
    TRACE_WARNING("Setting item '%s' not found\r\n", item);
    return NULL;
}

bool settings_get_bool(const char *item)
{
    return settings_get_bool_ovl(item, NULL);
}

bool settings_get_bool_ovl(const char *item, const char *overlay_name)
{
    if (!item)
    {
        return false;
    }

    setting_item_t *opt = settings_get_by_name_ovl(item, overlay_name);
    if (!opt || opt->type != TYPE_BOOL)
    {
        return false;
    }

    return *((bool *)opt->ptr);
}

bool settings_set_bool(const char *item, bool value)
{
    return settings_set_bool_ovl(item, value, NULL);
}
bool settings_set_bool_ovl(const char *item, bool value, const char *overlay_name)
{
    return settings_set_bool_id(item, value, get_overlay_id(overlay_name));
}
bool settings_set_bool_id(const char *item, bool value, uint8_t settingsId)
{
    if (!item)
    {
        return false;
    }

    setting_item_t *opt = settings_get_by_name_id(item, settingsId);
    if (!opt || opt->type != TYPE_BOOL)
    {
        return false;
    }

    *((bool *)opt->ptr) = value;

    if (settingsId > 0)
    {
        opt->overlayed = true;
    }
    else if (!opt->internal)
    {
        settings_changed_id(settingsId);
    }
    return true;
}

int32_t settings_get_signed(const char *item)
{
    return settings_get_signed_ovl(item, NULL);
}

int32_t settings_get_signed_ovl(const char *item, const char *overlay_name)
{
    if (!item)
    {
        return 0;
    }

    setting_item_t *opt = settings_get_by_name_ovl(item, overlay_name);
    if (!opt || opt->type != TYPE_SIGNED)
    {
        return 0;
    }

    return *((int32_t *)opt->ptr);
}

bool settings_set_signed(const char *item, int32_t value)
{
    return settings_set_signed_ovl(item, value, NULL);
}
bool settings_set_signed_ovl(const char *item, int32_t value, const char *overlay_name)
{
    return settings_set_signed_id(item, value, get_overlay_id(overlay_name));
}
bool settings_set_signed_id(const char *item, int32_t value, uint8_t settingsId)
{
    if (!item)
    {
        return false;
    }

    setting_item_t *opt = settings_get_by_name_id(item, settingsId);
    if (!opt || opt->type != TYPE_SIGNED)
    {
        return false;
    }

    if (value < opt->min.signed_value || value > opt->max.signed_value)
    {
        TRACE_ERROR("  %s = %d out of bounds\r\n", opt->option_name, value);
        return false;
    }

    *((int32_t *)opt->ptr) = value;

    if (settingsId > 0)
    {
        opt->overlayed = true;
    }
    else if (!opt->internal)
    {
        settings_changed_id(settingsId);
    }
    return true;
}

uint32_t settings_get_unsigned(const char *item)
{
    return settings_get_unsigned_ovl(item, NULL);
}
uint32_t settings_get_unsigned_ovl(const char *item, const char *overlay_name)
{
    if (!item)
    {
        return 0;
    }

    setting_item_t *opt = settings_get_by_name_ovl(item, overlay_name);
    if (!opt || opt->type != TYPE_UNSIGNED)
    {
        return 0;
    }

    return *((uint32_t *)opt->ptr);
}

bool settings_set_unsigned(const char *item, uint32_t value)
{
    return settings_set_unsigned_ovl(item, value, NULL);
}
bool settings_set_unsigned_ovl(const char *item, uint32_t value, const char *overlay_name)
{
    return settings_set_unsigned_id(item, value, get_overlay_id(overlay_name));
}
bool settings_set_unsigned_id(const char *item, uint32_t value, uint8_t settingsId)
{
    if (!item)
    {
        return false;
    }

    setting_item_t *opt = settings_get_by_name_id(item, settingsId);
    if (!opt || opt->type != TYPE_UNSIGNED)
    {
        return false;
    }

    if (value < opt->min.unsigned_value || value > opt->max.unsigned_value)
    {
        TRACE_ERROR("  %s = %u out of bounds\r\n", opt->option_name, value);
        return false;
    }

    *((uint32_t *)opt->ptr) = value;

    if (settingsId > 0)
    {
        opt->overlayed = true;
    }
    else if (!opt->internal)
    {
        settings_changed_id(settingsId);
    }
    return true;
}

float settings_get_float(const char *item)
{
    return settings_get_float_ovl(item, NULL);
}
float settings_get_float_ovl(const char *item, const char *overlay_name)
{
    if (!item)
    {
        return 0;
    }

    setting_item_t *opt = settings_get_by_name_ovl(item, overlay_name);
    if (!opt || opt->type != TYPE_FLOAT)
    {
        return 0;
    }

    return *((float *)opt->ptr);
}

bool settings_set_float(const char *item, float value)
{
    return settings_set_float_ovl(item, value, NULL);
}
bool settings_set_float_ovl(const char *item, float value, const char *overlay_name)
{
    return settings_set_float_id(item, value, get_overlay_id(overlay_name));
}
bool settings_set_float_id(const char *item, float value, uint8_t settingsId)
{
    if (!item)
    {
        return false;
    }

    setting_item_t *opt = settings_get_by_name_id(item, settingsId);
    if (!opt || opt->type != TYPE_FLOAT)
    {
        return false;
    }

    if (value < opt->min.float_value || value > opt->max.float_value)
    {
        TRACE_ERROR("  %s = %f out of bounds\r\n", opt->option_name, value);
        return false;
    }

    *((float *)opt->ptr) = value;

    if (settingsId > 0)
    {
        opt->overlayed = true;
    }
    else if (!opt->internal)
    {
        settings_changed_id(settingsId);
    }
    return true;
}

const char *settings_get_string(const char *item)
{
    return settings_get_string_id(item, 0);
}
const char *settings_get_string_ovl(const char *item, const char *overlay_name)
{
    return settings_get_string_id(item, get_overlay_id(overlay_name));
}
const char *settings_get_string_id(const char *item, uint8_t settingsId)
{
    if (!item)
    {
        return NULL;
    }

    setting_item_t *opt = settings_get_by_name_id(item, settingsId);
    if (!opt || opt->type != TYPE_STRING)
    {
        return NULL;
    }

    return *(const char **)opt->ptr;
}

bool settings_set_string(const char *item, const char *value)
{
    return settings_set_string_id(item, value, 0);
}
bool settings_set_string_ovl(const char *item, const char *value, const char *overlay_name)
{
    return settings_set_string_id(item, value, get_overlay_id(overlay_name));
}
bool settings_set_string_id(const char *item, const char *value, uint8_t settingsId)
{
    if (!item || !value)
    {
        return false;
    }

    setting_item_t *opt = settings_get_by_name_id(item, settingsId);
    if (!opt || opt->type != TYPE_STRING)
    {
        return false;
    }

    char **ptr = (char **)opt->ptr;
    char *old_ptr = *ptr;

    *ptr = strdup(value);

    if (settingsId > 0)
    {
        opt->overlayed = true;
    }
    else if (!opt->internal)
    {
        settings_changed_id(settingsId);
    }

    if (old_ptr)
    {
        osFreeMem(old_ptr);
    }

    return true;
}

uint64_t *settings_get_u64_array(const char *item, size_t *len)
{
    return settings_get_u64_array_id(item, 0, len);
}
uint64_t *settings_get_u64_array_ovl(const char *item, const char *overlay_name, size_t *len)
{
    return settings_get_u64_array_id(item, get_overlay_id(overlay_name), len);
}
uint64_t *settings_get_u64_array_id(const char *item, uint8_t settingsId, size_t *len)
{
    if (!item)
    {
        return NULL;
    }

    setting_item_t *opt = settings_get_by_name_id(item, settingsId);
    if (!opt || opt->type != TYPE_U64_ARRAY)
    {
        return NULL;
    }

    *len = opt->size;
    return *(uint64_t **)opt->ptr;
}

bool settings_set_u64_array(const char *item, const uint64_t *value, size_t len)
{
    return settings_set_u64_array_id(item, value, len, 0);
}
bool settings_set_u64_array_ovl(const char *item, const uint64_t *value, size_t len, const char *overlay_name)
{
    return settings_set_u64_array_id(item, value, len, get_overlay_id(overlay_name));
}
bool settings_set_u64_array_id(const char *item, const uint64_t *value, size_t len, uint8_t settingsId)
{
    if (!item || !value)
    {
        return false;
    }

    setting_item_t *opt = settings_get_by_name_id(item, settingsId);
    if (!opt || opt->type != TYPE_U64_ARRAY)
    {
        return false;
    }

    uint64_t **ptr = (uint64_t **)opt->ptr;
    if (*ptr)
    {
        if (opt->size > 0)
        {
            opt->size = 0;
            osFreeMem(*ptr);
        }
    }

    *ptr = osAllocMem(sizeof(uint64_t) * len);
    if (*ptr)
    {
        osMemcpy(*ptr, value, sizeof(uint64_t) * len);
    }
    opt->size = len;

    if (settingsId > 0)
    {
        opt->overlayed = true;
    }
    else if (!opt->internal)
    {
        settings_changed_id(settingsId);
    }
    return true;
}

void settings_loop()
{
    FsFileStat stat;
    if (fsGetFileStat(config_file_path, &stat) == NO_ERROR)
    {
        if (compareDateTime(&stat.modified, &settings_last_load))
        {
            TRACE_INFO("Settings file changed. Reloading.\r\n");
            settings_load();
        }
    }
    if (fsGetFileStat(config_overlay_file_path, &stat) == NO_ERROR)
    {
        if (compareDateTime(&stat.modified, &settings_last_load_ovl))
        {
            TRACE_INFO("Overlay settings file changed. Reloading.\r\n");
            settings_load();
        }
    }
}

static char *settings_sanitize_box_id(const char *input_id)
{
    char *new_str = osAllocMem(osStrlen(input_id) + 1);
    if (new_str == NULL)
    {
        return NULL;
    }

    char *dst = new_str;
    const char *src = input_id;
    while (*src)
    {
        if (isalnum((unsigned char)*src) || *src == '_' || *src == '-')
        {
            *dst++ = *src;
        }
        src++;
    }
    *dst = '\0'; // null terminate the string

    return new_str;
}

bool settings_set_by_string(const char *item, const char *value)
{
    return settings_set_by_string_id(item, value, 0);
}
bool settings_set_by_string_ovl(const char *item, const char *value, const char *overlay_name)
{
    return settings_set_by_string_id(item, value, get_overlay_id(overlay_name));
}
bool settings_set_by_string_id(const char *item, const char *value, uint8_t settingsId)
{
    bool success = false;
    setting_item_t *opt = settings_get_by_name_id(item, settingsId);
    if (opt == NULL)
    {
        TRACE_ERROR("Settings: %s not found\r\n", item);
        return ERROR_NOT_FOUND;
    }
    else if (opt)
    {
        switch (opt->type)
        {
        case TYPE_BOOL:
        {
            success = settings_set_bool_id(item, !strcasecmp(value, "true"), settingsId);
            break;
        }
        case TYPE_STRING:
        {
            success = settings_set_string_id(item, value, settingsId);
            break;
        }
        case TYPE_HEX:
        {
            uint32_t data = strtoul(value, NULL, 16);
            success = settings_set_unsigned_id(item, data, settingsId);
            break;
        }

        case TYPE_UNSIGNED:
        {
            uint32_t data = strtoul(value, NULL, 10);
            success = settings_set_unsigned_id(item, data, settingsId);
            break;
        }

        case TYPE_SIGNED:
        {
            int32_t data = strtol(value, NULL, 10);
            success = settings_set_signed_id(item, data, settingsId);
            break;
        }

        case TYPE_FLOAT:
        {
            float data = strtof(value, NULL);
            success = settings_set_float_id(item, data, settingsId);
            break;
        }

        default:
            break;
        }
    }
    else
    {
        TRACE_WARNING("Setting: '%s' cannot be set to '%s'\r\n", item, value);
    }

    return success;
}

/* unused? */
void settings_load_all_certs()
{
    for (size_t id = 0; id < MAX_OVERLAYS; id++)
    {
        settings_load_certs_id(id);
    }
}

error_t settings_try_load_certs_id(uint8_t settingsId)
{
    ERR_RETURN(load_cert("internal.server.ca", "core.server_cert.file.ca", "core.server_cert.data.ca", settingsId));
    ERR_RETURN(load_cert("internal.server.ca_key", "core.server_cert.file.ca_key", "core.server_cert.data.ca_key", settingsId));
    ERR_RETURN(load_cert("internal.server.crt", "core.server_cert.file.crt", "core.server_cert.data.crt", settingsId));
    ERR_RETURN(load_cert("internal.server.key", "core.server_cert.file.key", "core.server_cert.data.key", settingsId));

    /* do not fail when client-role certs are missing */
    load_cert("internal.client.ca", "core.client_cert.file.ca", "core.client_cert.data.ca", settingsId);
    load_cert("internal.client.crt", "core.client_cert.file.crt", "core.client_cert.data.crt", settingsId);
    load_cert("internal.client.key", "core.client_cert.file.key", "core.client_cert.data.key", settingsId);

    test_boxine_ca(settingsId);

    const char *server_crt = settings_get_string("internal.server.crt");
    const char *server_ca_crt = settings_get_string("internal.server.ca");

    char *chain = custom_asprintf("%s%s", server_crt, server_ca_crt);
    settings_set_string_id("internal.server.cert_chain", chain, settingsId);
    osFreeMem(chain);
    return NO_ERROR;
}

error_t settings_load_certs_id(uint8_t settingsId)
{
    if (!get_settings_id(settingsId)->internal.config_used)
    {
        return NO_ERROR;
    }

    if (get_settings_id(settingsId)->internal.autogen_certs && settings_try_load_certs_id(settingsId) != NO_ERROR)
    {
        TRACE_INFO("********************************************\r\n");
        TRACE_INFO("   No certificates found. Generating.\r\n");
        TRACE_INFO("   This will take several minutes...\r\n");
        TRACE_INFO("********************************************\r\n");
        cert_generate_default();
        TRACE_INFO("********************************************\r\n");
        TRACE_INFO("   FINISHED\r\n");
        TRACE_INFO("********************************************\r\n");
    }

    return NO_ERROR;
}

bool test_boxine_ca(uint8_t settingsId)
{
    const char *client_ca_crt = settings_get_string_id("internal.client.ca", settingsId);

    size_t boxine_ca_length = 2008;
    size_t ca_length = osStrlen(client_ca_crt);
    if (ca_length > 0)
    {
        if (ca_length != boxine_ca_length)
        {
            TRACE_WARNING("Client CA length mismatch %" PRIuSIZE " expected %" PRIuSIZE "\r\n", ca_length, boxine_ca_length);
            return false;
        }
        else
        {
            if (osStrstr(client_ca_crt, "MC0JveGluZSBHbW") == NULL   // Boxine GmbH
                || osStrstr(client_ca_crt, "DAlCb3hpbmUgQ") == NULL) // Boxine
            {
                TRACE_WARNING("Client CA does not match Boxine\r\n");
                return false;
            }
        }
        return true;
    }
    return false;
}
