/**
 * Hardware dependent part of the run-time configuration (wx-serial-menu modification):
 *  - storage in the last 2 KB flash page of the STM32L412 (RSM4x4 boards)
 *  - polled serial console on USART3 (PB10 = TX, PB11 = RX), 9600 baud 8N1, 3.3 V levels
 *
 * PB10/PB11 are shared with the I2C2 bus of the external sensor. The console is only active
 * during a short window right after reset and hands the pins back before i2c_init() runs.
 * Nothing must be connected to those pins other than the sensor or the serial adapter.
 */

#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "config.h"
#include "runtime_config.h"

#ifdef RS41_RSM4x4

#include <stm32l4xx_hal.h>

#include "gpio.h"
#include "runtime_menu.h"

// Time without any received byte after which the firmware starts normally
#ifndef RUNTIME_MENU_WINDOW_MS
#define RUNTIME_MENU_WINDOW_MS 15000
#endif

#define RUNTIME_MENU_BAUD_RATE 9600
#define RUNTIME_MENU_LINE_MAX  100

// Last page of the 128 KB flash. The linker script must not place code there (see the .ld file).
#define RT_FLASH_PAGE_SIZE 2048U
#define RT_FLASH_ADDRESS   (FLASH_BASE + (128U * 1024U) - RT_FLASH_PAGE_SIZE)
#define RT_FLASH_PAGE      ((RT_FLASH_ADDRESS - FLASH_BASE) / RT_FLASH_PAGE_SIZE)

bool runtime_config_hw_load(runtime_config *out)
{
    return runtime_config_from_blob((const void *) RT_FLASH_ADDRESS, sizeof(runtime_config), out);
}

bool runtime_config_hw_save(const runtime_config *cfg)
{
    runtime_config image = *cfg;
    runtime_config_finalize(&image);

    bool ok = true;

    HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);

    FLASH_EraseInitTypeDef erase = {
        .TypeErase = FLASH_TYPEERASE_PAGES,
        .Banks = FLASH_BANK_1,
        .Page = RT_FLASH_PAGE,
        .NbPages = 1,
    };
    uint32_t page_error = 0;
    if (HAL_FLASHEx_Erase(&erase, &page_error) != HAL_OK) {
        ok = false;
    }

    for (uint32_t offset = 0; ok && offset < sizeof(image); offset += 8) {
        uint64_t doubleword;
        memcpy(&doubleword, ((const uint8_t *) &image) + offset, 8);
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, RT_FLASH_ADDRESS + offset, doubleword) != HAL_OK) {
            ok = false;
        }
    }

    HAL_FLASH_Lock();

    if (!ok) {
        return false;
    }

    // Read back what is really stored
    runtime_config check;
    return runtime_config_hw_load(&check) && memcmp(&check, &image, sizeof(image)) == 0;
}

// ---- USART3, registers only (the HAL USART driver is synchronous-only in this project) ----

static void console_init(void)
{
    GPIO_InitTypeDef gpio_init = {0};

    gpio_init.Pin = PIN_EXT_USART_TX;
    gpio_init.Mode = GPIO_MODE_AF_PP;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_LOW;
    gpio_init.Alternate = GPIO_AF7_USART3;
    HAL_GPIO_Init(BANK_EXT_USART_TX, &gpio_init);

    gpio_init.Pin = PIN_EXT_USART_RX;
    gpio_init.Mode = GPIO_MODE_AF_PP;
    gpio_init.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(BANK_EXT_USART_RX, &gpio_init);

    EXT_USART_CLK_ENABLE();

    EXT_USART->CR1 = 0;
    EXT_USART->CR2 = 0;
    EXT_USART->CR3 = 0;
    EXT_USART->BRR = (HAL_RCC_GetPCLK1Freq() + RUNTIME_MENU_BAUD_RATE / 2) / RUNTIME_MENU_BAUD_RATE;
    EXT_USART->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
}

static void console_deinit(void)
{
    // Wait for the last character to leave
    while ((EXT_USART->ISR & USART_ISR_TC) == 0) {}

    EXT_USART->CR1 = 0;
    EXT_USART_CLK_DISABLE();

    HAL_GPIO_DeInit(BANK_EXT_USART_TX, PIN_EXT_USART_TX);
    HAL_GPIO_DeInit(BANK_EXT_USART_RX, PIN_EXT_USART_RX);
}

static void console_write(const char *text)
{
    for (; *text; text++) {
        while ((EXT_USART->ISR & USART_ISR_TXE) == 0) {}
        EXT_USART->TDR = (uint8_t) *text;
    }
}

// Returns true and stores the byte if one has arrived
static bool console_read(uint8_t *byte)
{
    uint32_t isr = EXT_USART->ISR;

    if (isr & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE)) {
        EXT_USART->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF;
    }
    if (isr & USART_ISR_RXNE) {
        *byte = (uint8_t) EXT_USART->RDR;
        return true;
    }
    return false;
}

static const runtime_menu_ops menu_ops = {
    .save = runtime_config_hw_save,
};

static void run_menu(runtime_config *cfg)
{
    static char line[RUNTIME_MENU_LINE_MAX];
    static char reply[1800];
    size_t length = 0;
    uint32_t deadline_start = HAL_GetTick();

    console_init();

    console_write("\r\nRS41ng wx-serial-menu. Press ENTER within 15 s to configure (HELP for commands).\r\n");

    // Phase 1: wait for the first byte. Phase 2: any byte restarts the idle timer.
    for (;;) {
        uint8_t byte;

        if ((HAL_GetTick() - deadline_start) >= RUNTIME_MENU_WINDOW_MS) {
            console_write("\r\nTimeout, starting.\r\n");
            break;
        }
        if (!console_read(&byte)) {
            continue;
        }
        deadline_start = HAL_GetTick();

        if (byte == '\r' || byte == '\n') {
            if (length == 0) {
                // A bare ENTER (or the LF of a CR LF pair) only shows the prompt
                console_write("> ");
                continue;
            }
            line[length] = '\0';
            length = 0;
            console_write("\r\n");

            runtime_menu_result result = runtime_menu_execute(cfg, &menu_ops, line, reply, sizeof(reply));
            console_write(reply);
            if (result == RT_MENU_EXIT) {
                break;
            }
            console_write("> ");
        } else if (byte == 0x08 || byte == 0x7F) {
            if (length > 0) {
                length--;
                console_write("\b \b");
            }
        } else if (byte >= 0x20 && byte < 0x7F) {
            if (length < RUNTIME_MENU_LINE_MAX - 1) {
                line[length++] = (char) byte;
                char echo[2] = { (char) byte, '\0' };
                console_write(echo);
            }
        }
    }

    console_deinit();
}

void runtime_config_boot(void)
{
    runtime_config cfg;

    if (!runtime_config_hw_load(&cfg)) {
        runtime_config_defaults(&cfg);
    }

    run_menu(&cfg);

    runtime_config_apply(&cfg);
}

#else /* !RS41_RSM4x4 */

// Other targets keep the compile-time configuration

bool runtime_config_hw_load(runtime_config *out)
{
    (void) out;
    return false;
}

bool runtime_config_hw_save(const runtime_config *cfg)
{
    (void) cfg;
    return false;
}

void runtime_config_boot(void)
{
    runtime_config cfg;
    runtime_config_defaults(&cfg);
    runtime_config_apply(&cfg);
}

#endif
