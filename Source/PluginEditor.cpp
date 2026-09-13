#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "WebBridge/BridgeScript.h"

BrowserInstrumentAudioProcessorEditor::BrowserInstrumentAudioProcessorEditor (BrowserInstrumentAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    processorRef.setEditor (this);

    // 1. Attach persistent browser owned by AudioProcessor (so it keeps playing even when closed!)
    auto* browser = processorRef.getOrCreateBrowser();
    if (browser != nullptr)
    {
        if (processorRef.getHiddenHost() != nullptr)
            processorRef.getHiddenHost()->setVisible (false);

        addAndMakeVisible (*browser);
        browser->setVisible (true);
    }

    // 2. Navigation controls styling
    auto setupNavBtn = [this] (juce::TextButton& btn, const juce::String& tooltip)
    {
        btn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff22252c));
        btn.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff373c47));
        btn.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffd1d5db));
        btn.setTooltip (tooltip);
        addAndMakeVisible (btn);
    };

    setupNavBtn (backButton, "Go Back");
    backButton.onClick = [this] { if (auto* b = processorRef.getBrowser()) b->goBack(); };

    setupNavBtn (forwardButton, "Go Forward");
    forwardButton.onClick = [this] { if (auto* b = processorRef.getBrowser()) b->goForward(); };

    setupNavBtn (reloadButton, "Refresh Page");
    reloadButton.onClick = [this]
    {
        if (auto* b = processorRef.getBrowser())
            b->goToURL (urlEditor.getText().trim());
    };

    setupNavBtn (homeButton, "Home (YPC-2000)");
    homeButton.onClick = [this]
    {
        navigateTo ("https://ypc2000.fun/");
    };

    setupNavBtn (goButton, "Load URL");
    goButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff082f36));
    goButton.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff00f4f4));
    goButton.onClick = [this] { navigateTo (urlEditor.getText().trim()); };

    // Cyan LCD Digital URL Editor
    juce::String initialUrl = processorRef.getLastLoadedUrl();
    if (initialUrl.isEmpty() || initialUrl == "https://strudel.cc/")
        initialUrl = "https://ypc2000.fun/";
    urlEditor.setText (initialUrl, false);
    urlEditor.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::bold)));
    urlEditor.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff021b1b));
    urlEditor.setColour (juce::TextEditor::textColourId, juce::Colour (0xff00f4f4)); // Cyan LCD text
    urlEditor.setColour (juce::TextEditor::outlineColourId, juce::Colour (0xff004848));
    urlEditor.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colour (0xff00ffff));
    urlEditor.onReturnKey = [this] { navigateTo (urlEditor.getText().trim()); };
    addAndMakeVisible (urlEditor);

    // 3. Output Buffer Cushion Selector (ultra-low latency options)
    bufferSizeCombo.addItem ("BUFFER: 64 smp (~1.3ms) [ZERO]", 1);
    bufferSizeCombo.addItem ("BUFFER: 128 smp (~2.6ms) [ULTRA-LOW]", 2);
    bufferSizeCombo.addItem ("BUFFER: 256 smp (~5.3ms) [FAST]", 3);
    bufferSizeCombo.addItem ("BUFFER: 512 smp (~10ms) [SAFE]", 4);
    bufferSizeCombo.addItem ("BUFFER: 1024 smp (~21ms)", 5);
    bufferSizeCombo.addItem ("BUFFER: 2048 smp (~43ms)", 6);

    int currentCushion = processorRef.getBridgeServer().getJitterCushionSamples();
    if (currentCushion <= 64)         bufferSizeCombo.setSelectedId (1, juce::dontSendNotification);
    else if (currentCushion <= 128)   bufferSizeCombo.setSelectedId (2, juce::dontSendNotification);
    else if (currentCushion <= 256)   bufferSizeCombo.setSelectedId (3, juce::dontSendNotification);
    else if (currentCushion <= 512)   bufferSizeCombo.setSelectedId (4, juce::dontSendNotification);
    else if (currentCushion <= 1024)  bufferSizeCombo.setSelectedId (5, juce::dontSendNotification);
    else                              bufferSizeCombo.setSelectedId (6, juce::dontSendNotification);

    bufferSizeCombo.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff12141c));
    bufferSizeCombo.setColour (juce::ComboBox::textColourId, juce::Colour (0xff00f4f4));
    bufferSizeCombo.setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff004848));
    bufferSizeCombo.setColour (juce::ComboBox::arrowColourId, juce::Colour (0xff00f4f4));
    bufferSizeCombo.setTooltip ("Ultra-low latency buffer cushion selector");

    bufferSizeCombo.onChange = [this]
    {
        int id = bufferSizeCombo.getSelectedId();
        int smp = 256;
        if (id == 1) smp = 64;
        else if (id == 2) smp = 128;
        else if (id == 3) smp = 256;
        else if (id == 4) smp = 512;
        else if (id == 5) smp = 1024;
        else if (id == 6) smp = 2048;

        processorRef.getBridgeServer().setJitterCushionSamples (smp);
    };
    addAndMakeVisible (bufferSizeCombo);

    // 7. Flush Buffer Button
    flushBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff1e293b));
    flushBtn.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff38bdf8));
    flushBtn.setTooltip ("Flush jitter buffer instantly");
    flushBtn.onClick = [this]
    {
        processorRef.getBridgeServer().flushAudioBuffer();
    };
    addAndMakeVisible (flushBtn);

    // 8. Telemetry LCD readout
    telemetryLabel.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 11.5f, juce::Font::bold)));
    telemetryLabel.setColour (juce::Label::backgroundColourId, juce::Colour (0xff021b1b));
    telemetryLabel.setColour (juce::Label::textColourId, juce::Colour (0xff00f4f4));
    telemetryLabel.setColour (juce::Label::outlineColourId, juce::Colour (0xff004848));
    telemetryLabel.setJustificationType (juce::Justification::centred);
    telemetryLabel.setText ("DAW: INITIALIZING...", juce::dontSendNotification);
    addAndMakeVisible (telemetryLabel);

    // 9. Output Gain Slider
    gainSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    gainSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 46, 18);
    gainSlider.setColour (juce::Slider::thumbColourId, juce::Colour (0xff00f4f4));
    gainSlider.setColour (juce::Slider::trackColourId, juce::Colour (0xff082f36));
    gainSlider.setColour (juce::Slider::backgroundColourId, juce::Colour (0xff12151b));
    gainSlider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (0xff021b1b));
    gainSlider.setColour (juce::Slider::textBoxTextColourId, juce::Colour (0xff00f4f4));
    gainSlider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colour (0xff004848));
    addAndMakeVisible (gainSlider);

    gainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processorRef.getAPVTS(), "gain", gainSlider);

    gainLabel.setText ("GAIN", juce::dontSendNotification);
    gainLabel.setFont (juce::Font (juce::FontOptions (10.5f).withStyle ("Bold")));
    gainLabel.setColour (juce::Label::textColourId, juce::Colour (0xff94a3b8));
    gainLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (gainLabel);

    // Window setup
    setResizable (true, true);
    setResizeLimits (850, 600, 2560, 1440);
    setSize (1120, 760);

    startTimerHz (30);
}

BrowserInstrumentAudioProcessorEditor::~BrowserInstrumentAudioProcessorEditor()
{
    stopTimer();
    processorRef.setEditor (nullptr);
    processorRef.reattachBrowserToHiddenHost();
}

void BrowserInstrumentAudioProcessorEditor::onBrowserUrlChanged (const juce::String& url)
{
    urlEditor.setText (url, false);
}

void BrowserInstrumentAudioProcessorEditor::sendMidiToBrowser (const juce::MidiMessage& msg)
{
    if (auto* b = processorRef.getBrowser())
    {
        if (msg.getRawDataSize() < 1)
            return;

        const auto* raw = msg.getRawData();
        int status = raw[0];
        int d1 = msg.getRawDataSize() > 1 ? raw[1] : 0;
        int d2 = msg.getRawDataSize() > 2 ? raw[2] : 0;

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

void BrowserInstrumentAudioProcessorEditor::navigateTo (const juce::String& url)
{
    juce::String target = url.trim();
    if (target.isEmpty())
        return;

    if (!target.startsWithIgnoreCase ("http://") && !target.startsWithIgnoreCase ("https://") && !target.startsWithIgnoreCase ("file://"))
    {
        if (target.startsWith ("localhost") || target.startsWith ("127.0.0.1"))
            target = "http://" + target;
        else
            target = "https://" + target;
    }

    urlEditor.setText (target, false);
    processorRef.setLastLoadedUrl (target);

    if (auto* b = processorRef.getBrowser())
    {
        b->goToURL (target);
    }
}

void BrowserInstrumentAudioProcessorEditor::timerCallback()
{
    auto& bridge = processorRef.getBridgeServer();
    bridge.tickWatchdog();

    isConnected = bridge.isClientConnected();

    float peak = bridge.getPeakAudioOutLevel();
    currentAudioPeak = currentAudioPeak * 0.75f + peak * 0.25f;

    if (bridge.checkAndResetMidiInActivity())
        midiInFlashFrames = 6;
    else if (midiInFlashFrames > 0)
        midiInFlashFrames--;

    if (bridge.checkAndResetMidiOutActivity())
        midiOutFlashFrames = 6;
    else if (midiOutFlashFrames > 0)
        midiOutFlashFrames--;

    // Update Telemetry LCD display (constant width string, never vibrates on audio peak)
    double sr = processorRef.getEffectiveSampleRate() / 1000.0;
    int cushion = bridge.getJitterCushionSamples();
    const bool isPlaying = processorRef.isDawPlaying();

    juce::String stateStr = isPlaying ? "SYNC: RUN" : "SYNC: STOP";
    juce::String telem = "DAW: " + juce::String (sr, 1) + "k | " + stateStr + " | BUF: " + juce::String (cushion) + "s";
    telemetryLabel.setText (telem, juce::dontSendNotification);

    repaint (0, getHeight() - 34, getWidth(), 34);
}

void BrowserInstrumentAudioProcessorEditor::paint (juce::Graphics& g)
{
    const int w = getWidth();
    const int h = getHeight();

    // 1. Top bar background - dark titanium brushed texture
    const int topBarH = 42;
    juce::ColourGradient topGrad (juce::Colour (0xff1c2028), 0, 0,
                                  juce::Colour (0xff12151b), 0, (float) topBarH, false);
    g.setGradientFill (topGrad);
    g.fillRect (0, 0, w, topBarH);

    g.setColour (juce::Colour (0xff2b313d));
    g.drawHorizontalLine (topBarH - 1, 0.0f, (float)w);

    // 2. Bottom status bar background
    juce::ColourGradient botGrad (juce::Colour (0xff12151b), 0, (float)(h - 34),
                                  juce::Colour (0xff0a0c10), 0, (float)h, false);
    g.setGradientFill (botGrad);
    g.fillRect (0, h - 34, w, 34);

    g.setColour (juce::Colour (0xff232834));
    g.drawHorizontalLine (h - 34, 0.0f, (float)w);

    // Status Indicator Dot
    float statusX = 14.0f;
    float statusY = (float)(h - 22);

    juce::Colour dotCol = isConnected ? juce::Colour (0xff22c55e) : juce::Colour (0xff00f4f4);
    g.setColour (dotCol);
    g.fillEllipse (statusX, statusY, 10.0f, 10.0f);

    g.setFont (juce::FontOptions (11.0f).withStyle ("SemiBold"));
    g.setColour (juce::Colour (0xffd1d5db));
    juce::String statusText = isConnected ? "DAW CHANNEL ROUTED" : "DAW READY";
    g.drawText (statusText, (int)statusX + 16, h - 31, 150, 28, juce::Justification::centredLeft);

    // MIDI IN LED
    int midiInX = 185;
    juce::Colour midiInCol = (midiInFlashFrames > 0) ? juce::Colour (0xff38bdf8) : juce::Colour (0xff1b2434);
    g.setColour (midiInCol);
    g.fillRoundedRectangle ((float)midiInX, (float)(h - 23), 12.0f, 12.0f, 3.0f);
    g.setColour (juce::Colour (0xff94a3b8));
    g.setFont (juce::FontOptions (10.5f));
    g.drawText ("MIDI IN", midiInX + 16, h - 31, 48, 28, juce::Justification::centredLeft);

    // MIDI OUT LED
    int midiOutX = 255;
    juce::Colour midiOutCol = (midiOutFlashFrames > 0) ? juce::Colour (0xfffb923c) : juce::Colour (0xff2f221a);
    g.setColour (midiOutCol);
    g.fillRoundedRectangle ((float)midiOutX, (float)(h - 23), 12.0f, 12.0f, 3.0f);
    g.setColour (juce::Colour (0xff94a3b8));
    g.drawText ("MIDI OUT", midiOutX + 16, h - 31, 55, 28, juce::Justification::centredLeft);

    // Audio Output Peak Meter
    int meterX = 330;
    int meterWidth = 85;
    int meterHeight = 10;
    int meterY = h - 22;

    g.setColour (juce::Colour (0xff151821));
    g.fillRoundedRectangle ((float)meterX, (float)meterY, (float)meterWidth, (float)meterHeight, 2.0f);

    float peakClamped = juce::jlimit (0.0f, 1.0f, currentAudioPeak);
    if (peakClamped > 0.001f)
    {
        juce::ColourGradient meterGrad (juce::Colour (0xff22c55e), (float)meterX, 0,
                                        juce::Colour (0xffef4444), (float)(meterX + meterWidth), 0, false);
        meterGrad.addColour (0.75, juce::Colour (0xffeab308));
        g.setGradientFill (meterGrad);
        g.fillRoundedRectangle ((float)meterX, (float)meterY, (float)meterWidth * peakClamped, (float)meterHeight, 2.0f);
    }

    // Separate fixed dB readout right beside the meter
    float dbVal = (currentAudioPeak > 0.0001f) ? juce::Decibels::gainToDecibels (currentAudioPeak) : -96.0f;
    juce::String dbStr = (dbVal <= -95.0f) ? "-inf" : juce::String (dbVal, 1);
    g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 10.5f, juce::Font::bold)));
    g.setColour (juce::Colour (0xff94a3b8));
    g.drawText (dbStr + " dB", meterX + meterWidth + 6, h - 31, 54, 28, juce::Justification::centredLeft);
}

void BrowserInstrumentAudioProcessorEditor::paintOverChildren (juce::Graphics& g)
{
    // Draw 4 titanium corner screws for industrial aesthetic
    auto drawScrew = [&g] (int cx, int cy)
    {
        g.setColour (juce::Colour (0xff151820));
        g.fillEllipse ((float)(cx - 5), (float)(cy - 5), 10.0f, 10.0f);
        g.setColour (juce::Colour (0xff4b5563));
        g.drawEllipse ((float)(cx - 5), (float)(cy - 5), 10.0f, 10.0f, 1.0f);
        g.setColour (juce::Colour (0xff6b7280));
        g.drawLine ((float)(cx - 3), (float)(cy - 3), (float)(cx + 3), (float)(cy + 3), 1.2f);
    };

    drawScrew (8, 8);
    drawScrew (getWidth() - 8, 8);
    drawScrew (8, getHeight() - 8);
    drawScrew (getWidth() - 8, getHeight() - 8);
}

void BrowserInstrumentAudioProcessorEditor::resized()
{
    const int w = getWidth();
    const int h = getHeight();

    // Top Control Bar (y: 7, height: 28)
    const int topBarH = 42;
    const int rY = 7;
    const int btnH = 28;

    int leftX = 16;
    backButton.setBounds (leftX, rY, 28, btnH);
    leftX += 32;
    forwardButton.setBounds (leftX, rY, 28, btnH);
    leftX += 32;
    reloadButton.setBounds (leftX, rY, 28, btnH);
    leftX += 32;
    homeButton.setBounds (leftX, rY, 28, btnH);
    leftX += 36;

    const int comboW = 210;
    const int goW = 44;

    bufferSizeCombo.setBounds (w - comboW - 16, rY, comboW, btnH);

    const int rightTaken = comboW + 24;
    const int availUrlW = w - rightTaken - leftX - goW - 14;

    urlEditor.setBounds (leftX, rY, juce::jmax (180, availUrlW), btnH);
    goButton.setBounds (leftX + juce::jmax (180, availUrlW) + 6, rY, goW, btnH);

    // Middle: Embedded Browser Component (full space!)
    const int botBarH = 34;
    if (auto* b = processorRef.getBrowser())
    {
        b->setBounds (0, topBarH, w, h - topBarH - botBarH);
    }

    // Bottom telemetry bar:
    const int botY = h - 29;
    flushBtn.setBounds (476, botY, 60, 24);

    int gainW = 140;
    gainSlider.setBounds (w - gainW - 16, botY, gainW, 24);
    gainLabel.setBounds (w - gainW - 68, h - 31, 50, 28);

    int rightEdgeOfFlush = 544;
    int leftEdgeOfGain = w - gainW - 75;
    int telemW = juce::jmax (120, leftEdgeOfGain - rightEdgeOfFlush);
    telemetryLabel.setBounds (rightEdgeOfFlush, h - 30, telemW, 26);
}
