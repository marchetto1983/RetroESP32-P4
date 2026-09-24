Double Buffering ST7789V — Technical Checkpoint
Repository

Repository:

https://github.com/marchetto1983/RetroESP32-P4

Branch di lavoro:

WT9932P4-TINY

Componente principale:

components/odroid

Emulatore:

components/gngeo

File LCD principale:

components/odroid/odroid_display.c

File GnGeo rilevante:

components/gngeo/esp32_platform.c

Obiettivo

Eliminare i blocchi del loop dell'emulatore GnGeo causati dal trasferimento del framebuffer verso ST7789V tramite SPI.

Configurazione:

ST7789V

SPI

80 MHz

320×240

RGB565

ESP32-P4

Obiettivo fondamentale:

il trasferimento SPI/DMA non deve bloccare il task dell'emulatore.

Se entrambi i buffer DMA LCD sono occupati, il refresh LCD deve essere scartato e GnGeo deve continuare.

LCD double buffering — STATO VERIFICATO

In components/odroid/odroid_display.c è presente:

#define LCD_DMA_FB_COUNT 2


con due DMA framebuffer:

s_lcd_dma_fb[2]


e:

s_lcd_dma_index
s_lcd_dma_slots


s_lcd_dma_slots è un counting semaphore con due slot.

La configurazione LCD utilizza:

.trans_queue_depth = 2


Il callback:

st7789_color_trans_done()


restituisce lo slot al termine del trasferimento DMA tramite:

xSemaphoreGiveFromISR(
    s_lcd_dma_slots,
    &high_task_woken
);


Questo comportamento è corretto e NON deve essere modificato senza una ragione verificata.

lcd_submit_frame() — STATO VERIFICATO

Il percorso desiderato e attuale è:

static bool lcd_submit_frame(const uint16_t *src, bool byte_swap)


Controlli iniziali:

if (!src || !s_lcd_panel || !s_lcd_initialized ||
    !s_lcd_dma_slots ||
    !s_lcd_dma_fb[0] ||
    !s_lcd_dma_fb[1]) {
    return false;
}


Acquisizione slot:

if (xSemaphoreTake(s_lcd_dma_slots, 0) != pdTRUE) {
    return false;
}


IMPORTANTE:

xSemaphoreTake(..., 0)


deve rimanere NON-BLOCKING.

NON trasformarlo in una chiamata che aspetta.

Il framebuffer sorgente viene copiato nel DMA buffer selezionato.

Il byte swap, quando richiesto, viene eseguito direttamente nel DMA buffer.

Poi viene chiamato:

esp_lcd_panel_draw_bitmap(
    s_lcd_panel,
    0,
    0,
    LCD_H_RES,
    LCD_V_RES,
    dst
);


Se il submit fallisce, lo slot viene restituito.

Se il submit riesce:

s_lcd_dma_index ^= 1;


Il submit quindi non aspetta la fine della trasmissione SPI.

display_flush() — STATO VERIFICATO

Il normale percorso display_flush() utilizza:

bool submitted = lcd_submit_frame(s_framebuffer, false);


Se il submit fallisce:

if (!submitted) {
    return;
}


Questo è intenzionale.

NON bisogna aspettare che il display termini.

Il vecchio percorso che trattava un errore di esp_lcd_panel_draw_bitmap() come unico meccanismo di controllo del trasferimento è stato eliminato dal normale percorso.

display_emu_flush_320x240() — STATO VERIFICATO

Il percorso 320×240 utilizza:

lcd_submit_frame(buf, byte_swap);


e NON deve chiamare direttamente:

esp_lcd_panel_draw_bitmap(...)


Non deve essere creato un ulteriore tmp per il normale percorso 320×240.

Il byte swap viene eseguito da lcd_submit_frame() nel DMA buffer.

display_lcd_draw_raw() — NON ANCORA MODIFICATO

Esiste ancora una chiamata diretta a:

esp_lcd_panel_draw_bitmap(...)


all'interno di:

display_lcd_draw_raw()


Questa funzione è separata dal normale percorso framebuffer 320×240.

NON modificarla automaticamente.

Prima bisogna verificare i suoi caller e stabilire se viene utilizzata durante l'emulazione GnGeo.

GnGeo — percorso verificato

Il percorso effettivo individuato nel repository è:

GnGeo
  |
  v
draw_screen()
  |
  v
screen_update()
  |
  v
neo_vidQueue
  |
  v
neo_video_task()
  |
  v
ili9341_write_frame_rgb565_custom()
  |
  v
display_emu_flush_320x240()
  |
  v
lcd_submit_frame()
  |
  +--> LCD DMA FB0
  |
  +--> LCD DMA FB1
  |
  v
SPI
  |
  v
ST7789V


Quindi GnGeo raggiunge effettivamente il nuovo percorso LCD.

NON è necessario modificare GnGeo alla cieca.

GnGeo ha un proprio double buffering

In:

components/gngeo/esp32_platform.c


sono presenti:

static uint16_t *lcd_fb_arr[2] = { NULL, NULL };
static int lcd_fb_write = 0;
static QueueHandle_t neo_vidQueue = NULL;
static TaskHandle_t neo_videoTaskHandle = NULL;
static volatile bool neo_videoTaskRunning = false;


screen_init() alloca due framebuffer GnGeo da 304×224.

Il video task consuma i framebuffer tramite neo_vidQueue.

Il producer alterna:

FB0
FB1
FB0
FB1
...


La queue utilizza xQueueOverwrite().

Quindi la queue è concettualmente un meccanismo "latest frame wins", non una coda di più frame.

PUNTO APERTO PRINCIPALE

Il prossimo problema da verificare NON è lcd_submit_frame().

Il prossimo problema è la proprietà/lifetime dei due framebuffer GnGeo:

lcd_fb_arr[0]
lcd_fb_arr[1]


Bisogna verificare se il producer può riutilizzare un framebuffer mentre:

neo_video_task()


lo sta ancora leggendo.

Sequenza potenzialmente critica:

Producer                    Video task

FB0 <- frame N
queue FB0  ------------->   usa FB0

FB1 <- frame N+1
queue FB1

FB0 <- frame N+2
       ^
       |
       possibile riutilizzo
       mentre video task
       sta ancora consumando FB0


Questa è una possibile race e deve essere verificata prima di modificare il codice.

IMPORTANTISSIMO: NON CONFONDERE I DUE DOUBLE BUFFER

Esistono due livelli distinti:

Double buffer GnGeo
lcd_fb_arr[0]
lcd_fb_arr[1]


Serve a separare il rendering GnGeo dal video task.

Double buffer LCD DMA
s_lcd_dma_fb[0]
s_lcd_dma_fb[1]


Serve a separare il framebuffer GnGeo dal trasferimento SPI DMA.

Il fatto che il secondo sia corretto NON dimostra automaticamente che il primo sia corretto.

Perché il double buffer LCD protegge il framebuffer GnGeo

lcd_submit_frame() copia il framebuffer sorgente nel DMA buffer:

GnGeo framebuffer
      |
      | memcpy / byte swap
      v
LCD DMA framebuffer
      |
      v
SPI DMA


Il DMA LCD quindi non dovrebbe utilizzare direttamente:

lcd_fb_arr[]


Questo è corretto.

Stato build

ESP-IDF:

5.5.2

La build è stata verificata come OK.

Era presente un errore in:

components/odroid/odroid_input.c


relativo a:

adc_oneshot_new_unit()


La chiamata corretta è:

adc_oneshot_new_unit(
    &unit_cfg,
    &s_battery_adc_handle
);


La build è poi risultata OK.

TEST RUNTIME ANCORA DA FARE

Non è ancora stato dimostrato runtime che:

GnGeo continui a produrre frame mentre SPI trasmette;

il DMA LCD sia realmente asincrono rispetto al task GnGeo;

il drop dei refresh avvenga quando entrambi i DMA buffer sono occupati;

il task dell'emulatore non venga bloccato dal display;

il double buffering GnGeo non abbia una race;

il problema originale dei blocchi a 80 MHz sia completamente risolto.

REGOLA DI LAVORO

Procedere una modifica alla volta.

NON modificare contemporaneamente:

lcd_submit_frame()

callback DMA

trans_queue_depth

display_flush()

display_emu_flush_320x240()

screen_update()

neo_video_task()

altro codice GnGeo

Prima verificare il comportamento attuale.

NEXT STEP — RIPARTIRE ESATTAMENTE DA QUI

La prossima verifica deve essere:

Verificare la lifetime dei framebuffer lcd_fb_arr[2]

Analizzare in dettaglio:

screen_update()
    |
    +--> quale buffer viene scritto?
    |
    +--> quando viene messo in neo_vidQueue?
    |
    +--> quando viene riutilizzato?
    |
    v
neo_video_task()
    |
    +--> quando riceve il puntatore?
    |
    +--> quanto tempo lo utilizza?
    |
    +--> quando termina di leggerlo?


Bisogna determinare se esiste una sincronizzazione che impedisce:

producer scrive FB0


mentre contemporaneamente:

video task legge FB0


Se non esiste, il problema è nel double buffering GnGeo e NON nel double buffering LCD.

NON apportare ancora modifiche.

Prima dimostrare il comportamento dal codice.

CHECKPOINT

Quando si riprende il lavoro, partire da questa frase:

"Ripartiamo dal checkpoint DOUBLE_BUFFERING_STATUS.md: il lato LCD DMA è già stato verificato; il prossimo punto da verificare è la lifetime/race di lcd_fb_arr[2] tra screen_update() e neo_video_task()."

Questo checkpoint è lo stato tecnico di riferimento.