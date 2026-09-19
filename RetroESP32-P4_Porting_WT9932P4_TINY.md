# RetroESP32-P4 — Porting WT9932P4-TINY + ST7789V

## 1. Obiettivo del progetto

Portare il progetto:

* GitHub: `https://github.com/giltal/RetroESP32-P4`

dalla scheda/display originale basata sulla soluzione Guition:

* `https://www.guition.com/esp32p4-display-module/esp32p4-display`

alla scheda:

* Wireless-Tag WT9932P4-TINY
* `https://wiki.wireless-tag.com/docs/en/WT9932P4-TINY/board_features.html`

con display SPI:

* ST7789V
* risoluzione 320x240
* collegamenti:

  * MOSI
  * CLK
  * DC
  * CS
  * RST
  * LED
  * GND
  * VCC

Display acquistato/verificato:
`https://it.aliexpress.com/item/1005009902563318.html`

ESP-IDF utilizzato:

```text
ESP-IDF 5.5.2
```

Ambiente Windows:

```text
IDF_PATH            = C:\Espressif\frameworks\esp-idf-v5.5.2
IDF_PYTHON_ENV_PATH = C:\Espressif\python_env\idf5.5_py3.11_env
```

Root progetto:

```text
D:\ESP32\RetroESP32-P4
```

---

# 2. Pinout hardware definitivo

## SDMMC

```c
#define RG_STORAGE_SDMMC_HOST          SDMMC_HOST_SLOT_1
#define RG_STORAGE_SDMMC_SPEED         SDMMC_FREQ_HIGHSPEED

#define RG_GPIO_SDMMC_CLK              GPIO_NUM_43
#define RG_GPIO_SDMMC_CMD              GPIO_NUM_44
#define RG_GPIO_SDMMC_D0               GPIO_NUM_39
#define RG_GPIO_SDMMC_D1               GPIO_NUM_40
#define RG_GPIO_SDMMC_D2               GPIO_NUM_41
#define RG_GPIO_SDMMC_D3               GPIO_NUM_42
```

## Audio I2S

```c
#define RG_GPIO_SND_I2S_BCK         GPIO_NUM_49
#define RG_GPIO_SND_I2S_WS          GPIO_NUM_48
#define RG_GPIO_SND_I2S_DATA        GPIO_NUM_50
#define RG_GPIO_SND_AMP_ENABLE      GPIO_NUM_47
```

Configurazione audio:

```text
I2S peripheral : I2S_NUM_0
BCLK           : GPIO49
WS/LRCLK       : GPIO48
DATA OUT       : GPIO50
AMP ENABLE     : GPIO47

MCLK           : GPIO_NUM_NC
DIN            : GPIO_NUM_NC
```

Il progetto deve usare audio **I2S TX-only**.

Non viene utilizzato:

```text
ES8311
I2C audio codec
I2S RX
MCLK fisico
```

Assunzione attuale:

```text
AMP ENABLE GPIO47 = active HIGH
```

Da verificare eventualmente contro lo schema/datasheet della WT9932P4-TINY se l'audio non funziona.

---

# 3. Display ST7789V

Pinout definitivo:

```c
#define RG_GPIO_LCD_MISO            GPIO_NUM_18
#define RG_GPIO_LCD_MOSI            GPIO_NUM_19
#define RG_GPIO_LCD_CLK             GPIO_NUM_22
#define RG_GPIO_LCD_CS              GPIO_NUM_20
#define RG_GPIO_LCD_DC              GPIO_NUM_21
#define RG_GPIO_LCD_RST             GPIO_NUM_23
#define RG_GPIO_LCD_BCKL            GPIO_NUM_17
```

Display:

```text
Controller : ST7789V
Resolution : 320x240
Interface  : SPI
```

Il display è stato già verificato funzionante fino a:

```text
80 MHz SPI
```

Quindi la frequenza SPI target può essere:

```text
80 MHz
```

---

# 4. Orientamento display

È già stato verificato che il seguente init orienta correttamente il display:

```c
#define RG_SCREEN_INIT() \
    ILI9341_CMD(0x36, 0x60); \
    ILI9341_CMD(0x3A, 0x05); \
    ILI9341_CMD(0x11); \
    ILI9341_CMD(0x29);
```

Questo orientamento deve essere mantenuto nel porting ST7789V.

Nota:
il codice originale utilizza macro/funzioni con nome `ILI9341_CMD`, ma il pannello fisico da portare è ST7789V. Va verificato nel driver display come sono implementati i comandi.

---

# 5. File del componente `odroid`

Elenco originale da portare:

```text
components/odroid/CMakeLists.txt
components/odroid/Kconfig.projbuild

components/odroid/odroid_audio.c
components/odroid/odroid_display.c
components/odroid/odroid_input.c
components/odroid/odroid_sdcard.c
components/odroid/odroid_settings.c
components/odroid/odroid_system.c

components/odroid/include/odroid_audio.h
components/odroid/include/odroid_display.h
components/odroid/include/odroid_input.h
components/odroid/include/odroid_sdcard.h
components/odroid/include/odroid_settings.h
components/odroid/include/odroid_system.h
components/odroid/include/pins_config.h
```

Alcuni file sono già stati affrontati durante il porting.

---

# 6. Audio — stato del porting

## `components/audio/include/audio.h`

È stato modificato per eliminare la dipendenza dal codec ES8311/I2C.

La nuova struttura `audio_config_t` deve essere coerente con:

```text
i2s_num
mclk_io
bclk_io
ws_io
dout_io
din_io
pa_ctrl_io
sample_rate
volume
```

Non deve più essere necessario un:

```c
i2c_handle
```

per l'audio.

---

# 7. `components/audio/CMakeLists.txt`

Il file è stato corretto.

Contenuto attuale previsto:

```cmake
idf_component_register(
    SRCS
        "audio.c"

    INCLUDE_DIRS
        "include"

    REQUIRES
        esp_driver_i2s
        esp_driver_gpio
)
```

IMPORTANTE:

Il primo tentativo conteneva erroneamente i delimitatori Markdown:

````text
```cmake
````

nel file reale.

Questo ha prodotto:

````text
Parse error.
Expected a command name, got unquoted argument with text "```cmake".
````

Il problema è stato corretto.

---

# 8. `components/audio/audio.c`

Il driver è stato convertito concettualmente da:

```text
I2S TX + RX
ES8311
I2C
codec control
```

a:

```text
I2S TX only
nessun codec I2C
nessun ES8311
```

Configurazione prevista:

```text
BCLK -> cfg->bclk_io -> GPIO49
WS   -> cfg->ws_io   -> GPIO48
DOUT -> cfg->dout_io -> GPIO50
MCLK -> cfg->mclk_io -> GPIO_NUM_NC
DIN  -> cfg->din_io  -> GPIO_NUM_NC
AMP  -> cfg->pa_ctrl_io -> GPIO47
```

Nel file sono presenti:

```c
#define AUDIO_MCLK_GPIO GPIO_NUM_NC
#define AUDIO_DIN_GPIO  GPIO_NUM_NC
```

ma è stato deciso che il driver definitivo deve preferibilmente utilizzare i valori contenuti in `audio_config_t`, evitando di duplicare i GPIO.

La funzione I2S utilizza le API ESP-IDF 5.5.2:

```c
i2s_new_channel()
i2s_channel_init_std_mode()
i2s_channel_enable()
i2s_channel_write()
```

e:

```c
I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
    AUDIO_BITS_PER_SAMPLE,
    AUDIO_SLOT_MODE
)
```

Formato:

```text
16 bit
stereo
Philips I2S
```

---

# 9. Audio — MCLK e DIN

Devono rimanere:

```c
#define AUDIO_MCLK_GPIO GPIO_NUM_NC
#define AUDIO_DIN_GPIO  GPIO_NUM_NC
```

Non sostituire con GPIO reali.

La scheda non richiede MCLK fisico e non viene utilizzato I2S input.

---

# 10. `components/odroid/odroid_system.c`

È stato modificato per eliminare il vecchio percorso:

```text
I2C
ES8311
ST7701
GT911
```

e utilizzare il nuovo audio:

```text
I2S_NUM_0

BCLK = GPIO49
WS   = GPIO48
DOUT = GPIO50
AMP  = GPIO47

MCLK = NC
DIN  = NC
```

La configurazione audio prevista è:

```c
audio_config_t audio_cfg =
    AUDIO_CONFIG_DEFAULT();

audio_cfg.i2s_num     = I2S_NUM_0;
audio_cfg.mclk_io     = GPIO_NUM_NC;
audio_cfg.bclk_io     = GPIO_NUM_49;
audio_cfg.ws_io       = GPIO_NUM_48;
audio_cfg.dout_io     = GPIO_NUM_50;
audio_cfg.din_io      = GPIO_NUM_NC;
audio_cfg.pa_ctrl_io  = GPIO_NUM_47;
audio_cfg.sample_rate = 16000;
audio_cfg.volume      = 60;

esp_err_t audio_ret = audio_init(&audio_cfg);
```

Il display deve essere inizializzato da:

```text
components/odroid/odroid_display.c
```

e non da `odroid_system.c`.

La vecchia inizializzazione ST7701/GT911 non deve essere utilizzata.

---

# 11. I2C

Per il nuovo hardware l'audio non utilizza più I2C.

La funzione di compatibilità:

```c
odroid_system_get_i2c_bus()
```

è stata mantenuta con ritorno:

```c
NULL
```

ma prima della versione finale bisogna verificare se qualche altro componente del progetto la utilizza davvero.

Se viene utilizzata da touch/display o altro componente, bisognerà decidere se rimuovere quella dipendenza oppure mantenere un bus I2C per quella periferica.

---

# 12. Vecchio hardware eliminato dal percorso audio/display

La vecchia scheda utilizzava componenti che non devono più essere richiesti dal nuovo hardware:

```text
ES8311
esp_codec_dev
I2C audio
GT911 touch
ST7701
MIPI-DSI
```

Il nuovo hardware utilizza:

```text
ST7789V
SPI
I2S
```

---

# 13. Dipendenza `esp_codec_dev`

Durante la build è stato scoperto che il componente audio originale dichiarava:

```yaml
## IDF Component Manager Manifest File
dependencies:
  idf: "^5.0"
  espressif/esp_codec_dev: "^1.3.4"
```

Dato che il nuovo `audio.c` non utilizza più ES8311/esp_codec_dev, è stata rimossa la dipendenza.

Il file:

```text
components/audio/idf_component.yml
```

ora deve contenere:

```yaml
## IDF Component Manager Manifest File
dependencies:
  idf: "^5.0"
```

Le directory:

```text
launcher/managed_components/
apps/gb/managed_components/
apps/nes/managed_components/
```

non devono essere modificate manualmente.

Sono directory generate/gestite dal Component Manager.

---

# 14. Pipeline di compilazione

IMPORTANTE:

Non utilizzare semplicemente:

```powershell
idf.py build
```

dalla root per verificare il progetto completo.

Il repository contiene la pipeline:

```text
build_all.ps1
build_all_lcd.bat
```

La build corretta utilizzata finora è:

```powershell
cd D:\ESP32\RetroESP32-P4
.\build_all.ps1
```

Lo script:

1. configura l'ambiente ESP-IDF;
2. compila il launcher;
3. compila i vari emulatori;
4. produce i binari;
5. prepara il firmware finale.

---

# 15. Prima build

La prima esecuzione di:

```powershell
.\build_all.ps1
```

ha trovato inizialmente un errore nel nostro:

```text
components/audio/CMakeLists.txt
```

perché nel file erano presenti i delimitatori Markdown:

````text
```cmake
````

Questo è stato corretto.

---

# 16. Seconda build — errore attuale

Dopo la correzione del CMake, la pipeline è arrivata a:

```text
=== Building sms ===
```

e poi si è fermata per:

```text
ERROR: Cannot establish a connection to the component registry.
```

con richiesta a:

```text
https://components-file.espressif.com/components/espressif/esp_codec_dev.json
```

La ricerca nel repository ha mostrato:

```text
components\audio\idf_component.yml:
4:  espressif/esp_codec_dev: "^1.3.4"
```

oltre a copie generate sotto:

```text
apps\gb\managed_components\espressif__esp_codec_dev\
apps\nes\managed_components\espressif__esp_codec_dev\
launcher\managed_components\espressif__esp_codec_dev\
```

La dipendenza nel componente `audio` è stata identificata come la causa da rimuovere.

---

# 17. STATO ESATTO AL MOMENTO DELLA PAUSA

Ultima modifica effettuata:

```text
components/audio/idf_component.yml
```

da:

```yaml
dependencies:
  idf: "^5.0"
  espressif/esp_codec_dev: "^1.3.4"
```

a:

```yaml
dependencies:
  idf: "^5.0"
```

NON è stata ancora eseguita una nuova build dopo questa modifica.

---

# 18. PROSSIMO PASSO ESATTO

Quando si riprende il lavoro:

NON modificare altri file preventivamente.

Eseguire dalla root:

```powershell
cd D:\ESP32\RetroESP32-P4
.\build_all.ps1
```

Poi:

* aspettare il primo errore;
* se la build procede, lasciarla procedere;
* se si ferma, riportare il primo errore completo;
* correggere solo quello;
* rilanciare la build.

Procedura da seguire:

```text
BUILD
  ↓
PRIMO ERRORE REALE
  ↓
CORREZIONE DEL SOLO FILE NECESSARIO
  ↓
BUILD
  ↓
PRIMO ERRORE REALE SUCCESSIVO
```

Non fare modifiche speculative a più file contemporaneamente.

---

# 19. File ancora da verificare

Dopo che la build sarà nuovamente operativa, verificare sistematicamente:

```text
components/odroid/CMakeLists.txt
components/odroid/Kconfig.projbuild
components/odroid/odroid_audio.c
components/odroid/odroid_display.c
components/odroid/odroid_input.c
components/odroid/odroid_sdcard.c
components/odroid/odroid_settings.c

components/odroid/include/odroid_audio.h
components/odroid/include/odroid_display.h
components/odroid/include/odroid_input.h
components/odroid/include/odroid_sdcard.h
components/odroid/include/odroid_settings.h
components/odroid/include/odroid_system.h
components/odroid/include/pins_config.h
```

In particolare controllare eventuali riferimenti residui a:

```text
ES8311
esp_codec_dev
I2C audio
ST7701
GT911
MIPI-DSI
vecchi GPIO Guition
```

---

# 20. Pinout finale da tenere sempre come riferimento

```text
========================
WT9932P4-TINY
========================

SDMMC
CLK  = GPIO43
CMD  = GPIO44
D0   = GPIO39
D1   = GPIO40
D2   = GPIO41
D3   = GPIO42


AUDIO I2S
BCLK = GPIO49
WS   = GPIO48
DATA = GPIO50
AMP  = GPIO47

MCLK = NC
DIN  = NC


ST7789V SPI
MISO = GPIO18
MOSI = GPIO19
CLK  = GPIO22
CS   = GPIO20
DC   = GPIO21
RST  = GPIO23
BL   = GPIO17


DISPLAY
320x240
ST7789V
SPI = 80 MHz
```

---

# 21. Regola per continuare il progetto

L'obiettivo è arrivare a una build funzionante senza modificare inutilmente il progetto originale.

Ogni modifica deve rispettare:

```text
Hardware reale
      ↓
pinout WT9932P4-TINY
      ↓
ESP-IDF 5.5.2
      ↓
driver ESP-IDF appropriato
      ↓
API già utilizzate da RetroESP32-P4
```

Prima si fa compilare il progetto.

Solo dopo si passa ai test hardware:

```text
1. boot
2. display
3. retroilluminazione
4. SDMMC
5. input
6. audio
7. emulatori
8. firmware merged
```

---

# 22. Nota importante sulla metodologia

Durante i lavori precedenti si sono verificati alcuni cambi di direzione dovuti a modifiche fatte prima di avere l'errore concreto della build.

Da questo punto in poi:

**NON anticipare correzioni di altri file.**

Si lavora sempre con:

```text
un file
→ build
→ errore
→ correzione
→ build
```

Questo evita di perdere il filo e permette di sapere esattamente quale modifica ha causato o risolto ogni errore.
