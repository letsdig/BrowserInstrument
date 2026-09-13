#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

class BrowserInstrumentAudioProcessorEditor : public juce::AudioProcessorEditor,
                                              private juce::Timer
{
public:
    explicit BrowserInstrumentAudioProcessorEditor (BrowserInstrumentAudioProcessor&);
    ~BrowserInstrumentAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    void navigateTo (const juce::String& url);
    void sendMidiToBrowser (const juce::MidiMessage& msg);
    void onBrowserUrlChanged (const juce::String& url);

private:
    BrowserInstrumentAudioProcessor& processorRef;

    // --- Top Navigation & Control Bar ---
    juce::TextButton backButton    { juce::CharPointer_UTF8 ("\xe2\x97\x80") }; // ◀
    juce::TextButton forwardButton { juce::CharPointer_UTF8 ("\xe2\x96\xb6") }; // ▶
    juce::TextButton reloadButton  { juce::CharPointer_UTF8 ("\xe2\x86\xbb") }; // ⟳
    juce::TextButton homeButton    { juce::CharPointer_UTF8 ("\xe2\x8c\x82") }; // ⌂
    juce::TextEditor urlEditor;
    juce::TextButton goButton      { "GO" };
    juce::ComboBox bufferSizeCombo;

    // --- Bottom Bar: Status, Telemetry, Controls ---
    juce::TextButton flushBtn      { juce::CharPointer_UTF8 ("\xe2\x9f\xb2 Flush") }; // ⟲ Flush
    juce::Label telemetryLabel;
    juce::Slider gainSlider;
    juce::Label gainLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gainAttachment;

    bool isConnected = false;
    float currentAudioPeak = 0.0f;
    int midiInFlashFrames = 0;
    int midiOutFlashFrames = 0;

    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BrowserInstrumentAudioProcessorEditor)
};
