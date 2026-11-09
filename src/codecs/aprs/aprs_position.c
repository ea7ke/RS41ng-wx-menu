#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aprs.h"
#include "aprs_position.h"
#include "runtime_config.h"

size_t aprs_generate_position(uint8_t *payload, size_t length, telemetry_data *data,
        char symbol_table, char symbol, bool include_timestamp, char *comment)
{
    aprs_packet_counter++;

    char timestamp[12];

    int16_t la_degrees, lo_degrees;
    uint8_t la_minutes, la_h_minutes, lo_minutes, lo_h_minutes;

    convert_degrees_to_dmh(data->gps.latitude_degrees_10000000 / 10, &la_degrees, &la_minutes, &la_h_minutes);
    convert_degrees_to_dmh(data->gps.longitude_degrees_10000000 / 10, &lo_degrees, &lo_minutes, &lo_h_minutes);

    int16_t heading_degrees = (int16_t) ((float) data->gps.heading_degrees_100000 / 100000.0f);
    int16_t ground_speed_knots = (int16_t) (((float) data->gps.ground_speed_cm_per_second / 100.0f) * 3.6f / 1.852f);
    int32_t altitude_feet = (data->gps.altitude_mm / 1000) * 3280 / 1000;

    if (include_timestamp) {
        aprs_generate_timestamp(timestamp, sizeof(timestamp), data);
    } else {
        strncpy(timestamp, "!", sizeof(timestamp));
    }

    // Altitude format chosen at run time (wx-serial-menu): feet in the standard "/A=" field,
    // metres as free text, or omitted
    char altitude[24];
    switch (rt_config.altitude_mode) {
        case RT_ALT_OFF:
            altitude[0] = '\0';
            break;
        case RT_ALT_METERS:
            snprintf(altitude, sizeof(altitude), " %ldm", (long) (data->gps.altitude_mm / 1000));
            break;
        default:
            snprintf(altitude, sizeof(altitude), "/A=%06d", (int) altitude_feet);
            break;
    }

    return snprintf((char *) payload,
            length,
            ("%s%02d%02d.%02u%c%c%03d%02u.%02u%c%c%03d/%03d%s%s"),
            timestamp,
            abs(la_degrees), la_minutes, la_h_minutes,
            la_degrees > 0 ? 'N' : 'S',
            symbol_table,
            abs(lo_degrees), lo_minutes, lo_h_minutes,
            lo_degrees > 0 ? 'E' : 'W',
            symbol,
            heading_degrees,
            ground_speed_knots,
            altitude,
            comment
    );
}
