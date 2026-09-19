/*
 * RetroESP32-P4 Audio Driver
 *
 * WT9932P4-TINY
 *
 * I2S TX-only audio output.
 *
 * Hardware:
 *
 *   BCLK  -> GPIO49
 *   WS    -> GPIO48
 *   DOUT  -> GPIO50
 *   AMP   -> GPIO47
 *
 * No ES8311 codec.
 * No I2C audio control.
 * No I2S RX.
 * No MCLK output.
 */

#include "audio.h"

#include <math.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "freertos/FreeRTOS.h"

#include "driver/i2s_std.h"
#include "driver/gpio.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_heap_caps.h"


#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif


static const char *TAG = "audio";


/* =========================================================================
 * I2S CONFIGURATION
 * =========================================================================
 */

#define AUDIO_BITS_PER_SAMPLE \
    I2S_DATA_BIT_WIDTH_16BIT

#define AUDIO_SLOT_MODE \
    I2S_SLOT_MODE_STEREO

#define AUDIO_MCLK_MULTIPLE \
    I2S_MCLK_MULTIPLE_256

#define AUDIO_WRITE_CHUNK \
    4096


/* =========================================================================
 * DRIVER STATE
 * =========================================================================
 */

static i2s_chan_handle_t s_tx_handle = NULL;

static audio_config_t s_config;

static bool s_initialized = false;

static int s_volume = 60;
/*
 * Software volume buffer.
 *
 * This must NOT be allocated on the stack of audio_play_pcm(),
 * because audio_play_pcm() is called from the NeoGeo audio task,
 * whose stack is intentionally small.
 */
static int16_t *s_volume_buffer = NULL;

static const size_t s_volume_buffer_samples =
    AUDIO_WRITE_CHUNK / sizeof(int16_t);


/* =========================================================================
 * AMPLIFIER CONTROL
 * =========================================================================
 */

static esp_err_t amplifier_init(
    const audio_config_t *cfg)
{
    if (!cfg) {
        return ESP_ERR_INVALID_ARG;
    }


    /*
     * -1 / GPIO_NUM_NC means that the amplifier control
     * is not connected.
     */
    if (cfg->pa_ctrl_io < 0) {

        ESP_LOGI(
            TAG,
            "Amplifier GPIO disabled"
        );

        return ESP_OK;
    }


    gpio_config_t gpio_cfg = {
        .pin_bit_mask =
            (1ULL << cfg->pa_ctrl_io),

        .mode =
            GPIO_MODE_OUTPUT,

        .pull_up_en =
            GPIO_PULLUP_DISABLE,

        .pull_down_en =
            GPIO_PULLDOWN_DISABLE,

        .intr_type =
            GPIO_INTR_DISABLE,
    };


    esp_err_t ret =
        gpio_config(
            &gpio_cfg
        );


    if (ret != ESP_OK) {

        ESP_LOGE(
            TAG,
            "Failed to configure amplifier GPIO%d: %s",
            cfg->pa_ctrl_io,
            esp_err_to_name(ret)
        );

        return ret;
    }


    /*
     * Amplifier disabled while I2S is being initialized.
     */
    gpio_set_level(
        (gpio_num_t)cfg->pa_ctrl_io,
        0
    );


    ESP_LOGI(
        TAG,
        "Amplifier control: GPIO%d",
        cfg->pa_ctrl_io
    );


    return ESP_OK;
}


static void amplifier_enable(
    const audio_config_t *cfg,
    bool enable)
{
    if (!cfg) {
        return;
    }


    if (cfg->pa_ctrl_io < 0) {
        return;
    }


    gpio_set_level(
        (gpio_num_t)cfg->pa_ctrl_io,
        enable ? 1 : 0
    );
}


/* =========================================================================
 * I2S DRIVER INITIALIZATION
 * =========================================================================
 */

static esp_err_t i2s_driver_init(
    const audio_config_t *cfg)
{
    if (!cfg) {
        return ESP_ERR_INVALID_ARG;
    }


    /*
     * TX ONLY.
     *
     * The original driver created both TX and RX because the ES8311
     * codec supported both directions.
     *
     * Our hardware only requires audio output.
     */
    i2s_chan_config_t chan_cfg =
        I2S_CHANNEL_DEFAULT_CONFIG(
            (i2s_port_t)cfg->i2s_num,
            I2S_ROLE_MASTER
        );


    chan_cfg.auto_clear =
        true;


    esp_err_t ret =
        i2s_new_channel(
            &chan_cfg,
            &s_tx_handle,
            NULL
        );


    if (ret != ESP_OK) {

        ESP_LOGE(
            TAG,
            "i2s_new_channel failed: %s",
            esp_err_to_name(ret)
        );

        return ret;
    }


    /*
     * Standard Philips I2S.
     *
     * 16-bit stereo.
     *
     * GPIO_NUM_NC is used for MCLK and DIN because this board
     * does not route either signal to the audio hardware.
     */
    i2s_std_config_t std_cfg = {

        .clk_cfg =
            I2S_STD_CLK_DEFAULT_CONFIG(
                cfg->sample_rate
            ),

        .slot_cfg =
            I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
                AUDIO_BITS_PER_SAMPLE,
                AUDIO_SLOT_MODE
            ),

        .gpio_cfg = {

            .mclk =
                (gpio_num_t)cfg->mclk_io,

            .bclk =
                (gpio_num_t)cfg->bclk_io,

            .ws =
                (gpio_num_t)cfg->ws_io,

            .dout =
                (gpio_num_t)cfg->dout_io,

            .din =
                (gpio_num_t)cfg->din_io,

            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };


    /*
     * 256 × sample rate internal clock multiple.
     *
     * No MCLK is physically routed because cfg->mclk_io is
     * GPIO_NUM_NC.
     */
    std_cfg.clk_cfg.mclk_multiple =
        AUDIO_MCLK_MULTIPLE;


    ret =
        i2s_channel_init_std_mode(
            s_tx_handle,
            &std_cfg
        );


    if (ret != ESP_OK) {

        ESP_LOGE(
            TAG,
            "I2S TX initialization failed: %s",
            esp_err_to_name(ret)
        );


        i2s_del_channel(
            s_tx_handle
        );


        s_tx_handle =
            NULL;


        return ret;
    }


    ret =
        i2s_channel_enable(
            s_tx_handle
        );


    if (ret != ESP_OK) {

        ESP_LOGE(
            TAG,
            "I2S TX enable failed: %s",
            esp_err_to_name(ret)
        );


        i2s_del_channel(
            s_tx_handle
        );


        s_tx_handle =
            NULL;


        return ret;
    }


    ESP_LOGI(
        TAG,
        "I2S%d initialized",
        cfg->i2s_num
    );


    ESP_LOGI(
        TAG,
        "Sample rate : %d Hz",
        cfg->sample_rate
    );


    ESP_LOGI(
        TAG,
        "BCLK        : GPIO%d",
        cfg->bclk_io
    );


    ESP_LOGI(
        TAG,
        "WS          : GPIO%d",
        cfg->ws_io
    );


    ESP_LOGI(
        TAG,
        "DOUT        : GPIO%d",
        cfg->dout_io
    );


    ESP_LOGI(
        TAG,
        "MCLK        : GPIO%d",
        cfg->mclk_io
    );


    ESP_LOGI(
        TAG,
        "DIN         : GPIO%d",
        cfg->din_io
    );


    return ESP_OK;
}


/* =========================================================================
 * PUBLIC INITIALIZATION
 * =========================================================================
 */

esp_err_t audio_init(
    const audio_config_t *config)
{
    if (s_initialized) {

        ESP_LOGW(
            TAG,
            "Audio already initialized"
        );

        return ESP_OK;
    }


    if (!config) {

        ESP_LOGE(
            TAG,
            "Invalid audio configuration"
        );

        return ESP_ERR_INVALID_ARG;
    }


    s_config =
        *config;


    if (s_config.sample_rate <= 0) {

        s_config.sample_rate =
            16000;
    }


    if (s_config.volume < 0) {
        s_config.volume = 0;
    }


    if (s_config.volume > 100) {
        s_config.volume = 100;
    }


    s_volume =
        s_config.volume;
	/*
	 * Allocate the software-volume buffer in internal RAM.
	 *
	 * audio_play_pcm() runs in the NeoGeo audio task, so this buffer
	 * must never live on that task's stack.
	 */
	s_volume_buffer =
		(int16_t *)heap_caps_malloc(
			AUDIO_WRITE_CHUNK,
			MALLOC_CAP_INTERNAL |
			MALLOC_CAP_8BIT
		);

	if (!s_volume_buffer) {
		ESP_LOGE(
			TAG,
			"Failed to allocate software volume buffer"
		);

		return ESP_ERR_NO_MEM;
	}


    /*
     * Configure amplifier GPIO first, but keep amplifier disabled.
     */
    esp_err_t ret =
        amplifier_init(
            &s_config
        );


    if (ret != ESP_OK) {
        return ret;
    }


    /*
     * Initialize I2S TX.
     */
    ret =
        i2s_driver_init(
            &s_config
        );


    if (ret != ESP_OK) {

        amplifier_enable(
            &s_config,
            false
        );

        return ret;
    }


    /*
     * I2S is ready, so enable the amplifier.
     */
    amplifier_enable(
        &s_config,
        true
    );


    s_initialized =
        true;


    ESP_LOGI(
        TAG,
        "Audio subsystem ready"
    );


    ESP_LOGI(
        TAG,
        "I2S TX-only / no ES8311 / no I2C"
    );


    return ESP_OK;
}


/* =========================================================================
 * TONE GENERATOR
 * =========================================================================
 */

esp_err_t audio_play_tone(
    uint32_t freq_hz,
    uint32_t duration_ms,
    int volume)
{
    if (!s_initialized ||
        !s_tx_handle) {

        return ESP_ERR_INVALID_STATE;
    }


    if (freq_hz == 0) {
        return ESP_ERR_INVALID_ARG;
    }


    if (volume < 0) {
        volume = 0;
    }


    if (volume > 100) {
        volume = 100;
    }


    const int sample_rate =
        s_config.sample_rate;


    const int frames_per_chunk =
        1024;


    const size_t buffer_size =
        frames_per_chunk *
        2 *
        sizeof(int16_t);


    int16_t *buffer =
        (int16_t *)heap_caps_malloc(
            buffer_size,
            MALLOC_CAP_INTERNAL |
            MALLOC_CAP_8BIT
        );


    if (!buffer) {

        ESP_LOGE(
            TAG,
            "Failed to allocate tone buffer"
        );

        return ESP_ERR_NO_MEM;
    }


    const double phase_increment =
        2.0 *
        M_PI *
        (double)freq_hz /
        (double)sample_rate;


    double phase =
        0.0;


    const int amplitude =
        16000 *
        volume /
        100;


    uint32_t total_frames =
        0;


    if (duration_ms > 0) {

        total_frames =
            (
                (uint32_t)sample_rate *
                duration_ms
            ) /
            1000;
    }


    uint32_t frames_written =
        0;


    while (
        duration_ms == 0 ||
        frames_written < total_frames
    ) {

        int frames =
            frames_per_chunk;


        if (
            duration_ms > 0 &&
            (
                total_frames -
                frames_written
            ) < (uint32_t)frames_per_chunk
        ) {

            frames =
                (int)(
                    total_frames -
                    frames_written
                );
        }


        for (int i = 0;
             i < frames;
             ++i) {

            int16_t sample =
                (int16_t)(
                    amplitude *
                    sin(phase)
                );


            buffer[i * 2] =
                sample;


            buffer[i * 2 + 1] =
                sample;


            phase +=
                phase_increment;


            if (
                phase >=
                2.0 * M_PI
            ) {

                phase -=
                    2.0 * M_PI;
            }
        }


        size_t bytes_written =
            0;


        esp_err_t ret =
            i2s_channel_write(
                s_tx_handle,
                buffer,
                frames *
                2 *
                sizeof(int16_t),
                &bytes_written,
                portMAX_DELAY
            );


        if (ret != ESP_OK) {

            ESP_LOGE(
                TAG,
                "I2S tone write failed: %s",
                esp_err_to_name(ret)
            );


            heap_caps_free(
                buffer
            );


            return ret;
        }


        frames_written +=
            frames;
    }


    heap_caps_free(
        buffer
    );


    return ESP_OK;
}


/* =========================================================================
 * STOP
 * =========================================================================
 */

esp_err_t audio_stop(void)
{
    if (!s_initialized ||
        !s_tx_handle) {

        return ESP_ERR_INVALID_STATE;
    }


    esp_err_t ret =
        i2s_channel_disable(
            s_tx_handle
        );


    if (ret != ESP_OK) {
        return ret;
    }


    ret =
        i2s_channel_enable(
            s_tx_handle
        );


    return ret;
}


/* =========================================================================
 * SOFTWARE VOLUME
 * =========================================================================
 */

esp_err_t audio_set_volume(
    int volume)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }


    if (volume < 0) {
        volume = 0;
    }


    if (volume > 100) {
        volume = 100;
    }


    s_volume =
        volume;


    ESP_LOGI(
        TAG,
        "Volume set to %d%%",
        s_volume
    );


    return ESP_OK;
}


/* =========================================================================
 * SAMPLE RATE
 * =========================================================================
 */

esp_err_t audio_set_sample_rate(
    int sample_rate)
{
    if (!s_initialized ||
        !s_tx_handle) {

        return ESP_ERR_INVALID_STATE;
    }


    if (sample_rate <= 0) {
        return ESP_ERR_INVALID_ARG;
    }


    if (
        sample_rate ==
        s_config.sample_rate
    ) {

        return ESP_OK;
    }


    /*
     * A reset to zero means the previous application changed
     * sample-rate state. In that case we need to actually
     * reconfigure the I2S clock.
     */
    if (s_config.sample_rate == 0) {

        sample_rate =
            sample_rate;
    }


    ESP_LOGI(
        TAG,
        "Reconfiguring I2S sample rate: %d -> %d Hz",
        s_config.sample_rate,
        sample_rate
    );


    esp_err_t ret =
        i2s_channel_disable(
            s_tx_handle
        );


    if (ret != ESP_OK) {

        ESP_LOGE(
            TAG,
            "Failed to disable I2S TX: %s",
            esp_err_to_name(ret)
        );

        return ret;
    }


    i2s_std_clk_config_t clk_cfg =
        I2S_STD_CLK_DEFAULT_CONFIG(
            sample_rate
        );


    clk_cfg.mclk_multiple =
        AUDIO_MCLK_MULTIPLE;


    ret =
        i2s_channel_reconfig_std_clock(
            s_tx_handle,
            &clk_cfg
        );


    if (ret != ESP_OK) {

        ESP_LOGE(
            TAG,
            "Failed to reconfigure I2S clock: %s",
            esp_err_to_name(ret)
        );


        /*
         * Try to restore the TX channel.
         */
        i2s_channel_enable(
            s_tx_handle
        );


        return ret;
    }


    ret =
        i2s_channel_enable(
            s_tx_handle
        );


    if (ret != ESP_OK) {
        return ret;
    }


    s_config.sample_rate =
        sample_rate;


    ESP_LOGI(
        TAG,
        "I2S sample rate set to %d Hz",
        sample_rate
    );


    return ESP_OK;
}


/* =========================================================================
 * RESET SAMPLE RATE
 * =========================================================================
 */

void audio_reset_sample_rate(void)
{
    /*
     * Keep the same behavior expected by RetroESP32.
     *
     * The next audio_set_sample_rate() call will reconfigure
     * the I2S clock.
     */
    s_config.sample_rate =
        0;
}


/* =========================================================================
 * PCM OUTPUT
 * =========================================================================
 */

esp_err_t audio_play_pcm(
    const void *data,
    size_t len,
    int sample_rate)
{
    if (!s_initialized ||
        !s_tx_handle) {

        return ESP_ERR_INVALID_STATE;
    }


    if (!data ||
        len == 0) {

        return ESP_ERR_INVALID_ARG;
    }


    /*
     * The emulator supplies the sample rate with every PCM block.
     */
    if (
        sample_rate > 0 &&
        sample_rate !=
        s_config.sample_rate
    ) {
        esp_err_t ret =
            audio_set_sample_rate(
                sample_rate
            );


        if (ret != ESP_OK) {
            return ret;
        }
    }


    /*
     * At 100% volume we can send the original PCM buffer directly.
     */
    if (s_volume >= 100) {

        const uint8_t *ptr =
            (const uint8_t *)data;


        size_t remaining =
            len;


        while (remaining > 0) {
            size_t to_write =
                remaining >
                AUDIO_WRITE_CHUNK
                ? AUDIO_WRITE_CHUNK
                : remaining;


            size_t bytes_written =
                0;


            esp_err_t ret =
                i2s_channel_write(
                    s_tx_handle,
                    ptr,
                    to_write,
                    &bytes_written,
                    portMAX_DELAY
                );


            if (ret != ESP_OK) {
                ESP_LOGE(
                    TAG,
                    "PCM write failed: %s",
                    esp_err_to_name(ret)
                );


                return ret;
            }


            if (bytes_written == 0) {
                return ESP_FAIL;
            }


            ptr +=
                bytes_written;

            remaining -=
                bytes_written;
        }


        return ESP_OK;
    }


    /*
     * Software volume processing.
     *
     * The volume buffer is allocated in internal heap memory
     * during audio_init(). It must NOT be allocated on the
     * stack because this function is called by the NeoGeo
     * audio task.
     *
     * Input:
     *
     *   signed 16-bit stereo PCM
     *
     *   L R L R L R ...
     */
    const int16_t *input =
        (const int16_t *)data;


    size_t sample_count =
        len /
        sizeof(int16_t);


    if (!s_volume_buffer) {
        return ESP_ERR_INVALID_STATE;
    }


    const size_t buffer_samples =
        s_volume_buffer_samples;


    size_t processed =
        0;


    while (
        processed <
        sample_count
    ) {

        size_t count =
            sample_count -
            processed;


        if (
            count >
            buffer_samples
        ) {
            count =
                buffer_samples;
        }


        for (size_t i = 0;
             i < count;
             ++i) {

            int32_t sample =
                input[
                    processed + i
                ];


            sample =
                (
                    sample *
                    s_volume
                ) /
                100;


            if (sample > 32767) {
                sample = 32767;
            }


            if (sample < -32768) {
                sample = -32768;
            }


            s_volume_buffer[i] =
                (int16_t)sample;
        }


        size_t bytes_written =
            0;


        esp_err_t ret =
            i2s_channel_write(
                s_tx_handle,
                s_volume_buffer,
                count *
                sizeof(int16_t),
                &bytes_written,
                portMAX_DELAY
            );


        if (ret != ESP_OK) {
            ESP_LOGE(
                TAG,
                "PCM volume write failed: %s",
                esp_err_to_name(ret)
            );


            return ret;
        }


        if (bytes_written == 0) {
            return ESP_FAIL;
        }


        processed +=
            bytes_written /
            sizeof(int16_t);
    }


    return ESP_OK;
}
