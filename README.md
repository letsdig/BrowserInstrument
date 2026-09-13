# BrowserInstrument VST3

**BrowserInstrument** is an open-source VST3 & Standalone audio plugin built with JUCE that embeds a modern web browser (WebKitGTK) directly into your DAW (e.g. Bitwig Studio, Reaper, Ableton Live via wrapper) and turns any Web Audio & Web MIDI instrument (such as [YPC2000](https://ypc2000.fun/), AcidMachine, WebSynths, Roland 50 Studio, Strudel, etc.) into a studio-grade channel instrument.

For in-depth technical details, architecture diagrams, and configuration, see [documentation.md](documentation.md).

---

## Key Features

- **Direct Native Low-Latency Audio Pipeline**:
  - Direct 32-bit float PCM audio capture from WebKitGTK / GStreamer into JUCE's `processBlock`.
  - Isolated PulseAudio/PipeWire null sink (`BrowserInstrumentSink`): zero leakage to external desktop speakers.
  - Real-time backlog enforcement eliminates buffer accumulation for imperceptible (< 10ms) round-trip latency.
- **Full Web MIDI & Keyboard Integration**:
  - **DAW -> Browser**: DAW MIDI notes and CC messages are dispatched to Web MIDI `navigator.requestMIDIAccess()` and virtual keyboard events (`1..9`, `A..L` for MPC samplers).
  - **Browser -> DAW**: On-screen sequencers or virtual pads output MIDI back into DAW MIDI clips.
- **Universal DAW Transport & Tempo Sync**:
  - Background, automatic synchronization: sends standard MIDI Realtime **Start (`0xFA`)** and **Stop (`0xFC`)** as well as live project BPM updates to web sequencers without intrusive GUI buttons.
- **DAW Audio Resampling (Audio In)**:
  - Supports `navigator.mediaDevices.getUserMedia`: route DAW channel audio into web samplers (e.g. YPC2000 sampling inputs) in real-time.
- **Embedded Chromium / Modern Web Engine**:
  - WebKitGTK with modern User-Agent emulation (`Chrome/128.0.0.0`), Web Audio API, Web MIDI API, and YouTube / media iframe audio support.

---

## Requirements (Linux)

- **OS**: Linux (Ubuntu 22.04+, Debian 12+, Fedora 38+, Arch Linux, etc.)
- **Audio Server**: PipeWire (with `pipewire-pulse`) or PulseAudio
- **Compiler**: GCC 11+ or Clang 14+ (C++20)
- **Build System**: CMake 3.22+ and Ninja (recommended) or Make
- **Libraries**:
  ```bash
  sudo apt-get update
  sudo apt-get install -y \
      build-essential cmake ninja-build pkg-config \
      libpulse-dev pulseaudio-utils libwebkit2gtk-4.1-dev \
      libgtk-3-dev libasound2-dev libfreetype-dev libcurl4-openssl-dev
  ```

---

## Build & Install Instructions

```bash
# 1. Clone repository
git clone https://github.com/letsdig/BrowserInstrument.git
cd BrowserInstrument

# 2. Configure build with Ninja
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release

# 3. Compile in Release mode
cmake --build build --config Release -j$(nproc)

# 4. Copy VST3 bundle to user VST3 directory
mkdir -p ~/.vst3
cp -r build/BrowserInstrument_artefacts/Release/VST3/BrowserInstrument.vst3 ~/.vst3/
```

After copying, rescan your VST plugins inside your DAW (e.g. Bitwig Studio) and insert **BrowserInstrument** on an Instrument track.

---

## Documentation

Full Italian and English documentation is available in [documentation.md](documentation.md).

---

## License

MIT License.
