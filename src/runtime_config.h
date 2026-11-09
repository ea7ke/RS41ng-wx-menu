#ifndef __RUNTIME_CONFIG_H
#define __RUNTIME_CONFIG_H

/**
 * Run-time configuration (wx-serial-menu modification).
 *
 * A subset of the settings that are normally compile-time constants in config.h can be
 * changed at run time through a serial menu (see runtime_menu.h) and are stored in the last
 * page of the STM32L412 flash (RSM4x4 boards only). The values in config.h are used as
 * factory defaults when nothing valid is stored.
 *
 * This file and runtime_config.c are hardware independent so that they can be unit tested
 * on the host (see tests/runtime_menu_test.c).
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define RT_CONFIG_MAGIC   0x43345352UL   /* "RS4C" in little endian */
#define RT_CONFIG_VERSION 1

#define RT_CALLSIGN_MAX_LEN 6
#define RT_COMMENT_MAX_LEN  60
#define RT_PATH_MAX_LEN     31
#define RT_LOCATOR_LEN      4

// 70 cm amateur band (IARU Region 1) allowed for the built-in APRS transmitter
#define RT_APRS_FREQ_MIN_HZ 430000000UL
#define RT_APRS_FREQ_MAX_HZ 440000000UL

#define RT_INTERVAL_MIN_S   30
#define RT_INTERVAL_MAX_S   3600

typedef enum _rt_protocol {
    RT_PROTOCOL_APRS = 0,
    RT_PROTOCOL_WSPR = 1,   // needs an external Si5351 on the I2C bus (HF)
    RT_PROTOCOL_BOTH = 2,
} rt_protocol;

typedef enum _rt_mode {
    RT_MODE_WEATHER = 0,    // APRS weather station report
    RT_MODE_TRACKER = 1,    // APRS position report
} rt_mode;

// Altitude in APRS position (tracker) packets. Value 0 is the original behaviour, so
// configurations stored by earlier versions (this byte was reserved and zero) keep working.
typedef enum _rt_altitude {
    RT_ALT_FEET = 0,        // standard APRS "/A=" field, in feet
    RT_ALT_OFF = 1,         // no altitude
    RT_ALT_METERS = 2,      // free text " 49m" before the comment (not machine readable by APRS clients)
} rt_altitude;

/**
 * Stored layout. No implicit padding (checked at compile time), 8-byte multiple so that it
 * can be written with the STM32L4 double-word flash programming.
 */
typedef struct _runtime_config {
    uint32_t magic;
    uint16_t version;
    uint16_t size;

    char callsign[RT_CALLSIGN_MAX_LEN + 2];
    uint8_t ssid;                  // 0..15
    uint8_t mode;                  // rt_mode
    uint8_t protocol;              // rt_protocol
    uint8_t tx_power;              // Si4032: 0..7

    uint32_t aprs_frequency_hz;
    uint32_t aprs_interval_s;

    char comment[RT_COMMENT_MAX_LEN + 1];
    char path[RT_PATH_MAX_LEN + 1];    // digipeater path, e.g. "WIDE1-1,WIDE2-1"; empty = none

    uint8_t sensor_enabled;        // external BMP280/BME280
    uint8_t sensor_address;        // 0x76 or 0x77
    uint8_t wspr_dbm;
    uint8_t altitude_mode;         // rt_altitude
    uint8_t reserved_pad[3];

    uint32_t wspr_frequency_hz;
    char wspr_locator[RT_LOCATOR_LEN + 1];   // empty = take the locator from GPS
    uint8_t reserved1[3];

    uint32_t crc32;                // CRC-32 of all the bytes before this field
} runtime_config;

typedef char rt_config_size_must_be_144_bytes[(sizeof(runtime_config) == 144) ? 1 : -1];

// Active configuration, valid after runtime_config_apply() / runtime_config_boot()
extern runtime_config rt_config;

// " " + comment, in the form expected by the message template engine (see config.c)
extern char rt_aprs_comment_template[RT_COMMENT_MAX_LEN + 2];

#ifdef __cplusplus
extern "C" {
#endif

// Factory defaults taken from config.h
void runtime_config_defaults(runtime_config *cfg);

// Checks every field (ranges, characters, frequency limits)
bool runtime_config_validate(const runtime_config *cfg);

uint32_t runtime_config_crc32(const runtime_config *cfg);

// Fills in magic/version/size/crc32 so that the structure can be stored
void runtime_config_finalize(runtime_config *cfg);

// Parses a stored image. Returns false (and leaves *out untouched) if it is not valid.
bool runtime_config_from_blob(const void *blob, size_t length, runtime_config *out);

// Makes cfg the active configuration
void runtime_config_apply(const runtime_config *cfg);

// APRS SSID as the character expected by ax25_encode_packet_aprs(): '0'..'9', 'A'..'F'
char runtime_config_ssid_char(uint8_t ssid);

bool runtime_config_aprs_frequency_ok(uint32_t hz);
bool runtime_config_wspr_frequency_ok(uint32_t hz);

// Hardware dependent part (runtime_config_hw.c)
bool runtime_config_hw_load(runtime_config *out);
bool runtime_config_hw_save(const runtime_config *cfg);

// Loads the stored configuration (or the defaults), runs the serial menu window on RSM4x4
// and applies the result. Call once, right after system_init().
void runtime_config_boot(void);

#ifdef __cplusplus
}
#endif

#endif
