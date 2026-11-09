#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "runtime_config.h"
#include "runtime_menu.h"
#include "telemetry.h"
#include "codecs/aprs/aprs_position.h"

static int save_calls = 0;
static runtime_config saved;
static bool save_ok = true;

static bool fake_save(const runtime_config *cfg)
{
    save_calls++;
    saved = *cfg;
    return save_ok;
}

static const runtime_menu_ops ops = { .save = fake_save };

static runtime_menu_result run(runtime_config *cfg, const char *command, char *reply, size_t reply_size)
{
    char line[160];
    snprintf(line, sizeof(line), "%s", command);
    return runtime_menu_execute(cfg, &ops, line, reply, reply_size);
}

#define EXPECT_OK(cmd) do { \
    runtime_menu_result r = run(&cfg, cmd, reply, sizeof(reply)); \
    assert(r == RT_MENU_CONTINUE); \
    if (strncmp(reply, "OK", 2) != 0) { printf("'%s' -> %s\n", cmd, reply); } \
    assert(strncmp(reply, "OK", 2) == 0); } while (0)

#define EXPECT_ERR(cmd) do { \
    runtime_config before = cfg; \
    run(&cfg, cmd, reply, sizeof(reply)); \
    if (strncmp(reply, "ERR", 3) != 0) { printf("'%s' -> %s\n", cmd, reply); } \
    assert(strncmp(reply, "ERR", 3) == 0); \
    assert(memcmp(&before, &cfg, sizeof(cfg)) == 0); } while (0)

int main(void)
{
    runtime_config cfg;
    char reply[2048];
    uint32_t hz;

    runtime_config_defaults(&cfg);
    assert(runtime_config_validate(&cfg));

    // MHz parser
    assert(runtime_menu_parse_mhz("432.5", &hz) && hz == 432500000UL);
    assert(runtime_menu_parse_mhz("14.0971", &hz) && hz == 14097100UL);
    assert(runtime_menu_parse_mhz("432", &hz) && hz == 432000000UL);
    assert(runtime_menu_parse_mhz("432.500001", &hz) && hz == 432500001UL);
    assert(!runtime_menu_parse_mhz("", &hz));
    assert(!runtime_menu_parse_mhz("4.3.2", &hz));
    assert(!runtime_menu_parse_mhz("432.5x", &hz));
    assert(!runtime_menu_parse_mhz("432.5000001", &hz));

    // Reading commands
    run(&cfg, "help", reply, sizeof(reply));
    assert(strstr(reply, "SET FREQ") != NULL);
    run(&cfg, "  show  ", reply, sizeof(reply));
    assert(strstr(reply, "CALL      EA7KE-13") != NULL);
    assert(strstr(reply, "432.500 MHz") != NULL);
    run(&cfg, "", reply, sizeof(reply));
    assert(reply[0] == '\0');

    // Settings
    EXPECT_OK("set call ea7ke-9");
    assert(strcmp(cfg.callsign, "EA7KE") == 0 && cfg.ssid == 9);
    EXPECT_OK("SET SSID 13");
    assert(cfg.ssid == 13);
    EXPECT_OK("SET FREQ 433.775");
    assert(cfg.aprs_frequency_hz == 433775000UL);
    EXPECT_OK("SET INTERVAL 600");
    assert(cfg.aprs_interval_s == 600);
    EXPECT_OK("SET COMMENT Estacion meteo Sevilla");
    assert(strcmp(cfg.comment, "Estacion meteo Sevilla") == 0);
    EXPECT_OK("SET PATH wide1-1,wide2-1");
    assert(strcmp(cfg.path, "WIDE1-1,WIDE2-1") == 0);
    EXPECT_OK("SET PATH none");
    assert(cfg.path[0] == '\0');
    EXPECT_OK("SET MODE tracker");
    assert(cfg.mode == RT_MODE_TRACKER);
    EXPECT_OK("SET MODE wx");
    assert(cfg.mode == RT_MODE_WEATHER);
    EXPECT_OK("SET PROTO both");
    assert(cfg.protocol == RT_PROTOCOL_BOTH);
    EXPECT_OK("SET PROTO wspr");
    assert(cfg.protocol == RT_PROTOCOL_WSPR);
    EXPECT_OK("SET PROTO aprs");
    assert(cfg.protocol == RT_PROTOCOL_APRS);
    EXPECT_OK("SET ALT m");
    assert(cfg.altitude_mode == RT_ALT_METERS);
    EXPECT_OK("SET ALT off");
    assert(cfg.altitude_mode == RT_ALT_OFF);
    EXPECT_OK("SET ALT ft");
    assert(cfg.altitude_mode == RT_ALT_FEET);
    EXPECT_OK("SET POWER 7");
    assert(cfg.tx_power == 7);
    EXPECT_OK("SET SENSOR 0x76");
    assert(cfg.sensor_enabled && cfg.sensor_address == 0x76);
    EXPECT_OK("SET SENSOR OFF");
    assert(!cfg.sensor_enabled);
    EXPECT_OK("SET SENSOR 77");
    assert(cfg.sensor_enabled && cfg.sensor_address == 0x77);
    EXPECT_OK("SET WSPRFREQ 14.0971");
    assert(cfg.wspr_frequency_hz == 14097100UL);
    EXPECT_OK("SET WSPRDBM 17");
    assert(cfg.wspr_dbm == 17);
    EXPECT_OK("SET LOCATOR im67");
    assert(strcmp(cfg.wspr_locator, "IM67") == 0);
    EXPECT_OK("SET LOCATOR gps");
    assert(cfg.wspr_locator[0] == '\0');

    // Rejected values leave the configuration untouched
    EXPECT_ERR("SET CALL");
    EXPECT_ERR("SET CALL TOOLONGCALL");
    EXPECT_ERR("SET CALL EA7KE-16");
    EXPECT_ERR("SET CALL EA 7");
    EXPECT_ERR("SET SSID 16");
    EXPECT_ERR("SET FREQ 144.800");
    EXPECT_ERR("SET FREQ abc");
    EXPECT_ERR("SET INTERVAL 5");
    EXPECT_ERR("SET INTERVAL 99999");
    EXPECT_ERR("SET INTERVAL -1");
    EXPECT_ERR("SET COMMENT bad|char");
    EXPECT_ERR("SET COMMENT 0123456789012345678901234567890123456789012345678901234567890");
    EXPECT_ERR("SET PATH WIDE1-1;X");
    EXPECT_ERR("SET MODE foo");
    EXPECT_ERR("SET PROTO foo");
    EXPECT_ERR("SET POWER 8");
    EXPECT_ERR("SET ALT yards");
    EXPECT_ERR("SET SENSOR 0x50");
    EXPECT_ERR("SET WSPRFREQ 432.5");
    EXPECT_ERR("SET WSPRFREQ 14.5");
    EXPECT_ERR("SET WSPRDBM 61");
    EXPECT_ERR("SET LOCATOR ZZ99");
    EXPECT_ERR("SET LOCATOR IM6");
    EXPECT_ERR("SET BOGUS 1");
    EXPECT_ERR("SET");
    EXPECT_ERR("FROBNICATE");

    // SAVE / DEFAULTS / EXIT
    EXPECT_OK("SET CALL EB7XYZ-5");
    EXPECT_OK("SAVE");
    assert(save_calls == 1);
    assert(strcmp(saved.callsign, "EB7XYZ") == 0 && saved.ssid == 5);
    assert(runtime_config_validate(&saved));

    // The stored image survives a round trip through the blob parser
    runtime_config loaded;
    assert(runtime_config_from_blob(&saved, sizeof(saved), &loaded));
    assert(memcmp(&loaded, &saved, sizeof(loaded)) == 0);

    // Corrupted or erased flash falls back to the defaults
    runtime_config corrupted = saved;
    corrupted.callsign[0] ^= 1;
    assert(!runtime_config_from_blob(&corrupted, sizeof(corrupted), &loaded));
    unsigned char erased[sizeof(runtime_config)];
    memset(erased, 0xFF, sizeof(erased));
    assert(!runtime_config_from_blob(erased, sizeof(erased), &loaded));
    assert(!runtime_config_from_blob(&saved, sizeof(saved) - 1, &loaded));

    save_ok = false;
    run(&cfg, "save", reply, sizeof(reply));
    assert(strncmp(reply, "ERR", 3) == 0);
    save_ok = true;

    EXPECT_OK("DEFAULTS");
    runtime_config defaults;
    runtime_config_defaults(&defaults);
    runtime_config_finalize(&defaults);
    runtime_config_finalize(&cfg);
    assert(memcmp(&defaults, &cfg, sizeof(cfg)) == 0);

    assert(run(&cfg, "exit", reply, sizeof(reply)) == RT_MENU_EXIT);

    // Applying publishes the globals used by the firmware
    runtime_config_apply(&cfg);
    assert(strcmp(rt_config.callsign, cfg.callsign) == 0);
    assert(rt_aprs_comment_template[0] == ' ');
    assert(strcmp(rt_aprs_comment_template + 1, cfg.comment) == 0);
    assert(runtime_config_ssid_char(13) == 'D');
    assert(runtime_config_ssid_char(0) == '0');
    assert(runtime_config_ssid_char(9) == '9');
    assert(runtime_config_ssid_char(15) == 'F');

    // Long input never overflows the reply buffer
    char tiny[16];
    run(&cfg, "help", tiny, sizeof(tiny));
    assert(strlen(tiny) < sizeof(tiny));

    // Altitude formats in position packets (49 m = 160 ft)
    telemetry_data telemetry;
    memset(&telemetry, 0, sizeof(telemetry));
    telemetry.gps.altitude_mm = 49000;
    uint8_t packet[128];

    rt_config.altitude_mode = RT_ALT_FEET;
    aprs_generate_position(packet, sizeof(packet), &telemetry, '/', 'O', false, " WX");
    assert(strstr((char *) packet, "/A=000160 WX") != NULL);
    rt_config.altitude_mode = RT_ALT_METERS;
    aprs_generate_position(packet, sizeof(packet), &telemetry, '/', 'O', false, " WX");
    assert(strstr((char *) packet, "/A=") == NULL && strstr((char *) packet, " 49m WX") != NULL);
    rt_config.altitude_mode = RT_ALT_OFF;
    aprs_generate_position(packet, sizeof(packet), &telemetry, '/', 'O', false, " WX");
    assert(strstr((char *) packet, "/A=") == NULL && strstr((char *) packet, "m WX") == NULL);
    assert(strstr((char *) packet, "000/000 WX") != NULL);

    printf("runtime_menu_test OK\n");
    return 0;
}
