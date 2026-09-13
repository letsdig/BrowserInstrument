#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "WebBridge/WebBridgeServer.h"
#include "InstrumentBrowserComponent.h"
#include "PulseCaptureThread.h"

class BrowserInstrumentAudioProcessorEditor;

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
    void setLastLoadedUrl (const juce::String& url) { currentUrl = url; }

    void setEditor (BrowserInstrumentAudioProcessorEditor* ed) { activeEditor.store (ed); }
    void pushBase64AudioFromBrowser (const juce::String& /*base64*/, double /*sourceSampleRate*/ = 0.0) {}

    bool isDawPlaying() const noexcept { return wasDawPlaying.load(); }
    double getDawBpm() const noexcept { return lastDawBpm.load(); }

    void setPreferredSampleRate (int rate) noexcept;
    int getPreferredSampleRate() const noexcept { return preferredSampleRate.load(); }
    int getEffectiveSampleRate() const noexcept
    {
        int p = preferredSampleRate.load();
        return p > 0 ? p : (bridgeServer.getDawSampleRate() > 0 ? bridgeServer.getDawSampleRate() : 48000);
    }

    int getCachedAssetsCount() const noexcept { return cachedAssetsCount.load(); }
    bool isPageFullyCached() const noexcept { return pageFullyCached.load(); }
    void setPageFullyCached (bool cached, int count = 0)
    {
        pageFullyCached.store (cached);
        cachedAssetsCount.store (count);
    }

    InstrumentBrowserComponent* getOrCreateBrowser();
    InstrumentBrowserComponent* getBrowser() const noexcept { return browser.get(); }
    void createPersistentBrowser();

    void reattachBrowserToHiddenHost();
    void detachBrowserFromHiddenHost();
    juce::Component* getHiddenHost() const noexcept { return hiddenHost.get(); }

private:
    struct HiddenBrowserHost : public juce::Component
    {
        HiddenBrowserHost() { setOpaque (false); }
    };

    WebBridge::WebBridgeServer bridgeServer;
    std::unique_ptr<PulseAudioCaptureThread> pulseCaptureThread;
    std::unique_ptr<InstrumentBrowserComponent> browser;
    std::unique_ptr<HiddenBrowserHost> hiddenHost;

    juce::AudioProcessorValueTreeState apvts;
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    std::atomic<float>* outputGainParam = nullptr;
    std::atomic<float>* bridgeEnabledParam = nullptr;
    std::atomic<float>* sendInputParam = nullptr;

    std::atomic<BrowserInstrumentAudioProcessorEditor*> activeEditor { nullptr };
    std::atomic<bool> wasDawPlaying { false };
    std::atomic<double> lastDawBpm { 120.0 };
    std::atomic<int> preferredSampleRate { 0 };
    std::atomic<int> cachedAssetsCount { 0 };
    std::atomic<bool> pageFullyCached { false };

    juce::String currentUrl { "https://ypc2000.fun/" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrowserInstrumentAudioProcessor)
};
