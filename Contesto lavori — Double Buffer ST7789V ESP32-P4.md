Contesto lavori — Double Buffer ST7789V ESP32-P4
Progetto

Repository:

RetroESP32-P4

Branch interessato:

WT9932P4-TINY

File principale:

components/odroid/odroid_display.c

Obiettivo: eliminare i blocchi dell'emulatore causati dal trasferimento SPI verso il display ST7789V a 80 MHz, usando double buffering completo lato LCD DMA.

Il display fisico è:

ST7789V

SPI

80 MHz

risoluzione 320x240

L'emulatore lavora anche a 320x240 in alcuni percorsi, quindi non è necessaria una scalatura per il display fisico.

Problema iniziale

Il display SPI aveva un solo buffer/una sola transazione disponibile.

L'ipotesi è che il task dell'emulatore potesse essere rallentato/bloccato aspettando che il trasferimento del frame precedente terminasse.

Nel vecchio progetto il display era parallelo e la PPA si occupava del trasferimento/scaricamento.

Nel branch attuale il display è SPI ST7789V a 80 MHz.

L'obiettivo è permettere all'emulatore di continuare a produrre frame mentre il display sta ancora trasmettendo quello precedente.

Strategia scelta

Fare double buffering completo:

Emulatore framebuffer
        |
        v
lcd_submit_frame()
        |
        +----> DMA buffer 0 ----> SPI ----> ST7789V
        |
        +----> DMA buffer 1 ----> SPI ----> ST7789V


Quando un buffer è ancora occupato dal DMA, l'altro può essere usato per preparare il frame successivo.

Se entrambi sono occupati, NON bisogna bloccare il task dell'emulatore.

Il frame viene semplicemente scartato:

lcd_submit_frame() == false
        |
        +--> drop refresh
        +--> emulatore continua


Questo è intenzionale: meglio perdere un refresh LCD che bloccare il loop dell'emulatore.

Modifiche già presenti

Nel file odroid_display.c sono già presenti le parti fondamentali del double buffering.

Sono presenti:

#define LCD_DMA_FB_COUNT 2


e:

s_lcd_dma_fb[2]


Sono presenti due buffer DMA LCD.

È presente anche:

s_lcd_dma_slots


come semaforo/counting semaphore per tenere traccia dei due buffer disponibili.

Il semaforo viene inizializzato con 2 slot.

È presente il callback:

st7789_color_trans_done()


che restituisce lo slot al termine della trasmissione DMA.

La configurazione LCD usa:

.trans_queue_depth = 2


Questo è voluto e fa parte del double buffering.

Funzione lcd_submit_frame()

La funzione attuale è sostanzialmente questa:

static bool lcd_submit_frame(const uint16_t *src, bool byte_swap)
{
    if (!src || !s_lcd_panel || !s_lcd_initialized ||
        !s_lcd_dma_slots ||
        !s_lcd_dma_fb[0] ||
        !s_lcd_dma_fb[1]) {
        return false;
    }

    /*
     * Non-blocking acquisition.
     *
     * If both DMA buffers are still being transmitted, don't stall the
     * emulator task waiting for the LCD.
     */
    if (xSemaphoreTake(s_lcd_dma_slots, 0) != pdTRUE) {
        return false;
    }

    uint16_t *dst = s_lcd_dma_fb[s_lcd_dma_index];

    if (!byte_swap) {
        memcpy(dst, src, FB_SIZE);
    } else {
        for (int i = 0; i < FB_PIXELS; ++i) {
            uint16_t p = src[i];
            dst[i] = (uint16_t)((p >> 8) | (p << 8));
        }
    }

    esp_err_t ret = esp_lcd_panel_draw_bitmap(
        s_lcd_panel,
        0,
        0,
        LCD_H_RES,
        LCD_V_RES,
        dst
    );

    if (ret != ESP_OK) {
        /*
         * No callback will be generated for a failed submission, therefore
         * return the slot manually.
         */
        xSemaphoreGive(s_lcd_dma_slots);

        ESP_LOGE(
            TAG,
            "LCD DMA submit failed: %s",
            esp_err_to_name(ret)
        );

        return false;
    }

    s_lcd_dma_index ^= 1;

    return true;
}


Questa funzione è il cuore del nuovo sistema.

Importante:

xSemaphoreTake(s_lcd_dma_slots, 0)


usa timeout 0.

Quindi NON aspetta.

display_emu_flush_320x240()

Questa funzione era stata modificata per usare:

lcd_submit_frame(buf, byte_swap)


al posto del vecchio:

esp_lcd_panel_draw_bitmap(...)


e questo è il percorso che vogliamo mantenere.

Il percorso HDMI deve rimanere separato.

Per il display fisico 320x240, la parte dovrebbe essere:

if (!s_lcd_initialized || !s_lcd_panel) {
    return;
}

if (!lcd_submit_frame(buf, byte_swap)) {
    /*
     * Both LCD DMA buffers may still be busy.
     *
     * Do NOT wait here: dropping this LCD refresh is preferable
     * to blocking the emulator task.
     */
    return;
}


Quindi:

niente tmp

niente esp_lcd_panel_draw_bitmap() diretto

niente secondo percorso LCD

lcd_submit_frame() deve gestire anche byte_swap

ERRORE ANCORA DA SISTEMARE

Nel file attuale c'è/era un residuo nel percorso display_flush().

Era presente:

int64_t t1 = esp_timer_get_time();

if (ret != ESP_OK) {
    ESP_LOGE(
        TAG,
        "ST7789 frame transfer failed: %s",
        esp_err_to_name(ret)
    );
    return;
}


Questo è SBAGLIATO perché in quel percorso non esiste più un ret.

La chiamata è:

bool submitted = lcd_submit_frame(s_framebuffer, false);


quindi il risultato è submitted, non ret.

Il blocco:

if (ret != ESP_OK) {
    ...
}


deve essere eliminato.

Dopo:

int64_t t1 = esp_timer_get_time();


deve rimanere il codice di timing, ad esempio:

s_timing_lcd_acc += t1 - t0;
s_timing_count++;


NON bisogna reintrodurre:

ST7789 frame transfer failed


e NON bisogna creare artificialmente un ret in quel punto.

Punto esatto da verificare domani

Prima cosa da fare domani:

Aprire il branch:

WT9932P4-TINY

Aprire:

components/odroid/odroid_display.c

Verificare che il commit/versione sia quella più recente.

Il commit/versione indicato dall'utente è:

"modifiche display double buffer 3"

È importante non consultare una versione vecchia della pagina GitHub.

Se serve leggere il sorgente, usare direttamente il file del branch corrente/RAW.

Stato attuale

La situazione desiderata è:

                    +----------------+
                    | Emulator frame |
                    +-------+--------+
                            |
                            v
                  lcd_submit_frame()
                            |
                   non-blocking take
                            |
                 +----------+----------+
                 |                     |
                 v                     v
          DMA buffer 0          DMA buffer 1
                 |                     |
                 +----------+----------+
                            |
                            v
                       SPI 80 MHz
                            |
                            v
                         ST7789V


Se entrambi i buffer sono occupati:

lcd_submit_frame()
        |
        v
xSemaphoreTake(..., 0)
        |
        v
false
        |
        v
drop frame
        |
        v
emulatore continua

Prossimo lavoro

Il prossimo step NON è rifare tutto.

È controllare display_emu_flush_320x240() e assicurarsi che sia completamente migrata al nuovo percorso:

lcd_submit_frame(buf, byte_swap)


Deve essere eliminato definitivamente il vecchio percorso:

esp_lcd_panel_draw_bitmap(
    s_lcd_panel,
    0,
    0,
    EMU_W,
    EMU_H,
    tmp
);


e anche il relativo:

uint16_t *tmp = alloc_emu_buffer();


perché il byte swap viene già effettuato direttamente nel buffer DMA scelto da:

lcd_submit_frame()


Quindi NON serve più creare un buffer tmp.

Regola importante per evitare confusione

Procedere una modifica alla volta.

Non cambiare contemporaneamente:

lcd_submit_frame()

callback DMA

trans_queue_depth

display_flush()

display_emu_flush_320x240()

Prima verificare ogni pezzo.

La configurazione desiderata finale è:

trans_queue_depth = 2
        +
2 DMA framebuffer
        +
counting semaphore con 2 slot
        +
callback che restituisce lo slot
        +
lcd_submit_frame() non-blocking
        +
display_flush() usa lcd_submit_frame()
        +
display_emu_flush_320x240() usa lcd_submit_frame()

Obiettivo finale

Il comportamento desiderato è:

emulatore a 60 FPS

ST7789V SPI a 80 MHz

double buffering DMA

nessuna attesa bloccante del task emulatore

eventuale drop di un refresh quando entrambi i buffer sono occupati

byte swap effettuato nel buffer DMA

nessun tmp aggiuntivo per il normale percorso LCD

nessun vecchio esp_lcd_panel_draw_bitmap() diretto fuori da lcd_submit_frame()

L'idea è che il display possa "tamponare" temporalmente due frame mentre il gioco continua a girare, senza trasformare il trasferimento SPI in un punto di blocco del loop emulativo.