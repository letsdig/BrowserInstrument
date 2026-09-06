#include "PluginProcessor.h"
#include "PluginEditor.h"

BrowserInstrumentAudioProcessor::BrowserInstrumentAudioProcessor()
    : AudioProcessor (BusesProperties()
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                        .withInput  ("Audio In (Sampler)", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "Parameters", createParameterLayout())
{
    // Fix WebKitGTK crashes on Linux NVIDIA drivers
    setenv ("WEBKIT_DISABLE_DMABUF_RENDERER", "1", 1);
    setenv ("WEBKIT_DISABLE_COMPOSITING_MODE", "1", 1);

    outputGainParam = apvts.getRawParameterValue ("gain");
    bridgeEnabledParam = apvts.getRawParameterValue ("bridgeEnabled");
    sendInputParam = apvts.getRawParameterValue ("sendInput");

    bridgeServer.startServer();
}

BrowserInstrumentAudioProcessor::~BrowserInstrumentAudioProcessor()
{
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
}

void BrowserInstrumentAudioProcessor::releaseResources()
{
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
        for (int i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
            buffer.clear (i, 0, numSamples);
        return;
    }

    // 1. Forward DAW Audio Input to Web Sampler (e.g. for sampling in YPC2000)
    if (sendInput && totalNumInputChannels > 0)
    {
        bridgeServer.sendAudioToBrowser (buffer.getArrayOfReadPointers(), totalNumInputChannels, numSamples);
    }

    // 2. Forward DAW MIDI Events to Browser Instrument (both WebSocket and in-memory WebKit IPC)
    auto* ed = activeEditor.load();
    for (const auto metadata : midiMessages)
    {
        bridgeServer.sendMidiToBrowser (metadata.getMessage());

        if (ed != nullptr)
        {
            ed->sendMidiToBrowser (metadata.getMessage());
        }
    }

    // Clear buffer outputs before writing browser audio
    for (int i = 0; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, numSamples);

    // 3. Read Web Audio PCM from the bridge FIFO into DAW output buffer
    int samplesRead = bridgeServer.readAudioFromBrowser (buffer.getArrayOfWritePointers(), totalNumOutputChannels, numSamples);

    // 4. Apply output gain
    if (samplesRead > 0 && std::abs (gain - 1.0f) > 0.001f)
    {
        buffer.applyGain (0, samplesRead, gain);
    }

    // 5. Read any MIDI generated by the browser (sequencer, virtual pads) into DAW midiMessages
    bridgeServer.getMidiFromBrowser (midiMessages, 0);
}

void BrowserInstrumentAudioProcessor::pushBase64AudioFromBrowser (const juce::String& base64)
{
    juce::MemoryOutputStream mos;
    if (juce::Base64::convertFromBase64 (mos, base64))
    {
        const size_t numBytes = mos.getDataSize();
        const int numFloats = static_cast<int> (numBytes / sizeof (float));
        if (numFloats >= 2)
        {
            const float* f32 = reinterpret_cast<const float*> (mos.getData());
            const int sampleCount = numFloats / 2;
            bridgeServer.writeAudioToFifo (f32, 2, sampleCount);
        }
    }
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
                currentUrl = vt.getProperty ("currentUrl").toString();
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BrowserInstrumentAudioProcessor();
}
