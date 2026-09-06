# BrowserInstrument VST3

**BrowserInstrument** is a VST3 & Standalone audio plugin built with JUCE that embeds a web browser and turns any Web Audio & Web MIDI application (such as [YPC2000](https://ypc2000.fun/), AcidMachine, WebSynths, Roland 50 Studio, etc.) into a channel instrument inside your DAW (e.g. Bitwig Studio).

## Key Features

- **Bidirectional Web Audio Engine**:
  - **Audio Out (Browser -> DAW)**: Intercepts `window.AudioContext` and streams 32-bit float PCM audio buffers directly into JUCE's `processBlock` via an ultra-low latency local WebSocket bridge (`ws://127.0.0.1:8766`).
  - **Audio In (DAW -> Web Sampler)**: Intercepts `navigator.mediaDevices.getUserMedia({ audio: true })`. When samplers like YPC2000 record audio, they capture the DAW channel's incoming audio directly.
- **Bidirectional Web MIDI**:
  - **DAW -> Browser**: DAW MIDI notes and CC messages are converted to Web MIDI events dispatched to the web instrument.
  - **Browser -> DAW**: On-screen sequencers or pads sending Web MIDI are routed out to the DAW track to record into MIDI clips.
- **Dual Mode**:
  - **Embedded WebKitGTK**: Full browser directly inside the plugin editor window with Chromium User-Agent emulation (`Chrome/128.0.0.0`).
  - **External Browser / Tab Bridge**: Launch Google Chrome in borderless App Mode (`--app=...`), or use the 1-click Bookmarklet from `http://127.0.0.1:8766/` to hook any browser tab into your DAW track.
- **Offline Download & Localhost Server**:
  - Download web instruments locally into `~/.local/share/BrowserInstrument/offline/`.
  - Built-in HTTP server serves offline instruments from localhost at zero latency without requiring an internet connection.

## Requirements (Linux)

- CMake 3.22+
- Ninja or Make
- GCC 12+ or Clang 15+ (C++20)
- `pkg-config`, `libwebkit2gtk-4.1-dev`, `libgtk-3-dev`, `libasound2-dev`, `libfreetype-dev`
- JUCE 8/9 (automatically discovered if placed in `~/Scrivania/JUCE` or fetched via CMake)

## Build Instructions

```bash
git clone https://github.com/letsdig/BrowserInstrument.git
cd BrowserInstrument

cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release

# Copy VST3 bundle to user VST3 directory
mkdir -p ~/.vst3
cp -r build/BrowserInstrument_artefacts/Release/VST3/BrowserInstrument.vst3 ~/.vst3/
```

## License

MIT License.
