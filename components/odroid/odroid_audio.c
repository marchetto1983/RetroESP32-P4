/*
 * Odroid Audio Compatibility Layer — ESP32-P4
 *
 * WT9932P4-TINY audio configuration
 *
 * Hardware:
 *
 *   I2S BCLK  -> GPIO49
 *   I2S WS    -> GPIO48
 *   I2S DATA  -> GPIO50
 *   AMP EN    -> GPIO47
 *
 * The original RetroESP32-P4 implementation was written around the
 * Guition board and its ES8311 codec.
 *
 * This file intentionally remains a compatibility layer.
 *
 * The actual I2S peripheral configuration is implemented by the
 * lower-level audio component exposed through:
 *
 *   audio_set_sample_rate()
 *   audio_set_volume()
 *   audio_play_pcm()
 *   audio_stop()
 *
 * Therefore this file does NOT initialize I2S itself.
 */

#include "odroid_audio.h"
#include "odroid_settings.h"
#include "audio.h"

#include "esp_log.h"
#include "esp_err.h"

#include <string.h>
#include <stdlib.h>


static const char *TAG = "odroid_audio";


/* =========================================================================
 * AUDIO STATE
 * =========================================================================
 */

static int s_sample_rate = 16000;

static odroid_volume_level s_volume_level =
    ODROID_VOLUME_LEVEL3;


/*
 * Software volume multipliers.
 *
 *   LEVEL0 = mute
 *   LEVEL1 = 12.5%
 *   LEVEL2 = 25%
 *   LEVEL3 = 50%
 *   LEVEL4 = 100%
 *
 * The lower-level audio driver may also have hardware/software volume
 * control. We keep the existing RetroESP32 volume semantics here so
 * the emulator API does not change.
 */
static const int volume_table[
    ODROID_VOLUME_LEVEL_COUNT
] = {
    0,
    125,
    250,
    500,
    1000
};


/* =========================================================================
 * INITIALIZATION
 * =========================================================================
 */

void odroid_audio_init(int sample_rate)
{
    if (sample_rate <= 0) {
        sample_rate = 16000;
    }


    s_sample_rate =
        sample_rate;


    /*
     * Configure the underlying I2S driver.
     *
     * IMPORTANT:
     *
     * The GPIO assignment is NOT done here.
     *
     * The low-level audio driver must use:
     *
     *   BCLK = GPIO49
     *   WS   = GPIO48
     *   DATA = GPIO50
     *   AMP  = GPIO47
     */
    esp_err_t ret =
        audio_set_sample_rate(
            sample_rate
        );


    if (ret != ESP_OK) {

        ESP_LOGW(
            TAG,
            "Failed to set sample rate to %d Hz: %s",
            sample_rate,
            esp_err_to_name(ret)
        );
    }


    /*
     * Restore persisted volume.
     */
    odroid_volume_level level =
        (odroid_volume_level)
        odroid_settings_Volume_get();


    if (level >= ODROID_VOLUME_LEVEL_COUNT) {

        level =
            ODROID_VOLUME_LEVEL3;
    }


    s_volume_level =
        level;


    /*
     * Apply the volume to the low-level audio driver.
     *
     * audio_set_volume() expects a percentage.
     */
    int pct =
        (
            (int)level *
            100
        ) /
        (
            ODROID_VOLUME_LEVEL_COUNT - 1
        );


    ret =
        audio_set_volume(
            pct
        );


    if (ret != ESP_OK) {

        ESP_LOGW(
            TAG,
            "Failed to restore volume: %d%%: %s",
            pct,
            esp_err_to_name(ret)
        );
    }


    ESP_LOGI(
        TAG,
        "Audio initialized: sample_rate=%d Hz, volume=%d/%d",
        s_sample_rate,
        s_volume_level,
        ODROID_VOLUME_LEVEL_COUNT - 1
    );
}


/* =========================================================================
 * TERMINATION
 * =========================================================================
 */

void odroid_audio_terminate(void)
{
    /*
     * Stop the I2S/DMA stream.
     *
     * The lower-level audio driver is responsible for disabling the
     * amplifier output if required.
     */
    audio_stop();


    ESP_LOGI(
        TAG,
        "Audio terminated"
    );
}


/* =========================================================================
 * VOLUME SET
 * =========================================================================
 */

void odroid_audio_volume_set(int volume)
{
    if (volume < 0) {
        volume = 0;
    }


    if (volume >= ODROID_VOLUME_LEVEL_COUNT) {

        volume =
            ODROID_VOLUME_LEVEL_COUNT - 1;
    }


    s_volume_level =
        (odroid_volume_level)volume;


    /*
     * Convert RetroESP32's 5-step volume to 0..100%.
     */
    int pct =
        (
            volume *
            100
        ) /
        (
            ODROID_VOLUME_LEVEL_COUNT - 1
        );


    esp_err_t ret =
        audio_set_volume(
            pct
        );


    if (ret != ESP_OK) {

        ESP_LOGW(
            TAG,
            "Failed to set audio volume to %d%%: %s",
            pct,
            esp_err_to_name(ret)
        );

        return;
    }


    ESP_LOGI(
        TAG,
        "Volume set: %d/%d -> %d%%",
        volume,
        ODROID_VOLUME_LEVEL_COUNT - 1,
        pct
    );
}


/* =========================================================================
 * VOLUME GET
 * =========================================================================
 */

odroid_volume_level odroid_audio_volume_get(void)
{
    return s_volume_level;
}


/* =========================================================================
 * VOLUME CHANGE
 * =========================================================================
 */

void odroid_audio_volume_change(void)
{
    int level =
        (
            (int)s_volume_level +
            1
        ) %
        ODROID_VOLUME_LEVEL_COUNT;


    odroid_audio_volume_set(
        level
    );


    /*
     * Persist the setting.
     */
    odroid_settings_Volume_set(
        level
    );
}


/* =========================================================================
 * PCM SUBMISSION
 * =========================================================================
 */

void odroid_audio_submit(
    short *stereoAudioBuffer,
    int frameCount)
{
    if (!stereoAudioBuffer) {
        return;
    }


    if (frameCount <= 0) {
        return;
    }


    /*
     * Stereo:
     *
     *   L R L R L R ...
     *
     * Two samples per frame.
     */
    int total_samples =
        frameCount * 2;


    int vol_num =
        volume_table[
            s_volume_level
        ];


    /*
     * At full volume there is nothing to modify.
     */
    if (vol_num >= 1000) {

        size_t len =
            (
                size_t)total_samples *
                sizeof(short);


        audio_play_pcm(
            stereoAudioBuffer,
            len,
            s_sample_rate
        );


        return;
    }


    /*
     * At zero volume we don't need to allocate a temporary buffer.
     *
     * Submit silence instead.
     */
    if (vol_num <= 0) {

        odroid_audio_submit_zero();

        return;
    }


    /*
     * Do NOT modify the emulator's original PCM buffer in place.
     *
     * Allocate a temporary buffer so the emulator retains ownership
     * of its source samples.
     */
    size_t buffer_size =
        (
            size_t)total_samples *
            sizeof(short);


    short *volume_buffer =
        (short *)malloc(
            buffer_size
        );


    if (!volume_buffer) {

        /*
         * If allocation fails, submit the original buffer rather than
         * dropping the audio completely.
         */
        audio_play_pcm(
            stereoAudioBuffer,
            buffer_size,
            s_sample_rate
        );

        return;
    }


    /*
     * Apply software attenuation.
     *
     * Integer arithmetic:
     *
     *   sample × volume / 1000
     */
    for (int i = 0;
         i < total_samples;
         ++i) {

        volume_buffer[i] =
            (short)(
                (
                    (int)
                    stereoAudioBuffer[i] *
                    vol_num
                ) /
                1000
            );
    }


    /*
     * Send the temporary PCM buffer to the low-level I2S driver.
     */
    audio_play_pcm(
        volume_buffer,
        buffer_size,
        s_sample_rate
    );


    free(
        volume_buffer
    );
}


/* =========================================================================
 * SILENCE
 * =========================================================================
 */

void odroid_audio_submit_zero(void)
{
    /*
     * Small stereo silence buffer.
     *
     * 512 samples = 256 stereo frames.
     */
    static const short silence[512] = {
        0
    };


    audio_play_pcm(
        (short *)silence,
        sizeof(silence),
        s_sample_rate
    );
}


/* =========================================================================
 * SAMPLE RATE
 * =========================================================================
 */

int odroid_audio_sample_rate_get(void)
{
    return s_sample_rate;
}
