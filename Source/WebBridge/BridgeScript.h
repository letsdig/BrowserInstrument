#pragma once

#include <juce_core/juce_core.h>

namespace WebBridge
{

inline juce::String getInjectionScript(int bridgePort = 8765)
{
    return juce::String(R"JS(
(function() {
    if (window.__JUCE_BRIDGE_LOADED__) return;
    window.__JUCE_BRIDGE_LOADED__ = true;

    console.log("[JUCE-WebBridge] Initializing Web Audio & MIDI Bridge on port %PORT%...");

    const CONFIG = {
        port: %PORT%,
        muteSystemAudio: true,
        bufferSize: 512,
        reconnectDelayMs: 1500
    };

    let ws = null;
    let wsConnected = false;
    const dawInputRingBuffer = [];
    let activeAudioContexts = new Set();
    let bridgeNodes = new WeakMap();

    // =========================================================================
    // 1. WEBSOCKET CONNECTION
    // =========================================================================
    function connectWebSocket() {
        try {
            ws = new WebSocket("ws://127.0.0.1:" + CONFIG.port);
            ws.binaryType = "arraybuffer";

            ws.onopen = function() {
                console.log("[JUCE-WebBridge] WebSocket Connected to DAW engine!");
                wsConnected = true;
                notifyStatus("connected");
            };

            ws.onmessage = function(event) {
                if (!(event.data instanceof ArrayBuffer)) return;
                const data = new Uint8Array(event.data);
                if (data.length === 0) return;

                const packetType = data[0];

                if (packetType === 0x02 && data.length >= 4) {
                    // MIDI Packet from DAW: [0x02, status, data1, data2]
                    const status = data[1];
                    const d1 = data[2];
                    const d2 = data[3];
                    dispatchMidiFromDaw(status, d1, d2);
                } else if (packetType === 0x01 && data.length >= 4) {
                    // Audio Input from DAW (for sampling e.g. getUserMedia):
                    // [0x01, channels, lenLo, lenHi, ...float32]
                    const numChannels = data[1];
                    const sampleCount = data[2] | (data[3] << 8);
                    const f32 = new Float32Array(event.data, 4, sampleCount * numChannels);
                    for (let i = 0; i < f32.length; i++) {
                        dawInputRingBuffer.push(f32[i]);
                    }
                    // Limit buffer length to prevent memory leak
                    if (dawInputRingBuffer.length > 48000 * 2) {
                        dawInputRingBuffer.splice(0, dawInputRingBuffer.length - 48000);
                    }
                }
            };

            ws.onclose = function() {
                wsConnected = false;
                notifyStatus("disconnected");
                setTimeout(connectWebSocket, CONFIG.reconnectDelayMs);
            };

            ws.onerror = function() {
                ws.close();
            };
        } catch (err) {
            console.error("[JUCE-WebBridge] WS Error:", err);
            setTimeout(connectWebSocket, CONFIG.reconnectDelayMs);
        }
    }

    function notifyStatus(status) {
        if (window.__JUCE__ && window.__JUCE__.backend && window.__JUCE__.backend.emitEvent) {
            window.__JUCE__.backend.emitEvent("bridgeStatus", { status: status });
        }
    }

    connectWebSocket();

    // =========================================================================
    // 2. WEB MIDI SHIM (DAW <-> Browser Bidirectional MIDI)
    // =========================================================================
    class VirtualMIDIInput extends EventTarget {
        constructor() {
            super();
            this.id = "juce-daw-midi-in";
            this.name = "DAW MIDI Input (Channel Track)";
            this.manufacturer = "AudioWebLab JUCE";
            this.type = "input";
            this.state = "connected";
            this.connection = "open";
            this.onmidimessage = null;
        }

        _receiveMessage(bytes) {
            const event = new Event("midimessage");
            event.data = bytes;
            event.receivedTime = performance.now();

            if (typeof this.onmidimessage === "function") {
                try { this.onmidimessage(event); } catch (e) { console.error(e); }
            }
            this.dispatchEvent(event);
        }
    }

    class VirtualMIDIOutput {
        constructor() {
            this.id = "juce-daw-midi-out";
            this.name = "DAW MIDI Output (To Host)";
            this.manufacturer = "AudioWebLab JUCE";
            this.type = "output";
            this.state = "connected";
            this.connection = "open";
        }

        send(data, timestamp) {
            if (!wsConnected || !ws) return;
            const bytes = data instanceof Uint8Array ? data : new Uint8Array(data);
            const packet = new Uint8Array(1 + bytes.length);
            packet[0] = 0x02; // MIDI opcode
            packet.set(bytes, 1);
            try {
                ws.send(packet.buffer);
            } catch (e) {
                console.error("[JUCE-WebBridge] Error sending MIDI to DAW:", e);
            }
        }

        clear() {}
    }

    const virtualMidiInput = new VirtualMIDIInput();
    const virtualMidiOutput = new VirtualMIDIOutput();

    function dispatchMidiFromDaw(status, d1, d2) {
        const msgBytes = new Uint8Array([status, d1, d2]);
        virtualMidiInput._receiveMessage(msgBytes);
    }

    class VirtualMIDIAccess extends EventTarget {
        constructor() {
            super();
            this.inputs = new Map([[virtualMidiInput.id, virtualMidiInput]]);
            this.outputs = new Map([[virtualMidiOutput.id, virtualMidiOutput]]);
            this.sysexEnabled = true;
            this.onstatechange = null;
        }
    }

    const virtualMidiAccessInstance = new VirtualMIDIAccess();

    if (navigator) {
        navigator.requestMIDIAccess = async function() {
            console.log("[JUCE-WebBridge] requestMIDIAccess() intercepted, returning DAW virtual MIDI ports!");
            return virtualMidiAccessInstance;
        };
    }

    // =========================================================================
    // 3. WEB AUDIO SHIM (Intercepts Web Audio and Routes to DAW)
    // =========================================================================
    const OrigAudioContext = window.AudioContext || window.webkitAudioContext;

    if (OrigAudioContext) {
        function attachAudioTap(ctx) {
            if (activeAudioContexts.has(ctx)) return;
            activeAudioContexts.add(ctx);

            try {
                const bufferLen = CONFIG.bufferSize;
                const proc = ctx.createScriptProcessor(bufferLen, 2, 2);
                const silentGain = ctx.createGain();
                silentGain.gain.setValueAtTime(0, ctx.currentTime);

                proc.onaudioprocess = function(e) {
                    if (!wsConnected || !ws || ws.readyState !== WebSocket.OPEN) return;

                    const inL = e.inputBuffer.getChannelData(0);
                    const inR = e.inputBuffer.numberOfChannels > 1 ? e.inputBuffer.getChannelData(1) : inL;
                    const len = inL.length;

                    // Packet format: [0x01, numChannels=2, lenLo, lenHi, ...interleaved float32]
                    const packet = new Uint8Array(4 + len * 2 * 4);
                    packet[0] = 0x01; // Audio Output
                    packet[1] = 0x02; // 2 channels
                    packet[2] = len & 0xFF;
                    packet[3] = (len >> 8) & 0xFF;

                    const f32View = new Float32Array(packet.buffer, 4, len * 2);
                    for (let i = 0; i < len; i++) {
                        f32View[i * 2]     = inL[i];
                        f32View[i * 2 + 1] = inR[i];
                    }

                    try {
                        ws.send(packet.buffer);
                    } catch (err) {
                        // socket backpressure
                    }
                };

                // Keep processor running by connecting to silent gain then to real destination
                proc.connect(silentGain);
                silentGain.connect(ctx.destination);

                bridgeNodes.set(ctx, { proc: proc, silentGain: silentGain });
            } catch (err) {
                console.error("[JUCE-WebBridge] Failed to attach audio tap:", err);
            }
        }

        const origConnect = AudioNode.prototype.connect;
        AudioNode.prototype.connect = function(destination, outputIndex, inputIndex) {
            try {
                const ctx = this.context;
                if (ctx && ctx.destination && destination === ctx.destination) {
                    attachAudioTap(ctx);
                    const bridge = bridgeNodes.get(ctx);
                    if (bridge && bridge.proc) {
                        // Connect to our DAW capture processor!
                        origConnect.call(this, bridge.proc, outputIndex || 0, 0);

                        // If not muting system audio, also let it play natively
                        if (!CONFIG.muteSystemAudio) {
                            return origConnect.call(this, destination, outputIndex, inputIndex);
                        }
                        return destination;
                    }
                }
            } catch (e) {
                console.warn("[JUCE-WebBridge] Error in connect tap:", e);
            }
            return origConnect.apply(this, arguments);
        };

        window.AudioContext = class extends OrigAudioContext {
            constructor(...args) {
                super(...args);
                window.__juceAudioCtx = this;
                attachAudioTap(this);
            }
        };

        if (window.webkitAudioContext) {
            window.webkitAudioContext = window.AudioContext;
        }
    }

    // =========================================================================
    // 4. GETUSERMEDIA SHIM (Route DAW Track Audio -> Web Sampler e.g. YPC2000)
    // =========================================================================
    if (navigator && navigator.mediaDevices) {
        const origGetUserMedia = navigator.mediaDevices.getUserMedia ? 
            navigator.mediaDevices.getUserMedia.bind(navigator.mediaDevices) : null;

        navigator.mediaDevices.getUserMedia = async function(constraints) {
            if (constraints && (constraints.audio || constraints.audio === true)) {
                console.log("[JUCE-WebBridge] getUserMedia({ audio }) intercepted! Streaming DAW audio into web sampler...");
                const ctx = window.__juceAudioCtx || new OrigAudioContext();
                window.__juceAudioCtx = ctx;

                const streamDest = ctx.createMediaStreamDestination();
                const feedProc = ctx.createScriptProcessor(512, 0, 2);

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

            if (origGetUserMedia) {
                return origGetUserMedia(constraints);
            }
            throw new Error("getUserMedia not supported");
        };
    }

    // Export global bridge controller
    window.__JUCE_BRIDGE__ = {
        config: CONFIG,
        getConnected: () => wsConnected,
        setMuteSystemAudio: (mute) => { CONFIG.muteSystemAudio = !!mute; }
    };

    console.log("[JUCE-WebBridge] Injected successfully!");
})();
)JS").replace("%PORT%", juce::String(bridgePort));
}

} // namespace WebBridge
