#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "WebBridge/WebBridgeServer.h"

class BrowserInstrumentAudioProcessor : public juce::AudioProcessor
{
public:
    BrowserInstrumentAudioProcessor();
    ~BrowserInstrumentAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    using AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    WebBridge::WebBridgeServer& getBridgeServer() noexcept { return bridgeServer; }
    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts; }

    juce::String getLastLoadedUrl() const { return currentUrl; }
    void setLastLoadedUrl(const juce::String& url) { currentUrl = url; }

private:
    WebBridge::WebBridgeServer bridgeServer;
    juce::AudioProcessorValueTreeState apvts;
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    std::atomic<float>* outputGainParam = nullptr;
    std::atomic<float>* bridgeEnabledParam = nullptr;
    std::atomic<float>* sendInputParam = nullptr;

    juce::String currentUrl { "https://ypc2000.fun/" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrowserInstrumentAudioProcessor)
};
