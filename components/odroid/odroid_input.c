/*
 * Odroid Input Compatibility Layer — ESP32-P4 Implementation
 *
 * WT9932P4-TINY version
 *
 * Input sources:
 *   - USB HID gamepad
 *   - Paddle ADC
 *   - Battery ADC
 *
 * The original Guition display implementation also supported a GT911
 * touch panel. The new hardware uses a 320x240 ST7789V SPI display
 * without touch, therefore all GT911/touch-panel access has been removed.
 *
 * IMPORTANT:
 *
 * The original project used GPIO49 and GPIO50 for the custom GPIO
 * gamepad's analog joystick.
 *
 * On the WT9932P4-TINY configuration used by this project:
 *
 *   GPIO49 = I2S BCK
 *   GPIO48 = I2S WS
 *   GPIO50 = I2S DATA
 *   GPIO47 = amplifier enable
 *
 * Therefore the old GPIO gamepad analog inputs MUST NOT be initialized
 * here, otherwise they would conflict with the audio subsystem.
 *
 * Physical USB gamepad mapping:
 *
 *   D-pad / left-stick  → ODROID_INPUT_UP/DOWN/LEFT/RIGHT
 *   A                   → ODROID_INPUT_A
 *   B                   → ODROID_INPUT_B
 *   X                   → ODROID_INPUT_X
 *   Y                   → ODROID_INPUT_Y
 *   L1 / L2             → ODROID_INPUT_L
 *   R1 / R2             → ODROID_INPUT_R
 *   SELECT              → ODROID_INPUT_SELECT
 *   START               → ODROID_INPUT_START
 *
 * Virtual system buttons:
 *
 *   X → MENU
 *   Y → VOLUME
 *
 * unless odroid_input_xy_menu_disable is set.
 */

#include "odroid_input.h"
#include "gamepad.h"

#include <string.h>
#include <stdio.h>
#include <sys/stat.h>

#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "soc/adc_channel.h"
#include "driver/gpio.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


static const char *TAG = "odroid_input";


/* =========================================================================
 * GLOBAL STATE
 * =========================================================================
 */

static bool s_initialized = false;


/*
 * Kept for API compatibility.
 *
 * There is no touch controller on the new ST7789V display.
 */
bool odroid_input_touch_buttons_disable = false;


/*
 * X/Y → MENU/VOLUME compatibility switch.
 */
bool odroid_input_xy_menu_disable = false;


/* =========================================================================
 * PADDLE ADC
 * =========================================================================
 *
 * Original project:
 *
 *   GPIO51 = ADC2_CH2
 *
 * This pin is not part of the LCD or I2S pinout supplied for the
 * WT9932P4-TINY, so it can remain available for the paddle input.
 */

#define PADDLE_ADC_UNIT       ADC_UNIT_2
#define PADDLE_ADC_CHANNEL    ADC_CHANNEL_2   /* GPIO 51 */

volatile int odroid_paddle_adc_raw = -1;

#ifndef CONFIG_HDMI_OUTPUT
static adc_oneshot_unit_handle_t s_paddle_adc_handle = NULL;
#endif


/* =========================================================================
 * BATTERY ADC
 * =========================================================================
 *
 * Original project:
 *
 *   GPIO53 = ADC2_CH4
 *
 * Voltage divider:
 *
 *   68K battery side
 *   100K ground side
 *
 * Vgpio = Vbat × 100 / 168
 * Vbat  = Vgpio × 168 / 100
 */

#define BATTERY_ADC_CHANNEL    ADC_CHANNEL_4   /* GPIO 53 */
#define BATTERY_DIVIDER_NUM    168
#define BATTERY_DIVIDER_DEN    100

#ifndef CONFIG_HDMI_OUTPUT
static adc_oneshot_unit_handle_t s_battery_adc_handle = NULL;
#endif


/* =========================================================================
 * OLD CUSTOM GPIO GAMEPAD
 * =========================================================================
 *
 * The original implementation used:
 *
 *   GPIO49 = joystick left/right
 *   GPIO50 = joystick up/down
 *   GPIO52 = A/B
 *
 * GPIO49 and GPIO50 are now reserved for I2S:
 *
 *   GPIO49 = BCK
 *   GPIO48 = WS
 *   GPIO50 = DATA
 *
 * Consequently the entire old GPIO gamepad implementation is disabled.
 *
 * USB HID gamepads remain fully supported.
 */

static bool s_gpio_pad_detected = false;


/*
 * Kept as a compatibility function.
 *
 * It always returns false because there is no longer a board-specific
 * GPIO gamepad detector in this target configuration.
 */
bool odroid_input_gpio_pad_detected(void)
{
    return false;
}


/* =========================================================================
 * USB GAMEPAD MAPPING
 * =========================================================================
 *
 * Index:
 *
 *   0 = A
 *   1 = B
 *   2 = X
 *   3 = Y
 *   4 = L
 *   5 = R
 *   6 = SELECT
 *   7 = START
 *   8 = UP
 *   9 = DOWN
 *   10 = LEFT
 *   11 = RIGHT
 */

static odroid_usb_map_t s_usb_map = {
    .btn = {
        GAMEPAD_BTN_A,
        GAMEPAD_BTN_B,
        GAMEPAD_BTN_X,
        GAMEPAD_BTN_Y,

        GAMEPAD_BTN_L1 |
        GAMEPAD_BTN_L2,

        GAMEPAD_BTN_R1 |
        GAMEPAD_BTN_R2,

        GAMEPAD_BTN_SELECT,
        GAMEPAD_BTN_START,

        0,
        0,
        0,
        0,
    }
};

static bool s_usb_map_loaded = false;

static uint16_t s_usb_map_vid = 0;
static uint16_t s_usb_map_pid = 0;


/* =========================================================================
 * INPUT INITIALIZATION
 * =========================================================================
 */

void odroid_input_gamepad_init(void)
{
    if (s_initialized) {
        return;
    }


    /*
     * IMPORTANT:
     *
     * Do NOT initialize GPIO49/50/52 here.
     *
     * GPIO49 and GPIO50 belong to I2S on the WT9932P4-TINY.
     */


    /*
     * USB gamepad is initialized by odroid_system_init().
     */
    s_initialized = true;


#ifdef CONFIG_HDMI_OUTPUT

    ESP_LOGI(
        TAG,
        "Input subsystem ready (USB HID gamepad, HDMI mode)"
    );

#else

    ESP_LOGI(
        TAG,
        "Input subsystem ready (USB HID gamepad)"
    );

#endif
}


/* =========================================================================
 * GAMEPAD READ
 * =========================================================================
 */

void odroid_input_gamepad_read(
    odroid_gamepad_state *state)
{
    if (!state) {
        return;
    }


    memset(
        state,
        0,
        sizeof(odroid_gamepad_state)
    );


    gamepad_state_t gp;

    memset(
        &gp,
        0,
        sizeof(gp)
    );


    gamepad_get_state(&gp);


    if (gp.connected) {

        /*
         * Auto-load mapping for this controller when:
         *
         *   - first connected
         *   - VID changes
         *   - PID changes
         */
        uint16_t vid = 0;
        uint16_t pid = 0;

        gamepad_get_vid_pid(
            &vid,
            &pid
        );


        if (!s_usb_map_loaded ||
            vid != s_usb_map_vid ||
            pid != s_usb_map_pid) {

            odroid_input_usb_map_load();
        }


        /* -------------------------------------------------------------
         * D-PAD
         * -------------------------------------------------------------
         */

        state->values[
            ODROID_INPUT_UP
        ] =
            (gp.dpad & GAMEPAD_DPAD_UP)
            ? 1
            : 0;


        state->values[
            ODROID_INPUT_DOWN
        ] =
            (gp.dpad & GAMEPAD_DPAD_DOWN)
            ? 1
            : 0;


        state->values[
            ODROID_INPUT_LEFT
        ] =
            (gp.dpad & GAMEPAD_DPAD_LEFT)
            ? 1
            : 0;


        state->values[
            ODROID_INPUT_RIGHT
        ] =
            (gp.dpad & GAMEPAD_DPAD_RIGHT)
            ? 1
            : 0;


        /* -------------------------------------------------------------
         * LEFT ANALOG STICK → D-PAD
         * -------------------------------------------------------------
         *
         * Axis range is expected to be approximately -128..127.
         */
        if (gp.axis_ly < -64) {

            state->values[
                ODROID_INPUT_UP
            ] = 1;

        } else if (gp.axis_ly > 64) {

            state->values[
                ODROID_INPUT_DOWN
            ] = 1;
        }


        if (gp.axis_lx < -64) {

            state->values[
                ODROID_INPUT_LEFT
            ] = 1;

        } else if (gp.axis_lx > 64) {

            state->values[
                ODROID_INPUT_RIGHT
            ] = 1;
        }


        /* -------------------------------------------------------------
         * OPTIONAL D-PAD BUTTON MAPPING
         * -------------------------------------------------------------
         *
         * Some USB controllers report their D-pad as ordinary buttons.
         */
        if (s_usb_map.btn[8] &&
            (gp.buttons & s_usb_map.btn[8])) {

            state->values[
                ODROID_INPUT_UP
            ] = 1;
        }


        if (s_usb_map.btn[9] &&
            (gp.buttons & s_usb_map.btn[9])) {

            state->values[
                ODROID_INPUT_DOWN
            ] = 1;
        }


        if (s_usb_map.btn[10] &&
            (gp.buttons & s_usb_map.btn[10])) {

            state->values[
                ODROID_INPUT_LEFT
            ] = 1;
        }


        if (s_usb_map.btn[11] &&
            (gp.buttons & s_usb_map.btn[11])) {

            state->values[
                ODROID_INPUT_RIGHT
            ] = 1;
        }


        /* -------------------------------------------------------------
         * FACE BUTTONS
         * ------------------------------------------------------------- */

        state->values[
            ODROID_INPUT_A
        ] =
            (gp.buttons & s_usb_map.btn[0])
            ? 1
            : 0;


        state->values[
            ODROID_INPUT_B
        ] =
            (gp.buttons & s_usb_map.btn[1])
            ? 1
            : 0;


        state->values[
            ODROID_INPUT_X
        ] =
            (gp.buttons & s_usb_map.btn[2])
            ? 1
            : 0;


        state->values[
            ODROID_INPUT_Y
        ] =
            (gp.buttons & s_usb_map.btn[3])
            ? 1
            : 0;


        /* -------------------------------------------------------------
         * SHOULDERS
         * ------------------------------------------------------------- */

        state->values[
            ODROID_INPUT_L
        ] =
            (gp.buttons & s_usb_map.btn[4])
            ? 1
            : 0;


        state->values[
            ODROID_INPUT_R
        ] =
            (gp.buttons & s_usb_map.btn[5])
            ? 1
            : 0;


        /* -------------------------------------------------------------
         * SYSTEM BUTTONS
         * ------------------------------------------------------------- */

        state->values[
            ODROID_INPUT_SELECT
        ] =
            (gp.buttons & s_usb_map.btn[6])
            ? 1
            : 0;


        state->values[
            ODROID_INPUT_START
        ] =
            (gp.buttons & s_usb_map.btn[7])
            ? 1
            : 0;


#ifndef CONFIG_HDMI_OUTPUT

        /*
         * Paddle ADC.
         *
         * Only read here when explicitly initialized.
         */
        if (s_paddle_adc_handle) {

            int raw = 0;

            if (adc_oneshot_read(
                    s_paddle_adc_handle,
                    PADDLE_ADC_CHANNEL,
                    &raw
                ) == ESP_OK) {

                odroid_paddle_adc_raw = raw;
            }
        }

#endif
    }


#ifdef CONFIG_HDMI_OUTPUT

    /*
     * HDMI mode:
     *
     * L2 → MENU
     * R2 → VOLUME
     *
     * This behavior from the original implementation is preserved.
     */
    {
        gamepad_state_t gp_hdmi;

        memset(
            &gp_hdmi,
            0,
            sizeof(gp_hdmi)
        );

        gamepad_get_state(&gp_hdmi);


        if (gp_hdmi.connected) {

            if (gp_hdmi.buttons &
                GAMEPAD_BTN_L2) {

                state->values[
                    ODROID_INPUT_MENU
                ] = 1;
            }


            if (gp_hdmi.buttons &
                GAMEPAD_BTN_R2) {

                state->values[
                    ODROID_INPUT_VOLUME
                ] = 1;
            }
        }
    }

#endif


    /* -------------------------------------------------------------
     * X / Y SYSTEM SHORTCUTS
     * -------------------------------------------------------------
     *
     * Preserved from the original project:
     *
     * X → MENU
     * Y → VOLUME
     *
     * This is disabled for emulators that use X/Y natively by setting
     * odroid_input_xy_menu_disable.
     */
    if (!odroid_input_xy_menu_disable) {

        state->values[
            ODROID_INPUT_MENU
        ] |=
            state->values[
                ODROID_INPUT_X
            ];


        state->values[
            ODROID_INPUT_VOLUME
        ] |=
            state->values[
                ODROID_INPUT_Y
            ];
    }
}


/* =========================================================================
 * RAW INPUT
 * =========================================================================
 */

odroid_gamepad_state odroid_input_read_raw(void)
{
    odroid_gamepad_state state;

    odroid_input_gamepad_read(
        &state
    );

    return state;
}


/* =========================================================================
 * PADDLE ADC
 * =========================================================================
 */

void odroid_paddle_adc_init(void)
{
#ifdef CONFIG_HDMI_OUTPUT

    /*
     * HDMI target has no paddle ADC.
     */
    return;

#else

    if (s_paddle_adc_handle) {
        return;
    }


    /*
     * Reuse the battery ADC unit when possible.
     *
     * ADC oneshot handles can be shared between the functions
     * as long as they refer to the same ADC unit.
     */
    if (s_battery_adc_handle) {

        s_paddle_adc_handle =
            s_battery_adc_handle;

    } else {

        adc_oneshot_unit_init_cfg_t unit_cfg = {
            .unit_id = PADDLE_ADC_UNIT,
        };


        esp_err_t err =
            adc_oneshot_new_unit(
                &unit_cfg,
                &s_paddle_adc_handle
            );


        if (err != ESP_OK) {

            ESP_LOGW(
                TAG,
                "Paddle ADC: unit init failed (%s)",
                esp_err_to_name(err)
            );

            return;
        }
    }


    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten =
            ADC_ATTEN_DB_12,

        .bitwidth =
            ADC_BITWIDTH_12,
    };


    esp_err_t err =
        adc_oneshot_config_channel(
            s_paddle_adc_handle,
            PADDLE_ADC_CHANNEL,
            &chan_cfg
        );


    if (err != ESP_OK) {

        ESP_LOGW(
            TAG,
            "Paddle ADC: channel init failed (%s)",
            esp_err_to_name(err)
        );

        return;
    }


    ESP_LOGI(
        TAG,
        "Paddle ADC initialized: ADC2_CH2 (GPIO51)"
    );

#endif
}


/* =========================================================================
 * BATTERY ADC INITIALIZATION
 * =========================================================================
 */

void odroid_input_battery_level_init(void)
{
#ifdef CONFIG_HDMI_OUTPUT

    /*
     * HDMI target has no battery.
     */
    return;

#else

    if (s_battery_adc_handle) {
        return;
    }


    /*
     * Reuse paddle ADC unit if it has already been initialized.
     */
    if (s_paddle_adc_handle) {

        s_battery_adc_handle =
            s_paddle_adc_handle;

    } else {

        adc_oneshot_unit_init_cfg_t unit_cfg = {
            .unit_id = ADC_UNIT_2,
        };


        esp_err_t err =
            adc_oneshot_new_unit(
                &unit_cfg,
                &s_battery_adc_handle
            );


        if (err != ESP_OK) {

            ESP_LOGW(
                TAG,
                "Battery ADC: unit init failed (%s)",
                esp_err_to_name(err)
            );

            return;
        }
    }


    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten =
            ADC_ATTEN_DB_12,

        .bitwidth =
            ADC_BITWIDTH_12,
    };


    esp_err_t err =
        adc_oneshot_config_channel(
            s_battery_adc_handle,
            BATTERY_ADC_CHANNEL,
            &chan_cfg
        );


    if (err != ESP_OK) {

        ESP_LOGW(
            TAG,
            "Battery ADC: channel init failed (%s)",
            esp_err_to_name(err)
        );

        return;
    }


    ESP_LOGI(
        TAG,
        "Battery ADC initialized: ADC2_CH4 (GPIO53), divider 68K/100K"
    );

#endif
}


/* =========================================================================
 * BATTERY READ
 * =========================================================================
 */

void odroid_input_battery_level_read(
    odroid_battery_state *state)
{
    if (!state) {
        return;
    }


#ifdef CONFIG_HDMI_OUTPUT

    /*
     * HDMI target:
     * no battery telemetry.
     */
    state->millivolts = 4200;
    state->percentage = 100;
    state->charging = false;

#else

    if (!s_battery_adc_handle) {

        /*
         * ADC not initialized.
         *
         * Preserve original fallback behavior.
         */
        state->millivolts = 4200;
        state->percentage = 100;
        state->charging = false;

        return;
    }


    /*
     * Average four ADC samples.
     */
    int sum = 0;
    int ok_count = 0;


    for (int i = 0; i < 4; ++i) {

        int raw = 0;


        esp_err_t err =
            adc_oneshot_read(
                s_battery_adc_handle,
                BATTERY_ADC_CHANNEL,
                &raw
            );


        if (err == ESP_OK) {

            sum += raw;
            ok_count++;
        }
    }


    int raw_avg =
        ok_count > 0
        ? sum / ok_count
        : 0;


    /*
     * ADC raw → GPIO millivolts.
     *
     * This keeps the calibration used by the original project.
     */
    int gpio_mv =
        raw_avg *
        3861 /
        4095;


    /*
     * Voltage divider:
     *
     * Vbat = Vgpio × 168 / 100
     */
    int bat_mv =
        gpio_mv *
        BATTERY_DIVIDER_NUM /
        BATTERY_DIVIDER_DEN;


    /*
     * Original charging heuristic.
     */
    bool charging =
        bat_mv > 3900;


    /*
     * Clamp battery range.
     */
    if (bat_mv > 3900) {
        bat_mv = 3900;
    }


    if (bat_mv < 3300) {
        bat_mv = 3300;
    }


    /*
     * Convert 3300..3900 mV to 0..100%.
     */
    int pct =
        (bat_mv - 3300) *
        100 /
        (3900 - 3300);


    if (pct > 100) {
        pct = 100;
    }


    if (pct < 0) {
        pct = 0;
    }


    state->millivolts =
        bat_mv;

    state->percentage =
        pct;

    state->charging =
        charging;

#endif
}


/* =========================================================================
 * BATTERY MONITOR
 * =========================================================================
 */

void odroid_input_battery_monitor_enabled_set(
    bool enabled)
{
    /*
     * No separate monitor task is currently required.
     *
     * Battery level is sampled on demand.
     */
    (void)enabled;
}


/* =========================================================================
 * USB GAMEPAD STATUS
 * =========================================================================
 */

bool odroid_input_usb_gamepad_connected(void)
{
    return gamepad_is_connected();
}


/* =========================================================================
 * USB GAMEPAD BUTTON MAPPING
 * =========================================================================
 *
 * Mapping file:
 *
 *   4-byte magic "GMAP"
 *   followed by ODROID_USB_MAP_COUNT uint32_t values
 *
 * Stored on:
 *
 *   /sd/odroid/gamepad/VID_PID.map
 */

#define GMAP_MAGIC 0x50414D47
/* "GMAP" little-endian */


/* -------------------------------------------------------------------------
 * Default mapping
 * -------------------------------------------------------------------------
 */

static void usb_map_set_defaults(void)
{
    s_usb_map.btn[0] =
        GAMEPAD_BTN_A;


    s_usb_map.btn[1] =
        GAMEPAD_BTN_B;


    s_usb_map.btn[2] =
        GAMEPAD_BTN_X;


    s_usb_map.btn[3] =
        GAMEPAD_BTN_Y;


    s_usb_map.btn[4] =
        GAMEPAD_BTN_L1 |
        GAMEPAD_BTN_L2;


    s_usb_map.btn[5] =
        GAMEPAD_BTN_R1 |
        GAMEPAD_BTN_R2;


    s_usb_map.btn[6] =
        GAMEPAD_BTN_SELECT;


    s_usb_map.btn[7] =
        GAMEPAD_BTN_START;


    /*
     * D-pad:
     *
     * 0 means use native D-pad / analog stick detection.
     */
    s_usb_map.btn[8] =
        0;


    s_usb_map.btn[9] =
        0;


    s_usb_map.btn[10] =
        0;


    s_usb_map.btn[11] =
        0;
}


/* -------------------------------------------------------------------------
 * Mapping file path
 * -------------------------------------------------------------------------
 */

static bool usb_map_get_path(
    char *buf,
    size_t buflen)
{
    if (!buf || buflen == 0) {
        return false;
    }


    uint16_t vid = 0;
    uint16_t pid = 0;


    gamepad_get_vid_pid(
        &vid,
        &pid
    );


    if (vid == 0 &&
        pid == 0) {

        return false;
    }


    snprintf(
        buf,
        buflen,
        "/sd/odroid/gamepad/%04X_%04X.map",
        vid,
        pid
    );


    return true;
}


/* =========================================================================
 * MAPPING EXISTS
 * =========================================================================
 */

bool odroid_input_usb_map_exists(void)
{
    char path[80];


    if (!usb_map_get_path(
            path,
            sizeof(path))) {

        return false;
    }


    struct stat st;


    return (
        stat(
            path,
            &st
        ) == 0
    );
}


/* =========================================================================
 * MAPPING LOAD
 * =========================================================================
 */

void odroid_input_usb_map_load(void)
{
    uint16_t vid = 0;
    uint16_t pid = 0;


    gamepad_get_vid_pid(
        &vid,
        &pid
    );


    /*
     * Always start from defaults.
     */
    usb_map_set_defaults();


    s_usb_map_loaded = true;

    s_usb_map_vid =
        vid;

    s_usb_map_pid =
        pid;


    char path[80];


    if (!usb_map_get_path(
            path,
            sizeof(path))) {

        return;
    }


    /*
     * SD may not be mounted yet.
     */
    FILE *f =
        fopen(
            path,
            "rb"
        );


    if (!f) {

        ESP_LOGI(
            TAG,
            "No custom map for %04X:%04X, using defaults",
            vid,
            pid
        );

        return;
    }


    uint32_t magic = 0;


    if (fread(
            &magic,
            4,
            1,
            f
        ) != 1 ||
        magic != GMAP_MAGIC) {

        ESP_LOGW(
            TAG,
            "Invalid map file magic for %04X:%04X",
            vid,
            pid
        );

        fclose(f);

        return;
    }


    uint32_t data[
        ODROID_USB_MAP_COUNT
    ];


    memset(
        data,
        0,
        sizeof(data)
    );


    size_t nread =
        fread(
            data,
            sizeof(uint32_t),
            ODROID_USB_MAP_COUNT,
            f
        );


    fclose(f);


    /*
     * Old files contain only the original eight mappings.
     */
    if (nread < 8) {

        ESP_LOGW(
            TAG,
            "Short map file for %04X:%04X (got %d entries)",
            vid,
            pid,
            (int)nread
        );


        usb_map_set_defaults();

        return;
    }


    /*
     * Apply complete mapping.
     */
    for (int i = 0;
         i < ODROID_USB_MAP_COUNT;
         ++i) {

        s_usb_map.btn[i] =
            data[i];
    }


    ESP_LOGI(
        TAG,
        "Loaded custom map for %04X:%04X",
        vid,
        pid
    );
}


/* =========================================================================
 * MAPPING SAVE
 * =========================================================================
 */

bool odroid_input_usb_map_save(
    const odroid_usb_map_t *map)
{
    if (!map) {
        return false;
    }


    char path[80];


    if (!usb_map_get_path(
            path,
            sizeof(path))) {

        return false;
    }


    /*
     * Ensure directories exist.
     *
     * The SD card must already be mounted.
     */
    mkdir(
        "/sd/odroid",
        0775
    );


    mkdir(
        "/sd/odroid/gamepad",
        0775
    );


    FILE *f =
        fopen(
            path,
            "wb"
        );


    if (!f) {

        ESP_LOGE(
            TAG,
            "Failed to create map file: %s",
            path
        );

        return false;
    }


    uint32_t magic =
        GMAP_MAGIC;


    fwrite(
        &magic,
        4,
        1,
        f
    );


    fwrite(
        map->btn,
        sizeof(uint32_t),
        ODROID_USB_MAP_COUNT,
        f
    );


    fclose(f);


    /*
     * Apply immediately.
     */
    memcpy(
        &s_usb_map,
        map,
        sizeof(odroid_usb_map_t)
    );


    ESP_LOGI(
        TAG,
        "Saved map to %s",
        path
    );


    return true;
}


/* =========================================================================
 * MAPPING GET
 * =========================================================================
 */

void odroid_input_usb_map_get(
    odroid_usb_map_t *map)
{
    if (!map) {
        return;
    }


    memcpy(
        map,
        &s_usb_map,
        sizeof(odroid_usb_map_t)
    );
}


/* =========================================================================
 * MAPPING SET
 * =========================================================================
 */

void odroid_input_usb_map_set(
    const odroid_usb_map_t *map)
{
    if (!map) {
        return;
    }


    memcpy(
        &s_usb_map,
        map,
        sizeof(odroid_usb_map_t)
    );


    /*
     * Mark the map as loaded so the next gamepad read does not
     * immediately overwrite it with the SD/default mapping.
     */
    s_usb_map_loaded = true;
}