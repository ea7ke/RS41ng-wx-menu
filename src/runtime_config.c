#include <stdio.h>
#include <string.h>

#include "config.h"
#include "strlcpy.h"
#include "runtime_config.h"

runtime_config rt_config;
char rt_aprs_comment_template[RT_COMMENT_MAX_LEN + 2];

// Amateur HF bands (IARU Region 1) accepted for WSPR through the external Si5351.
// Adjust this table to the privileges of your licence if needed.
typedef struct _rt_band {
    uint32_t low_hz;
    uint32_t high_hz;
} rt_band;

static const rt_band rt_wspr_bands[] = {
        {  1810000UL,  2000000UL },
        {  3500000UL,  3800000UL },
        {  5351500UL,  5366500UL },
        {  7000000UL,  7200000UL },
        { 10100000UL, 10150000UL },
        { 14000000UL, 14350000UL },
        { 18068000UL, 18168000UL },
        { 21000000UL, 21450000UL },
        { 24890000UL, 24990000UL },
        { 28000000UL, 29700000UL },
};

static uint8_t ssid_from_char(char c)
{
    if (c >= '0' && c <= '9') {
        return (uint8_t) (c - '0');
    }
    if (c >= 'A' && c <= 'F') {
        return (uint8_t) (c - 'A' + 10);
    }
    return 0;
}

char runtime_config_ssid_char(uint8_t ssid)
{
    if (ssid <= 9) {
        return (char) ('0' + ssid);
    }
    return (char) ('A' + (ssid - 10));
}

bool runtime_config_aprs_frequency_ok(uint32_t hz)
{
    return hz >= RT_APRS_FREQ_MIN_HZ && hz <= RT_APRS_FREQ_MAX_HZ;
}

bool runtime_config_wspr_frequency_ok(uint32_t hz)
{
    for (size_t i = 0; i < sizeof(rt_wspr_bands) / sizeof(rt_wspr_bands[0]); i++) {
        if (hz >= rt_wspr_bands[i].low_hz && hz <= rt_wspr_bands[i].high_hz) {
            return true;
        }
    }
    return false;
}

void runtime_config_defaults(runtime_config *cfg)
{
    memset(cfg, 0, sizeof(*cfg));

    cfg->magic = RT_CONFIG_MAGIC;
    cfg->version = RT_CONFIG_VERSION;
    cfg->size = sizeof(*cfg);

    strlcpy(cfg->callsign, CALLSIGN, sizeof(cfg->callsign));
    cfg->ssid = ssid_from_char(APRS_SSID);
    cfg->mode = APRS_WEATHER_REPORT_ENABLE ? RT_MODE_WEATHER : RT_MODE_TRACKER;
    cfg->protocol = RT_PROTOCOL_APRS;
#if defined(RS41)
    cfg->tx_power = RADIO_SI4032_TX_POWER;
#else
    cfg->tx_power = 5;
#endif

    cfg->aprs_frequency_hz = RADIO_TX_FREQUENCY_APRS_1200;
    cfg->aprs_interval_s = (APRS_TIME_SYNC_SECONDS > 0) ? APRS_TIME_SYNC_SECONDS : 300;

    strlcpy(cfg->comment, APRS_COMMENT, sizeof(cfg->comment));
    strlcpy(cfg->path, APRS_RELAYS, sizeof(cfg->path));

    cfg->sensor_enabled = SENSOR_BMP280_ENABLE ? 1 : 0;
    cfg->sensor_address = SENSOR_BMP280_I2C_ADDRESS;
    cfg->wspr_dbm = WSPR_DBM;

    cfg->wspr_frequency_hz = RADIO_SI5351_TX_FREQUENCY_WSPR;
    if (WSPR_LOCATOR_FIXED_ENABLED) {
        strlcpy(cfg->wspr_locator, WSPR_LOCATOR_FIXED, sizeof(cfg->wspr_locator));
    }

    cfg->crc32 = runtime_config_crc32(cfg);
}

uint32_t runtime_config_crc32(const runtime_config *cfg)
{
    const uint8_t *data = (const uint8_t *) cfg;
    size_t length = offsetof(runtime_config, crc32);
    uint32_t crc = 0xFFFFFFFFUL;

    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320UL : 0);
        }
    }
    return ~crc;
}

void runtime_config_finalize(runtime_config *cfg)
{
    cfg->magic = RT_CONFIG_MAGIC;
    cfg->version = RT_CONFIG_VERSION;
    cfg->size = sizeof(*cfg);
    cfg->crc32 = runtime_config_crc32(cfg);
}

// true if the buffer holds a NUL terminated string of at most max_len characters,
// all of them accepted by ok()
static bool string_ok(const char *s, size_t buffer_size, size_t max_len, bool (*ok)(char))
{
    size_t length = 0;
    while (length < buffer_size && s[length] != '\0') {
        length++;
    }
    if (length >= buffer_size || length > max_len) {
        return false;
    }
    for (size_t i = 0; i < length; i++) {
        if (!ok(s[i])) {
            return false;
        }
    }
    return true;
}

static bool char_callsign(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

static bool char_path(char c)
{
    return char_callsign(c) || c == '-' || c == ',';
}

static bool char_comment(char c)
{
    // Printable ASCII. '|' and '~' are reserved by the APRS specification.
    return c >= 0x20 && c <= 0x7E && c != '|' && c != '~';
}

static bool char_locator(char c)
{
    return (c >= 'A' && c <= 'R') || (c >= '0' && c <= '9');
}

bool runtime_config_validate(const runtime_config *cfg)
{
    size_t callsign_length = strnlen(cfg->callsign, sizeof(cfg->callsign));

    if (callsign_length < 3 || !string_ok(cfg->callsign, sizeof(cfg->callsign), RT_CALLSIGN_MAX_LEN, char_callsign)) {
        return false;
    }
    if (cfg->ssid > 15 || cfg->mode > RT_MODE_TRACKER || cfg->protocol > RT_PROTOCOL_BOTH || cfg->tx_power > 7) {
        return false;
    }
    if (!runtime_config_aprs_frequency_ok(cfg->aprs_frequency_hz)) {
        return false;
    }
    if (cfg->aprs_interval_s < RT_INTERVAL_MIN_S || cfg->aprs_interval_s > RT_INTERVAL_MAX_S) {
        return false;
    }
    if (!string_ok(cfg->comment, sizeof(cfg->comment), RT_COMMENT_MAX_LEN, char_comment)) {
        return false;
    }
    if (!string_ok(cfg->path, sizeof(cfg->path), RT_PATH_MAX_LEN, char_path)) {
        return false;
    }
    if (cfg->sensor_enabled > 1 || (cfg->sensor_address != 0x76 && cfg->sensor_address != 0x77)) {
        return false;
    }
    if (cfg->altitude_mode > RT_ALT_METERS) {
        return false;
    }
    if (cfg->wspr_dbm > 60 || !runtime_config_wspr_frequency_ok(cfg->wspr_frequency_hz)) {
        return false;
    }

    size_t locator_length = strnlen(cfg->wspr_locator, sizeof(cfg->wspr_locator));
    if (locator_length != 0) {
        if (locator_length != RT_LOCATOR_LEN ||
            !string_ok(cfg->wspr_locator, sizeof(cfg->wspr_locator), RT_LOCATOR_LEN, char_locator)) {
            return false;
        }
    }

    return true;
}

bool runtime_config_from_blob(const void *blob, size_t length, runtime_config *out)
{
    runtime_config candidate;

    if (length < sizeof(candidate)) {
        return false;
    }
    memcpy(&candidate, blob, sizeof(candidate));

    if (candidate.magic != RT_CONFIG_MAGIC || candidate.version != RT_CONFIG_VERSION ||
        candidate.size != sizeof(candidate)) {
        return false;
    }
    if (candidate.crc32 != runtime_config_crc32(&candidate)) {
        return false;
    }
    if (!runtime_config_validate(&candidate)) {
        return false;
    }

    *out = candidate;
    return true;
}

void runtime_config_apply(const runtime_config *cfg)
{
    rt_config = *cfg;
    snprintf(rt_aprs_comment_template, sizeof(rt_aprs_comment_template), " %s", cfg->comment);
}
