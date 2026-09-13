#pragma once

#include <juce_core/juce_core.h>
#include <pulse/simple.h>
#include <pulse/error.h>
#include "WebBridge/WebBridgeServer.h"

class PulseAudioCaptureThread : public juce::Thread
{
public:
    PulseAudioCaptureThread (WebBridge::WebBridgeServer& server)
        : juce::Thread ("PulseAudioCaptureThread"),
          bridgeServer (server)
    {
    }

    ~PulseAudioCaptureThread() override
    {
        stopCapture();
    }

    void startCapture (int sampleRate)
    {
        targetSampleRate.store (sampleRate > 0 ? sampleRate : 48000);
        ensureSinkExists();
        if (! isThreadRunning())
        {
            startThread (juce::Thread::Priority::highest);
        }
    }

    void stopCapture()
    {
        signalThreadShouldExit();
        stopThread (1500);
    }

    void run() override
    {
        while (! threadShouldExit())
        {
            const int currentRate = targetSampleRate.load();
            pa_sample_spec ss;
            ss.format   = PA_SAMPLE_FLOAT32LE;
            ss.rate     = (uint32_t) currentRate;
            ss.channels = 2;

            constexpr int framesPerBlock = 128; // 128 stereo frames (~2.6ms at 48kHz)

            pa_buffer_attr attr;
            attr.maxlength = 1024 * 2 * sizeof (float); // Cap internal driver buffer to ~20ms max
            attr.tlength   = (uint32_t) -1;
            attr.prebuf    = (uint32_t) -1;
            attr.minreq    = (uint32_t) -1;
            attr.fragsize  = framesPerBlock * 2 * sizeof (float); // 128 stereo frames (~2.6ms fragment size)

            int error = 0;
            pa_simple* pulseHandle = pa_simple_new (nullptr,
                                                    "BrowserInstrumentCapture",
                                                    PA_STREAM_RECORD,
                                                    "BrowserInstrumentSink.monitor",
                                                    "DAW Capture",
                                                    &ss,
                                                    nullptr,
                                                    &attr,
                                                    &error);

            if (pulseHandle == nullptr)
            {
                ensureSinkExists();
                for (int i = 0; i < 5 && ! threadShouldExit(); ++i)
                    juce::Thread::sleep (100);
                continue;
            }

            float blockBuffer[framesPerBlock * 2];

            while (! threadShouldExit())
            {
                int readErr = 0;
                if (pa_simple_read (pulseHandle, blockBuffer, sizeof (blockBuffer), &readErr) < 0)
                {
                    break; // stream read error or server reset, reconnect
                }

                // Unconditionally write continuous stream to FIFO without dropping frames
                bridgeServer.writeAudioToFifo (blockBuffer, 2, framesPerBlock, (double) currentRate);
            }

            pa_simple_free (pulseHandle);
        }
    }

    static void ensureSinkExists()
    {
        ::system ("pactl list sinks short 2>/dev/null | grep -q 'BrowserInstrumentSink' || "
                  "pactl load-module module-null-sink sink_name=BrowserInstrumentSink sink_properties=device.description=BrowserInstrumentSink >/dev/null 2>&1");
    }

private:
    WebBridge::WebBridgeServer& bridgeServer;
    std::atomic<int> targetSampleRate { 48000 };
};
