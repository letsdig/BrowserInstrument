#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "WebBridge/BridgeScript.h"

BrowserInstrumentAudioProcessor::BrowserInstrumentAudioProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Parameters", createParameterLayout())
{
    // Force WebKit / GStreamer audio to null sink so it NEVER outputs to physical speakers
    setenv ("PULSE_SINK", "BrowserInstrumentSink", 1);
    // Force ultra-low latency on PulseAudio / PipeWire streams (5ms)
    setenv ("PULSE_LATENCY_MSEC", "5", 1);
    // Block WebKit from using DMABUF / Compositing mode (prevents GPU driver crashes on Linux)
    setenv ("WEBKIT_DISABLE_DMABUF_RENDERER", "1", 1);
    setenv ("WEBKIT_DISABLE_COMPOSITING_MODE", "1", 1);

    outputGainParam = apvts.getRawParameterValue ("gain");
    bridgeEnabledParam = apvts.getRawParameterValue ("bridgeEnabled");
    sendInputParam = apvts.getRawParameterValue ("sendInput");

    bridgeServer.startServer();

    // Start PipeWire / PulseAudio capture from BrowserInstrumentSink.monitor into DAW FIFO
    PulseAudioCaptureThread::ensureSinkExists();
    pulseCaptureThread = std::make_unique<PulseAudioCaptureThread> (bridgeServer);
    pulseCaptureThread->startCapture (getEffectiveSampleRate());
}

BrowserInstrumentAudioProcessor::~BrowserInstrumentAudioProcessor()
{
    if (pulseCaptureThread != nullptr)
    {
        pulseCaptureThread->stopCapture();
        pulseCaptureThread.reset();
    }
    browser.reset();
    bridgeServer.stopServer();
}

juce::AudioProcessorValueTreeState::ParameterLayout BrowserInstrumentAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID ("gain", 1),
        "Output Gain",
        juce::NormalisableRange<float> (0.0f, 2.0f, 0.01f),
        1.0f));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID ("bridgeEnabled", 1),
        "Bridge Audio & MIDI",
        true));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID ("sendInput", 1),
        "Send DAW Audio to Web Sampler",
        true));

    return { params.begin(), params.end() };
}

const juce::String BrowserInstrumentAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool BrowserInstrumentAudioProcessor::acceptsMidi() const
{
    return true;
}

bool BrowserInstrumentAudioProcessor::producesMidi() const
{
    return true;
}

bool BrowserInstrumentAudioProcessor::isMidiEffect() const
{
    return false;
}

double BrowserInstrumentAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int BrowserInstrumentAudioProcessor::getNumPrograms()
{
    return 1;
}

int BrowserInstrumentAudioProcessor::getCurrentProgram()
{
    return 0;
}

void BrowserInstrumentAudioProcessor::setCurrentProgram (int /*index*/) {}
const juce::String BrowserInstrumentAudioProcessor::getProgramName (int /*index*/) { return {}; }
void BrowserInstrumentAudioProcessor::changeProgramName (int /*index*/, const juce::String& /*newName*/) {}

void BrowserInstrumentAudioProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    bridgeServer.setDawSampleRate ((int) sampleRate);
    bridgeServer.startServer();
    if (pulseCaptureThread != nullptr)
        pulseCaptureThread->startCapture ((int) sampleRate);
}

void BrowserInstrumentAudioProcessor::releaseResources()
{
    if (pulseCaptureThread != nullptr)
        pulseCaptureThread->stopCapture();
}

bool BrowserInstrumentAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    if (! layouts.getMainInputChannelSet().isDisabled()
     && layouts.getMainInputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainInputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    return true;
}

void BrowserInstrumentAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    const int totalNumInputChannels  = getTotalNumInputChannels();
    const int totalNumOutputChannels = getTotalNumOutputChannels();
    const int numSamples = buffer.getNumSamples();

    const bool bridgeEnabled = (bridgeEnabledParam != nullptr) ? (bridgeEnabledParam->load() > 0.5f) : true;
    const bool sendInput = (sendInputParam != nullptr) ? (sendInputParam->load() > 0.5f) : true;
    const float gain = (outputGainParam != nullptr) ? outputGainParam->load() : 1.0f;

    if (!bridgeEnabled)
    {
        for (int i = 0; i < totalNumOutputChannels; ++i)
            buffer.clear (i, 0, numSamples);
        return;
    }

    // 0. Automatic Universal DAW Transport & Tempo Sync (Background, always-on)
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            const bool isPlaying = pos->getIsPlaying();
            if (isPlaying != wasDawPlaying.load())
            {
                wasDawPlaying.store (isPlaying);

                // Send MIDI Realtime Start (0xFA) or Stop (0xFC) to Web MIDI
                const uint8_t rtStatus = isPlaying ? 0xFA : 0xFC;
                juce::MidiMessage transportMsg (rtStatus);
                bridgeServer.sendMidiToBrowser (transportMsg);

                if (browser != nullptr)
                {
                    auto* b = browser.get();
                    juce::MessageManager::callAsync ([b, isPlaying, rtStatus]()
                    {
                        if (b)
                        {
                            b->evaluateJavascript (
                                "(function() {"
                                "    if (window.__JUCE_BRIDGE__) {"
                                "        if (typeof window.__JUCE_BRIDGE__.setTransportPlay === 'function') window.__JUCE_BRIDGE__.setTransportPlay(" + juce::String (isPlaying ? "true" : "false") + ");"
                                "        if (typeof window.__JUCE_BRIDGE__.dispatchMidiFromDaw === 'function') window.__JUCE_BRIDGE__.dispatchMidiFromDaw(" + juce::String (rtStatus) + ", 0, 0);"
                                "    }"
                                "    if (window.strudelMirror) {"
                                "        if (" + juce::String (isPlaying ? "true" : "false") + ") { if (typeof window.strudelMirror.evaluate === 'function') window.strudelMirror.evaluate(); }"
                                "        else { if (typeof window.strudelMirror.stop === 'function') window.strudelMirror.stop(); }"
                                "    }"
                                "})();"
                            );
                        }
                    });
                }
            }

            if (const auto bpmOpt = pos->getBpm())
            {
                const double currentBpm = *bpmOpt;
                if (std::abs (currentBpm - lastDawBpm.load()) > 0.1)
                {
                    lastDawBpm.store (currentBpm);
                    if (browser != nullptr)
                    {
                        auto* b = browser.get();
                        juce::MessageManager::callAsync ([b, currentBpm]()
                        {
                            if (b)
                            {
                                b->evaluateJavascript (
                                    "if (window.__JUCE_BRIDGE__ && typeof window.__JUCE_BRIDGE__.setBpm === 'function') {"
                                    "    window.__JUCE_BRIDGE__.setBpm(" + juce::String (currentBpm, 2) + ");"
                                    "}"
                                );
                            }
                        });
                    }
                }
            }
        }
    }

    // 1. Forward DAW incoming audio to browser (if channel has audio input, e.g. for sampling)
    if (sendInput && totalNumInputChannels > 0)
    {
        bridgeServer.sendAudioToBrowser (buffer.getArrayOfReadPointers(), totalNumInputChannels, numSamples);
    }

    // 2. Forward DAW incoming MIDI to WebBridge & browser
    for (const auto metadata : midiMessages)
    {
        const auto msg = metadata.getMessage();
        bridgeServer.sendMidiToBrowser (msg);

        if (browser != nullptr && msg.getRawDataSize() >= 1)
        {
            const auto* raw = msg.getRawData();
            int status = raw[0];
            int d1 = msg.getRawDataSize() > 1 ? raw[1] : 0;
            int d2 = msg.getRawDataSize() > 2 ? raw[2] : 0;
            auto* b = browser.get();
            juce::MessageManager::callAsync ([b, status, d1, d2]()
            {
                if (b)
                {
                    b->evaluateJavascript (
                        "if (window.__JUCE_BRIDGE__) window.__JUCE_BRIDGE__.dispatchMidiFromDaw(" +
                        juce::String (status) + "," + juce::String (d1) + "," + juce::String (d2) + ");");
                }
            });
        }
    }

    // 3. Clear buffer outputs before writing captured Web Audio
    for (int i = 0; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, numSamples);

    // 4. Read Web Audio PCM from the bridge FIFO into DAW output buffer!
    int samplesRead = bridgeServer.readAudioFromBrowser (buffer.getArrayOfWritePointers(), totalNumOutputChannels, numSamples);

    // 5. Apply output gain if adjusted
    if (samplesRead > 0 && std::abs (gain - 1.0f) > 0.001f)
    {
        buffer.applyGain (0, samplesRead, gain);
    }

    // 6. Read any MIDI generated by the browser (sequencer, virtual pads) into DAW midiMessages
    bridgeServer.getMidiFromBrowser (midiMessages, 0);
}

// (pushBase64AudioFromBrowser removed; all audio captured directly via PulseAudio)

InstrumentBrowserComponent* BrowserInstrumentAudioProcessor::getOrCreateBrowser()
{
    if (browser == nullptr)
    {
        createPersistentBrowser();
    }
    return browser.get();
}

void BrowserInstrumentAudioProcessor::createPersistentBrowser()
{
    if (browser != nullptr)
        return;

    juce::WebBrowserComponent::Options options;
    options = options.withNativeIntegrationEnabled (true)
                     .withKeepPageLoadedWhenBrowserIsHidden()
                     .withUserAgent ("Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/128.0.0.0 Safari/537.36")
                     .withUserScript (WebBridge::getInjectionScript (bridgeServer.getPort(), getEffectiveSampleRate()))
                     .withEventListener ("dawMidiData", [this] (const juce::var& data)
                     {
                         if (auto* obj = data.getDynamicObject())
                         {
                             int status = (int) obj->getProperty ("status");
                             int d1 = (int) obj->getProperty ("d1");
                             int d2 = (int) obj->getProperty ("d2");
                             bridgeServer.injectMidiFromBrowser (status, d1, d2);
                         }
                     });

    browser = std::make_unique<InstrumentBrowserComponent> (
        options,
        [this] (const juce::String& loadedUrl)
        {
            setLastLoadedUrl (loadedUrl);
            if (auto* ed = activeEditor.load())
                ed->onBrowserUrlChanged (loadedUrl);
        },
        [this] (const juce::String& /*loadedUrl*/)
        {
            if (browser)
            {
                browser->evaluateJavascript (
                    WebBridge::getInjectionScript (bridgeServer.getPort(), getEffectiveSampleRate()));
            }
        });

    auto target = getLastLoadedUrl();
    if (target.trim().isEmpty() || target == "https://strudel.cc/")
        target = "https://ypc2000.fun/";
    browser->goToURL (target);
}

void BrowserInstrumentAudioProcessor::reattachBrowserToHiddenHost()
{
    juce::MessageManager::callAsync ([this]()
    {
        if (browser == nullptr)
            return;

        // If an editor is currently active and owns the browser, don't reparent to hidden host
        if (activeEditor.load() != nullptr)
            return;

        if (hiddenHost == nullptr)
        {
            hiddenHost = std::make_unique<HiddenBrowserHost>();
            hiddenHost->setBounds (-10000, -10000, 16, 16);
            hiddenHost->addToDesktop (0);
        }

        hiddenHost->setVisible (true);
        hiddenHost->addAndMakeVisible (*browser);
        browser->setBounds (hiddenHost->getLocalBounds());
    });
}

bool BrowserInstrumentAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* BrowserInstrumentAudioProcessor::createEditor()
{
    return new BrowserInstrumentAudioProcessorEditor (*this);
}

void BrowserInstrumentAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("currentUrl", currentUrl, nullptr);
    state.setProperty ("prefSampleRate", preferredSampleRate.load(), nullptr);
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void BrowserInstrumentAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));

    if (xmlState.get() != nullptr)
    {
        if (xmlState->hasTagName (apvts.state.getType()))
        {
            auto vt = juce::ValueTree::fromXml (*xmlState);
            apvts.replaceState (vt);
            if (vt.hasProperty ("currentUrl"))
            {
                currentUrl = vt.getProperty ("currentUrl").toString();
                if (currentUrl == "https://strudel.cc/" || currentUrl.isEmpty())
                    currentUrl = "https://ypc2000.fun/";
            }
            if (vt.hasProperty ("prefSampleRate"))
                preferredSampleRate.store ((int) vt.getProperty ("prefSampleRate"));
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BrowserInstrumentAudioProcessor();
}
