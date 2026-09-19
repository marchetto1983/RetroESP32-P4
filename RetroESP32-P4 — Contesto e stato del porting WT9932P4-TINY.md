RetroESP32-P4 — Porting WT9932P4-TINY + ST7789V
1. Obiettivo del progetto

Portare il progetto:

Repository originale: https://github.com/giltal/RetroESP32-P4

Branch di lavoro: WT9932P4-TINY

Repository personale: https://github.com/marchetto1983/RetroESP32-P4/tree/WT9932P4-TINY

dalla scheda/display originale basata sulla soluzione Guition alla:

Wireless-Tag WT9932P4-TINY

Display SPI ST7789V

Risoluzione 320x240

Ambiente:

ESP-IDF 5.5.2
Windows


Root progetto:

D:\ESP32\RetroESP32-P4


Variabili ambiente:

IDF_PATH            = C:\Espressif\frameworks\esp-idf-v5.5.2
IDF_PYTHON_ENV_PATH = C:\Espressif\python_env\idf5.5_py3.11_env

2. Hardware definitivo
SDMMC
#define RG_STORAGE_SDMMC_HOST          SDMMC_HOST_SLOT_1
#define RG_STORAGE_SDMMC_SPEED         SDMMC_FREQ_HIGHSPEED

#define RG_GPIO_SDMMC_CLK              GPIO_NUM_43
#define RG_GPIO_SDMMC_CMD              GPIO_NUM_44
#define RG_GPIO_SDMMC_D0               GPIO_NUM_39
#define RG_GPIO_SDMMC_D1               GPIO_NUM_40
#define RG_GPIO_SDMMC_D2               GPIO_NUM_41
#define RG_GPIO_SDMMC_D3               GPIO_NUM_42

Audio I2S
#define RG_GPIO_SND_I2S_BCK         GPIO_NUM_49
#define RG_GPIO_SND_I2S_WS          GPIO_NUM_48
#define RG_GPIO_SND_I2S_DATA        GPIO_NUM_50
#define RG_GPIO_SND_AMP_ENABLE      GPIO_NUM_47


Configurazione:

I2S peripheral : I2S_NUM_0
BCLK           : GPIO49
WS/LRCLK       : GPIO48
DOUT           : GPIO50
AMP ENABLE     : GPIO47

MCLK           : GPIO_NUM_NC
DIN            : GPIO_NUM_NC


Audio:

I2S TX-only
16 bit
stereo
Philips I2S
sample rate 16000 Hz
volume iniziale 60


Non utilizzare:

ES8311
esp_codec_dev
I2C audio
I2S RX
MCLK fisico


Assunzione:

AMP ENABLE GPIO47 = active HIGH

3. Display ST7789V
#define RG_GPIO_LCD_MISO            GPIO_NUM_18
#define RG_GPIO_LCD_MOSI            GPIO_NUM_19
#define RG_GPIO_LCD_CLK             GPIO_NUM_22
#define RG_GPIO_LCD_CS              GPIO_NUM_20
#define RG_GPIO_LCD_DC              GPIO_NUM_21
#define RG_GPIO_LCD_RST             GPIO_NUM_23
#define RG_GPIO_LCD_BCKL            GPIO_NUM_17


Display:

Controller : ST7789V
Resolution : 320x240
Interface  : SPI
SPI target : 80 MHz


Il display è stato verificato funzionante fino a 80 MHz.

4. Orientamento display

L'orientamento corretto è stato verificato con:

MADCTL = 0x60


Nel file:

components/odroid/include/pins_config.h


deve quindi esserci:

#define LCD_MADCTL_VALUE        0x60


Non cambiare nuovamente questo valore senza una ragione precisa.

Durante il test è stato provato 0x68, ma il risultato è stato:

azzurrino → rosso


quindi è stato deciso di tornare a:

0x60

5. Input GPIO fisico

Il nuovo hardware utilizza i pulsanti GPIO fisici.

Mapping definitivo:

#define RG_GAMEPAD_GPIO_MAP {\
    {RG_KEY_LEFT,   .num = GPIO_NUM_4,  .pullup = 1, .level = 0},\
    {RG_KEY_RIGHT,  .num = GPIO_NUM_2,  .pullup = 1, .level = 0},\
    {RG_KEY_UP,     .num = GPIO_NUM_5,  .pullup = 1, .level = 0},\
    {RG_KEY_DOWN,   .num = GPIO_NUM_3,  .pullup = 1, .level = 0},\
    {RG_KEY_START,  .num = GPIO_NUM_46, .pullup = 1, .level = 0},\
    {RG_KEY_SELECT, .num = GPIO_NUM_45, .pullup = 1, .level = 0},\
    {RG_KEY_A,      .num = GPIO_NUM_26, .pullup = 1, .level = 0},\
    {RG_KEY_B,      .num = GPIO_NUM_27, .pullup = 1, .level = 0},\
    {RG_KEY_X,      .num = GPIO_NUM_13, .pullup = 1, .level = 0},\
    {RG_KEY_Y,      .num = GPIO_NUM_12, .pullup = 1, .level = 0},\
    {RG_KEY_L,      .num = GPIO_NUM_11, .pullup = 1, .level = 0},\
    {RG_KEY_R,      .num = GPIO_NUM_14, .pullup = 1, .level = 0},\
}


Comportamento elettrico:

pull-up interno abilitato

HIGH = pulsante rilasciato
LOW  = pulsante premuto

Stato del porting input

Il progetto originale utilizzava ancora il percorso USB gamepad.

È stato modificato:

components/odroid/odroid_input.c


per utilizzare direttamente i 12 GPIO.

La lettura ora segue:

GPIO
 ↓
gpio_get_level()
 ↓
LOW = pressed
HIGH = released
 ↓
odroid_gamepad_state
 ↓
launcher / emulatori


Il percorso USB è stato mantenuto nel codice per compatibilità, ma non è più necessario per il gamepad fisico.

6. Input — risultato verificato

Il gamepad GPIO funziona.

Sono stati verificati:

launcher
navigazione
selezione
avvio NES


Il pad USB NON è necessario.

7. Audio — porting

components/audio/include/audio.h è stato adattato per eliminare la dipendenza dal codec ES8311/I2C.

La struttura audio_config_t utilizza:

i2s_num
mclk_io
bclk_io
ws_io
dout_io
din_io
pa_ctrl_io
sample_rate
volume


Configurazione attuale:

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


API ESP-IDF 5.5.2 utilizzate:

i2s_new_channel()
i2s_channel_init_std_mode()
i2s_channel_enable()
i2s_channel_write()

8. Audio CMake

components/audio/CMakeLists.txt corretto.

Contenuto:

idf_component_register(
    SRCS
        "audio.c"

    INCLUDE_DIRS
        "include"

    REQUIRES
        esp_driver_i2s
        esp_driver_gpio
)


In precedenza erano stati accidentalmente inseriti nel file reale i delimitatori Markdown:

```cmake


Questo aveva causato:

Parse error.
Expected a command name, got unquoted argument with text "```cmake".


Il problema è stato risolto.

9. esp_codec_dev

Il vecchio:

components/audio/idf_component.yml


conteneva:

dependencies:
  idf: "^5.0"
  espressif/esp_codec_dev: "^1.3.4"


È stato corretto in:

dependencies:
  idf: "^5.0"


Il nuovo audio non utilizza:

ES8311
esp_codec_dev
I2C codec


Non modificare manualmente:

launcher/managed_components/
apps/gb/managed_components/
apps/nes/managed_components/


perché sono directory gestite dal Component Manager.

10. Display — stato attuale

Il display:

ST7789V
320x240
SPI
80 MHz


si accende e visualizza correttamente la schermata iniziale.

L'orientamento è corretto.

Il problema ancora aperto riguarda i colori.

Sintomo

Nel launcher:

azzurro/azzurrino → colore errato
giallo → colore errato


È stato verificato che il problema esiste già nel launcher, quindi NON è un problema specifico del framebuffer NES.

È stato provato:

LCD_MADCTL_VALUE = 0x60


e successivamente:

LCD_MADCTL_VALUE = 0x68


Con 0x68:

l'azzurrino è diventato rosso


Per questo si è deciso di tornare a:

#define LCD_MADCTL_VALUE 0x60

11. Punto attuale del problema colori

Il file principale interessato è:

components/odroid/odroid_display.c


Configurazione attuale del driver:

.rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB,
.data_endian    = LCD_RGB_DATA_ENDIAN_BIG,
.bits_per_pixel = 16,


Il framebuffer utilizza:

uint16_t
RGB565


Il display viene aggiornato tramite:

esp_lcd_panel_draw_bitmap()


Nel codice esiste inoltre:

ili9341_write_frame_rectangleLE()


nome ereditato dal progetto originale.

Il progetto contiene anche una funzione:

display_emu_flush_320x240()


che gestisce eventualmente un byte_swap.

NON aggiungere altri byte swap alla cieca.

12. Ultima modifica proposta per i colori

È stata proposta una modifica alla funzione:

static esp_err_t st7789_apply_known_init(void)


aggiungendo la configurazione esplicita di:

RAMCTRL = 0xB0


con:

00 E8


La funzione proposta è:

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

ATTENZIONE

Questa modifica è stata proposta ma deve ancora essere verificata con una build/test.

La configurazione deve essere:

#define LCD_MADCTL_VALUE 0x60


e:

RAMCTRL = 00 E8


Non modificare contemporaneamente:

.rgb_ele_order
.data_endian
LCD_PIXEL_FORMAT

13. Stato NES

NES funziona.

È stato possibile:

avviare NES
giocare
aprire il menu in-game
uscire dal gioco
tornare al launcher


Quindi non serve introdurre una nuova combinazione START + SELECT.

Il mapping previsto rimane:

X = GPIO13
Y = GPIO12


Nel porting è stata mantenuta la logica:

X → MENU
Y → VOLUME


Durante il gioco è stato possibile uscire aprendo il menu, anche se non è ancora stato determinato con certezza se il tasto usato fosse X o Y.

Da verificare eventualmente in seguito.

14. Situazione attuale complessiva
FUNZIONANTE
✅ ESP32-P4 WT9932P4-TINY
✅ ESP-IDF 5.5.2
✅ boot
✅ launcher
✅ display ST7789V
✅ risoluzione 320x240
✅ SPI 80 MHz
✅ orientamento display
✅ gamepad GPIO
✅ D-Pad
✅ A/B/X/Y
✅ L/R
✅ START/SELECT
✅ avvio NES
✅ esecuzione NES
✅ menu in-game
✅ ritorno al launcher

DA SISTEMARE
⚠️ ordine/formato colori ST7789V
⚠️ verificare eventualmente X/Y per apertura menu
⚠️ audio da testare realmente sull'hardware
⚠️ SDMMC da testare realmente

15. Pinout completo di riferimento
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


GAMEPAD
LEFT   = GPIO4
RIGHT  = GPIO2
UP     = GPIO5
DOWN   = GPIO3

START  = GPIO46
SELECT = GPIO45

A      = GPIO26
B      = GPIO27
X      = GPIO13
Y      = GPIO12
L      = GPIO11
R      = GPIO14

16. Tabella conflitti GPIO
LCD:
17,18,19,20,21,22,23

I2S:
47,48,49,50

SDMMC:
39,40,41,42,43,44

GAMEPAD:
2,3,4,5
11,12,13,14
26,27
45,46


Non risultano sovrapposizioni tra le periferiche sopra.

17. Pipeline di build

La build completa originale:

cd D:\ESP32\RetroESP32-P4
.\build_all.ps1


compila:

launcher
GB
NES
SMS
e altri emulatori


La build completa può richiedere diverse ore.

Per lo sviluppo è quindi preferibile una build ridotta:

launcher
+
NES


L'obiettivo è utilizzare:

build_test.ps1


per velocizzare i test.

18. Metodo di sviluppo da seguire

IMPORTANTE:

Non modificare molti file contemporaneamente.

Procedura:

BUILD
  ↓
PRIMO ERRORE REALE
  ↓
CORREZIONE DEL SOLO FILE NECESSARIO
  ↓
BUILD
  ↓
PRIMO ERRORE REALE SUCCESSIVO


Per i problemi hardware:

una modifica
↓
build
↓
flash
↓
test
↓
osservazione
↓
prossima modifica


Evitare correzioni speculative.

19. Prossimo passo esatto

Riprendendo il lavoro:

Passo 1

Verificare:

#define LCD_MADCTL_VALUE 0x60


in:

components/odroid/include/pins_config.h

Passo 2

Verificare che st7789_apply_known_init() contenga:

const uint8_t ramctrl[] = {
    0x00,
    0xE8
};


e:

esp_lcd_panel_io_tx_param(
    s_lcd_io,
    0xB0,
    ramctrl,
    sizeof(ramctrl)
);

Passo 3

Compilare soltanto il launcher:

cd D:\ESP32\RetroESP32-P4
idf.py -C launcher fullclean
idf.py -C launcher build

Passo 4

Flashare e verificare i colori del launcher.

Testare almeno:

bianco
nero
rosso
verde
blu
giallo
azzurro/ciano

Passo 5

NON modificare altro se i colori sono ancora errati.

A quel punto analizzare precisamente:

RGB565
↓
framebuffer
↓
ili9341_write_frame_rectangleLE()
↓
esp_lcd_panel_draw_bitmap()
↓
SPI
↓
ST7789V

20. Obiettivo finale

Dopo la stabilizzazione del display:

1. boot
2. launcher
3. colori corretti
4. retroilluminazione
5. gamepad GPIO
6. SDMMC
7. audio I2S
8. NES
9. altri emulatori
10. build_test.ps1
11. firmware merged
12. build_all.ps1 finale


La priorità immediata rimane:

ST7789V → colori RGB565 corretti


e poi:

audio + SDMMC


senza modificare inutilmente le parti già funzionanti.