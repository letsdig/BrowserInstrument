# Documentazione Tecnica & Guida Utente: BrowserInstrument VST3

**BrowserInstrument** è un plugin audio VST3 e Standalone open-source sviluppato con il framework JUCE. Integra un web browser moderno (WebKitGTK) direttamente all'interno della DAW (es. Bitwig Studio, Reaper, Ableton via wrapper, ecc.), consentendo di suonare e campionare qualsiasi web app musicale (come [YPC2000](https://ypc2000.fun/), AcidMachine, Roland 50 Studio, WebSynths, Strudel, ecc.) come se fosse uno strumento nativo di canale.

---

## 1. Architettura Audio & Risoluzione Latenza

### 1.1 Flusso Audio Nativo e Isolamento (Zero Leakage)
In ambiente Linux (PipeWire / PulseAudio), il plugin crea e gestisce un sink virtuale isolato:
- **Sink Nome**: `BrowserInstrumentSink` (modulo `module-null-sink`).
- All'avvio dell'istanza del plugin, la variabile d'ambiente `PULSE_SINK=BrowserInstrumentSink` forza il motore multimediale del browser (WebKitGTK / GStreamer) a inviare **tutto** l'audio (Web Audio API, elementi HTML5 `<audio>` e `<video>`, iframe YouTube) a questo sink virtuale.
- **Nessuna dispersione**: l'audio del browser non esce mai direttamente dagli altoparlanti di sistema o da ALSA, ma viene confinato internamente.

### 1.2 Cattura in Tempo Reale (PulseAudio Capture)
Il thread C++ in background `PulseAudioCaptureThread`:
1. Si connette direttamente a `BrowserInstrumentSink.monitor` tramite le API C di `libpulse-simple` con priorità thread elevata (`Thread::Priority::highest`).
2. Configura uno stream PCM a 32-bit float (`PA_SAMPLE_FLOAT32LE`) a 48 kHz (o al sample rate nativo della DAW).
3. Utilizza frammenti a bassissima latenza (`fragsize = 128 stereo frames`, ~2.6 ms) e `PULSE_LATENCY_MSEC=5`.
4. Riversa il flusso continuo nella ring buffer FIFO del bridge audio interno.

### 1.3 Zero-Latency Real-Time Enforcement
Per garantire una risposta istantanea sui pad e sulle note MIDI (inferiore a 10 ms totali):
- Se la DAW viene messa in pausa, il motore di cattura scarta automaticamente i campioni accumulati non appena la riproduzione riprende.
- In `readAudioFromBrowser`, il buffer viene dimensionato dinamicamente in base alla dimensione del blocco della DAW (`numSamples`):
  ```cpp
  const int targetBuf = juce::jmax(cushion, numSamples);
  ```
- Questo previene qualsiasi accumulo di campioni vecchi e garantisce che la DAW riproduca **sempre e solo l'audio in tempo reale immediato**, senza interruzioni o blocchi di re-buffering.

---

## 2. Architettura Web MIDI & Sincronizzazione DAW

### 2.1 Emulazione Web MIDI API
Il plugin inietta all'avvio della pagina (`document_start`) uno script JavaScript che:
1. Emula completamente le API standard `navigator.requestMIDIAccess()`.
2. Espone una porta di ingresso virtuale (`BrowserInstrument In`).
3. Converte i messaggi MIDI in arrivo dalla traccia DAW in standard `MIDIMessageEvent` per i listener delle web application.
4. Per strumenti tipo MPC / campionatori con trigger da tastiera (come YPC2000), invia contemporaneamente eventi `KeyboardEvent` (`keydown`/`keyup`) mappati sui pad `1..9`, `0`, `Q..P`, `A..L`, `Z..M`.

### 2.2 Sincronizzazione Automatica del Trasporto (Always-On Background Sync)
Senza appesantire la GUI con pulsanti manuali, il plugin sincronizza automaticamente il trasporto in background:
- **MIDI Start (`0xFA`)**: inviato non appena il tasto Play della DAW viene premuto.
- **MIDI Stop (`0xFC`)**: inviato quando la DAW si arresta.
- **BPM Sync**: aggiornato in tempo reale tramite `window.__JUCE_BRIDGE__.setBpm(bpm)` per agganciare i sequencer e i delay delle web app al tempo del progetto DAW.
- Supporto pulito per ambienti di live-coding come **Strudel** (`strudelMirror.evaluate()` e `stop()`).

### 2.3 Campionamento dalla DAW (DAW -> Web Sampler)
Il plugin emula `navigator.mediaDevices.getUserMedia({ audio: true })`:
- Quando un campionatore web attiva la funzione di registrazione/campionamento, riceve direttamente il segnale audio in ingresso proveniente dalla traccia della DAW, consentendo il resampling interno.

---

## 3. Requisiti di Sistema (Linux)

### 3.1 Dipendenze Software
- **Sistema Operativo**: Linux (Ubuntu 22.04+, Debian 12+, Fedora 38+, Arch Linux, ecc.)
- **Server Audio**: PipeWire (con pacchetto `pipewire-pulse`) oppure PulseAudio nativo
- **Compilatore**: GCC 11+ o Clang 14+ con supporto a C++20
- **Build System**: CMake 3.22+ e Ninja (consigliato) o Make
- **Librerie di Sviluppo**:
  ```bash
  sudo apt-get update
  sudo apt-get install -y \
      build-essential \
      cmake \
      ninja-build \
      pkg-config \
      libpulse-dev \
      pulseaudio-utils \
      libwebkit2gtk-4.1-dev \
      libgtk-3-dev \
      libasound2-dev \
      libfreetype-dev \
      libcurl4-openssl-dev
  ```

---

## 4. Istruzioni di Compilazione ed Installazione

### 4.1 Clonazione del Repository
```bash
git clone https://github.com/letsdig/BrowserInstrument.git
cd BrowserInstrument
```

### 4.2 Configurazione con CMake
```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
```

### 4.3 Compilazione
```bash
cmake --build build --config Release -j$(nproc)
```

I file generati si troveranno in:
- VST3: `build/BrowserInstrument_artefacts/Release/VST3/BrowserInstrument.vst3`
- Standalone: `build/BrowserInstrument_artefacts/Release/Standalone/BrowserInstrument`

### 4.4 Installazione del VST3 per la DAW
Copia il bundle `.vst3` nella cartella VST3 utente:
```bash
mkdir -p ~/.vst3
cp -r build/BrowserInstrument_artefacts/Release/VST3/BrowserInstrument.vst3 ~/.vst3/
```

In Bitwig Studio o in altra DAW, effettua una nuova scansione dei plugin (*Rescan VSTs*) e inserisci **BrowserInstrument** su una traccia Instrument.

---

## 5. Utilizzo del Plugin

1. **Navigazione Web**:
   - Inserisci qualsiasi URL nella barra in alto (es. `https://ypc2000.fun/`) e premi `Invio` o il tasto **GO**.
   - I pulsanti di navigazione `◀`, `▶`, `⟳` e `⌂` permettono di spostarsi nella cronologia e ricaricare la pagina.
2. **Buffer Cushion Selector (Top Right)**:
   - Permette di selezionare il margine di buffer (`64 smp ~1.3ms`, `128 smp ~2.6ms`, `256 smp ~5.3ms`, `512 smp ~10ms`).
   - Se la scheda audio del sistema ha un buffer molto piccolo (es. 128 o 256 campioni), seleziona `FAST (256 smp)` o `SAFE (512 smp)` per un'ottima stabilità e latenza impercettibile.
3. **Telemetry & Indicatori (Barra Inferiore)**:
   - **LED MIDI IN**: lampeggia in azzurro alla ricezione di note/CC dalla DAW.
   - **LED MIDI OUT**: lampeggia in arancione se la web app invia MIDI alla DAW.
   - **Peak Meter & dB**: misuratore RMS/Peak del segnale audio uscente verso il mixer.
   - **Tasto Flush**: svuota e resetta all'istante la ring buffer audio in caso di cambio frequenza di campionamento.
   - **Output Gain**: fader rotativo per regolare il livello di uscita (+0 dB default).

---

## 6. Risoluzione dei Problemi (Troubleshooting)

| Problema | Causa Possibile | Soluzione |
| :--- | :--- | :--- |
| **Nessun suono / Silenzio** | Buffer cushion impostato a un valore inferiore al block size della DAW | Nel menu a tendina in alto a destra, imposta il buffer su `256 smp` o `512 smp`. |
| **Audio intermittente / Scatti** | CPU sovraccarica o buffer audio della scheda sonora troppo compresso | Aumenta leggermente il buffer della DAW o imposta il cushion su `512 smp [SAFE]`. |
| **Pagine web con crash GPU** | Driver video incompatibili con WebKitGTK DMABUF | Il plugin disabilita già automaticamente DMABUF e compositing mode (`WEBKIT_DISABLE_DMABUF_RENDERER=1`) all'avvio. |
| **Sink non trovato** | PipeWire non ha caricato `BrowserInstrumentSink` | Il plugin esegue `pactl load-module module-null-sink` automaticamente; in alternativa lancia `pactl load-module module-null-sink sink_name=BrowserInstrumentSink`. |

---

## 7. Licenza
Distribuito sotto licenza **MIT**.
