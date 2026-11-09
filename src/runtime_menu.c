#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "strlcpy.h"
#include "runtime_config.h"
#include "runtime_menu.h"

typedef struct _outbuf {
    char *data;
    size_t capacity;
    size_t length;
} outbuf;

static void out_printf(outbuf *o, const char *format, ...)
{
    if (o->capacity == 0 || o->length >= o->capacity - 1) {
        return;
    }

    va_list args;
    va_start(args, format);
    int written = vsnprintf(o->data + o->length, o->capacity - o->length, format, args);
    va_end(args);

    if (written < 0) {
        return;
    }
    size_t room = o->capacity - o->length - 1;
    o->length += ((size_t) written < room) ? (size_t) written : room;
}

static char *skip_spaces(char *s)
{
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    return s;
}

static void trim_end(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) {
        s[--n] = '\0';
    }
}

static void upcase(char *s)
{
    for (; *s; s++) {
        if (*s >= 'a' && *s <= 'z') {
            *s = (char) (*s - 'a' + 'A');
        }
    }
}

// Splits off the first word. Returns it (NUL terminated) and moves *rest after it.
static char *next_token(char **rest)
{
    char *start = skip_spaces(*rest);
    char *end = start;
    while (*end != '\0' && *end != ' ' && *end != '\t') {
        end++;
    }
    if (*end != '\0') {
        *end = '\0';
        end++;
    }
    *rest = skip_spaces(end);
    return start;
}

static bool parse_uint(const char *s, uint32_t max, uint32_t *value)
{
    if (*s == '\0') {
        return false;
    }
    uint32_t result = 0;
    for (; *s; s++) {
        if (*s < '0' || *s > '9') {
            return false;
        }
        result = result * 10 + (uint32_t) (*s - '0');
        if (result > max) {
            return false;
        }
    }
    *value = result;
    return true;
}

static bool parse_hex(const char *s, uint32_t *value)
{
    if (s[0] == '0' && (s[1] == 'X' || s[1] == 'x')) {
        s += 2;
    }
    if (*s == '\0') {
        return false;
    }
    uint32_t result = 0;
    for (; *s; s++) {
        uint32_t digit;
        if (*s >= '0' && *s <= '9') {
            digit = (uint32_t) (*s - '0');
        } else if (*s >= 'A' && *s <= 'F') {
            digit = (uint32_t) (*s - 'A' + 10);
        } else if (*s >= 'a' && *s <= 'f') {
            digit = (uint32_t) (*s - 'a' + 10);
        } else {
            return false;
        }
        result = result * 16 + digit;
        if (result > 0xFFFF) {
            return false;
        }
    }
    *value = result;
    return true;
}

bool runtime_menu_parse_mhz(const char *text, uint32_t *hz)
{
    uint32_t whole = 0;
    uint32_t fraction = 0;
    uint32_t fraction_digits = 0;
    bool seen_digit = false;
    bool in_fraction = false;

    for (; *text; text++) {
        if (*text == '.') {
            if (in_fraction) {
                return false;
            }
            in_fraction = true;
        } else if (*text >= '0' && *text <= '9') {
            seen_digit = true;
            if (in_fraction) {
                if (fraction_digits >= 6) {
                    return false;
                }
                fraction = fraction * 10 + (uint32_t) (*text - '0');
                fraction_digits++;
            } else {
                whole = whole * 10 + (uint32_t) (*text - '0');
                if (whole > 4000) {
                    return false;
                }
            }
        } else {
            return false;
        }
    }
    if (!seen_digit) {
        return false;
    }
    while (fraction_digits < 6) {
        fraction *= 10;
        fraction_digits++;
    }
    *hz = whole * 1000000UL + fraction;
    return true;
}

// "432.5", "14.0956": at least 3 decimals, trailing zeros trimmed after that
static void format_mhz(char *dst, size_t size, uint32_t hz)
{
    snprintf(dst, size, "%lu.%06lu", (unsigned long) (hz / 1000000UL), (unsigned long) (hz % 1000000UL));
    size_t n = strlen(dst);
    while (n > 0 && dst[n - 1] == '0' && dst[n - 4] != '.' && dst[n - 3] != '.' && dst[n - 2] != '.') {
        dst[--n] = '\0';
    }
}

static const char *protocol_name(uint8_t protocol)
{
    switch (protocol) {
        case RT_PROTOCOL_WSPR: return "WSPR";
        case RT_PROTOCOL_BOTH: return "BOTH";
        default: return "APRS";
    }
}

static void print_help(outbuf *o)
{
    out_printf(o,
            "Commands (case insensitive):\r\n"
            "  SHOW                     show the current values\r\n"
            "  SET CALL <call[-ssid]>   callsign, e.g. EA7KE or EA7KE-13\r\n"
            "  SET SSID <0-15>          APRS SSID (13 = weather station)\r\n"
            "  SET FREQ <MHz>           APRS frequency, 430.000-440.000\r\n"
            "  SET INTERVAL <seconds>   APRS interval, 30-3600\r\n"
            "  SET COMMENT <text>       APRS comment, max 60 chars\r\n"
            "  SET PATH <path|NONE>     digipeater path, e.g. WIDE1-1,WIDE2-1\r\n"
            "  SET MODE <WX|TRACKER>    weather station or position tracker\r\n"
            "  SET PROTO <APRS|WSPR|BOTH>  WSPR needs an external Si5351\r\n"
            "  SET ALT <FT|M|OFF>       tracker altitude: feet (APRS /A=), metres (text) or none\r\n"
            "  SET POWER <0-7>          Si4032 output power (5 = 25 mW)\r\n"
            "  SET SENSOR <OFF|0x76|0x77>  external BME280/BMP280\r\n"
            "  SET WSPRFREQ <MHz>       WSPR transmit frequency (HF amateur bands)\r\n"
            "  SET WSPRDBM <0-60>       WSPR reported power\r\n"
            "  SET LOCATOR <AA00|GPS>   WSPR locator or GPS derived\r\n"
            "  DEFAULTS                 restore the factory values\r\n"
            "  SAVE                     store the values permanently\r\n"
            "  EXIT                     start the firmware (unsaved values apply to this boot only)\r\n");
}

static void print_show(outbuf *o, const runtime_config *cfg)
{
    char frequency[20];

    format_mhz(frequency, sizeof(frequency), cfg->aprs_frequency_hz);
    out_printf(o, "CALL      %s-%u\r\n", cfg->callsign, (unsigned) cfg->ssid);
    out_printf(o, "FREQ      %s MHz\r\n", frequency);
    out_printf(o, "INTERVAL  %lu s\r\n", (unsigned long) cfg->aprs_interval_s);
    out_printf(o, "MODE      %s\r\n", cfg->mode == RT_MODE_WEATHER ? "WX" : "TRACKER");
    out_printf(o, "PROTO     %s\r\n", protocol_name(cfg->protocol));
    out_printf(o, "ALT       %s\r\n", cfg->altitude_mode == RT_ALT_OFF ? "OFF" : (cfg->altitude_mode == RT_ALT_METERS ? "M" : "FT"));
    out_printf(o, "POWER     %u\r\n", (unsigned) cfg->tx_power);
    out_printf(o, "COMMENT   %s\r\n", cfg->comment);
    out_printf(o, "PATH      %s\r\n", cfg->path[0] ? cfg->path : "(none)");
    if (cfg->sensor_enabled) {
        out_printf(o, "SENSOR    0x%02X\r\n", (unsigned) cfg->sensor_address);
    } else {
        out_printf(o, "SENSOR    OFF\r\n");
    }
    format_mhz(frequency, sizeof(frequency), cfg->wspr_frequency_hz);
    out_printf(o, "WSPRFREQ  %s MHz\r\n", frequency);
    out_printf(o, "WSPRDBM   %u\r\n", (unsigned) cfg->wspr_dbm);
    out_printf(o, "LOCATOR   %s\r\n", cfg->wspr_locator[0] ? cfg->wspr_locator : "GPS");

    if (cfg->mode == RT_MODE_WEATHER && !cfg->sensor_enabled) {
        out_printf(o, "Note: WX mode without SENSOR sends empty weather data.\r\n");
    }
}

// Applies "SET <key> <value>" to the candidate configuration. On failure it returns false and
// points *hint at the explanation of the accepted values.
static bool apply_setting(runtime_config *t, char *key, char *value, const char **hint)
{
    uint32_t number;

    upcase(key);

    if (strcmp(key, "CALL") == 0) {
        upcase(value);
        *hint = "1-6 letters/digits, optionally -SSID (0-15)";
        char *dash = strchr(value, '-');
        if (dash != NULL) {
            *dash = '\0';
            if (!parse_uint(dash + 1, 15, &number)) {
                return false;
            }
            t->ssid = (uint8_t) number;
        }
        if (strlen(value) > RT_CALLSIGN_MAX_LEN) {
            return false;
        }
        memset(t->callsign, 0, sizeof(t->callsign));
        strlcpy(t->callsign, value, sizeof(t->callsign));
        return true;
    }
    if (strcmp(key, "SSID") == 0) {
        *hint = "0-15";
        if (!parse_uint(value, 15, &number)) {
            return false;
        }
        t->ssid = (uint8_t) number;
        return true;
    }
    if (strcmp(key, "FREQ") == 0) {
        *hint = "430.000-440.000 MHz";
        return runtime_menu_parse_mhz(value, &t->aprs_frequency_hz);
    }
    if (strcmp(key, "INTERVAL") == 0) {
        *hint = "30-3600 seconds";
        if (!parse_uint(value, 65535, &number)) {
            return false;
        }
        t->aprs_interval_s = number;
        return true;
    }
    if (strcmp(key, "COMMENT") == 0) {
        *hint = "printable ASCII, max 60 chars, no | or ~";
        if (strlen(value) > RT_COMMENT_MAX_LEN) {
            return false;
        }
        memset(t->comment, 0, sizeof(t->comment));
        strlcpy(t->comment, value, sizeof(t->comment));
        return true;
    }
    if (strcmp(key, "PATH") == 0) {
        *hint = "NONE or digipeaters separated by commas, max 31 chars";
        upcase(value);
        if (strcmp(value, "NONE") == 0 || strcmp(value, "-") == 0) {
            value[0] = '\0';
        }
        if (strlen(value) > RT_PATH_MAX_LEN) {
            return false;
        }
        memset(t->path, 0, sizeof(t->path));
        strlcpy(t->path, value, sizeof(t->path));
        return true;
    }
    if (strcmp(key, "MODE") == 0) {
        *hint = "WX or TRACKER";
        upcase(value);
        if (strcmp(value, "WX") == 0 || strcmp(value, "WEATHER") == 0) {
            t->mode = RT_MODE_WEATHER;
            return true;
        }
        if (strcmp(value, "TRACKER") == 0 || strcmp(value, "TRK") == 0) {
            t->mode = RT_MODE_TRACKER;
            return true;
        }
        return false;
    }
    if (strcmp(key, "PROTO") == 0) {
        *hint = "APRS, WSPR or BOTH";
        upcase(value);
        if (strcmp(value, "APRS") == 0) {
            t->protocol = RT_PROTOCOL_APRS;
            return true;
        }
        if (strcmp(value, "WSPR") == 0) {
            t->protocol = RT_PROTOCOL_WSPR;
            return true;
        }
        if (strcmp(value, "BOTH") == 0) {
            t->protocol = RT_PROTOCOL_BOTH;
            return true;
        }
        return false;
    }
    if (strcmp(key, "ALT") == 0) {
        *hint = "FT (feet, APRS standard), M (metres, as text) or OFF";
        upcase(value);
        if (strcmp(value, "FT") == 0 || strcmp(value, "FEET") == 0) {
            t->altitude_mode = RT_ALT_FEET;
            return true;
        }
        if (strcmp(value, "M") == 0 || strcmp(value, "METERS") == 0 || strcmp(value, "METRES") == 0) {
            t->altitude_mode = RT_ALT_METERS;
            return true;
        }
        if (strcmp(value, "OFF") == 0) {
            t->altitude_mode = RT_ALT_OFF;
            return true;
        }
        return false;
    }
    if (strcmp(key, "POWER") == 0) {
        *hint = "0-7";
        if (!parse_uint(value, 7, &number)) {
            return false;
        }
        t->tx_power = (uint8_t) number;
        return true;
    }
    if (strcmp(key, "SENSOR") == 0) {
        *hint = "OFF, 0x76 or 0x77";
        upcase(value);
        if (strcmp(value, "OFF") == 0) {
            t->sensor_enabled = 0;
            return true;
        }
        if (!parse_hex(value, &number)) {
            return false;
        }
        t->sensor_enabled = 1;
        t->sensor_address = (uint8_t) number;
        return true;
    }
    if (strcmp(key, "WSPRFREQ") == 0) {
        *hint = "a frequency inside an HF amateur band (1.810-29.700 MHz), in MHz";
        return runtime_menu_parse_mhz(value, &t->wspr_frequency_hz);
    }
    if (strcmp(key, "WSPRDBM") == 0) {
        *hint = "0-60";
        if (!parse_uint(value, 60, &number)) {
            return false;
        }
        t->wspr_dbm = (uint8_t) number;
        return true;
    }
    if (strcmp(key, "LOCATOR") == 0) {
        *hint = "4 character locator such as IM67, or GPS";
        upcase(value);
        memset(t->wspr_locator, 0, sizeof(t->wspr_locator));
        if (strcmp(value, "GPS") == 0) {
            return true;
        }
        if (strlen(value) != RT_LOCATOR_LEN) {
            return false;
        }
        strlcpy(t->wspr_locator, value, sizeof(t->wspr_locator));
        return true;
    }

    *hint = "unknown setting, type HELP";
    return false;
}

runtime_menu_result runtime_menu_execute(runtime_config *cfg, const runtime_menu_ops *ops,
        char *line, char *out, size_t out_size)
{
    outbuf o = { .data = out, .capacity = out_size, .length = 0 };
    if (out_size > 0) {
        out[0] = '\0';
    }

    trim_end(line);
    char *rest = skip_spaces(line);
    if (*rest == '\0') {
        return RT_MENU_CONTINUE;
    }

    char *command = next_token(&rest);
    upcase(command);

    if (strcmp(command, "HELP") == 0 || strcmp(command, "?") == 0) {
        print_help(&o);
    } else if (strcmp(command, "SHOW") == 0) {
        print_show(&o, cfg);
    } else if (strcmp(command, "SET") == 0) {
        char *key = next_token(&rest);
        char *value = rest;
        const char *hint = "";

        if (*key == '\0') {
            out_printf(&o, "ERR usage: SET <setting> <value>, type HELP\r\n");
            return RT_MENU_CONTINUE;
        }

        runtime_config candidate = *cfg;
        if (!apply_setting(&candidate, key, value, &hint) || !runtime_config_validate(&candidate)) {
            out_printf(&o, "ERR %s\r\n", hint[0] ? hint : "invalid value");
        } else {
            *cfg = candidate;
            out_printf(&o, "OK\r\n");
        }
    } else if (strcmp(command, "DEFAULTS") == 0) {
        runtime_config_defaults(cfg);
        out_printf(&o, "OK factory values loaded (use SAVE to keep them)\r\n");
    } else if (strcmp(command, "SAVE") == 0) {
        runtime_config_finalize(cfg);
        if (!runtime_config_validate(cfg)) {
            out_printf(&o, "ERR configuration is not valid\r\n");
        } else if (ops == NULL || ops->save == NULL || !ops->save(cfg)) {
            out_printf(&o, "ERR could not write the flash\r\n");
        } else {
            out_printf(&o, "OK saved\r\n");
        }
    } else if (strcmp(command, "EXIT") == 0) {
        out_printf(&o, "OK starting\r\n");
        return RT_MENU_EXIT;
    } else {
        out_printf(&o, "ERR unknown command, type HELP\r\n");
    }

    return RT_MENU_CONTINUE;
}
