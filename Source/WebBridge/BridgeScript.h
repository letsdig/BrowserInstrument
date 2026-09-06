#pragma once

#include <juce_core/juce_core.h>

namespace WebBridge
{

inline juce::String getInjectionScript(int bridgePort = 8788)
{
    return juce::String(R"JS(
(function() {
    if (window.__JUCE_BRIDGE_LOADED__) {
        console.log("[JUCE-WebBridge] Re-arming context scan...");
        if (typeof scanAndHook === "function") scanAndHook();
        return;
    }
    window.__JUCE_BRIDGE_LOADED__ = true;

    console.log("[JUCE-WebBridge] Initializing DAW Web Audio & MIDI Bridge for Bitwig Studio...");

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
        bufferSize: 512
    };

    // =========================================================================
    // 0. DIRECT NATIVE JUCE IN-MEMORY IPC (100% immune to CORS / Mixed Content)
    // =========================================================================
    function sendNativeJuceEvent(eventId, payload) {
        try {
            const jsonStr = JSON.stringify({ eventId: eventId, payload: payload });
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
    // 1. WEBSOCKET CONNECTION (For External Chrome & Localhost)
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
                    tryConnect();
                }
            }, 1200);

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

        const bytes = new Uint8Array([status, d1, d2]);
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
    // =========================================================================
    const OrigAudioContext = window.AudioContext || window.webkitAudioContext;
    const origConnect = (typeof AudioNode !== "undefined" && AudioNode.prototype) ? AudioNode.prototype.connect : null;

    function hookContext(ctx) {
        if (!ctx || ctx.__juceHooked) return;
        ctx.__juceHooked = true;
        allHookedContexts.add(ctx);
        window.__juceAudioCtx = ctx;

        try {
            const masterTap = ctx.createGain();
            masterTap.gain.value = 1.0;
            ctx.__juceMasterTap = masterTap;

            const proc = ctx.createScriptProcessor(CONFIG.bufferSize, 2, 2);
            ctx.__juceProc = proc;

            const silentSink = ctx.createGain();
            // Imperceptible non-zero gain when muted so browser never treats as dead branch
            silentSink.gain.value = CONFIG.muteSystemAudio ? 0.00001 : 1.0;
            ctx.__juceSilentSink = silentSink;

            // Direct native connection to destination (bypassing custom connect hook)
            if (origConnect) {
                origConnect.call(masterTap, proc);
                origConnect.call(proc, silentSink);
                try {
                    origConnect.call(silentSink, ctx.destination);
                } catch(e) {}
            }

            proc.onaudioprocess = function(e) {
                const inL = e.inputBuffer.getChannelData(0);
                const inR = e.inputBuffer.numberOfChannels > 1 ? e.inputBuffer.getChannelData(1) : inL;
                const len = inL.length;

                // When OS audio is unmuted, pass audio through to destination
                if (!CONFIG.muteSystemAudio) {
                    const outL = e.outputBuffer.getChannelData(0);
                    const outR = e.outputBuffer.numberOfChannels > 1 ? e.outputBuffer.getChannelData(1) : outL;
                    outL.set(inL);
                    if (outR !== outL) outR.set(inR);
                }

                // 1. Direct Native In-Memory IPC to JUCE (100% immune to Mixed Content / CORS)
                const f32 = new Float32Array(len * 2);
                for (let i = 0; i < len; i++) {
                    f32[i * 2 + 0] = inL[i];
                    f32[i * 2 + 1] = inR[i];
                }
                const u8 = new Uint8Array(f32.buffer);
                let bin = "";
                const chunkSz = 1024;
                for (let i = 0; i < u8.length; i += chunkSz) {
                    bin += String.fromCharCode.apply(null, u8.subarray(i, i + chunkSz));
                }
                const b64 = btoa(bin);

                sendNativeJuceEvent("dawAudioData", { pcm: b64, channels: 2, samples: len });

                // 2. WebSocket transmission (for external Google Chrome)
                if (wsConnected && ws && ws.readyState === WebSocket.OPEN) {
                    const packet = new Uint8Array(4 + len * 2 * 4);
                    packet[0] = 0x01; // Audio Output
                    packet[1] = 0x02; // 2 channels
                    packet[2] = len & 0xFF;
                    packet[3] = (len >> 8) & 0xFF;

                    const packetF32 = new Float32Array(packet.buffer, 4, len * 2);
                    for (let i = 0; i < len; i++) {
                        packetF32[i * 2 + 0] = inL[i];
                        packetF32[i * 2 + 1] = inR[i];
                    }

                    try {
                        ws.send(packet.buffer);
                    } catch (err) {}
                }
            };

            console.log("[JUCE-WebBridge] AudioContext hooked successfully for Bitwig VST3!");
        } catch (err) {
            console.error("[JUCE-WebBridge] Error hooking AudioContext:", err);
        }
    }

    function updateMuteState() {
        for (const ctx of allHookedContexts) {
            if (ctx && ctx.__juceSilentSink) {
                ctx.__juceSilentSink.gain.value = CONFIG.muteSystemAudio ? 0.00001 : 1.0;
            }
        }
    }

    if (OrigAudioContext) {
        window.AudioContext = class extends OrigAudioContext {
            constructor(...args) {
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

                        // If connecting to native destination: redirect to master tap!
                        if (tap && (destination === this.context.destination || 
                                    (typeof AudioDestinationNode !== "undefined" && destination instanceof AudioDestinationNode))) {
                            origConnect.call(this, tap, outputIndex || 0, inputIndex || 0);

                            if (!CONFIG.muteSystemAudio) {
                                return origConnect.call(this, destination, outputIndex || 0, inputIndex || 0);
                            }
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
    // 4. HTML5 MEDIA ELEMENT CAPTURE (<audio>, <video>, YouTube in YPC2000)
    // =========================================================================
    if (typeof HTMLMediaElement !== "undefined") {
        const origPlay = HTMLMediaElement.prototype.play;
        HTMLMediaElement.prototype.play = function() {
            try {
                if (!this.__juceMediaTapped && window.__juceAudioCtx && window.__juceAudioCtx.__juceMasterTap) {
                    this.__juceMediaTapped = true;
                    try {
                        const source = window.__juceAudioCtx.createMediaElementSource(this);
                        source.connect(window.__juceAudioCtx.__juceMasterTap);
                        if (CONFIG.muteSystemAudio) {
                            this.muted = false; // keep element playing internally
                        }
                    } catch (err) {}
                }
            } catch(e) {}
            return origPlay.apply(this, arguments);
        };
    }

    // =========================================================================
    // 5. GETUSERMEDIA SHIM (Sample directly from Bitwig Track Audio)
    // =========================================================================
    if (typeof navigator !== "undefined" && navigator.mediaDevices) {
        const origGetUserMedia = navigator.mediaDevices.getUserMedia ? 
            navigator.mediaDevices.getUserMedia.bind(navigator.mediaDevices) : null;

        navigator.mediaDevices.getUserMedia = async function(constraints) {
            if (constraints && (constraints.audio || constraints.audio === true)) {
                console.log("[JUCE-WebBridge] getUserMedia({ audio }) intercepted! Streaming Bitwig track audio into sampler...");
                const ctx = window.__juceAudioCtx || new OrigAudioContext();
                hookContext(ctx);

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

    scanAndHook();
    if (typeof window !== "undefined") {
        window.addEventListener("DOMContentLoaded", scanAndHook);
        window.addEventListener("load", scanAndHook);
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
        sendTestMidi: (note = 60, vel = 100) => {
            dispatchMidiFromDaw(0x90, note, vel);
            setTimeout(() => dispatchMidiFromDaw(0x80, note, 0), 250);
        },
        resumeAudio: resumeAllContexts
    };

    console.log("[JUCE-WebBridge] All hooks active: Bitwig Track Audio & MIDI Ready!");
})();
)JS").replace("%PORT%", juce::String(bridgePort));
}

} // namespace WebBridge
