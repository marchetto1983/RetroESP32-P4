/*
 * Odroid System Compatibility Layer — ESP32-P4 Implementation
 *
 * Initializes the hardware required by RetroESP32-P4:
 *
 *   PPA 2D Engine
 *   USB HID Gamepad
 *   I2S Audio
 *
 * Display initialization is handled by odroid_display.c.
 *
 * This hardware configuration does NOT use:
 *
 *   - I2C audio codec
 *   - ES8311
 *   - GT911 touch controller
 *   - ST7701 MIPI-DSI display
 */

#include "odroid_system.h"

#include "ppa_engine.h"
#include "gamepad.h"
#include "audio.h"

#include <stdbool.h>

#include "esp_log.h"
#include "esp_err.h"

#include "driver/gpio.h"
#include "driver/i2s_std.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


static const char *TAG = "odroid_system";

static bool s_initialized = false;


/* =========================================================================
 * SYSTEM INITIALIZATION
 * =========================================================================
 */

void odroid_system_init(void)
{
    if (s_initialized) {
        return;
    }


    ESP_LOGI(
        TAG,
        "=== RetroESP32-P4 System Init ==="
    );


    /* ---------------------------------------------------------------------
     * 1. PPA 2D Engine
     * ---------------------------------------------------------------------
     */

    ESP_LOGI(
        TAG,
        "Initializing PPA 2D engine..."
    );


    ESP_ERROR_CHECK(
        ppa_engine_init()
    );


    /* ---------------------------------------------------------------------
     * 2. USB HID Gamepad
     * ---------------------------------------------------------------------
     */

    ESP_LOGI(
        TAG,
        "Initializing USB HID gamepad host..."
    );


    gamepad_config_t gp_cfg =
        GAMEPAD_CONFIG_DEFAULT();


    ESP_ERROR_CHECK(
        gamepad_init(
            &gp_cfg
        )
    );


    /* ---------------------------------------------------------------------
     * 3. I2S Audio
     *
     * WT9932P4-TINY:
     *
     *   BCLK  -> GPIO49
     *   WS    -> GPIO48
     *   DOUT  -> GPIO50
     *   AMP   -> GPIO47
     *
     * No ES8311.
     * No I2C.
     * No I2S RX.
     * No MCLK.
     * ---------------------------------------------------------------------
     */

    ESP_LOGI(
        TAG,
        "Initializing audio (I2S TX)..."
    );


    audio_config_t audio_cfg =
        AUDIO_CONFIG_DEFAULT();


    /*
     * I2S peripheral.
     */
    audio_cfg.i2s_num =
        I2S_NUM_0;


    /*
     * No MCLK is connected on this hardware.
     */
    audio_cfg.mclk_io =
        GPIO_NUM_NC;


    /*
     * I2S clock.
     */
    audio_cfg.bclk_io =
        GPIO_NUM_49;


    /*
     * I2S word select / LRCLK.
     */
    audio_cfg.ws_io =
        GPIO_NUM_48;


    /*
     * I2S serial data output.
     */
    audio_cfg.dout_io =
        GPIO_NUM_50;


    /*
     * No I2S input is used.
     */
    audio_cfg.din_io =
        GPIO_NUM_NC;


    /*
     * External amplifier enable.
     *
     * Active HIGH.
     */
    audio_cfg.pa_ctrl_io =
        GPIO_NUM_47;


    /*
     * Default emulator audio sample rate.
     */
    audio_cfg.sample_rate =
        16000;


    /*
     * Initial software volume.
     */
    audio_cfg.volume =
        60;


    esp_err_t audio_ret =
        audio_init(
            &audio_cfg
        );


    if (audio_ret != ESP_OK) {

        ESP_LOGW(
            TAG,
            "Audio init failed (0x%x), continuing without audio",
            audio_ret
        );
    }


    /* ---------------------------------------------------------------------
     * Display
     *
     * The ST7789V SPI display is initialized by odroid_display.c.
     *
     * Do NOT initialize the old Guition ST7701 here.
     * Do NOT initialize GT911 touch here.
     * ---------------------------------------------------------------------
     */

    ESP_LOGI(
        TAG,
        "Display initialization handled by odroid_display.c"
    );


    /* ---------------------------------------------------------------------
     * System initialization complete
     * ---------------------------------------------------------------------
     */

    s_initialized =
        true;


    ESP_LOGI(
        TAG,
        "=== System Init Complete ==="
    );
}


/* =========================================================================
 * I2C BUS ACCESSOR
 * =========================================================================
 *
 * Kept for API compatibility with the original RetroESP32-P4 code.
 *
 * The new WT9932P4-TINY configuration does not initialize an I2C bus,
 * therefore there is no bus handle to return.
 */

void *odroid_system_get_i2c_bus(void)
{
    return NULL;
}


/* =========================================================================
 * OTA APPLICATION SWITCHING
 * =========================================================================
 */

void odroid_system_application_set(
    int slot)
{
    /*
     * On the original Odroid Go, this selects an OTA partition
     * containing the requested emulator.
     *
     * On the ESP32-P4 this remains a stub.
     */

    ESP_LOGW(
        TAG,
        "Application set to slot %d "
        "(stub — emulator launch not yet implemented)",
        slot
    );
}


/* =========================================================================
 * SLEEP
 * =========================================================================
 */

void odroid_system_sleep(void)
{
    /*
     * No deep-sleep implementation for this compatibility layer.
     */

    ESP_LOGW(
        TAG,
        "odroid_system_sleep() called — "
        "no deep sleep on P4, doing nothing"
    );
}


/* =========================================================================
 * LED
 * =========================================================================
 */

void odroid_system_led_set(
    int value)
{
    /*
     * The original Guition-specific implementation did not provide
     * a usable indicator LED for this compatibility layer.
     */

    (void)value;
}
