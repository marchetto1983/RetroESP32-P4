/*
 * RetroESP32-P4
 *
 * Odroid Display Compatibility Layer — ESP32-P4
 *
 * LCD target:
 *   ST7789V
 *   320x240
 *   SPI
 *   80 MHz
 *
 * WT9932P4-TINY LCD pinout:
 *   MISO  GPIO18
 *   MOSI  GPIO19
 *   CLK   GPIO22
 *   CS    GPIO20
 *   DC    GPIO21
 *   RST   GPIO23
 *   BCKL  GPIO17
 *
 * The original project was designed around a 480x800 MIPI-DSI
 * ST7701 display. The original display path was:
 *
 *   320x240 framebuffer
 *        -> PPA rotate
 *        -> PPA scale
 *        -> 480x800 ST7701
 *
 * The new display path is:
 *
 *   320x240 RGB565 framebuffer
 *        -> SPI DMA
 *        -> ST7789V 320x240
 *
 * The public ILI9341-compatible API is deliberately preserved so
 * emulator code does not need to be modified.
 */
#include "odroid_display.h"
#include "pins_config.h"
#ifdef CONFIG_HDMI_OUTPUT
#include "ppa_engine.h"
#include "hdmi_display.h"
#include "odroid_system.h"
#include "esp_cache.h"
#else
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_commands.h"
#endif
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "odroid_display";

/* =========================================================================
 * DISPLAY GEOMETRY
 * =========================================================================
 */
#ifdef CONFIG_HDMI_OUTPUT
#define FB_W 640
#define FB_H 480
#else
#define FB_W 320
#define FB_H 240
#endif
#define FB_PIXELS   (FB_W * FB_H)
#define FB_SIZE     (FB_PIXELS * sizeof(uint16_t))

/* Emulator standard framebuffer. */
#define EMU_W       320
#define EMU_H       240
#define EMU_PIXELS  (EMU_W * EMU_H)
#define EMU_SIZE    (EMU_PIXELS * sizeof(uint16_t))

/* =========================================================================
 * GLOBAL FRAMEBUFFER
 * =========================================================================
 */
static uint16_t *s_framebuffer = NULL;
static bool s_fb_dirty = false;

/* =========================================================================
 * DISPLAY MUTEX
 * =========================================================================
 */
static SemaphoreHandle_t s_display_mutex = NULL;
static void ensure_mutex(void)
{
    if (!s_display_mutex) {
        s_display_mutex = xSemaphoreCreateMutex();
        if (!s_display_mutex) {
            ESP_LOGE(TAG, "Failed to create display mutex");
            abort();
        }
    }
}
void odroid_display_lock(void)
{
    ensure_mutex();
    xSemaphoreTake(s_display_mutex, portMAX_DELAY);
}
void odroid_display_unlock(void)
{
    if (s_display_mutex) {
        xSemaphoreGive(s_display_mutex);
    }
}
void odroid_display_lock_gb_display(void)
{
    odroid_display_lock();
}
void odroid_display_unlock_gb_display(void)
{
    odroid_display_unlock();
}
void odroid_display_lock_nes_display(void)
{
    odroid_display_lock();
}
void odroid_display_unlock_nes_display(void)
{
    odroid_display_unlock();
}
void odroid_display_lock_sms_display(void)
{
    odroid_display_lock();
}
void odroid_display_unlock_sms_display(void)
{
    odroid_display_unlock();
}

/* =========================================================================
 * ST7789V
 * =========================================================================
 */
#ifndef CONFIG_HDMI_OUTPUT
static esp_lcd_panel_io_handle_t s_lcd_io = NULL;
static esp_lcd_panel_handle_t s_lcd_panel = NULL;
static bool s_lcd_initialized = false;
#endif
/*
 * Your tested initialization:
 *
 *   0x36, 0x60  -> MADCTL
 *   0x3A, 0x05  -> RGB565
 *   0x11        -> Sleep Out
 *   0x29        -> Display ON
 *
 * We intentionally restore these values after the generic ESP-IDF
 * ST7789 initialization so the panel keeps the exact orientation
 * already verified on your hardware.
 */
static esp_err_t st7789_write_cmd(uint8_t cmd)
{
    if (!s_lcd_io) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_lcd_panel_io_tx_param(
        s_lcd_io,
        cmd,
        NULL,
        0
    );
}
static esp_err_t st7789_write_cmd_u8(uint8_t cmd, uint8_t value)
{
    if (!s_lcd_io) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_lcd_panel_io_tx_param(
        s_lcd_io,
        cmd,
        &value,
        1
    );
}

/*
 * Re-apply the known-good panel configuration.
 */
static esp_err_t st7789_apply_known_init(void)
{
    esp_err_t ret;

    /*
     * MADCTL
     *
     * 0x60 is the orientation already tested by the user.
     */
    ret = st7789_write_cmd_u8(
        LCD_CMD_MADCTL,
        LCD_MADCTL_VALUE
    );
    if (ret != ESP_OK) {
        return ret;
    }

    /*
     * RGB565.
     */
    ret = st7789_write_cmd_u8(
        LCD_CMD_COLMOD,
        LCD_PIXEL_FORMAT
    );
    if (ret != ESP_OK) {
        return ret;
    }

    /*
     * RAMCTRL
     *
     * Explicitly configure the ST7789 memory data order.
     *
     * 0xB0 = RAMCTRL
     * 0x00, 0xE8 = RGB565 memory configuration.
     */
    {
        const uint8_t ramctrl[] = {
            0x00,
            0xE8
        };

        ret = esp_lcd_panel_io_tx_param(
            s_lcd_io,
            0xB0,
            ramctrl,
            sizeof(ramctrl)
        );
        if (ret != ESP_OK) {
            return ret;
        }
    }

    /*
     * Sleep Out.
     */
    ret = st7789_write_cmd(0x11);
    if (ret != ESP_OK) {
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(120));

    /*
     * Display ON.
     */
    ret = st7789_write_cmd(0x29);
    if (ret != ESP_OK) {
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(20));

    return ESP_OK;
}

/* =========================================================================
 * BACKLIGHT
 * =========================================================================
 */
#define BL_GPIO       LCD_BK_LIGHT_GPIO
#define BL_LEDC_CH    LEDC_CHANNEL_0
#define BL_LEDC_TIMER LEDC_TIMER_0
#define BL_DUTY_RES   LEDC_TIMER_13_BIT
#define BL_DUTY_MAX   ((1 << 13) - 1)
#define BL_FREQ_HZ    5000 //20000
static bool s_backlight_init = false;

static void backlight_init(void)
{
    if (s_backlight_init) {
        return;
    }
    ledc_timer_config_t timer_cfg = {
        .speed_mode       = LEDC_LOW_SPEED_MODE,
        .duty_resolution  = BL_DUTY_RES,
        .timer_num        = BL_LEDC_TIMER,
        .freq_hz          = BL_FREQ_HZ,
        .clk_cfg          = LEDC_AUTO_CLK,
    };
    esp_err_t ret = ledc_timer_config(&timer_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Backlight timer init failed: %s",
            esp_err_to_name(ret)
        );
        return;
    }
    ledc_channel_config_t channel_cfg = {
        .gpio_num       = BL_GPIO,
        .speed_mode     = LEDC_LOW_SPEED_MODE,
        .channel        = BL_LEDC_CH,
        .intr_type      = LEDC_INTR_DISABLE,
        .timer_sel      = BL_LEDC_TIMER,
        .duty           = BL_DUTY_MAX,
        .hpoint         = 0,
        .flags.output_invert = 0,
    };
    ret = ledc_channel_config(&channel_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Backlight channel init failed: %s",
            esp_err_to_name(ret)
        );
        return;
    }
    s_backlight_init = true;
    ESP_LOGI(
        TAG,
        "ST7789 backlight initialized on GPIO%d",
        BL_GPIO
    );
}

static void backlight_set_percent(uint8_t percent)
{
    if (!s_backlight_init) {
        return;
    }
    if (percent > 100) {
        percent = 100;
    }
    uint32_t duty =
        ((uint32_t)percent * BL_DUTY_MAX) / 100;
    ledc_set_duty(
        LEDC_LOW_SPEED_MODE,
        BL_LEDC_CH,
        duty
    );
    ledc_update_duty(
        LEDC_LOW_SPEED_MODE,
        BL_LEDC_CH
    );
}

/* =========================================================================
 * ST7789 INITIALIZATION
 * =========================================================================
 */
static esp_err_t st7789_spi_init(void)
{
    if (s_lcd_initialized) {
        return ESP_OK;
    }
    ESP_LOGI(TAG, "Initializing ST7789V SPI LCD");
    spi_bus_config_t bus_cfg = {
        .mosi_io_num     = LCD_SPI_MOSI,
        .miso_io_num     = LCD_SPI_MISO,
        .sclk_io_num     = LCD_SPI_CLK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .data4_io_num    = -1,
        .data5_io_num    = -1,
        .data6_io_num    = -1,
        .data7_io_num    = -1,
        .max_transfer_sz = FB_SIZE,
        .flags           = SPICOMMON_BUSFLAG_MASTER,
        .isr_cpu_id      = ESP_INTR_CPU_AFFINITY_AUTO,
        .intr_flags      = 0,
    };
    esp_err_t ret = spi_bus_initialize(
        LCD_SPI_HOST,
        &bus_cfg,
        SPI_DMA_CH_AUTO
    );
    if (ret != ESP_OK) {
        ESP_LOGE(
            TAG,
            "spi_bus_initialize failed: %s",
            esp_err_to_name(ret)
        );
        return ret;
    }

    /*
     * ST7789 SPI interface.
     *
     * 80 MHz is intentionally used because the actual display/cabling
     * has already been tested at this frequency.
     */
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num       = LCD_SPI_DC,
        .cs_gpio_num       = LCD_SPI_CS,
        .pclk_hz           = LCD_SPI_FREQ_HZ,
        .lcd_cmd_bits      = 8,
        .lcd_param_bits    = 8,
        .spi_mode          = 0,
        .trans_queue_depth = 8,
        .on_color_trans_done = NULL,
        .user_ctx           = NULL,
        .flags = {
            .dc_high_on_cmd = 0,
            .octal_mode = 0,
            .quad_mode = 0,
            .sio_mode = 0,
            .lsb_first = 0,
            .cs_high_active = 0,
        },
    };
    ret = esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)LCD_SPI_HOST,
        &io_cfg,
        &s_lcd_io
    );
    if (ret != ESP_OK) {
        ESP_LOGE(
            TAG,
            "esp_lcd_new_panel_io_spi failed: %s",
            esp_err_to_name(ret)
        );
        spi_bus_free(LCD_SPI_HOST);
        return ret;
    }

    /*
     * ESP-IDF ST7789 configuration.
     *
     * We deliberately select RGB here and then explicitly write
     * MADCTL=0x60 below. This keeps the panel orientation exactly
     * equal to the known-good initialization supplied by the user.
     *
     * BIG endian is used for RGB565 on the SPI wire.
     */
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = LCD_RST,
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB,
        .data_endian    = LCD_RGB_DATA_ENDIAN_BIG,
        .bits_per_pixel = 16,
        .flags = {
            .reset_active_high = 0,
        },
        .vendor_config = NULL,
    };
    ret = esp_lcd_new_panel_st7789(
        s_lcd_io,
        &panel_cfg,
        &s_lcd_panel
    );
    if (ret != ESP_OK) {
        ESP_LOGE(
            TAG,
            "esp_lcd_new_panel_st7789 failed: %s",
            esp_err_to_name(ret)
        );
        esp_lcd_panel_io_del(s_lcd_io);
        s_lcd_io = NULL;
        spi_bus_free(LCD_SPI_HOST);
        return ret;
    }

    ret = esp_lcd_panel_reset(s_lcd_panel);
    if (ret != ESP_OK) {
        ESP_LOGE(
            TAG,
            "ST7789 reset failed: %s",
            esp_err_to_name(ret)
        );
        return ret;
    }

    ret = esp_lcd_panel_init(s_lcd_panel);
    if (ret != ESP_OK) {
        ESP_LOGE(
            TAG,
            "ST7789 panel init failed: %s",
            esp_err_to_name(ret)
        );
        return ret;
    }

    /*
     * Override the generic ESP-IDF ST7789 initialization with the
     * exact initialization already verified on the physical display.
     */
    ret = st7789_apply_known_init();
    if (ret != ESP_OK) {
        ESP_LOGE(
            TAG,
            "ST7789 custom init failed: %s",
            esp_err_to_name(ret)
        );
        return ret;
    }

    /*
     * No software rotation is used.
     *
     * The panel itself is already configured by MADCTL=0x60.
     */
    /*eliminato tramite AI perché l'immagine risultava ruotata di 90 gradiret = esp_lcd_panel_swap_xy(
        s_lcd_panel,
        false
    );
    if (ret != ESP_OK) {
        ESP_LOGW(
            TAG,
            "swap_xy(false) returned %s",
            esp_err_to_name(ret)
        );
    }*/

    /*
     * Do not call esp_lcd_panel_mirror() here.
     *
     * MADCTL=0x60 is the known-good physical orientation and should
     * remain untouched.
     */

    /*
     * Display ON is already sent by our known-good initialization.
     */
    s_lcd_initialized = true;
    ESP_LOGI(
        TAG,
        "ST7789V ready: %dx%d RGB565 SPI @ %d Hz",
        LCD_H_RES,
        LCD_V_RES,
        LCD_SPI_FREQ_HZ
    );
    return ESP_OK;
}

/* =========================================================================
 * HDMI
 * =========================================================================
 */
#ifdef CONFIG_HDMI_OUTPUT
static hdmi_display_t s_hdmi_disp;
static bool s_hdmi_initialized = false;
#define HDMI_OUT_W   640
#define HDMI_OUT_H   480

static esp_err_t hdmi_init_if_needed(void)
{
    if (s_hdmi_initialized) {
        return ESP_OK;
    }
    esp_err_t ret = hdmi_display_init(
        HDMI_MODE_640x480,
        &s_hdmi_disp,
        odroid_system_get_i2c_bus()
    );
    if (ret != ESP_OK) {
        ESP_LOGE(
            TAG,
            "HDMI display init failed: 0x%x",
            ret
        );
        return ret;
    }
    s_hdmi_initialized = true;
    ESP_LOGI(
        TAG,
        "HDMI initialized: %dx%d",
        s_hdmi_disp.h_res,
        s_hdmi_disp.v_res
    );
    return ESP_OK;
}
#endif

/* =========================================================================
 * TIMING
 * =========================================================================
 */
static int64_t s_timing_lcd_acc = 0;
static int64_t s_timing_pal_acc = 0;
static int s_timing_count = 0;
#define TIMING_INTERVAL 60

/* =========================================================================
 * EMULATOR BUFFER
 * =========================================================================
 */
static uint16_t *s_emu_scaled = NULL;

static uint16_t *alloc_emu_buffer(void)
{
    if (s_emu_scaled) {
        return s_emu_scaled;
    }
    /*
     * Prefer internal SRAM because emulator rendering happens very often.
     */
    s_emu_scaled = heap_caps_aligned_calloc(
        64,
        1,
        EMU_SIZE,
        MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA
    );
    if (!s_emu_scaled) {
        s_emu_scaled = heap_caps_aligned_calloc(
            64,
            1,
            EMU_SIZE,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA
        );
    }
    if (!s_emu_scaled) {
        ESP_LOGE(
            TAG,
            "Failed to allocate emulator buffer: %d bytes",
            EMU_SIZE
        );
    }
    return s_emu_scaled;
}

/* =========================================================================
 * DISPLAY FLUSH
 * =========================================================================
 */
void display_flush(void)
{
    if (!s_fb_dirty || !s_framebuffer) {
        return;
    }
    s_fb_dirty = false;

#ifdef CONFIG_HDMI_OUTPUT
    if (!s_hdmi_initialized) {
        return;
    }
    /*
     * HDMI keeps the old PPA RGB565 -> RGB888 path.
     */
    int64_t t0 = esp_timer_get_time();
    esp_err_t ret = ppa_scale_rgb565_to_rgb888(
        s_framebuffer,
        FB_W,
        FB_H,
        1.0f,
        1.0f,
        s_hdmi_disp.fb,
        s_hdmi_disp.fb_size,
        NULL,
        NULL,
        false
    );
    int64_t t1 = esp_timer_get_time();
    if (ret != ESP_OK) {
        ESP_LOGE(
            TAG,
            "HDMI PPA flush failed: 0x%x",
            ret
        );
        return;
    }
    esp_cache_msync(
        s_hdmi_disp.fb,
        s_hdmi_disp.fb_size,
        ESP_CACHE_MSYNC_FLAG_DIR_C2M
    );
    int64_t t2 = esp_timer_get_time();
    s_timing_lcd_acc += t2 - t1;
    s_timing_count++;
#else
    if (!s_lcd_initialized || !s_lcd_panel) {
        return;
    }
    /*
     * Direct 320x240 -> ST7789V.
     *
     * No PPA.
     * No rotation.
     * No scaling.
     */
    int64_t t0 = esp_timer_get_time();
    esp_err_t ret = esp_lcd_panel_draw_bitmap(
        s_lcd_panel,
        0,
        0,
        LCD_H_RES,
        LCD_V_RES,
        s_framebuffer
    );
    int64_t t1 = esp_timer_get_time();
    if (ret != ESP_OK) {
        ESP_LOGE(
            TAG,
            "ST7789 frame transfer failed: %s",
            esp_err_to_name(ret)
        );
        return;
    }
    s_timing_lcd_acc += t1 - t0;
    s_timing_count++;
#endif

    if (s_timing_count >= TIMING_INTERVAL) {
        printf(
            "DISP TIMING (%d frames): LCD=%.1fms PAL=%.1fms\n",
            s_timing_count,
            s_timing_lcd_acc /
                (s_timing_count * 1000.0f),
            s_timing_pal_acc /
                (s_timing_count * 1000.0f)
        );
        s_timing_lcd_acc = 0;
        s_timing_pal_acc = 0;
        s_timing_count = 0;
    }
}

void display_flush_force(void)
{
    s_fb_dirty = true;
    display_flush();
}

void display_set_scale(float sx, float sy)
{
    /*
     * Kept for API compatibility.
     *
     * The physical display is already 320x240, so the normal LCD path
     * does not perform any scaling.
     */
    ESP_LOGI(
        TAG,
        "display_set_scale(%.3f, %.3f) ignored for native 320x240 LCD",
        sx,
        sy
    );
}

/* =========================================================================
 * EMULATOR 320x240 FLUSH
 * =========================================================================
 */
static void display_emu_flush_320x240(
    const uint16_t *buf,
    bool byte_swap)
{
    if (!buf) {
        return;
    }

#ifdef CONFIG_HDMI_OUTPUT
    if (!s_hdmi_initialized) {
        return;
    }
    esp_err_t ret = ppa_scale_rgb565_to_rgb888(
        buf,
        EMU_W,
        EMU_H,
        (float)HDMI_OUT_W / EMU_W,
        (float)HDMI_OUT_H / EMU_H,
        s_hdmi_disp.fb,
        s_hdmi_disp.fb_size,
        NULL,
        NULL,
        byte_swap
    );
    if (ret != ESP_OK) {
        ESP_LOGE(
            TAG,
            "HDMI emulator flush failed: 0x%x",
            ret
        );
        return;
    }
    esp_cache_msync(
        s_hdmi_disp.fb,
        s_hdmi_disp.fb_size,
        ESP_CACHE_MSYNC_FLAG_DIR_C2M
    );
#else
    /*
     * The physical display is exactly 320x240.
     *
     * Therefore the emulator framebuffer is sent directly.
     */
    if (!s_lcd_initialized || !s_lcd_panel) {
        return;
    }

    if (!byte_swap) {
        esp_err_t ret = esp_lcd_panel_draw_bitmap(
            s_lcd_panel,
            0,
            0,
            EMU_W,
            EMU_H,
            buf
        );
        if (ret != ESP_OK) {
            ESP_LOGE(
                TAG,
                "ST7789 emulator flush failed: %s",
                esp_err_to_name(ret)
            );
        }
    } else {
        /*
         * Some emulator paths provide RGB565 with the opposite byte
         * ordering. Use the common emulator buffer as a conversion
         * buffer instead of modifying the caller's framebuffer.
         */
        uint16_t *tmp = alloc_emu_buffer();
        if (!tmp) {
            return;
        }
        for (int i = 0; i < EMU_PIXELS; ++i) {
            uint16_t p = buf[i];
            tmp[i] = (uint16_t)(
                (p >> 8) |
                (p << 8)
            );
        }
        esp_err_t ret = esp_lcd_panel_draw_bitmap(
            s_lcd_panel,
            0,
            0,
            EMU_W,
            EMU_H,
            tmp
        );
        if (ret != ESP_OK) {
            ESP_LOGE(
                TAG,
                "ST7789 byte-swapped flush failed: %s",
                esp_err_to_name(ret)
            );
        }
    }
#endif
}

/* =========================================================================
 * ILI9341 COMPATIBILITY API
 * =========================================================================
 */
void ili9341_init(void)
{
    if (s_framebuffer) {
        return;
    }

    /*
     * The new physical LCD is native 320x240.
     *
     * This framebuffer is only 153600 bytes.
     */
    s_framebuffer = heap_caps_aligned_calloc(
        64,
        1,
        FB_SIZE,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA
    );
    if (!s_framebuffer) {
        /*
         * Fall back to internal DMA-capable memory if PSRAM allocation
         * fails.
         */
        s_framebuffer = heap_caps_aligned_calloc(
            64,
            1,
            FB_SIZE,
            MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA
        );
    }

    if (!s_framebuffer) {
        ESP_LOGE(
            TAG,
            "Failed to allocate framebuffer: %d bytes",
            FB_SIZE
        );
        return;
    }

    memset(
        s_framebuffer,
        0,
        FB_SIZE
    );

    ESP_LOGI(
        TAG,
        "Framebuffer allocated: %dx%d (%d bytes)",
        FB_W,
        FB_H,
        FB_SIZE
    );

#ifdef CONFIG_HDMI_OUTPUT
    if (hdmi_init_if_needed() != ESP_OK) {
        return;
    }
#else
    if (st7789_spi_init() != ESP_OK) {
        ESP_LOGE(
            TAG,
            "ST7789 SPI initialization failed"
        );
        return;
    }
    backlight_init();
    /*
     * Full brightness on startup.
     */
    backlight_set_percent(100);
#endif

    s_fb_dirty = true;
    /*
     * Clear physical display immediately.
     */
    display_flush_force();
}

void ili9341_write_frame_rectangleLE(
    int x,
    int y,
    int w,
    int h,
    const uint16_t *data)
{
    if (!s_framebuffer || !data) {
        return;
    }

    /*
     * Clip the rectangle.
     */
    int src_x = 0;
    int src_y = 0;
    if (x < 0) {
        src_x = -x;
        w += x;
        x = 0;
    }
    if (y < 0) {
        src_y = -y;
        h += y;
        y = 0;
    }
    if (x + w > FB_W) {
        w = FB_W - x;
    }
    if (y + h > FB_H) {
        h = FB_H - y;
    }
    if (w <= 0 || h <= 0) {
        return;
    }

    /*
     * Copy row-by-row.
     */
    for (int row = 0; row < h; ++row) {
        const uint16_t *src =
            data +
            (src_y + row) *
                (w + src_x) +
            src_x;
        uint16_t *dst =
            s_framebuffer +
            (y + row) * FB_W +
            x;
        memcpy(
            dst,
            src,
            (size_t)w * sizeof(uint16_t)
        );
    }

    s_fb_dirty = true;
}

void ili9341_clear(uint16_t color)
{
    if (!s_framebuffer) {
        return;
    }
    for (int i = 0; i < FB_PIXELS; ++i) {
        s_framebuffer[i] = color;
    }
    s_fb_dirty = true;
}

bool is_backlight_initialized(void)
{
#ifdef CONFIG_HDMI_OUTPUT
    return s_hdmi_initialized;
#else
    return s_backlight_init;
#endif
}

uint16_t *display_get_framebuffer(void)
{
    return s_framebuffer;
}

uint16_t *display_get_emu_buffer(void)
{
    return alloc_emu_buffer();
}

void display_emu_flush(void)
{
    if (s_emu_scaled) {
        display_emu_flush_320x240(
            s_emu_scaled,
            false
        );
    }
}

/* =========================================================================
 * RAW LCD DRAW
 * =========================================================================
 */
void display_lcd_draw_raw(
    uint16_t x,
    uint16_t y,
    uint16_t w,
    uint16_t h,
    const uint16_t *data)
{
    if (!data || !w || !h) {
        return;
    }

    odroid_display_lock();

#ifdef CONFIG_HDMI_OUTPUT
    /*
     * No direct portrait/raw LCD operation for HDMI.
     */
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)data;
#else
    if (s_lcd_initialized && s_lcd_panel) {
        /*
         * Clip to 320x240.
         */
        uint16_t draw_x = x;
        uint16_t draw_y = y;
        uint16_t draw_w = w;
        uint16_t draw_h = h;
        if (draw_x >= LCD_H_RES ||
            draw_y >= LCD_V_RES) {
            odroid_display_unlock();
            return;
        }
        if (draw_x + draw_w > LCD_H_RES) {
            draw_w = LCD_H_RES - draw_x;
        }
        if (draw_y + draw_h > LCD_V_RES) {
            draw_h = LCD_V_RES - draw_y;
        }

        esp_err_t ret = esp_lcd_panel_draw_bitmap(
            s_lcd_panel,
            draw_x,
            draw_y,
            draw_x + draw_w,
            draw_y + draw_h,
            data
        );
        if (ret != ESP_OK) {
            ESP_LOGE(
                TAG,
                "Raw LCD draw failed: %s",
                esp_err_to_name(ret)
            );
        }
    }
#endif

    odroid_display_unlock();
}

/* =========================================================================
 * GAME BOY
 * =========================================================================
 */
#define GAMEBOY_WIDTH   160
#define GAMEBOY_HEIGHT  144
#define GB_PIXELS       (GAMEBOY_WIDTH * GAMEBOY_HEIGHT)
static uint16_t *s_gb_temp = NULL;

static bool ensure_gb_temp(void)
{
    if (s_gb_temp) {
        return true;
    }
    s_gb_temp = heap_caps_aligned_calloc(
        64,
        1,
        GB_PIXELS * sizeof(uint16_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA
    );
    if (!s_gb_temp) {
        ESP_LOGE(
            TAG,
            "GB temp buffer allocation failed"
        );
        return false;
    }
    return true;
}

void ili9341_write_frame_gb(
    uint16_t *buffer,
    int scale)
{
    (void)scale;
    odroid_display_lock_gb_display();

    if (!buffer) {
        ili9341_clear(0x0000);
        display_flush();
        odroid_display_unlock_gb_display();
        return;
    }

    if (!ensure_gb_temp()) {
        odroid_display_unlock_gb_display();
        return;
    }

    uint16_t *emu = alloc_emu_buffer();
    if (!emu) {
        odroid_display_unlock_gb_display();
        return;
    }

    memcpy(
        s_gb_temp,
        buffer,
        GB_PIXELS * sizeof(uint16_t)
    );

    /*
     * 160x144 -> 320x240.
     *
     * Non-integer vertical scaling is handled by nearest neighbour.
     * This avoids the old 320x240 -> 480x640 rotation pipeline.
     */
    for (int y = 0; y < EMU_H; ++y) {
        int sy =
            y * GAMEBOY_HEIGHT /
            EMU_H;
        const uint16_t *src =
            &s_gb_temp[
                sy * GAMEBOY_WIDTH
            ];
        uint16_t *dst =
            &emu[
                y * EMU_W
            ];
        for (int x = 0; x < EMU_W; ++x) {
            int sx =
                x * GAMEBOY_WIDTH /
                EMU_W;
            dst[x] = src[sx];
        }
    }

    display_emu_flush_320x240(
        emu,
        false
    );

    odroid_display_unlock_gb_display();
}

/* =========================================================================
 * NES
 * =========================================================================
 */
#define NES_GAME_WIDTH   256
#define NES_GAME_HEIGHT  224
#define NES_PIXELS       (NES_GAME_WIDTH * NES_GAME_HEIGHT)
static uint16_t *s_nes_temp = NULL;

static bool ensure_nes_temp(void)
{
    if (s_nes_temp) {
        return true;
    }
    s_nes_temp = heap_caps_aligned_calloc(
        64,
        1,
        NES_PIXELS * sizeof(uint16_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA
    );
    if (!s_nes_temp) {
        ESP_LOGE(
            TAG,
            "NES temp buffer allocation failed"
        );
        return false;
    }
    return true;
}

void ili9341_write_frame_nes(
    uint8_t *buffer,
    uint16_t *myPalette,
    uint8_t scale)
{
    (void)scale;
    odroid_display_lock_nes_display();

    if (!buffer) {
        ili9341_clear(0x0000);
        display_flush();
        odroid_display_unlock_nes_display();
        return;
    }

    if (!myPalette ||
        !ensure_nes_temp()) {
        odroid_display_unlock_nes_display();
        return;
    }

    uint16_t *emu = alloc_emu_buffer();
    if (!emu) {
        odroid_display_unlock_nes_display();
        return;
    }

    /*
     * Palette conversion.
     *
     * The original NES path supplies a palette already byte-swapped
     * for the old hardware path. Preserve that behavior.
     */
    for (int i = 0; i < NES_PIXELS; ++i) {
        uint16_t pixel =
            myPalette[
                buffer[i]
            ];
        s_nes_temp[i] =
            (uint16_t)(
                (pixel >> 8) |
                (pixel << 8)
            );
    }

    /*
     * 256x224 -> 320x240.
     */
    for (int y = 0; y < EMU_H; ++y) {
        int sy =
            y * NES_GAME_HEIGHT /
            EMU_H;
        const uint16_t *src =
            &s_nes_temp[
                sy * NES_GAME_WIDTH
            ];
        uint16_t *dst =
            &emu[
                y * EMU_W
            ];
        for (int x = 0; x < EMU_W; ++x) {
            int sx =
                x * NES_GAME_WIDTH /
                EMU_W;
            dst[x] = src[sx];
        }
    }

    display_emu_flush_320x240(
        emu,
        false
    );

    odroid_display_unlock_nes_display();
}

/* =========================================================================
 * SMS / GAME GEAR
 * =========================================================================
 */
#define SMS_WIDTH        256
#define SMS_HEIGHT       192
#define GAMEGEAR_WIDTH   160
#define GAMEGEAR_HEIGHT  144
#define PIXEL_MASK       0x1F
#define SMS_MAX_PIXELS   (SMS_WIDTH * SMS_HEIGHT)
static uint16_t *s_sms_temp = NULL;

static bool ensure_sms_temp(void)
{
    if (s_sms_temp) {
        return true;
    }
    s_sms_temp = heap_caps_aligned_calloc(
        64,
        1,
        SMS_MAX_PIXELS * sizeof(uint16_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA
    );
    if (!s_sms_temp) {
        ESP_LOGE(
            TAG,
            "SMS temp buffer allocation failed"
        );
        return false;
    }
    return true;
}

void ili9341_write_frame_sms(
    uint8_t *buffer,
    uint16_t color[],
    uint8_t isGameGear,
    uint8_t scale)
{
    (void)scale;
    odroid_display_lock_sms_display();

    if (!buffer) {
        ili9341_clear(0x0000);
        display_flush();
        odroid_display_unlock_sms_display();
        return;
    }

    if (!color ||
        !ensure_sms_temp()) {
        odroid_display_unlock_sms_display();
        return;
    }

    uint16_t *emu = alloc_emu_buffer();
    if (!emu) {
        odroid_display_unlock_sms_display();
        return;
    }

    const int src_w =
        isGameGear ?
        GAMEGEAR_WIDTH :
        SMS_WIDTH;
    const int src_h =
        isGameGear ?
        GAMEGEAR_HEIGHT :
        SMS_HEIGHT;
    /*
     * Game Gear data is contained in a 256-wide source buffer
     * with a 48-pixel horizontal offset.
     */
    const int src_stride =
        isGameGear ?
        256 :
        SMS_WIDTH;
    const int src_x_off =
        isGameGear ?
        48 :
        0;

    for (int y = 0; y < src_h; ++y) {
        const uint8_t *src_row =
            &buffer[
                y * src_stride +
                src_x_off
            ];
        uint16_t *dst_row =
            &s_sms_temp[
                y * src_w
            ];
        for (int x = 0; x < src_w; ++x) {
            dst_row[x] =
                color[
                    src_row[x] &
                    PIXEL_MASK
                ];
        }
    }

    /*
     * Scale to the physical 320x240 panel.
     */
    for (int y = 0; y < EMU_H; ++y) {
        int sy =
            y * src_h /
            EMU_H;
        const uint16_t *src =
            &s_sms_temp[
                sy * src_w
            ];
        uint16_t *dst =
            &emu[
                y * EMU_W
            ];
        for (int x = 0; x < EMU_W; ++x) {
            int sx =
                x * src_w /
                EMU_W;
            dst[x] = src[sx];
        }
    }

    display_emu_flush_320x240(
        emu,
        false
    );

    odroid_display_unlock_sms_display();
}

/* =========================================================================
 * C64
 * =========================================================================
 */
void ili9341_write_frame_c64(
    uint8_t *buffer,
    uint16_t *palette)
{
    const int C64_DISPLAY_X = 384;
    const int C64_DISPLAY_Y = 272;
    odroid_display_lock();

    if (!buffer || !palette) {
        ili9341_clear(0x0000);
        display_flush();
        odroid_display_unlock();
        return;
    }

    uint16_t *emu = alloc_emu_buffer();
    if (!emu) {
        odroid_display_unlock();
        return;
    }

    /*
     * Center crop:
     *
     * 384x272 -> 320x240
     */
    const int offX =
        (C64_DISPLAY_X - EMU_W) / 2;
    const int offY =
        (C64_DISPLAY_Y - EMU_H) / 2;

    for (int y = 0; y < EMU_H; ++y) {
        int src_base =
            (y + offY) *
            C64_DISPLAY_X +
            offX;
        int dst_base =
            y * EMU_W;
        for (int x = 0; x < EMU_W; ++x) {
            emu[
                dst_base + x
            ] =
                palette[
                    buffer[
                        src_base + x
                    ]
                ];
        }
    }

    display_emu_flush_320x240(
        emu,
        false
    );

    odroid_display_unlock();
}

/* =========================================================================
 * ATARI 7800 / PROSYSTEM
 * =========================================================================
 */
void ili9341_write_frame_prosystem(
    uint8_t *buffer,
    uint16_t *palette)
{
    odroid_display_lock();

    if (!buffer || !palette) {
        ili9341_clear(0x0000);
        display_flush();
        odroid_display_unlock();
        return;
    }

    uint16_t *emu = alloc_emu_buffer();
    if (!emu) {
        odroid_display_unlock();
        return;
    }

    int64_t tp0 =
        esp_timer_get_time();

    /*
     * 320x240 indexed -> RGB565.
     *
     * Process four pixels at a time.
     */
    const uint32_t *in32 =
        (const uint32_t *)buffer;
    uint32_t *out32 =
        (uint32_t *)emu;

    for (int i = 0;
         i < EMU_PIXELS / 4;
         ++i) {
        uint32_t pix4 =
            in32[i];
        uint16_t p0 =
            palette[
                (pix4 >> 0) &
                0xFF
            ];
        uint16_t p1 =
            palette[
                (pix4 >> 8) &
                0xFF
            ];
        uint16_t p2 =
            palette[
                (pix4 >> 16) &
                0xFF
            ];
        uint16_t p3 =
            palette[
                (pix4 >> 24) &
                0xFF
            ];

        out32[i * 2] =
            p0 |
            ((uint32_t)p1 << 16);
        out32[i * 2 + 1] =
            p2 |
            ((uint32_t)p3 << 16);
    }

    s_timing_pal_acc +=
        esp_timer_get_time() -
        tp0;

    display_emu_flush_320x240(
        emu,
        false
    );

    odroid_display_unlock();
}

/* =========================================================================
 * ATARI LYNX
 * =========================================================================
 */
#define LYNX_GAME_WIDTH   160
#define LYNX_GAME_HEIGHT  102
#define LYNX_PIXELS       (LYNX_GAME_WIDTH * LYNX_GAME_HEIGHT)
static uint16_t *s_lynx_temp = NULL;

static bool ensure_lynx_temp(void)
{
    if (s_lynx_temp) {
        return true;
    }
    s_lynx_temp = heap_caps_aligned_calloc(
        64,
        1,
        LYNX_PIXELS * sizeof(uint16_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA
    );
    if (!s_lynx_temp) {
        ESP_LOGE(
            TAG,
            "Lynx temp buffer allocation failed"
        );
        return false;
    }
    return true;
}

void ili9341_write_frame_lynx(
    const uint16_t *buffer)
{
    odroid_display_lock();

    if (!buffer) {
        ili9341_clear(0x0000);
        display_flush();
        odroid_display_unlock();
        return;
    }

    if (!ensure_lynx_temp()) {
        odroid_display_unlock();
        return;
    }

    memcpy(
        s_lynx_temp,
        buffer,
        LYNX_PIXELS * sizeof(uint16_t)
    );

#ifdef CONFIG_HDMI_OUTPUT
    /*
     * HDMI keeps its original PPA scaling path.
     */
    if (s_hdmi_initialized) {
        esp_err_t ret =
            ppa_scale_rgb565_to_rgb888(
                s_lynx_temp,
                LYNX_GAME_WIDTH,
                LYNX_GAME_HEIGHT,
                (float)HDMI_OUT_W /
                    LYNX_GAME_WIDTH,
                (float)HDMI_OUT_H /
                    LYNX_GAME_HEIGHT,
                s_hdmi_disp.fb,
                s_hdmi_disp.fb_size,
                NULL,
                NULL,
                false
            );
        if (ret != ESP_OK) {
            ESP_LOGE(
                TAG,
                "Lynx HDMI scale failed: 0x%x",
                ret
            );
        } else {
            esp_cache_msync(
                s_hdmi_disp.fb,
                s_hdmi_disp.fb_size,
                ESP_CACHE_MSYNC_FLAG_DIR_C2M
            );
        }
    }
#else
    uint16_t *emu =
        alloc_emu_buffer();
    if (!emu) {
        odroid_display_unlock();
        return;
    }

    /*
     * 160x102 -> 320x240.
     *
     * This intentionally uses nearest neighbour instead of stretching
     * through the old 800x480 path.
     */
    for (int y = 0; y < EMU_H; ++y) {
        int sy =
            y * LYNX_GAME_HEIGHT /
            EMU_H;
        const uint16_t *src =
            &s_lynx_temp[
                sy * LYNX_GAME_WIDTH
            ];
        uint16_t *dst =
            &emu[
                y * EMU_W
            ];
        for (int x = 0; x < EMU_W; ++x) {
            int sx =
                x * LYNX_GAME_WIDTH /
                EMU_W;
            dst[x] = src[sx];
        }
    }

    display_emu_flush_320x240(
        emu,
        false
    );
#endif

    odroid_display_unlock();
}

/* =========================================================================
 * GENERIC RGB565 FRAME
 * =========================================================================
 */
void ili9341_write_frame_rgb565_ex(
    const uint16_t *buffer,
    bool byte_swap_input)
{
    odroid_display_lock();

    if (!buffer) {
        ili9341_clear(0x0000);
        display_flush();
        odroid_display_unlock();
        return;
    }

    /*
     * Input is already 320x240.
     */
    display_emu_flush_320x240(
        buffer,
        byte_swap_input
    );

    odroid_display_unlock();
}

void ili9341_write_frame_rgb565(
    const uint16_t *buffer)
{
    /*
     * Existing API contract:
     *
     * caller provides RGB565 data in the format expected by the
     * previous compatibility layer.
     */
    ili9341_write_frame_rgb565_ex(
        buffer,
        false
    );
}

/* =========================================================================
 * CUSTOM RGB565 FRAME
 * =========================================================================
 */
void ili9341_write_frame_rgb565_custom(
    const uint16_t *buffer,
    uint16_t in_w,
    uint16_t in_h,
    float scale,
    bool byte_swap_input)
{
    odroid_display_lock();

    if (!buffer) {
        ili9341_clear(0x0000);
        display_flush();
        odroid_display_unlock();
        return;
    }

    if (in_w == 0 ||
        in_h == 0) {
        odroid_display_unlock();
        return;
    }

    /*
     * The old implementation used:
     *
     *   scale + 270° rotation
     *
     * because the old LCD was portrait 480x800.
     *
     * The new panel is already landscape 320x240, so we keep the
     * requested scale only as a hint and fit the source into the
     * native panel using nearest-neighbour.
     *
     * For a source already 320x240 this is a direct copy.
     */
    uint16_t *emu =
        alloc_emu_buffer();
    if (!emu) {
        odroid_display_unlock();
        return;
    }

    /*
     * If the requested scale is zero or negative, use 1x.
     */
    if (scale <= 0.0f) {
        scale = 1.0f;
    }

    /*
     * Compute the scaled dimensions.
     */
    int scaled_w =
        (int)((float)in_w * scale);
    int scaled_h =
        (int)((float)in_h * scale);

    if (scaled_w <= 0 ||
        scaled_h <= 0) {
        odroid_display_unlock();
        return;
    }

    /*
     * Fit into 320x240 while preserving aspect ratio.
     */
    float fit =
        1.0f;
    if (scaled_w > EMU_W) {
        fit =
            (float)EMU_W /
            (float)scaled_w;
    }
    if (scaled_h > EMU_H) {
        float fy =
            (float)EMU_H /
            (float)scaled_h;
        if (fy < fit) {
            fit = fy;
        }
    }

    int final_w =
        (int)((float)scaled_w * fit);
    int final_h =
        (int)((float)scaled_h * fit);

    if (final_w <= 0) {
        final_w = 1;
    }
    if (final_h <= 0) {
        final_h = 1;
    }

    int x_off =
        (EMU_W - final_w) / 2;
    int y_off =
        (EMU_H - final_h) / 2;

    /*
     * Clear borders.
     */
    memset(
        emu,
        0,
        EMU_SIZE
    );

    /*
     * Nearest-neighbour scaling.
     *
     * This path is intentionally CPU based because custom-size frames
     * are not the main 60 FPS emulator path.
     */
    for (int y = 0;
         y < final_h;
         ++y) {
        int sy =
            (y * in_h) /
            final_h;
        if (sy >= in_h) {
            sy = in_h - 1;
        }

        for (int x = 0;
             x < final_w;
             ++x) {
            int sx =
                (x * in_w) /
                final_w;
            if (sx >= in_w) {
                sx = in_w - 1;
            }

            uint16_t pixel =
                buffer[
                    sy * in_w +
                    sx
                ];

            if (byte_swap_input) {
                pixel =
                    (uint16_t)(
                        (pixel >> 8) |
                        (pixel << 8)
                    );
            }

            emu[
                (y + y_off) * EMU_W +
                (x + x_off)
            ] = pixel;
        }
    }

    display_emu_flush_320x240(
        emu,
        false
    );

    odroid_display_unlock();
}

/* =========================================================================
 * POWER / PREPARE
 * =========================================================================
 */
void ili9341_poweroff(void)
{
#ifdef CONFIG_HDMI_OUTPUT
    return;
#else
    if (s_backlight_init) {
        ledc_set_duty(
            LEDC_LOW_SPEED_MODE,
            BL_LEDC_CH,
            0
        );
        ledc_update_duty(
            LEDC_LOW_SPEED_MODE,
            BL_LEDC_CH
        );
    }
#endif
}

void ili9341_prepare(void)
{
    /*
     * The ST7789V is initialized by ili9341_init().
     *
     * Kept as a compatibility no-op.
     */
}

/* =========================================================================
 * STATUS SCREENS
 * =========================================================================
 */
void odroid_display_show_sderr(int errNum)
{
    ESP_LOGE(
        TAG,
        "SD card error: %d",
        errNum
    );
    /*
     * Red screen.
     */
    ili9341_clear(0xF800);
    display_flush();
}

void odroid_display_show_hourglass(void)
{
    ESP_LOGI(
        TAG,
        "Hourglass (loading) indicator shown"
    );
}

void odroid_display_show_splash(void)
{
    ESP_LOGI(
        TAG,
        "Splash screen shown"
    );
}

void odroid_display_drain_spi(void)
{
    /*
     * Kept for compatibility with the original Odroid API.
     *
     * esp_lcd handles queued SPI transfers internally.
     */
}
