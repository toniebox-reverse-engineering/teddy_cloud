#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "debug.h"
#include "error.h"

#ifndef SETTINGS_H
#define SETTINGS_H

#ifndef BASE_PATH
#define BASE_PATH ""
#endif

#ifndef CONFIG_BASE_PATH
#define CONFIG_BASE_PATH "config"
#endif

#define TONIES_JSON_FILE "tonies.json"
#define TONIESV2_JSON_FILE "toniesV2.json"
#define TONIES_JSON_TMP_FILE TONIES_JSON_FILE ".tmp"
#define TONIES_CUSTOM_JSON_FILE "tonies.custom.json"
#define TONIESV2_CUSTOM_JSON_FILE "tonies.custom.json"
#define TONIEBOX_JSON_FILE "tonieboxes.json"
#define TONIEBOX_CUSTOM_JSON_FILE "tonieboxes.custom.json"
#define CONFIG_FILE "config.ini"
#define CONFIG_OVERLAY_FILE "config.overlay.ini"
#define CONFIG_VERSION 13
#define MAX_OVERLAYS 16 + 1

typedef enum
{
    LOGLEVEL_OFF = 0,
    LOGLEVEL_FATAL = 1,
    LOGLEVEL_ERROR = 2,
    LOGLEVEL_WARNING = 3,
    LOGLEVEL_INFO = 4,
    LOGLEVEL_DEBUG = 5,
    LOGLEVEL_VERBOSE = 6
} settings_loglevel;

typedef enum
{
    EAR_NONE = 0,
    EAR_SMALL = 1,
    EAR_BIG = 2,
} settings_earid;

typedef enum
{
    BOX_UNKNOWN = 0,
    BOX_CC3200 = 1,
    BOX_CC3235 = 2,
    BOX_ESP32 = 3,
} settings_box_type;

typedef enum
{
    LEVEL_NONE = 0,
    LEVEL_BASIC = 1,
    LEVEL_DETAIL = 2,
    LEVEL_EXPERT = 3,
    LEVEL_SECRET = 99,
} settings_level;

typedef struct
{
    bool enabled;
    char *remote_hostname;
    uint32_t remote_port;
    bool enableV1Claim;
    bool enableV1CloudReset;
    bool enableV1FreshnessCheck;
    bool enableV1Log;
    bool enableV1Time;
    bool enableV1Ota;
    bool enableV2Content;
    bool cacheOta;
    bool localOta;
    bool cacheContent;
    bool cacheToLibrary;
    bool markCustomTagByPass;
    bool prioCustomContent;
    bool updateOnLowerAudioId;
    bool dumpRuidAuthContentJson;
} settings_cloud_t;

typedef struct
{
    uint32_t bitrate;
    uint32_t ffmpeg_stream_buffer_ms;
    bool ffmpeg_stream_restart;
    bool ffmpeg_sweep_startup_buffer;
    uint32_t ffmpeg_sweep_delay_ms;
    uint32_t stream_max_size;

} settings_encode_t;

typedef struct
{
    bool split_model_content;
    bool ignore_web_version_mismatch;
    bool confirm_audioplayer_close;
} settings_frontend_t;

typedef struct
{
    bool enabled;
    char *hostname;
    uint32_t port;
    char *username;
    char *password;
    char *identification;
    char *topic;
    uint32_t qosLevel;
    bool retain_will;
    bool disable_on_error;
} settings_mqtt_t;

typedef struct
{
    char *name;
    char *id;
} settings_hass_t;

typedef struct
{
    bool api_access;
    bool overrideCloud;
    uint32_t max_vol_spk;
    uint32_t max_vol_hdp;
    bool slap_enabled;
    bool slap_back_left;
    uint32_t led;
    bool overrideFields;
    uint32_t field2;
    uint32_t field6;
} settings_toniebox_t;

typedef struct
{
    char *ca;
    char *ca_der;
    char *ca_key;
    char *crt;
    char *key;
    char *cert_chain;
} settings_cert_t;

typedef struct
{
    char *id;
    char *git_sha_short;
    char *git_sha;
    bool dirty;
    char *datetime;
    char *platform;
    char *os;
    char *architecture;
    char *v_short;
    char *v_long;
    char *v_full;
} settings_version_t;

typedef struct
{
    char *id;
    char *git_sha_short;
    char *git_sha;
    bool dirty;
    char *datetime;
    char *v_short;
    char *v_long;
    char *v_full;
} settings_version_web_t;

typedef struct
{
    settings_earid lastEarId;
    uint64_t lastEarpress;
    bool wasDoubleEarpress;
    uint32_t multipressTime;

    char *prodDomain;
    char *rtnlDomain;
} settings_internal_rtnl_t;

typedef struct
{
    settings_box_type boxIC;

    time_t uaVersionFirmware;
    time_t uaVersionServicePack;
    time_t uaVersionHardware;
    char *uaEsp32Firmware;

    char *rtnlVersion;
    char *rtnlFullVersion;
    char *rtnlDetail;
    char *rtnlRegion;

    uint32_t otaVersionSfx;
    uint32_t otaVersionServicePack;
    uint32_t otaVersionHtml;
    uint32_t otaVersionEu;
    uint32_t otaVersionPd;

} settings_internal_toniebox_firmware_t;

typedef struct
{
    bool incident;
    time_t blacklisted_domain_access;
    time_t crawler_access;
    time_t external_access;
    time_t robots_txt_access;

} settings_internal_security_mit_t;

typedef struct
{
    bool exit;
    int32_t returncode;
    settings_cert_t server;
    settings_cert_t client;
    bool config_init;
    bool config_used;
    bool config_changed;
    bool autogen_certs;
    bool logColorSupport;

    char *basedir;
    char *basedirfull;
    char *cwd;
    char *certdirfull;
    char *configdirfull;
    char *contentdirrel;
    char *contentdirfull;
    char *librarydirfull;
    char *datadirfull;
    char *wwwdirfull;
    char *firmwaredirfull;
    char *cachedirfull;

    char *overlayUniqueId;
    uint8_t overlayNumber;
    char *assign_unknown;

    settings_internal_rtnl_t rtnl;
    settings_version_t version;
    settings_version_web_t version_web;
    settings_internal_toniebox_firmware_t toniebox_firmware;
    settings_internal_security_mit_t security_mit;

    uint64_t *freshnessCache;

    time_t last_connection;
    char *last_ruid;
    time_t *last_ruid_time;
    char *ip;
    bool online;
} settings_internal_t;

typedef struct
{
    settings_cert_t file;
    settings_cert_t data;
} settings_cert_opt_t;

typedef struct
{
    uint32_t http_port;
    uint32_t https_web_port;
    uint32_t https_api_port;
    char *host_url;
    char *certdir;
    char *configdir;
    char *contentdir;
    char *firmwaredir;
    char *cachedir;
    char *librarydir;
    char *datadir;
    char *wwwdir;
    char *sslkeylogfile;
    settings_cert_opt_t server_cert;
    settings_cert_opt_t client_cert;
    char *allowOrigin;
    bool boxCertAuth;
    bool allowNewBox;

    bool flex_enabled;
    char *flex_uid;
    char *bind_ip;

    bool new_webgui_as_default;

    settings_level settings_level;

    bool tonies_json_auto_update;
    bool full_taf_validation;
    bool tap_taf_validation;
    bool track_pos_taf_validation;
} settings_core_t;

typedef struct
{
    bool warnAccess;
    bool lockAccess;
    bool httpsOnly;
    bool onBlacklistDomain;
    bool onCrawler;
    bool onExternal;
    bool onRobotsTxt;
    bool hardLock;
} settings_security_mit_t;

typedef struct
{
    settings_loglevel level;
    bool color;
    bool logFullAuth;
} settings_log_t;

typedef struct
{
    bool logRaw;
    char *logRawFile;
    bool logHuman;
    char *logHumanFile;
} settings_rtnl_t;

typedef struct
{
    bool enabled;
    char *filename;
} settings_pcap_t;

typedef struct
{
    bool cache_images;
    bool cache_preload;
} settings_tonie_json_t;

typedef struct
{
    bool pcm_encode_console_url;
} settings_web_debug_t;
typedef struct
{
    settings_web_debug_t web;
} settings_debug_t;

typedef struct
{
    uint32_t configVersion;
    char *commonName;
    char *boxName;
    char *boxModel;
    char *ip;
    settings_core_t core;
    settings_cloud_t cloud;
    settings_encode_t encode;
    settings_frontend_t frontend;
    settings_mqtt_t mqtt;
    settings_hass_t hass;
    settings_security_mit_t security_mit;
    settings_toniebox_t toniebox;
    settings_internal_t internal;
    settings_log_t log;
    settings_rtnl_t rtnl;
    settings_pcap_t pcap;
    settings_tonie_json_t tonie_json;
    settings_debug_t debug;
} settings_t;

typedef enum
{
    TYPE_BOOL,
    TYPE_SIGNED,
    TYPE_UNSIGNED,
    TYPE_HEX,
    TYPE_STRING,
    TYPE_FLOAT,
    TYPE_U64_ARRAY,
    TYPE_TREE_DESC,
    TYPE_END
} settings_type;

typedef union
{
    bool bool_value;
    int32_t signed_value;
    uint64_t unsigned_value;
    uint32_t hex_value;
    float float_value;
    const char *string_value;
    uint64_t *u64_array;
} setting_value_t;

/**
 * @typedef struct setting_item_t
 * @brief Structure representing a settings item.
 *
 * This structure encapsulates all the relevant information for a setting in the system,
 * such as its name, description, value pointer, type, initial value, range (min, max) and whether it is an internal setting.
 *
 * @var const char *option_name
 * The name of the setting item.
 *
 * @var const char *description
 * A description of the setting item, which can be used to give users an understanding of its purpose.
 *
 * @var const char *label
 * A short description of the setting item, which can be used as an entry name.
 *
 * @var void *ptr
 * A pointer to the value of the setting. The type of the value pointed to should match the specified setting type.
 *
 * @var settings_type type
 * The type of the setting item's value. This determines how the value should be interpreted.
 *
 * @var setting_value_t init
 * The initial value for the setting item. This is used when initializing the settings subsystem.
 *
 * @var setting_value_t min
 * The minimum allowable value for the setting item. This is used to enforce boundaries on setting values.
 *
 * @var setting_value_t max
 * The maximum allowable value for the setting item. This is used to enforce boundaries on setting values.
 *
 * @var bool internal
 * If true, this setting is intended for internal use by the system and should not be exposed to end users.
 */
typedef struct
{
    const char *option_name;
    const char *description;
    const char *label;
    void *ptr;
    settings_type type;
    setting_value_t init;
    setting_value_t min;
    setting_value_t max;
    size_t size;
    bool internal;
    bool overlayed;
    settings_level level;
} setting_item_t;

#define OPTION_START() setting_item_t option_map_array[] = {
#define OPTION_ADV_BOOL(o, p, d, short, desc, i, ov, lvl) {.option_name = o, .ptr = p, .init = {.bool_value = d}, .type = TYPE_BOOL, .description = desc, .label = short, .internal = i, .overlayed = ov, .level = lvl},
#define OPTION_ADV_SIGNED(o, p, d, minVal, maxVal, short, desc, i, ov, lvl) {.option_name = o, .ptr = p, .init = {.signed_value = d}, .min = {.signed_value = minVal}, .max = {.signed_value = maxVal}, .type = TYPE_SIGNED, .description = desc, .label = short, .internal = i, .overlayed = ov, .level = lvl},
#define OPTION_ADV_UNSIGNED(o, p, d, minVal, maxVal, short, desc, i, ov, lvl) {.option_name = o, .ptr = p, .init = {.unsigned_value = d}, .min = {.unsigned_value = minVal}, .max = {.unsigned_value = maxVal}, .type = TYPE_UNSIGNED, .description = desc, .label = short, .internal = i, .overlayed = ov, .level = lvl},
#define OPTION_ADV_FLOAT(o, p, d, minVal, maxVal, short, desc, i, ov, lvl) {.option_name = o, .ptr = p, .init = {.float_value = d}, .min = {.float_value = minVal}, .max = {.float_value = maxVal}, .type = TYPE_FLOAT, .description = desc, .label = short, .internal = i, .overlayed = ov, .level = lvl},
#define OPTION_ADV_STRING(o, p, d, short, desc, i, ov, lvl) {.option_name = o, .ptr = p, .init = {.string_value = d}, .type = TYPE_STRING, .description = desc, .label = short, .internal = i, .overlayed = ov, .level = lvl},
#define OPTION_ADV_U64_ARRAY(o, p, s, short, desc, i, ov, lvl) {.option_name = o, .ptr = p, .size = s, .type = TYPE_U64_ARRAY, .description = desc, .label = short, .internal = i, .overlayed = ov, .level = lvl},
#define OPTION_ADV_TREE_DESC(o, p, d, desc, i, ov, lvl) {.option_name = o, .ptr = p, .init = {.string_value = d}, .type = TYPE_TREE_DESC, .description = desc, .label = NULL, .internal = i, .overlayed = ov, .level = lvl},

#define OPTION_BOOL(o, p, d, short, desc, lvl) OPTION_ADV_BOOL(o, p, d, short, desc, false, false, lvl)
#define OPTION_SIGNED(o, p, d, min, max, short, desc, lvl) OPTION_ADV_SIGNED(o, p, d, min, max, short, desc, false, false, lvl)
#define OPTION_UNSIGNED(o, p, d, min, max, short, desc, lvl) OPTION_ADV_UNSIGNED(o, p, d, min, max, short, desc, false, false, lvl)
#define OPTION_FLOAT(o, p, d, min, max, short, desc, lvl) OPTION_ADV_FLOAT(o, p, d, min, max, short, desc, false, false, lvl)
#define OPTION_STRING(o, p, d, short, desc, lvl) OPTION_ADV_STRING(o, p, d, short, desc, false, false, lvl)
#define OPTION_U64_ARRAY(o, p, s, short, desc, lvl) OPTION_ADV_U64_ARRAY(o, p, s, short, desc, false, false, lvl)

#define OPTION_INTERNAL_BOOL(o, p, d, desc, lvl) OPTION_ADV_BOOL(o, p, d, desc, desc, true, false, lvl)
#define OPTION_INTERNAL_SIGNED(o, p, d, min, max, desc, lvl) OPTION_ADV_SIGNED(o, p, d, min, max, desc, desc, true, false, lvl)
#define OPTION_INTERNAL_UNSIGNED(o, p, d, min, max, desc, lvl) OPTION_ADV_UNSIGNED(o, p, d, min, max, desc, desc, true, false, lvl)
#define OPTION_INTERNAL_FLOAT(o, p, d, min, max, desc, lvl) OPTION_ADV_FLOAT(o, p, d, min, max, desc, desc, true, false, lvl)
#define OPTION_INTERNAL_STRING(o, p, d, desc, lvl) OPTION_ADV_STRING(o, p, d, desc, desc, true, false, lvl)
#define OPTION_INTERNAL_U64_ARRAY(o, p, s, desc, lvl) OPTION_ADV_U64_ARRAY(o, p, s, desc, desc, true, false, lvl)

#define OPTION_TREE_DESC(o, desc, lvl) OPTION_ADV_TREE_DESC(o, NULL, NULL, desc, false, false, lvl)

#define OPTION_END()     \
    {                    \
        .type = TYPE_END \
    }                    \
    }                    \
    ;

void overlay_settings_init_opt(setting_item_t *opt, setting_item_t *opt_src);

settings_t *get_settings();
settings_t *get_settings_ovl(const char *overlay_unique_id);
settings_t *get_settings_id(uint8_t settingsId);
settings_t *get_settings_cn(const char *commonName);
uint8_t get_overlay_id(const char *overlay_unique_id);

void settings_resolve_dir(char **resolvedPath, char *path, char *basePath);
void settings_changed_id(uint8_t settingsId);
void settings_loop();

/**
 * @brief Initializes the settings subsystem.
 *
 * This function should be called once, before any other settings functions are used.
 */
error_t settings_init(const char *cwd, const char *base_dir);

/**
 * @brief Deinitializes the settings subsystem.
 *
 * This function should be called to clean up all allocated memory.
 */
void settings_deinit();

/**
 * @brief Saves the current settings to a persistent storage (like a file or database).
 *
 * The settings_save() function is used to persistently store the current settings,
 * allowing them to be reloaded after the program is restarted. The exact method of storage
 * depends on the specific implementation: it may be saved to a file, a database, or another
 * type of persistent storage.
 *
 * This function does not return a value, and it is assumed that it will handle any errors
 * internally. If the settings cannot be saved for some reason (for instance, if the storage
 * medium is not available), it is up to the function implementation to handle this case.
 *
 * Usage:
 * @code
 *    settings_save();
 * @endcode
 */
error_t settings_save();

/**
 * @brief Loads settings from a persistent storage (like a file or database).
 *
 * The settings_load() function is used to load settings that have been saved persistently,
 * such as when the program is first started. The settings are loaded from the same place
 * that the settings_save() function saves to.
 *
 * This function does not return a value, and it is assumed that it will handle any errors
 * internally. If the settings cannot be loaded for some reason (for instance, if the storage
 * medium is not available or the settings file is corrupted), it is up to the function
 * implementation to handle this case.
 *
 * Usage:
 * @code
 *    settings_load();
 * @endcode
 */
error_t settings_load();

uint16_t settings_get_size();

/**
 * @brief Gets the setting item at a specific index.
 *
 * @param index The index of the setting item.
 * @return A pointer to the setting item, or NULL if the index is out of bounds.
 */
setting_item_t *settings_get(int index);
setting_item_t *settings_get_ovl(int index, const char *overlay_name);

/**
 * @brief Sets the value of a boolean setting item.
 *
 * @param item The name of the setting item.
 * @param value The new value for the setting item.
 */
bool settings_set_bool(const char *item, bool value);
bool settings_set_bool_ovl(const char *item, bool value, const char *overlay_name);
bool settings_set_bool_id(const char *item, bool value, uint8_t settingsId);

/**
 * @brief Gets the value of a boolean setting item.
 *
 * @param item The name of the setting item.
 * @return The current value of the setting item. If the item does not exist or is not a boolean, the behavior is undefined.
 */
bool settings_get_bool(const char *item);
bool settings_get_bool_ovl(const char *item, const char *overlay_name);

/**
 * @brief Gets the value of an signed integer setting item.
 *
 * @param item The name of the setting item.
 * @return The current value of the setting item. If the item does not exist or is not an integer, the behavior is undefined.
 */
int32_t settings_get_signed(const char *item);
int32_t settings_get_signed_ovl(const char *item, const char *overlay_name);

/**
 * @brief Sets the value of an integer setting item.
 *
 * @param item The name of the setting item.
 * @param value The new value for the setting item.
 */
bool settings_set_signed(const char *item, int32_t value);
bool settings_set_signed_ovl(const char *item, int32_t value, const char *overlay_name);
bool settings_set_signed_id(const char *item, int32_t value, uint8_t settingsId);

/**
 * @brief Gets the value of an unsigned integer setting item.
 *
 * @param item The name of the setting item.
 * @return The current value of the setting item. If the item does not exist or is not an integer, the behavior is undefined.
 */
uint32_t settings_get_unsigned(const char *item);
uint32_t settings_get_unsigned_ovl(const char *item, const char *overlay_name);

/**
 * @brief Sets the value of an unsigned integer setting item.
 *
 * @param item The name of the setting item.
 * @param value The new value for the setting item.
 */
bool settings_set_unsigned(const char *item, uint32_t value);
bool settings_set_unsigned_ovl(const char *item, uint32_t value, const char *overlay_name);
bool settings_set_unsigned_id(const char *item, uint32_t value, uint8_t settingsId);

/**
 * @brief Retrieves the value of a floating point setting item.
 *
 * @param item The name of the setting item.
 * @return The current float value of the setting item. If the item does not exist or is not a float, the behavior is undefined.
 */
float settings_get_float(const char *item);
float settings_get_float_ovl(const char *item, const char *overlay_name);

/**
 * @brief Sets the value of a floating point setting item.
 *
 * @param item The name of the setting item.
 * @param value The variable where the floating point value of the setting item will be stored. If the item does not exist or is not a float, the behavior is undefined.
 */
bool settings_set_float(const char *item, float value);
bool settings_set_float_ovl(const char *item, float value, const char *overlay_name);
bool settings_set_float_id(const char *item, float value, uint8_t settingsId);

/**
 * @brief Retrieves a setting item by its name.
 *
 * @param item The name of the setting item.
 * @return A pointer to the setting item, or NULL if the item does not exist.
 */
setting_item_t *settings_get_by_name(const char *item);
setting_item_t *settings_get_by_name_ovl(const char *item, const char *overlay_name);

/**
 * @brief Retrieves the value of a string setting item.
 *
 * @param item The name of the setting item.
 * @return The current string value of the setting item. If the item does not exist or is not a string, the behavior is undefined.
 */
const char *settings_get_string(const char *item);
const char *settings_get_string_ovl(const char *item, const char *overlay_name);
const char *settings_get_string_id(const char *item, uint8_t settingsId);

/**
 * @brief Sets the value of a string setting item.
 *
 * @param item The name of the setting item.
 * @param value The new string value for the setting item.
 */
bool settings_set_string(const char *item, const char *value);
bool settings_set_string_ovl(const char *item, const char *value, const char *overlay_name);
bool settings_set_string_id(const char *item, const char *value, uint8_t settingsId);

uint64_t *settings_get_u64_array(const char *item, size_t *len);
uint64_t *settings_get_u64_array_ovl(const char *item, const char *overlay_name, size_t *len);
uint64_t *settings_get_u64_array_id(const char *item, uint8_t settingsId, size_t *len);

bool settings_set_u64_array(const char *item, const uint64_t *value, size_t len);
bool settings_set_u64_array_ovl(const char *item, const uint64_t *value, size_t len, const char *overlay_name);
bool settings_set_u64_array_id(const char *item, const uint64_t *value, size_t len, uint8_t settingsId);

bool settings_set_by_string(const char *item, const char *value);
bool settings_set_by_string_ovl(const char *item, const char *value, const char *overlay_name);
bool settings_set_by_string_id(const char *item, const char *value, uint8_t settingsId);

void settings_load_all_certs();
error_t settings_try_load_certs_id(uint8_t settingsId);
error_t settings_load_certs_id(uint8_t settingsId);
bool test_boxine_ca(uint8_t settingsId);

#endif
