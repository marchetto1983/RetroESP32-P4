#pragma once

/*
 * RetroESP32-P4
 * WT9932P4-TINY + ST7789V 320x240 SPI
 *
 * Display:
 *   Resolution : 320x240
 *   Interface  : SPI
 *   SPI speed  : 80 MHz
 *
 * ST7789V:
 *   MISO : GPIO18
 *   MOSI : GPIO19
 *   CLK  : GPIO22
 *   CS   : GPIO20
 *   DC   : GPIO21
 *   RST  : GPIO23
 *   BCKL : GPIO17
 */

/* --------------------------------------------------------------------------
 * LCD
 * -------------------------------------------------------------------------- */

#define LCD_H_RES               320
#define LCD_V_RES               240

#define LCD_SPI_HOST            SPI2_HOST
#define LCD_SPI_MISO            18
#define LCD_SPI_MOSI            19
#define LCD_SPI_CLK             22
#define LCD_SPI_CS              20
#define LCD_SPI_DC              21
#define LCD_RST                 23
#define LCD_BK_LIGHT_GPIO       17

/* ST7789V SPI clock */
#define LCD_SPI_FREQ_HZ         (80 * 1000 * 1000)

/*
 * ST7789V initialization
 *
 * 0x36 = MADCTL
 * 0x60 = orientation already verified on the target display
 *
 * 0x3A = COLMOD
 * 0x05 = RGB565
 *
 * 0x11 = Sleep Out
 * 0x29 = Display ON
 */
#define LCD_MADCTL_VALUE        0x60
#define LCD_PIXEL_FORMAT        0x05


/* --------------------------------------------------------------------------
 * Touch
 * --------------------------------------------------------------------------
 *
 * The new ST7789V display has no touch controller.
 *
 * Keep the definitions disabled/invalid so code that conditionally checks
 * the old touch pins does not attempt to initialize a GT911.
 */

#define TP_I2C_SDA              -1
#define TP_I2C_SCL              -1
#define TP_RST                  -1
#define TP_INT                  -1


/* --------------------------------------------------------------------------
 * Audio / I2S
 * --------------------------------------------------------------------------
 *
 * WT9932P4-TINY pinout supplied for this project.
 */

#define I2S_BCLK_IO             49
#define I2S_WS_IO               48
#define I2S_DOUT_IO             50
#define AUDIO_PA_IO             47

/*
 * The previous Guition configuration used an ES8311 MCLK/DIN layout.
 * The WT9932P4-TINY pinout supplied here does not use those signals.
 */
#define I2S_MCLK_IO             -1
#define I2S_DIN_IO              -1


/* --------------------------------------------------------------------------
 * SD/MMC
 * -------------------------------------------------------------------------- */

#define SD_MMC_CLK              43
#define SD_MMC_CMD              44
#define SD_MMC_D0               39
#define SD_MMC_D1               40
#define SD_MMC_D2               41
#define SD_MMC_D3               42
