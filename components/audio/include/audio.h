/*
 * Audio driver for WT9932P4-TINY via I2S
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
 * There is no external I2C audio codec in this configuration.
 *
 * The original RetroESP32-P4 audio driver used an ES8311 codec
 * controlled through I2C. That dependency has been removed for
 * the WT9932P4-TINY port.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif


/* =========================================================================
 * HARDWARE CONFIGURATION
 * =========================================================================
 */

#define AUDIO_I2S_PORT          0

#define AUDIO_BCLK_GPIO         49
#define AUDIO_WS_GPIO           48
#define AUDIO_DOUT_GPIO         50

#define AUDIO_AMP_GPIO          47

/*
 * No MCLK and no I2S input are used.
 */
#define AUDIO_MCLK_GPIO         (-1)
#define AUDIO_DIN_GPIO          (-1)


/* =========================================================================
 * AUDIO CONFIGURATION
 * =========================================================================
 */

/**
 * @brief Audio configuration.
 *
 * This structure intentionally contains no I2C handle because the
 * WT9932P4-TINY audio path is direct I2S TX.
 */
typedef struct {

    int i2s_num;

    /*
     * I2S clock/data pins.
     */
    int mclk_io;
    int bclk_io;
    int ws_io;
    int dout_io;
    int din_io;

    /*
     * External amplifier enable GPIO.
     *
     * Set to -1 to disable amplifier GPIO control.
     */
    int pa_ctrl_io;

    /*
     * PCM sample rate in Hz.
     */
    int sample_rate;

    /*
     * Initial software volume, 0-100.
     */
    int volume;

} audio_config_t;


/* =========================================================================
 * DEFAULT CONFIGURATION
 * =========================================================================
 */

/**
 * @brief Default audio configuration for WT9932P4-TINY.
 *
 * I2S:
 *
 *   BCLK = GPIO49
 *   WS   = GPIO48
 *   DOUT = GPIO50
 *
 * Amplifier:
 *
 *   ENABLE = GPIO47
 *
 * MCLK and DIN are intentionally disabled.
 */
#define AUDIO_CONFIG_DEFAULT() { \
    .i2s_num = AUDIO_I2S_PORT, \
    .mclk_io = AUDIO_MCLK_GPIO, \
    .bclk_io = AUDIO_BCLK_GPIO, \
    .ws_io = AUDIO_WS_GPIO, \
    .dout_io = AUDIO_DOUT_GPIO, \
    .din_io = AUDIO_DIN_GPIO, \
    .pa_ctrl_io = AUDIO_AMP_GPIO, \
    .sample_rate = 16000, \
    .volume = 60, \
}


/* =========================================================================
 * PUBLIC API
 * =========================================================================
 */

/**
 * @brief Initialize the audio subsystem.
 *
 * Initializes the I2S TX peripheral and the external amplifier enable
 * GPIO.
 *
 * No I2C bus and no external codec are required.
 *
 * @param config Audio configuration.
 *
 * @return
 *   ESP_OK on success.
 */
esp_err_t audio_init(
    const audio_config_t *config
);


/**
 * @brief Play a sine wave tone through the speaker.
 *
 * @param freq_hz
 *      Tone frequency in Hz.
 *
 * @param duration_ms
 *      Duration in milliseconds.
 *      0 means play indefinitely.
 *
 * @param volume
 *      Tone volume, 0-100.
 *
 * @return
 *      ESP_OK on success.
 */
esp_err_t audio_play_tone(
    uint32_t freq_hz,
    uint32_t duration_ms,
    int volume
);


/**
 * @brief Stop/reset the current I2S audio stream.
 *
 * @return
 *      ESP_OK on success.
 */
esp_err_t audio_stop(void);


/**
 * @brief Set the software output volume.
 *
 * @param volume
 *      Volume from 0 to 100.
 *
 * @return
 *      ESP_OK on success.
 */
esp_err_t audio_set_volume(
    int volume
);


/**
 * @brief Change the I2S sample rate at runtime.
 *
 * @param sample_rate
 *      New sample rate in Hz.
 *
 * @return
 *      ESP_OK on success.
 */
esp_err_t audio_set_sample_rate(
    int sample_rate
);


/**
 * @brief Invalidate the cached sample rate.
 *
 * The next call to audio_set_sample_rate() or audio_play_pcm()
 * will reconfigure the I2S clock.
 */
void audio_reset_sample_rate(void);


/**
 * @brief Play a PCM buffer.
 *
 * Input format:
 *
 *   signed 16-bit PCM
 *   stereo interleaved
 *
 * Layout:
 *
 *   L R L R L R ...
 *
 * @param data
 *      Pointer to PCM data.
 *
 * @param len
 *      Data length in bytes.
 *
 * @param sample_rate
 *      Sample rate of the PCM data in Hz.
 *
 * @return
 *      ESP_OK on success.
 */
esp_err_t audio_play_pcm(
    const void *data,
    size_t len,
    int sample_rate
);


#ifdef __cplusplus
}
#endif
