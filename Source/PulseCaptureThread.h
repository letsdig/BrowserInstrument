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
        int rate = sampleRate > 0 ? sampleRate : 48000;
        targetSampleRate.store (rate);
        ensureSinkExists (rate);
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

            // Scale block size with sample rate to maintain ultra-low latency (~2.6ms) across all bitrates
            const int framesPerBlock = juce::jlimit (64, 512, (int) std::round (currentRate * 0.002667));

            pa_buffer_attr attr;
            attr.maxlength = static_cast<uint32_t> (32768 * 2 * sizeof (float)); // High headroom for 32-bit float streaming
            attr.tlength   = (uint32_t) -1;
            attr.prebuf    = (uint32_t) -1;
            attr.minreq    = (uint32_t) -1;
            attr.fragsize  = static_cast<uint32_t> (framesPerBlock * 2 * sizeof (float));

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
                ensureSinkExists (currentRate);
                for (int i = 0; i < 5 && ! threadShouldExit(); ++i)
                    juce::Thread::sleep (100);
                continue;
            }

            std::vector<float> blockBuffer (static_cast<size_t> (framesPerBlock * 2));

            while (! threadShouldExit())
            {
                int readErr = 0;
                if (pa_simple_read (pulseHandle, blockBuffer.data(), blockBuffer.size() * sizeof (float), &readErr) < 0)
                {
                    break; // stream read error or server reset, reconnect
                }

                // Unconditionally write continuous stream to FIFO without dropping frames
                bridgeServer.writeAudioToFifo (blockBuffer.data(), 2, framesPerBlock, (double) currentRate);
            }

            pa_simple_free (pulseHandle);
        }
    }

    static void ensureSinkExists (int targetRate = 96000)
    {
        const int rate = targetRate > 0 ? targetRate : 96000;
        const int sinkCheck = ::system ("pactl list sinks short 2>/dev/null | grep -q 'BrowserInstrumentSink'");
        if (sinkCheck != 0)
        {
            // Initialize sink with true 32-bit float and studio high bitrate
            juce::String cmd = "pactl load-module module-null-sink sink_name=BrowserInstrumentSink rate=" + juce::String (rate)
                             + " channels=2 format=float32le sink_properties=device.description=BrowserInstrumentSink >/dev/null 2>&1";
            ::system (cmd.toRawUTF8());
        }
    }

private:
    WebBridge::WebBridgeServer& bridgeServer;
    std::atomic<int> targetSampleRate { 48000 };
};
