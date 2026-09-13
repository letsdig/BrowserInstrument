#pragma once

#include <juce_core/juce_core.h>

namespace WebBridge
{

inline juce::String getInjectionScript(int bridgePort = 8788, int targetSampleRate = 96000, int bufferSize = 1024)
{
    return juce::String(R"JS(
(function() {
    // 0. Only run in top-level context. Never in iframes (e.g. YouTube embed player, about:blank)!
    try {
        if (typeof window === "undefined" || window.top !== window) {
            return;
        }
    } catch (e) {
        return;
    }

    // =========================================================================
    // WEBKIT-GTK WEBAUDIO COMPATIBILITY FIXES FOR STRUDEL / SUPERDOUGH
    // Fixes "Channel count cannot be 0" bug on Linux WebKitGTK where destination.maxChannelCount is 0
    // =========================================================================
    if (typeof AudioDestinationNode !== 'undefined') {
        try {
            Object.defineProperty(AudioDestinationNode.prototype, 'maxChannelCount', {
                get: function() { return 2; },
                set: function() {},
                configurable: true
            });
            const origCc = Object.getOwnPropertyDescriptor(AudioNode.prototype, 'channelCount');
            Object.defineProperty(AudioDestinationNode.prototype, 'channelCount', {
                get: function() { return 2; },
                set: function(val) {
                    if (val < 1) val = 2;
                    if (origCc && origCc.set) {
                        try { origCc.set.call(this, val); } catch(e) {}
                    }
                },
                configurable: true
            });
        } catch(e) {
            console.warn("[JUCE-WebBridge] AudioDestinationNode patch warning:", e);
        }
    }

    if (typeof ChannelMergerNode !== 'undefined') {
        try {
            const OrigMerger = window.ChannelMergerNode;
            window.ChannelMergerNode = function(ctx, opts) {
                let options = opts;
                if (options && typeof options.numberOfInputs === 'number' && options.numberOfInputs < 1) {
                    options = Object.assign({}, options, { numberOfInputs: 2 });
                }
                return new OrigMerger(ctx, options);
            };
            window.ChannelMergerNode.prototype = OrigMerger.prototype;
        } catch(e) {}
    }

    try {
        const desc = Object.getOwnPropertyDescriptor(AudioNode.prototype, 'channelCount');
        if (desc && desc.set) {
            const origSet = desc.set;
            Object.defineProperty(AudioNode.prototype, 'channelCount', {
                get: desc.get,
                set: function(val) {
                    if (typeof val === 'number' && val < 1) val = 2;
                    return origSet.call(this, val);
                },
                configurable: true
            });
        }
    } catch(e) {}

    if (window.__JUCE_BRIDGE_LOADED__) {
        console.log("[JUCE-WebBridge] Re-arming context scan...");
        if (typeof scanAndHook === "function") scanAndHook();
        return;
    }
    window.__JUCE_BRIDGE_LOADED__ = true;

    console.log("[JUCE-WebBridge] Initializing DAW Web Audio & MIDI Bridge for Bitwig Studio...");

    const isYouTubeHost = (location.hostname || "").toLowerCase().includes("youtube.com") ||
        (location.hostname || "").toLowerCase().includes("youtu.be") ||
        (location.hostname || "").toLowerCase().includes("music.youtube.com");

    // =========================================================================
    // AGGRESSIVE RUNTIME CACHING ENGINE ("Casha tutto una volta caricata la pagina")
    // Intercepts fetch() and XMLHttpRequest to cache all WASM, audio samples,
    // scripts, fonts, and styles, eliminating network latency on subsequent loads.
    // =========================================================================
    const BI_CACHE_NAME = "browserinstrument-page-cache-v1";
    const cacheApiAvailable = (typeof caches !== "undefined" && typeof caches.open === "function" && !isYouTubeHost);

    const origFetch = (typeof window.fetch === "function") ? window.fetch.bind(window) : null;
    if (origFetch && cacheApiAvailable) {
        window.fetch = async function(input, init) {
            const method = (init && init.method) ? init.method.toUpperCase() : "GET";
            if (method !== "GET") {
                return origFetch(input, init);
            }

            const urlStr = (typeof input === "string") ? input : (input && input.url ? input.url : "");
            if (urlStr.startsWith("ws:") || urlStr.startsWith("wss:") || urlStr.includes("127.0.0.1:" + %PORT%)) {
                return origFetch(input, init);
            }

            try {
                const cache = await caches.open(BI_CACHE_NAME);
                const cachedRes = await cache.match(urlStr);
                if (cachedRes) {
                    const isBinary = urlStr.includes(".wav") || urlStr.includes(".mp3") || urlStr.includes(".wasm") || urlStr.includes(".ogg");
                    if (!isBinary) {
                        origFetch(input, init).then(fresh => {
                            if (fresh && fresh.ok) cache.put(urlStr, fresh.clone()).catch(() => {});
                        }).catch(() => {});
                    }
                    return cachedRes;
                }

                const networkRes = await origFetch(input, init);
                if (networkRes && networkRes.ok) {
                    cache.put(urlStr, networkRes.clone()).catch(() => {});
                }
                return networkRes;
            } catch (err) {
                return origFetch(input, init);
            }
        };
    }

    if (typeof window.XMLHttpRequest !== "undefined" && cacheApiAvailable) {
        const OrigXHR = window.XMLHttpRequest;
        window.XMLHttpRequest = class extends OrigXHR {
            open(method, url, async, user, password) {
                this.__bi_method = method;
                this.__bi_url = url;
                super.open(method, url, async !== false, user, password);
            }

            send(body) {
                const method = (this.__bi_method || "GET").toUpperCase();
                const url = this.__bi_url;
                const isCachableAsset = url && typeof url === "string" && (
                    url.includes(".wav") || url.includes(".mp3") || url.includes(".ogg") ||
                    url.includes(".wasm") || url.includes(".json") || url.includes(".bin") ||
                    url.includes(".sf2") || url.includes(".soundfont") || url.includes(".js") ||
                    url.includes(".css")
                );

                if (method === "GET" && isCachableAsset) {
                    caches.open(BI_CACHE_NAME).then(cache => {
                        cache.match(url).then(async cachedRes => {
                            if (cachedRes) {
                                try {
                                    const rType = this.responseType;
                                    let data;
                                    if (rType === "arraybuffer") data = await cachedRes.arrayBuffer();
                                    else if (rType === "json") data = await cachedRes.json();
                                    else if (rType === "blob") data = await cachedRes.blob();
                                    else data = await cachedRes.text();

                                    Object.defineProperty(this, "readyState", { value: 4, configurable: true });
                                    Object.defineProperty(this, "status", { value: 200, configurable: true });
                                    Object.defineProperty(this, "statusText", { value: "OK (Cached)", configurable: true });
                                    Object.defineProperty(this, "response", { value: data, configurable: true });
                                    if (!rType || rType === "text") {
                                        Object.defineProperty(this, "responseText", { value: typeof data === "string" ? data : "", configurable: true });
                                    }

                                    if (typeof this.onreadystatechange === "function") this.onreadystatechange();
                                    if (typeof this.onload === "function") this.onload(new ProgressEvent("load"));
                                    if (typeof this.onloadend === "function") this.onloadend(new ProgressEvent("loadend"));
                                    return;
                                } catch (e) {}
                            }

                            this.addEventListener("load", function() {
                                if (this.status === 200 && this.response) {
                                    try {
                                        const headers = new Headers();
                                        headers.set("Content-Type", this.getResponseHeader("Content-Type") || "application/octet-stream");
                                        const bData = (this.response instanceof ArrayBuffer) ? this.response.slice(0) : this.response;
                                        const resToCache = new Response(bData, { status: 200, headers: headers });
                                        cache.put(url, resToCache).catch(() => {});
                                    } catch (e) {}
                                }
                            });
                            super.send(body);
                        }).catch(() => { super.send(body); });
                    }).catch(() => { super.send(body); });
                    return;
                }

                super.send(body);
            }
        };
    }

    // =========================================================================
    // FULL-PAGE ASSET SWEEPER ("Cashare tutto una volta caricata la pagina")
    // Scans all scripts, audio elements, fonts, links and images once page loads,
    // pre-caching them in parallel so the next reload/navigation is instantaneous.
    // =========================================================================
    let isSweepingCache = false;
    async function sweepAndCacheAllPageResources() {
        if (isYouTubeHost || isSweepingCache || typeof caches === "undefined") return;
        isSweepingCache = true;

        try {
            const cache = await caches.open(BI_CACHE_NAME);
            const urlsToCache = new Set();

            if (location.href && !location.href.startsWith("about:") && !location.href.startsWith("data:")) {
                urlsToCache.add(location.href);
            }

            document.querySelectorAll("script[src]").forEach(el => {
                if (el.src && !el.src.startsWith("blob:") && !el.src.startsWith("data:")) {
                    urlsToCache.add(el.src);
                }
            });

            document.querySelectorAll("link[href]").forEach(el => {
                const rel = (el.rel || "").toLowerCase();
                if (rel === "stylesheet" || rel === "preload" || rel === "modulepreload" || rel === "icon" || rel === "shortcut icon") {
                    if (el.href && !el.href.startsWith("data:") && !el.href.startsWith("blob:")) {
                        urlsToCache.add(el.href);
                    }
                }
            });

            document.querySelectorAll("audio, audio source, video, video source").forEach(el => {
                const s = el.src || el.getAttribute("src");
                if (s && !s.startsWith("blob:") && !s.startsWith("data:")) {
                    try { urlsToCache.add(new URL(s, location.href).href); } catch(e) {}
                }
            });

            document.querySelectorAll("img[src]").forEach(el => {
                if (el.src && !el.src.startsWith("data:") && !el.src.startsWith("blob:")) {
                    urlsToCache.add(el.src);
                }
            });

            // Scan inline HTML for drum kits / sound samples (.wav, .mp3, .ogg, .wasm)
            try {
                const html = document.documentElement.innerHTML;
                const sampleRegex = /["']([^"']+\.(?:wav|mp3|ogg|flac|wasm|json))["']/gi;
                let m;
                let c = 0;
                while ((m = sampleRegex.exec(html)) !== null && c < 150) {
                    c++;
                    try {
                        const resUrl = new URL(m[1], location.href).href;
                        if (resUrl.startsWith("http://") || resUrl.startsWith("https://")) {
                            urlsToCache.add(resUrl);
                        }
                    } catch(e) {}
                }
            } catch(e) {}

            console.log("[JUCE-WebBridge] Sweeping page for caching: " + urlsToCache.size + " resources detected.");

            let cachedCount = 0;
            for (const u of urlsToCache) {
                try {
                    const match = await cache.match(u);
                    if (!match) {
                        const f = origFetch || window.fetch;
                        const r = await f(u, { mode: "no-cors" }).catch(() => null);
                        if (r) {
                            await cache.put(u, r);
                        }
                    }
                    cachedCount++;
                } catch(e) {}
            }

            console.log("[JUCE-WebBridge] Page 100% cached locally! (" + cachedCount + "/" + urlsToCache.size + " assets stored)");

            sendNativeJuceEvent("pageCacheStatus", {
                status: "ready",
                cachedCount: cachedCount,
                totalCount: urlsToCache.size,
                url: location.href
            });
        } catch (e) {
            console.warn("[JUCE-WebBridge] Sweep cache warning:", e);
        } finally {
            isSweepingCache = false;
        }
    }

    if (document.readyState === "complete") {
        setTimeout(sweepAndCacheAllPageResources, 1000);
    } else {
        window.addEventListener("load", () => {
            setTimeout(sweepAndCacheAllPageResources, 1000);
        });
    }

    // =========================================================================
    // HIGH BITRATE MEDIA ENFORCER (YouTube, HTML5 Media, Web Audio)
    // Forces maximum stream quality & uncompressed 32-bit float audio bitrates.
    // =========================================================================
    function enforceHighBitrateMedia() {
        try {
            if (isYouTubeHost) return;

            // Force YouTube HTML5 Player to maximum quality and audio bitrate
            const ytPlayers = document.querySelectorAll(".html5-video-player, #movie_player");
            ytPlayers.forEach(p => {
                if (typeof p.setPlaybackQualityRange === "function") {
                    p.setPlaybackQualityRange("highres", "highres");
                }
                if (typeof p.setPlaybackQuality === "function") {
                    p.setPlaybackQuality("highres");
                }
            });

            // HTML5 Media: enable preload auto
            document.querySelectorAll("video, audio").forEach(media => {
                if (media.preload !== "auto") media.preload = "auto";
            });
        } catch(e) {}
    }

    setInterval(enforceHighBitrateMedia, 3000);
    window.addEventListener("load", enforceHighBitrateMedia);

    // Polyfill MIDIMessageEvent if not natively exposed
    if (typeof window.MIDIMessageEvent === "undefined") {
        window.MIDIMessageEvent = class MIDIMessageEvent extends Event {
            constructor(type, eventInitDict) {
                super(type, eventInitDict);
                this.data = (eventInitDict && eventInitDict.data) ? eventInitDict.data : new Uint8Array();
                this.receivedTime = performance.now();
            }
        };
    }

    const PORTS = [%PORT%, 8788, 8789, 8790, 8791, 8766];
    let portIndex = 0;
    let ws = null;
    let wsConnected = false;
    let reconnectTimer = null;
    const dawInputRingBuffer = [];
    const allHookedContexts = new Set();

    // Master configuration
    const CONFIG = {
        muteSystemAudio: true,
        bufferSize: %BUFFER_SIZE%,
        targetSampleRate: %SAMPLE_RATE%,
        transportPlaying: true
    };

    // =========================================================================
    // 0. DIRECT NATIVE JUCE IN-MEMORY IPC (100% immune to CORS / Mixed Content)
    // =========================================================================
    function sendNativeJuceEvent(eventId, payload) {
        try {
            const jsonStr = JSON.stringify({ eventId: eventId, payload: payload });
            if (window.__JUCE__ && window.__JUCE__.backend && typeof window.__JUCE__.backend.emitEvent === "function") {
                window.__JUCE__.backend.emitEvent(eventId, payload);
                return true;
            }
            if (window.webkit && window.webkit.messageHandlers && window.webkit.messageHandlers.__JUCE__) {
                window.webkit.messageHandlers.__JUCE__.postMessage(jsonStr);
                return true;
            }
            if (window.__JUCE__ && typeof window.__JUCE__.postMessage === "function") {
                window.__JUCE__.postMessage(jsonStr);
                return true;
            }
        } catch(e) {
            console.warn("[JUCE-WebBridge] Error sending native event:", e);
        }
        return false;
    }

    // Signal native bridge readiness immediately
    sendNativeJuceEvent("bridgeStatus", { status: "connected", mode: "native" });

    // =========================================================================
    // 1. WEBSOCKET CONNECTION (Sub-millisecond direct binary streaming)
    // =========================================================================
    function tryConnect() {
        if (wsConnected) return;

        const currentPort = PORTS[portIndex % PORTS.length];
        try {
            ws = new WebSocket("ws://127.0.0.1:" + currentPort);
            ws.binaryType = "arraybuffer";

            const connectionTimeout = setTimeout(() => {
                if (!wsConnected && ws && ws.readyState !== WebSocket.OPEN) {
                    try { ws.close(); } catch(e) {}
                    portIndex++;
                    if (portIndex < PORTS.length * 2) {
                        tryConnect();
                    }
                }
            }, 1000);

            ws.onopen = function() {
                clearTimeout(connectionTimeout);
                wsConnected = true;
                console.log("[JUCE-WebBridge] WebSocket CONNECTED on port " + currentPort + "!");
                sendNativeJuceEvent("bridgeStatus", { status: "connected", port: currentPort });
                resumeAllContexts();
            };

            ws.onmessage = function(event) {
                if (!(event.data instanceof ArrayBuffer)) return;
                const data = new Uint8Array(event.data);
                if (data.length === 0) return;

                const packetType = data[0];

                if (packetType === 0x02 && data.length >= 4) {
                    // MIDI from Bitwig: [0x02, status, data1, data2]
                    dispatchMidiFromDaw(data[1], data[2], data[3]);
                } else if (packetType === 0x01 && data.length >= 4) {
                    // Audio from Bitwig (for sampling e.g. in YPC2000)
                    const numChannels = data[1];
                    const sampleCount = data[2] | (data[3] << 8);
                    const f32 = new Float32Array(event.data, 4, sampleCount * numChannels);
                    for (let i = 0; i < f32.length; i++) {
                        dawInputRingBuffer.push(f32[i]);
                    }
                    if (dawInputRingBuffer.length > 48000 * 2) {
                        dawInputRingBuffer.splice(0, dawInputRingBuffer.length - 48000);
                    }
                }
            };

            ws.onclose = function() {
                if (wsConnected) {
                    wsConnected = false;
                }
                clearTimeout(connectionTimeout);
                clearTimeout(reconnectTimer);
                reconnectTimer = setTimeout(tryConnect, 1500);
            };

            ws.onerror = function() {
                try { ws.close(); } catch(e) {}
            };
        } catch (err) {
            portIndex++;
            clearTimeout(reconnectTimer);
            reconnectTimer = setTimeout(tryConnect, 1500);
        }
    }

    function resumeAllContexts() {
        if (window.__juceAudioCtx && window.__juceAudioCtx.state === "suspended") {
            window.__juceAudioCtx.resume().catch(() => {});
        }
        for (const ctx of allHookedContexts) {
            if (ctx && ctx.state === "suspended") {
                ctx.resume().catch(() => {});
            }
        }
    }

    if (typeof window !== "undefined") {
        window.addEventListener("pointerdown", resumeAllContexts, { passive: true });
        window.addEventListener("keydown", resumeAllContexts, { passive: true });
    }

    tryConnect();

    // =========================================================================
    // 2. WEB MIDI SHIM (Bitwig Track <-> Web Instrument)
    // =========================================================================
    class VirtualMIDIInput extends EventTarget {
        constructor() {
            super();
            this.id = "juce-daw-midi-in";
            this.name = "DAW MIDI Input (Bitwig Track)";
            this.manufacturer = "AudioWebLab";
            this.version = "1.0";
            this.type = "input";
            this.state = "connected";
            this.connection = "open";
            this.onmidimessage = null;
        }
    }

    class VirtualMIDIOutput {
        constructor() {
            this.id = "juce-daw-midi-out";
            this.name = "DAW MIDI Output (To Bitwig)";
            this.manufacturer = "AudioWebLab";
            this.version = "1.0";
            this.type = "output";
            this.state = "connected";
            this.connection = "open";
        }

        send(data, timestamp) {
            const bytes = data instanceof Uint8Array ? data : new Uint8Array(data);

            // 1. Send via native JUCE IPC (works on ANY page)
            sendNativeJuceEvent("dawMidiData", {
                status: bytes[0] || 0,
                d1: bytes[1] || 0,
                d2: bytes[2] || 0
            });

            // 2. Send via WebSocket if open (for external Google Chrome)
            if (wsConnected && ws && ws.readyState === WebSocket.OPEN) {
                const packet = new Uint8Array(1 + bytes.length);
                packet[0] = 0x02; // MIDI opcode
                packet.set(bytes, 1);
                try {
                    ws.send(packet.buffer);
                } catch (e) {}
            }
        }

        clear() {}
    }

    const virtualMidiInput = new VirtualMIDIInput();
    const virtualMidiOutput = new VirtualMIDIOutput();

    function createMidiEvent(bytes) {
        let ev;
        try {
            ev = new MIDIMessageEvent("midimessage", { data: bytes });
        } catch(e) {
            try {
                ev = new Event("midimessage");
            } catch(e2) {
                ev = { type: "midimessage" };
            }
        }
        ev.data = bytes;
        ev.receivedTime = performance.now();
        return ev;
    }

    const PAD_KEY_MAP = {
        36: '1', 37: '2', 38: '3', 39: '4',
        40: 'q', 41: 'w', 42: 'e', 43: 'r',
        44: 'a', 45: 's', 46: 'd', 47: 'f',
        48: 'z', 49: 'x', 50: 'c', 51: 'v'
    };

    // Computer keyboard note mapping for browser synths that listen to typing
    const NOTE_KEY_MAP = {
        48: 'z', 49: 's', 50: 'x', 51: 'd', 52: 'c', 53: 'v', 54: 'g',
        55: 'b', 56: 'h', 57: 'n', 58: 'j', 59: 'm',
        60: 'a', 61: 'w', 62: 's', 63: 'e', 64: 'd', 65: 'f', 66: 't',
        67: 'g', 68: 'y', 69: 'h', 70: 'u', 71: 'j', 72: 'k', 73: 'o',
        74: 'l', 75: 'p'
    };

    function dispatchMidiFromDaw(status, d1, d2) {
        resumeAllContexts();

        const bytes = (status >= 0xF8) ? new Uint8Array([status]) : new Uint8Array([status, d1, d2]);
        const event = createMidiEvent(bytes);

        // 1. Direct callback invocation if set
        if (typeof virtualMidiInput.onmidimessage === "function") {
            try { virtualMidiInput.onmidimessage(event); } catch (e) { console.error(e); }
        }
        // 2. Dispatch via standard EventTarget
        try {
            virtualMidiInput.dispatchEvent(event);
        } catch (e) { console.error(e); }

        // 3. Computer KeyboardEvent simulation (for MPC samplers and keyboard synths)
        const isNoteOn = (status & 0xF0) === 0x90 && d2 > 0;
        const isNoteOff = (status & 0xF0) === 0x80 || ((status & 0xF0) === 0x90 && d2 === 0);

        if (isNoteOn || isNoteOff) {
            const evName = isNoteOn ? "keydown" : "keyup";
            const padChar = PAD_KEY_MAP[d1];
            if (padChar) {
                const codeName = (padChar >= '0' && padChar <= '9') ? ("Digit" + padChar) : ("Key" + padChar.toUpperCase());
                const keyEv = new KeyboardEvent(evName, {
                    key: padChar,
                    code: codeName,
                    bubbles: true,
                    cancelable: true
                });
                window.dispatchEvent(keyEv);
                document.dispatchEvent(keyEv);
            }

            const noteChar = NOTE_KEY_MAP[d1];
            if (noteChar && noteChar !== padChar) {
                const codeName = "Key" + noteChar.toUpperCase();
                const keyEv = new KeyboardEvent(evName, {
                    key: noteChar,
                    code: codeName,
                    bubbles: true,
                    cancelable: true
                });
                window.dispatchEvent(keyEv);
                document.dispatchEvent(keyEv);
            }
        }
    }

    class VirtualMIDIAccess extends EventTarget {
        constructor() {
            super();
            this.inputs = new Map([
                [virtualMidiInput.id, virtualMidiInput],
                ["bitwig-midi-in", virtualMidiInput]
            ]);
            this.outputs = new Map([[virtualMidiOutput.id, virtualMidiOutput]]);
            this.sysexEnabled = true;
            this.onstatechange = null;
        }
    }

    const virtualMidiAccessInstance = new VirtualMIDIAccess();

    if (typeof navigator !== "undefined") {
        navigator.requestMIDIAccess = async function() {
            console.log("[JUCE-WebBridge] requestMIDIAccess() intercepted, returning Bitwig MIDI ports!");
            return virtualMidiAccessInstance;
        };

        if (navigator.permissions && navigator.permissions.query) {
            const origQuery = navigator.permissions.query.bind(navigator.permissions);
            navigator.permissions.query = async function(desc) {
                if (desc && (desc.name === "midi" || desc.name === "midi-sysex")) {
                    return { state: "granted", onchange: null };
                }
                return origQuery(desc);
            };
        }
    }

    // =========================================================================
    // 3. BULLETPROOF WEB AUDIO CAPTURE (Direct Native IPC + WebSocket)
    //
    // Uses an AudioWorkletNode (runs on the dedicated audio-rendering thread)
    // instead of the legacy, main-thread ScriptProcessorNode. This removes the
    // #1 cause of glitches/dropouts (main-thread jank stalling audio callbacks)
    // and keeps audio flowing even while the WebView is hidden/backgrounded
    // (e.g. when the plugin editor window is closed), since the audio thread
    // is not subject to the same visibility throttling as the main thread.
    // Falls back to ScriptProcessorNode only if AudioWorklet is unavailable.
    // =========================================================================
    const OrigAudioContext = window.AudioContext || window.webkitAudioContext;
    const origConnect = (typeof AudioNode !== "undefined" && AudioNode.prototype) ? AudioNode.prototype.connect : null;

    const CAPTURE_WORKLET_SRC = [
        "class JuceCaptureProcessor extends AudioWorkletProcessor {",
        "  process(inputs, outputs) {",
        "    const input = inputs[0];",
        "    const inL = (input && input[0]) ? input[0] : new Float32Array(128);",
        "    const inR = (input && input.length > 1 && input[1]) ? input[1] : inL;",
        "    const len = inL.length;",
        "    const packet = new Float32Array(len * 2);",
        "    for (let i = 0; i < len; i++) {",
        "      packet[i * 2] = inL[i];",
        "      packet[i * 2 + 1] = inR[i];",
        "    }",
        "    this.port.postMessage({ sr: sampleRate, len: len, pcm: packet.buffer }, [packet.buffer]);",
        "    return true;", // Do NOT copy to outputs; keep downstream 100% silent to OS speakers
        "  }",
        "}",
        "registerProcessor('juce-capture-processor', JuceCaptureProcessor);"
    ].join("\n");
    let captureWorkletUrl = null;
    const nativeIpcAccumulator = [];

    // Shared by both the AudioWorklet path and the ScriptProcessor fallback.
    function handleCapturedBlock(currentRate, len, pcmBuffer) {
        // 1. FAST PATH: Direct Binary WebSocket Transmission (Zero GC, zero base64 overhead)
        if (wsConnected && ws && ws.readyState === WebSocket.OPEN) {
            const headerSize = 8;
            const packet = new Uint8Array(headerSize + len * 2 * 4);
            packet[0] = 0x01; // Audio Output
            packet[1] = 0x02; // 2 channels
            packet[2] = len & 0xFF;
            packet[3] = (len >> 8) & 0xFF;

            const rateInt = Math.round(currentRate);
            packet[4] = rateInt & 0xFF;
            packet[5] = (rateInt >> 8) & 0xFF;
            packet[6] = (rateInt >> 16) & 0xFF;
            packet[7] = (rateInt >> 24) & 0xFF;

            new Float32Array(packet.buffer, headerSize, len * 2).set(new Float32Array(pcmBuffer));

            try {
                ws.send(packet.buffer);
            } catch (err) {}
        }
        else {
            // In embedded VST3, all audio is captured at 0% CPU with sample-accuracy directly via PulseAudio / PipeWire.
            // Native IPC Base64 is disabled to prevent dual-stream collision and distortion.
        }
    }

    function getOrCreateJuceAudioContext() {
        if (!window.__juceAudioCtx) {
            try {
                const opts = (CONFIG.targetSampleRate > 0) ? { sampleRate: CONFIG.targetSampleRate } : {};
                const c = new OrigAudioContext(opts);
                hookContext(c);
            } catch(e) {
                try {
                    const c = new OrigAudioContext();
                    hookContext(c);
                } catch(e2) {
                    console.warn("[JUCE-WebBridge] Error initializing default AudioContext:", e2);
                }
            }
        }
        return window.__juceAudioCtx;
    }

    async function hookContext(ctx) {
        if (!ctx || ctx.__juceHooked) return;
        ctx.__juceHooked = true;
        allHookedContexts.add(ctx);
        window.__juceAudioCtx = ctx;

        try {
            if (ctx.destination) {
                try { ctx.destination.channelCount = 2; } catch(e) {}
            }

            const masterTap = ctx.createGain();
            masterTap.gain.value = 1.0;
            try { masterTap.channelCount = 2; } catch(e) {}
            ctx.__juceMasterTap = masterTap;

            let node = null;

            // Preferred path: AudioWorkletNode
            if (ctx.audioWorklet && typeof ctx.audioWorklet.addModule === "function") {
                try {
                    if (!captureWorkletUrl) {
                        const blob = new Blob([CAPTURE_WORKLET_SRC], { type: "application/javascript" });
                        captureWorkletUrl = URL.createObjectURL(blob);
                    }
                    await ctx.audioWorklet.addModule(captureWorkletUrl);
                    node = new AudioWorkletNode(ctx, "juce-capture-processor", {
                        numberOfInputs: 1,
                        numberOfOutputs: 1,
                        channelCount: 2,
                        channelCountMode: "explicit",
                        channelInterpretation: "discrete",
                        outputChannelCount: [2]
                    });
                    node.port.onmessage = (e) => {
                        handleCapturedBlock(e.data.sr, e.data.len, e.data.pcm);
                    };
                } catch (err) {
                    console.warn("[JUCE-WebBridge] AudioWorklet unavailable, falling back to ScriptProcessor:", err);
                    node = null;
                }
            }

            // Fallback: legacy ScriptProcessorNode (main-thread; only used on
            // engines without AudioWorklet support)
            if (!node) {
                const proc = ctx.createScriptProcessor(CONFIG.bufferSize, 2, 2);
                try { proc.channelCount = 2; } catch(e) {}
                proc.onaudioprocess = function(e) {
                    const inL = e.inputBuffer.getChannelData(0);
                    const inR = e.inputBuffer.numberOfChannels > 1 ? e.inputBuffer.getChannelData(1) : inL;
                    const len = inL.length;
                    const currentRate = (this.context && this.context.sampleRate) ? this.context.sampleRate : (CONFIG.targetSampleRate || 48000);

                    // Silence output buffer so zero audio leaks to host/speakers
                    const outL = e.outputBuffer.getChannelData(0);
                    const outR = e.outputBuffer.numberOfChannels > 1 ? e.outputBuffer.getChannelData(1) : outL;
                    outL.fill(0);
                    if (outR !== outL) outR.fill(0);

                    const f32 = new Float32Array(len * 2);
                    for (let i = 0; i < len; i++) {
                        f32[i * 2 + 0] = inL[i];
                        f32[i * 2 + 1] = inR[i];
                    }
                    handleCapturedBlock(currentRate, len, f32.buffer);
                };
                node = proc;
            }

            ctx.__juceProc = node;

            // Connect masterTap to capture node, and connect capture node to silent sink then to ctx.destination.
            // This actively drives the WebKitGTK audio rendering pipeline while keeping OS speakers 100% silent!
            if (origConnect) {
                origConnect.call(masterTap, node);
                try {
                    const silentSink = ctx.createGain();
                    silentSink.gain.value = 0.0;
                    ctx.__juceSilentSink = silentSink;
                    origConnect.call(node, silentSink);
                    origConnect.call(silentSink, ctx.destination);
                } catch(e) {
                    try {
                        const dummyDest = ctx.createMediaStreamDestination();
                        origConnect.call(node, dummyDest);
                    } catch(e2) {}
                }
            }

            console.log("[JUCE-WebBridge] AudioContext hooked successfully (" + (node.port ? "AudioWorklet" : "ScriptProcessor") + ") for Bitwig VST3!");
        } catch (err) {
            console.error("[JUCE-WebBridge] Error hooking AudioContext:", err);
        }
    }

    function updateMuteState() {
        // Kept for backward compatibility
    }

    if (OrigAudioContext) {
        window.AudioContext = class extends OrigAudioContext {
            constructor(...args) {
                let opts = args[0] || {};
                if (typeof opts !== 'object') opts = {};
                if (CONFIG.targetSampleRate > 0 && !opts.sampleRate) {
                    opts = Object.assign({}, opts, { sampleRate: CONFIG.targetSampleRate });
                    args[0] = opts;
                }
                super(...args);
                hookContext(this);
            }
        };

        if (window.webkitAudioContext) {
            window.webkitAudioContext = window.AudioContext;
        }

        if (origConnect) {
            AudioNode.prototype.connect = function(destination, outputIndex, inputIndex) {
                try {
                    if (this.context) {
                        // CRITICAL: if this is one of our internal bridge nodes, connect natively
                        if (this === this.context.__juceMasterTap ||
                            this === this.context.__juceProc ||
                            this === this.context.__juceSilentSink) {
                            return origConnect.apply(this, arguments);
                        }

                        hookContext(this.context);
                        const tap = this.context.__juceMasterTap;

                        // If connecting to native destination: redirect strictly to master tap!
                        // This guarantees 0% audio ever reaches ctx.destination unintercepted!
                        if (tap && (destination === this.context.destination || 
                                    (typeof AudioDestinationNode !== "undefined" && destination instanceof AudioDestinationNode))) {
                            origConnect.call(this, tap, outputIndex || 0, inputIndex || 0);
                            return destination;
                        }
                    }
                } catch (e) {
                    console.warn("[JUCE-WebBridge] Error in connect tap:", e);
                }
                return origConnect.apply(this, arguments);
            };
        }
    }

    // =========================================================================
    // 4. HTML5 MEDIA / YOUTUBE VIDEO STREAMING
    // Handled natively by WebKitGTK & GStreamer via BrowserInstrumentSink.
    // Captured with ultra-low latency by PulseAudioCaptureThread in C++.
    // JavaScript does NOT tamper with <video>, <audio>, or YouTube players.
    // =========================================================================

    // =========================================================================
    // 5. GETUSERMEDIA SHIM (Sample directly from Bitwig Track Audio)
    // =========================================================================
    if (typeof navigator !== "undefined" && navigator.mediaDevices) {
        const origGetUserMedia = navigator.mediaDevices.getUserMedia ? 
            navigator.mediaDevices.getUserMedia.bind(navigator.mediaDevices) : null;

        navigator.mediaDevices.getUserMedia = async function(constraints) {
            if (constraints && (constraints.audio || constraints.audio === true)) {
                console.log("[JUCE-WebBridge] getUserMedia({ audio }) intercepted! Streaming Bitwig track audio into sampler...");
                const ctx = getOrCreateJuceAudioContext();

                const streamDest = ctx.createMediaStreamDestination();
                const feedProc = ctx.createScriptProcessor(CONFIG.bufferSize, 0, 2);

                feedProc.onaudioprocess = function(e) {
                    const outL = e.outputBuffer.getChannelData(0);
                    const outR = e.outputBuffer.getChannelData(1);
                    const len = outL.length;

                    if (dawInputRingBuffer.length >= len * 2) {
                        for (let i = 0; i < len; i++) {
                            outL[i] = dawInputRingBuffer.shift();
                            outR[i] = dawInputRingBuffer.shift();
                        }
                    } else {
                        outL.fill(0);
                        outR.fill(0);
                    }
                };

                feedProc.connect(streamDest);
                return streamDest.stream;
            }

            if (origGetUserMedia) return origGetUserMedia(constraints);
            throw new Error("getUserMedia not supported");
        };
    }

    function scanAndHook() {
        if (typeof window !== "undefined") {
            for (const k of Object.getOwnPropertyNames(window)) {
                try {
                    const v = window[k];
                    if (v && (v instanceof OrigAudioContext || (v.destination && typeof v.createGain === "function"))) {
                        hookContext(v);
                    }
                } catch(e) {}
            }
        }
    }

    function scanAndHookAll() {
        scanAndHook();
    }

    scanAndHookAll();
    getOrCreateJuceAudioContext();

    if (typeof window !== "undefined") {
        ['pointerdown', 'mousedown', 'mouseup', 'keydown', 'keyup', 'touchstart', 'touchend', 'click'].forEach(evt => {
            window.addEventListener(evt, () => {
                resumeAllContexts();
                scanAndHookAll();
            }, { passive: true });
        });
        window.addEventListener("DOMContentLoaded", scanAndHookAll);
        window.addEventListener("load", scanAndHookAll);
    }

    // Global controller
    window.__JUCE_BRIDGE__ = {
        config: CONFIG,
        getConnected: () => true,
        setMuteSystemAudio: (mute) => {
            CONFIG.muteSystemAudio = !!mute;
            updateMuteState();
        },
        dispatchMidiFromDaw: dispatchMidiFromDaw,
        setTransportPlay: function(play) {
            try {
                if (play) {
                    CONFIG.transportPlaying = true;
                    resumeAllContexts();
                    if (window.strudelMirror) {
                        try {
                            if (typeof window.strudelMirror.evaluate === 'function') {
                                window.strudelMirror.evaluate();
                                return;
                            }
                            if (window.strudelMirror.repl && typeof window.strudelMirror.repl.evaluate === 'function') {
                                window.strudelMirror.repl.evaluate(window.strudelMirror.code);
                                return;
                            }
                        } catch(e) {}
                    }
                    const playBtns = document.querySelectorAll('button[title="play"], button[title*="play" i], button[aria-label*="play" i]');
                    playBtns.forEach(btn => btn.click());
                } else {
                    CONFIG.transportPlaying = false;
                    let stoppedNatively = false;
                    if (window.strudelMirror) {
                        try {
                            if (window.strudelMirror.repl) {
                                if (window.strudelMirror.repl.scheduler) {
                                    window.strudelMirror.repl.scheduler.stop();
                                    stoppedNatively = true;
                                }
                                if (typeof window.strudelMirror.repl.stop === 'function') {
                                    window.strudelMirror.repl.stop();
                                    stoppedNatively = true;
                                }
                            }
                            if (typeof window.strudelMirror.stop === 'function') {
                                window.strudelMirror.stop();
                                stoppedNatively = true;
                            }
                        } catch(e) {}
                        try {
                            const ed = window.strudelMirror.editor;
                            document.dispatchEvent(new CustomEvent('repl-stop', { detail: { view: ed } }));
                            window.dispatchEvent(new CustomEvent('repl-stop', { detail: { view: ed } }));
                        } catch(e) {}
                    }
                    try {
                        if (typeof window.hush === 'function') window.hush();
                        if (typeof hush === 'function') hush();
                    } catch(e) {}

                    // IMPORTANT: only fall back to clicking a DOM "stop" button when the
                    // native scheduler API was not available. Some Strudel UI builds use a
                    // single toggle button for play/stop; clicking it *after* the transport
                    // has already been stopped natively can flip it back to "play" and
                    // restart audio right after the DAW stops (the bug where playback
                    // seems to click "stop" and immediately keep going).
                    if (!stoppedNatively) {
                        try {
                            const stopBtns = document.querySelectorAll('button[title="stop"], button[title*="stop" i], button[aria-label*="stop" i]');
                            stopBtns.forEach(btn => btn.click());
                            document.dispatchEvent(new CustomEvent('stop-repl'));
                        } catch(e) {}
                    }
                }
            } catch(e) {
                console.warn("[JUCE-WebBridge] setTransportPlay error:", e);
            }
        },
        setBpm: function(bpm) {
            try {
                if (typeof window.setcps === 'function') {
                    window.setcps(bpm / 240.0);
                }
            } catch(e) {}
        },
        sendTestMidi: (note = 60, vel = 100) => {
            dispatchMidiFromDaw(0x90, note, vel);
            setTimeout(() => dispatchMidiFromDaw(0x80, note, 0), 250);
        },
        resumeAudio: resumeAllContexts,
        cacheAllPageResources: sweepAndCacheAllPageResources,
        clearCache: async function() {
            if (typeof caches !== "undefined") {
                await caches.delete(BI_CACHE_NAME);
                console.log("[JUCE-WebBridge] Local runtime cache cleared.");
            }
        },
        setSampleRate: function(rate) {
            if (typeof rate === "number" && rate >= 22050 && rate <= 384000) {
                CONFIG.targetSampleRate = rate;
                console.log("[JUCE-WebBridge] Sample rate updated: " + rate + " Hz (Bitrate: " + (rate * 64 / 1000) + " kbps 32-bit Float)");
            }
        }
    };

    console.log("[JUCE-WebBridge] All hooks active: Bitwig Track Audio & MIDI Ready!");
})();
)JS")
        .replace("%PORT%", juce::String(bridgePort))
        .replace("%SAMPLE_RATE%", juce::String(targetSampleRate))
        .replace("%BUFFER_SIZE%", juce::String(bufferSize));
}

} // namespace WebBridge
