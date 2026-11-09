#ifndef __RUNTIME_MENU_H
#define __RUNTIME_MENU_H

/**
 * Text command interpreter for the serial configuration menu (hardware independent).
 *
 * One call handles one complete line typed by the user and writes the reply into 'out'.
 * The hardware layer (runtime_config_hw.c) takes care of the UART and of the flash.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "runtime_config.h"

typedef enum _runtime_menu_result {
    RT_MENU_CONTINUE = 0,
    RT_MENU_EXIT,           // EXIT command: leave the menu and start the firmware
} runtime_menu_result;

typedef struct _runtime_menu_ops {
    // Stores the configuration permanently. Returns false on failure.
    bool (*save)(const runtime_config *cfg);
} runtime_menu_ops;

#ifdef __cplusplus
extern "C" {
#endif

// 'line' is modified in place. The reply is always NUL terminated and uses CR LF line ends.
runtime_menu_result runtime_menu_execute(runtime_config *cfg, const runtime_menu_ops *ops,
        char *line, char *out, size_t out_size);

// "432.5" -> 432500000. Accepts up to 6 decimals.
bool runtime_menu_parse_mhz(const char *text, uint32_t *hz);

#ifdef __cplusplus
}
#endif

#endif
