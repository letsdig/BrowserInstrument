#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <functional>

class InstrumentBrowserComponent : public juce::WebBrowserComponent
{
public:
    InstrumentBrowserComponent (const Options& options,
                                std::function<void (const juce::String&)> onUrlChanged = nullptr,
                                std::function<void (const juce::String&)> onPageFinished = nullptr)
        : WebBrowserComponent (options),
          urlChangedCallback (std::move (onUrlChanged)),
          pageFinishedCallback (std::move (onPageFinished))
    {}

    void setUrlChangedCallback (std::function<void (const juce::String&)> cb)
    {
        urlChangedCallback = std::move (cb);
    }

    void setPageFinishedCallback (std::function<void (const juce::String&)> cb)
    {
        pageFinishedCallback = std::move (cb);
    }

    void pageFinishedLoading (const juce::String& url) override
    {
        if (urlChangedCallback)
            urlChangedCallback (url);
        if (pageFinishedCallback)
            pageFinishedCallback (url);
    }

private:
    std::function<void (const juce::String&)> urlChangedCallback;
    std::function<void (const juce::String&)> pageFinishedCallback;
};
