RetroESP32-P4 — Stato lavori WT9932P4-TINY
Data
19 settembre 2026
Hardware / Target
ESP32-P4
Board: WT9932P4-TINY
Display: ST7789V 320x240
Interfaccia display: SPI
SPI display: 80 MHz
PSRAM: 32 MB @ 200 MHz
ESP-IDF: 5.5.2
1. Punto di partenza
Il progetto originale RetroESP32-P4 era già completo e funzionante.
Gli emulatori risultano funzionanti:
NES
NeoGeo
altri emulatori presenti nel progetto
Il problema introdotto dalla modifica per WT9932P4-TINY riguardava principalmente il display, non il funzionamento degli emulatori.
La modifica principale fatta sul port WT9932P4-TINY è stata la sostituzione del display originale con un:
ST7789V SPI LCD
320x240 RGB565
SPI 80 MHz

Il file principale modificato per il display è:
components/odroid/odroid_display.c

Versione modificata:
https://github.com/marchetto1983/RetroESP32-P4/blob/WT9932P4-TINY/components/odroid/odroid_display.c

Versione originale:
https://github.com/marchetto1983/RetroESP32-P4/blob/main/components/odroid/odroid_display.c
2. Problema riscontrato
Durante l'esecuzione dei giochi il sistema continuava a funzionare:
emulatore attivo
audio funzionante
FPS regolari
input funzionante
possibilità di uscire dall'emulatore
ma il display poteva smettere di aggiornarsi.
Il comportamento era:
gioco continua a funzionare
        ↓
audio continua
        ↓
emulatore continua
        ↓
display smette di aggiornarsi
        ↓
rimane visualizzato l'ultimo frame ricevuto

Quindi il problema NON era un blocco dell'emulatore.
Era un problema nella pipeline di aggiornamento del display SPI.
Questo punto è importante e va mantenuto come riferimento per le analisi successive.
3. Diagnosi
Il problema è stato ricondotto alla gestione del buffer/display SPI nel nuovo odroid_display.c.
La versione modificata utilizzava una gestione dei buffer che poteva provocare una sovrapposizione/conflitto nella memoria utilizzata per la trasmissione del frame.
Il risultato era che il gioco continuava a produrre frame, ma il trasferimento verso il display poteva rimanere bloccato o non completarsi correttamente.
Il sintomo visibile era quindi:
ultimo frame visualizzato congelato

mentre CPU, audio ed emulatore continuavano a lavorare.
4. Correzione principale
È stata modificata la gestione della quantità di buffer utilizzati per il display.
Il parametro problematico è stato ridotto:
8 → 1

In particolare, la configurazione che utilizzava una quantità di buffer pari a 8 è stata portata a un singolo buffer.
La modifica è stata fatta per evitare la sovrapposizione/concorrenza tra i buffer utilizzati dalla pipeline SPI del display.
Concettualmente:
PRIMA
framebuffer
   ↓
buffer 0
buffer 1
buffer 2
buffer 3
buffer 4
buffer 5
buffer 6
buffer 7
   ↓
SPI

Dopo la correzione:
framebuffer
   ↓
buffer singolo
   ↓
SPI
   ↓
ST7789V

Questa modifica ha risolto il problema osservato durante i test.
5. Risultato del test
Dopo la modifica:
buffer count: 8 → 1

il display non si è più bloccato durante il gioco nel test effettuato.
Il gioco ha continuato a funzionare normalmente e il display ha continuato ad aggiornarsi.
Questo è il risultato più importante ottenuto finora.
Il test è stato effettuato inizialmente con NES.
6. Test NES
ROM utilizzata:
Punisher, The (USA).nes

Il log ha mostrato:
nofrendo_run: starting NES emulator

seguito dal caricamento corretto della ROM e dal funzionamento regolare dell'emulatore.
Gli FPS sono rimasti intorno a:
60 FPS

Esempi:
FPS:60.195
FPS:60.204
FPS:60.213
FPS:59.77

L'audio funzionava.
Il problema del display non si è ripresentato dopo la modifica da 8 a 1 buffer.
7. Informazioni importanti dal log NES
Il log ha confermato che il problema non era un crash dell'emulatore.
Durante l'esecuzione erano presenti continuamente valori come:
HEAP:0x1bb130c, FPS:60.195
HEAP:0x1bb6b50, FPS:60.204
HEAP:0x1bceb5c, FPS:60.205

quindi il main loop dell'emulatore continuava a girare.
Anche l'audio continuava a funzionare.
Questo conferma la distinzione fondamentale:
emulatore funzionante
+
audio funzionante
+
CPU funzionante
+
display congelato
=
problema display/SPI/framebuffer

non:
crash dell'emulatore
8. Uscita dall'emulatore
È stato osservato anche un problema separato:
quando si usciva dall'emulatore con la combinazione di pulsanti, il gioco terminava e il sistema tornava verso il launcher, ma il display poteva continuare a mostrare l'ultimo frame del gioco.
Questo problema NON deve essere confuso con il problema principale analizzato.
Il problema principale era:
durante il gioco l'emulatore continuava a funzionare ma il display smetteva di aggiornarsi.
La questione del frame rimasto visualizzato durante il ritorno al launcher è secondaria rispetto alla pipeline di aggiornamento del display e non è stata utilizzata come causa del blocco durante il gioco.
9. Log di boot
La configurazione attuale utilizza:
Project name: launcher
Project name: nes_app
ESP-IDF: v5.5.2
CPU: 360 MHz
PSRAM: 32 MB

Display:
ST7789V
320x240
RGB565
SPI @ 80 MHz

Framebuffer:
320x240
153600 bytes

Il display viene inizializzato correttamente:
odroid_display: Framebuffer allocated: 320x240 (153600 bytes)
odroid_display: Initializing ST7789V SPI LCD
odroid_display: ST7789V ready: 320x240 RGB565 SPI @ 80000000 Hz
10. Problemi presenti ma non correlati al blocco del display
Nel log sono presenti alcuni errori che non sono stati identificati come causa del problema principale.
GT911
Nel launcher:
i2c.master: this port has not been initialized

seguito da:
GT911 read error
GT911 init failed
Touch controller GT911 initialization failed

Il touch non è utilizzato per il funzionamento normale del gamepad/emulatore e non è stato considerato la causa del blocco del display.
LEDC
Nel launcher:
ledc: Fade service not installed
ledc_set_fade_time_and_start

Questo riguarda il fade del backlight.
Il display viene comunque inizializzato e visualizzato correttamente.
SD LDO
È presente:
ldo: The voltage value 0 is out of the recommended range

ma la SD viene montata correttamente a 40 MHz.
File PNG mancanti
Nel launcher:
Failed to open file: /sd/boot_logo.png

e:
Failed to open file: /sd/system_art/nes.png

Non sono stati considerati correlati al problema del blocco del display durante l'emulazione.
11. Build
In precedenza il progetto utilizzava:
build_all.ps1
generate_merged_bin.ps1

È stato creato:
build_quick.ps1

per compilare più rapidamente le applicazioni necessarie.
La configurazione è stata successivamente estesa per includere altri emulatori.
12. NeoGeo
Dopo la verifica del funzionamento del NES si è deciso di testare un secondo emulatore.
È stato scelto NeoGeo.
Il runtime NeoGeo del progetto si trova in:
components/gngeo/

NON esiste:
components/neogeo/

Questa distinzione è importante.
Il progetto utilizza quindi il core/runtime:
gngeo
13. ROM NeoGeo disponibili
Sono disponibili tre ROM:
mslug.zip
neodrift.zip
burningf.zip

La preparazione delle cache viene effettuata tramite:
SDcard/roms/neogeo/gen_cache.py

Repository:
https://github.com/marchetto1983/RetroESP32-P4/blob/WT9932P4-TINY/SDcard/roms/neogeo/gen_cache.py
14. Test gen_cache.py — Burning Fight
È stato eseguito:
python gen_cache.py burningf

Risultato:
Game:    Burning Fight (set 1) (burningf)
Year:    1991
Parent:  neogeo
Sprites: 4 MB
ADPCM-A: 2048 KB

Sprite cache:
C ROM pairs: 2
Encryption: none

Sono stati convertiti:
32,768 tiles

di cui:
30,961 visible
1,807 invisible

Cache generata:
burningf/burningf.ctile
burningf/burningf.cusage

Dimensioni:
burningf.ctile = 4,194,304 bytes
burningf.cusage = 8,196 bytes

Audio:
burningf/burningf.vroma

Dimensione:
2,097,152 bytes

Generazione completata correttamente in circa:
11 secondi
15. Struttura cache NeoGeo
Lo script indica esplicitamente di trasferire le cache nella directory:
/sd/roms/neogeo/burningf/

I tre comandi prodotti da gen_cache.py sono:
python tools/upload_papp.py burningf/burningf.cusage --dest /sd/roms/neogeo/burningf/burningf.cusage
python tools/upload_papp.py burningf/burningf.ctile --dest /sd/roms/neogeo/burningf/burningf.ctile
python tools/upload_papp.py burningf/burningf.vroma --dest /sd/roms/neogeo/burningf/burningf.vroma

Quindi le cache devono avere questa struttura:
/sd/roms/neogeo/burningf/
    burningf.cusage
    burningf.ctile
    burningf.vroma

gen_cache.py non trasferisce automaticamente la ROM ZIP.
16. Stato attuale NeoGeo
Non è ancora stato verificato nel runtime components/gngeo/ dove viene cercato esattamente:
burningf.zip

È invece già verificato che le cache devono essere destinate a:
/sd/roms/neogeo/burningf/

Prossimo passo:
analizzare components/gngeo/ per individuare:
apertura della ROM ZIP
ricerca del file .drv
ricerca delle cache .ctile
ricerca del .cusage
ricerca del .vroma
eventuale ricerca di gngeo_data.zip
costruzione dei path delle ROM NeoGeo
Non assumere ancora che burningf.zip debba stare nella stessa directory delle cache finché il runtime non lo conferma.
17. Punto tecnico principale da ricordare
Il problema risolto oggi NON era:
emulatore che crasha

e NON era:
audio

Il problema era:
emulatore continua
        ↓
audio continua
        ↓
FPS continuano
        ↓
display SPI smette di aggiornarsi
        ↓
ultimo frame rimane sullo ST7789V

La modifica che ha risolto il comportamento osservato è stata:
numero buffer display:
8 → 1

Questa modifica deve essere considerata la correzione principale del problema display.
18. Prossimo test
Il prossimo test da effettuare è:
NeoGeo
    ↓
generazione cache
    ↓
installazione ROM + cache
    ↓
avvio gioco
    ↓
controllo aggiornamento display
    ↓
controllo audio
    ↓
controllo stabilità nel tempo

L'obiettivo è verificare se la correzione:
8 → 1 buffer

risolve anche il problema con un emulatore diverso dal NES.
Il test NeoGeo è quindi utile anche per distinguere definitivamente tra:
problema specifico del NES

e:
problema generale della pipeline display SPI
Riferimenti
Repository:
https://github.com/marchetto1983/RetroESP32-P4/tree/WT9932P4-TINY

Display modificato:
components/odroid/odroid_display.c

Runtime NES:
components/nofrendo/nofrendo_run.c

Runtime NeoGeo:
components/gngeo/

Script cache NeoGeo:
SDcard/roms/neogeo/gen_cache.py

Build rapido:
build_quick.ps1