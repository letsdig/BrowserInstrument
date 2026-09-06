#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

class InstrumentBrowserComponent : public juce::WebBrowserComponent
{
public:
    InstrumentBrowserComponent (const Options& options, std::function<void (const juce::String&)> onUrlChanged)
        : WebBrowserComponent (options), urlChangedCallback (std::move (onUrlChanged))
    {}

    void pageFinishedLoading (const juce::String& url) override
    {
        if (urlChangedCallback)
            urlChangedCallback (url);
    }

private:
    std::function<void (const juce::String&)> urlChangedCallback;
};

class BrowserInstrumentAudioProcessorEditor : public juce::AudioProcessorEditor,
                                              public juce::Timer
{
public:
    explicit BrowserInstrumentAudioProcessorEditor (BrowserInstrumentAudioProcessor&);
    ~BrowserInstrumentAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    void navigateTo (const juce::String& url);
    void downloadCurrentPageOffline();
    void refreshOfflinePresets();
    void openInSystemChrome();

    BrowserInstrumentAudioProcessor& processorRef;

    // Top Navigation Components
    juce::TextButton backButton { "<" };
    juce::TextButton forwardButton { ">" };
    juce::TextButton reloadButton { juce::CharPointer_UTF8 ("\xe2\x86\xbb") }; // ↻
    juce::TextEditor urlEditor;
    juce::TextButton goButton { "GO" };

    // External Chrome & Offline Management
    juce::TextButton openChromeBtn { "Launch Chrome" };
    juce::TextButton downloadBtn { juce::CharPointer_UTF8 ("\xe2\xac\x87 Save Offline") }; // ⬇
    juce::ComboBox offlineCombo;

    // Preset Quick Links
    juce::TextButton ypc2000Btn { "YPC-2000" };
    juce::TextButton acidMachineBtn { "AcidMachine" };
    juce::TextButton webSynthsBtn { "WebSynths" };
    juce::TextButton roland50Btn { "Roland50" };

    // Bottom Status & Telemetry Bar
    juce::Slider gainSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gainAttachment;
    juce::Label gainLabel;

    // Activity state
    bool isConnected = false;
    float currentAudioPeak = 0.0f;
    int midiInFlashFrames = 0;
    int midiOutFlashFrames = 0;
    juce::String statusMessage;

    std::unique_ptr<InstrumentBrowserComponent> browser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrowserInstrumentAudioProcessorEditor)
};
