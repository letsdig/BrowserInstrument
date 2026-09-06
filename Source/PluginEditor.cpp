#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "WebBridge/BridgeScript.h"

BrowserInstrumentAudioProcessorEditor::BrowserInstrumentAudioProcessorEditor (BrowserInstrumentAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    // 1. Configure embedded browser options with injected user script and Chromium User-Agent
    juce::WebBrowserComponent::Options options;
    options = options.withNativeIntegrationEnabled (true)
                     .withKeepPageLoadedWhenBrowserIsHidden()
                     .withUserAgent ("Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/128.0.0.0 Safari/537.36")
                     .withUserScript (WebBridge::getInjectionScript (processorRef.getBridgeServer().getPort()));

    browser = std::make_unique<InstrumentBrowserComponent> (
        options,
        [this] (const juce::String& loadedUrl)
        {
            urlEditor.setText (loadedUrl, false);
            processorRef.setLastLoadedUrl (loadedUrl);
        });

    addAndMakeVisible (*browser);

    // 2. Navigation controls styling
    auto setupNavBtn = [this] (juce::TextButton& btn, const juce::String& tooltip)
    {
        btn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff282c37));
        btn.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff3a4050));
        btn.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
        btn.setTooltip (tooltip);
        addAndMakeVisible (btn);
    };

    setupNavBtn (backButton, "Go Back");
    backButton.onClick = [this] { if (browser) browser->goBack(); };

    setupNavBtn (forwardButton, "Go Forward");
    forwardButton.onClick = [this] { if (browser) browser->goForward(); };

    setupNavBtn (reloadButton, "Refresh Page");
    reloadButton.onClick = [this] { if (browser) browser->refresh(); };

    setupNavBtn (goButton, "Navigate");
    goButton.onClick = [this] { navigateTo (urlEditor.getText()); };

    urlEditor.setText (processorRef.getLastLoadedUrl(), false);
    urlEditor.setFont (juce::FontOptions (13.5f));
    urlEditor.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff14161c));
    urlEditor.setColour (juce::TextEditor::textColourId, juce::Colour (0xffe5e7eb));
    urlEditor.setColour (juce::TextEditor::outlineColourId, juce::Colour (0xff333847));
    urlEditor.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colour (0xff4f46e5));
    urlEditor.onReturnKey = [this] { navigateTo (urlEditor.getText()); };
    addAndMakeVisible (urlEditor);

    // 3. Open in System Chrome Button
    setupNavBtn (openChromeBtn, "Launch in System Google Chrome (App Mode)");
    openChromeBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff1e3a8a));
    openChromeBtn.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff93c5fd));
    openChromeBtn.onClick = [this] { openInSystemChrome(); };

    // 4. Download / Offline Button
    setupNavBtn (downloadBtn, "Download current web instrument for offline use");
    downloadBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff065f46));
    downloadBtn.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff6ee7b7));
    downloadBtn.onClick = [this] { downloadCurrentPageOffline(); };

    // 5. Offline Presets Combo Box
    offlineCombo.setTextWhenNothingSelected ("Offline Presets (Localhost)");
    offlineCombo.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff1e222d));
    offlineCombo.setColour (juce::ComboBox::textColourId, juce::Colour (0xffcbd5e1));
    offlineCombo.setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff333847));
    offlineCombo.onChange = [this]
    {
        const int selectedId = offlineCombo.getSelectedId();
        if (selectedId > 0)
        {
            juce::String presetName = offlineCombo.getItemText (offlineCombo.getSelectedItemIndex());
            juce::String localUrl = "http://127.0.0.1:" + juce::String (processorRef.getBridgeServer().getPort())
                                  + "/offline/" + presetName + "/index.html";
            navigateTo (localUrl);
        }
    };
    addAndMakeVisible (offlineCombo);
    refreshOfflinePresets();

    // 6. Preset Quick Bookmarks
    auto setupPresetBtn = [this] (juce::TextButton& btn, const juce::String& url)
    {
        btn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff202430));
        btn.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff32384a));
        btn.setColour (juce::TextButton::textColourOffId, juce::Colour (0xff818cf8));
        btn.onClick = [this, url] { navigateTo (url); };
        addAndMakeVisible (btn);
    };

    setupPresetBtn (ypc2000Btn, "https://ypc2000.fun/");
    setupPresetBtn (acidMachineBtn, "https://acid-machine.com/");
    setupPresetBtn (webSynthsBtn, "https://websynths.com/");
    setupPresetBtn (roland50Btn, "https://roland50.studio/");

    // 7. Output Gain Slider
    gainSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    gainSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 50, 18);
    gainSlider.setColour (juce::Slider::thumbColourId, juce::Colour (0xff6366f1));
    gainSlider.setColour (juce::Slider::trackColourId, juce::Colour (0xff2a2e3d));
    gainSlider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (0xff14161c));
    gainSlider.setColour (juce::Slider::textBoxTextColourId, juce::Colours::white);
    gainSlider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colour (0xff2e3344));
    addAndMakeVisible (gainSlider);

    gainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processorRef.getAPVTS(), "gain", gainSlider);

    gainLabel.setText ("OUTPUT GAIN", juce::dontSendNotification);
    gainLabel.setFont (juce::FontOptions (11.0f).withStyle ("Bold"));
    gainLabel.setColour (juce::Label::textColourId, juce::Colour (0xff9ca3af));
    addAndMakeVisible (gainLabel);

    // Initial navigation
    navigateTo (processorRef.getLastLoadedUrl());

    // Window setup
    setResizable (true, true);
    setResizeLimits (850, 600, 2560, 1440);
    setSize (1120, 760);

    // Start 30 Hz timer for status, peak meters, and LED blinking
    startTimerHz (30);
}

BrowserInstrumentAudioProcessorEditor::~BrowserInstrumentAudioProcessorEditor()
{
    stopTimer();
}

void BrowserInstrumentAudioProcessorEditor::navigateTo (const juce::String& url)
{
    juce::String target = url.trim();
    if (!target.startsWithIgnoreCase ("http://") && !target.startsWithIgnoreCase ("https://") && !target.startsWithIgnoreCase ("file://"))
    {
        target = "https://" + target;
    }

    urlEditor.setText (target, false);
    processorRef.setLastLoadedUrl (target);

    if (browser)
    {
        browser->goToURL (target);
    }
}

void BrowserInstrumentAudioProcessorEditor::openInSystemChrome()
{
    juce::String targetUrl = urlEditor.getText().trim();
    if (targetUrl.isEmpty())
        targetUrl = "https://ypc2000.fun/";

    // Check for google-chrome or chromium
    juce::String chromeBin = "/usr/bin/google-chrome";
    if (!juce::File (chromeBin).existsAsFile())
        chromeBin = "/usr/bin/chromium-browser";
    if (!juce::File (chromeBin).existsAsFile())
        chromeBin = "/usr/bin/chromium";

    // Launch in Chrome App Mode for dedicated borderless window
    juce::String cmd = chromeBin + " --app=\"" + targetUrl + "\" &";
    int res = std::system (cmd.toRawUTF8());
    juce::ignoreUnused (res);

    statusMessage = "Chrome App Mode Launched!";
}

void BrowserInstrumentAudioProcessorEditor::downloadCurrentPageOffline()
{
    juce::String targetUrl = urlEditor.getText().trim();
    if (targetUrl.isEmpty() || targetUrl.startsWithIgnoreCase ("file://") || targetUrl.contains ("127.0.0.1"))
    {
        statusMessage = "Please navigate to a valid web URL first";
        return;
    }

    juce::URL url (targetUrl);
    juce::String domain = url.getDomain();
    if (domain.isEmpty())
        domain = "instrument";

    domain = domain.replaceCharacter ('.', '_');
    auto offlineDir = WebBridge::WebBridgeServer::getOfflineDirectory().getChildFile (domain);
    offlineDir.createDirectory();

    statusMessage = "Downloading " + domain + " offline...";

    // Run wget in background thread
    juce::Thread::launch ([this, targetUrl, offlineDir, domain]()
    {
        juce::String cmd = "wget -q -E -k -p -N -P \"" + offlineDir.getFullPathName() + "\" \"" + targetUrl + "\"";
        int res = std::system (cmd.toRawUTF8());

        juce::MessageManager::callAsync ([this, res, domain]()
        {
            if (res == 0)
            {
                statusMessage = "Downloaded: " + domain + " (Localhost ready)";
                refreshOfflinePresets();
            }
            else
            {
                statusMessage = "Download completed with code " + juce::String (res);
                refreshOfflinePresets();
            }
        });
    });
}

void BrowserInstrumentAudioProcessorEditor::refreshOfflinePresets()
{
    offlineCombo.clear (juce::dontSendNotification);
    auto presets = processorRef.getBridgeServer().getOfflinePresets();
    for (int i = 0; i < presets.size(); ++i)
    {
        offlineCombo.addItem (presets[i], i + 1);
    }
}

void BrowserInstrumentAudioProcessorEditor::timerCallback()
{
    auto& bridge = processorRef.getBridgeServer();

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

    repaint (0, getHeight() - 32, getWidth(), 32);
}

void BrowserInstrumentAudioProcessorEditor::paint (juce::Graphics& g)
{
    const int w = getWidth();
    const int h = getHeight();

    // 1. Top bar background
    juce::ColourGradient topGrad (juce::Colour (0xff181a22), 0, 0,
                                  juce::Colour (0xff101217), 0, 78, false);
    g.setGradientFill (topGrad);
    g.fillRect (0, 0, w, 78);

    g.setColour (juce::Colour (0xff282c38));
    g.drawHorizontalLine (77, 0.0f, (float)w);

    // 2. Bottom status bar background
    juce::ColourGradient botGrad (juce::Colour (0xff101217), 0, (float)(h - 32),
                                  juce::Colour (0xff0a0b0e), 0, (float)h, false);
    g.setGradientFill (botGrad);
    g.fillRect (0, h - 32, w, 32);

    g.setColour (juce::Colour (0xff202430));
    g.drawHorizontalLine (h - 32, 0.0f, (float)w);

    // Status Indicator: Connection to Browser Web Audio Bridge
    float statusX = 14.0f;
    float statusY = (float)(h - 20);

    juce::Colour dotCol = isConnected ? juce::Colour (0xff22c55e) : juce::Colour (0xfff59e0b);
    g.setColour (dotCol);
    g.fillEllipse (statusX, statusY, 9.0f, 9.0f);

    g.setFont (juce::FontOptions (12.0f).withStyle ("SemiBold"));
    g.setColour (juce::Colour (0xffd1d5db));
    juce::String statusText = isConnected ? "BRIDGE CONNECTED" : "BRIDGE STANDBY";
    if (statusMessage.isNotEmpty())
        statusText = statusMessage;

    g.drawText (statusText + " (Port " + juce::String (processorRef.getBridgeServer().getPort()) + ")",
                (int)statusX + 16, h - 30, 280, 28, juce::Justification::centredLeft);

    // MIDI IN LED
    int midiInX = 330;
    juce::Colour midiInCol = (midiInFlashFrames > 0) ? juce::Colour (0xff38bdf8) : juce::Colour (0xff1f293d);
    g.setColour (midiInCol);
    g.fillRoundedRectangle ((float)midiInX, (float)(h - 22), 12.0f, 12.0f, 3.0f);
    g.setColour (juce::Colour (0xff9ca3af));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("MIDI IN", midiInX + 16, h - 30, 55, 28, juce::Justification::centredLeft);

    // MIDI OUT LED
    int midiOutX = 415;
    juce::Colour midiOutCol = (midiOutFlashFrames > 0) ? juce::Colour (0xfffb923c) : juce::Colour (0xff33251c);
    g.setColour (midiOutCol);
    g.fillRoundedRectangle ((float)midiOutX, (float)(h - 22), 12.0f, 12.0f, 3.0f);
    g.setColour (juce::Colour (0xff9ca3af));
    g.drawText ("MIDI OUT", midiOutX + 16, h - 30, 60, 28, juce::Justification::centredLeft);

    // Audio Output Peak Meter
    int meterX = 510;
    int meterWidth = 140;
    int meterHeight = 10;
    int meterY = h - 21;

    g.setColour (juce::Colour (0xff181c26));
    g.fillRoundedRectangle ((float)meterX, (float)meterY, (float)meterWidth, (float)meterHeight, 2.0f);

    float peakClamped = juce::jlimit (0.0f, 1.0f, currentAudioPeak);
    if (peakClamped > 0.01f)
    {
        juce::ColourGradient meterGrad (juce::Colour (0xff22c55e), (float)meterX, 0,
                                        juce::Colour (0xffef4444), (float)(meterX + meterWidth), 0, false);
        meterGrad.addColour (0.75, juce::Colour (0xffeab308));
        g.setGradientFill (meterGrad);
        g.fillRoundedRectangle ((float)meterX, (float)meterY, (float)meterWidth * peakClamped, (float)meterHeight, 2.0f);
    }

    g.setColour (juce::Colour (0xff9ca3af));
    g.drawText ("AUDIO OUT", meterX + meterWidth + 8, h - 30, 75, 28, juce::Justification::centredLeft);
}

void BrowserInstrumentAudioProcessorEditor::resized()
{
    const int w = getWidth();
    const int h = getHeight();

    // Row 1 (Navigation & Actions, y: 6 to 36)
    int x = 8;
    const int r1Y = 6;
    const int btnH = 28;

    backButton.setBounds (x, r1Y, 30, btnH);
    x += 34;
    forwardButton.setBounds (x, r1Y, 30, btnH);
    x += 34;
    reloadButton.setBounds (x, r1Y, 30, btnH);
    x += 38;

    int rightActionsWidth = 370;
    int urlWidth = w - x - rightActionsWidth - 10;
    if (urlWidth < 180) urlWidth = 180;

    urlEditor.setBounds (x, r1Y, urlWidth, btnH);
    x += urlWidth + 6;

    goButton.setBounds (x, r1Y, 40, btnH);
    x += 46;

    openChromeBtn.setBounds (x, r1Y, 115, btnH);
    x += 121;

    downloadBtn.setBounds (x, r1Y, 105, btnH);
    x += 111;

    offlineCombo.setBounds (x, r1Y, w - x - 8, btnH);

    // Row 2 (Quick Presets, y: 40 to 70)
    const int r2Y = 40;
    const int r2H = 26;
    int px = 8;
    const int numPresets = 4;
    const int presetW = juce::jmin (180, (w - 16) / numPresets);

    ypc2000Btn.setBounds (px, r2Y, presetW - 6, r2H);
    px += presetW;
    acidMachineBtn.setBounds (px, r2Y, presetW - 6, r2H);
    px += presetW;
    webSynthsBtn.setBounds (px, r2Y, presetW - 6, r2H);
    px += presetW;
    roland50Btn.setBounds (px, r2Y, presetW - 6, r2H);

    // Browser Component fills middle (from y: 78 to h - 32)
    if (browser)
    {
        browser->setBounds (0, 78, w, h - 78 - 32);
    }

    // Bottom bar layout
    int gainW = 160;
    gainSlider.setBounds (w - gainW - 14, h - 28, gainW, 24);
    gainLabel.setBounds (w - gainW - 100, h - 30, 90, 28);
}
