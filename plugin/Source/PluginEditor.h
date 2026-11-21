#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#include "PluginProcessor.h"

// The UI is design/ui.html in the system web view (WebView2 on Windows, WebKit on macOS).
// Every parameter has a relay: page and host stay in sync both ways. 30 times a second the
// page gets the live state (pitch, voices, harmonics, envelope) that drives the Chladni plate.
class NodalEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit NodalEditor(NodalProcessor&);
    ~NodalEditor() override;

    void resized() override;

private:
    struct Relays {
        std::vector<std::unique_ptr<juce::WebSliderRelay>> sliders;
        std::vector<std::unique_ptr<juce::WebToggleButtonRelay>> toggles;
        std::vector<std::unique_ptr<juce::WebComboBoxRelay>> combos;
    };
    static Relays createRelays();
    juce::WebBrowserComponent::Options createOptions(Relays&);
    void timerCallback() override;
    void sendTexts(bool all);
    void sendTable();
    void chooseFolder();
    void chooseFile();
    juce::var tableList() const;
    static juce::PropertiesFile::Options settingsOptions();

    NodalProcessor& processor_;
    Relays relays_;  // must exist before the web view
    juce::WebBrowserComponent webView_;
    std::vector<std::unique_ptr<juce::WebSliderParameterAttachment>> sliderAttachments_;
    std::vector<std::unique_ptr<juce::WebToggleButtonParameterAttachment>> toggleAttachments_;
    std::vector<std::unique_ptr<juce::WebComboBoxParameterAttachment>> comboAttachments_;
    std::unique_ptr<juce::FileChooser> chooser_;

    std::vector<float> shownValues_;
    int shownTableVersion_ = -1, shownTableIndex_ = -1, ticks_ = 0;
    bool pageReady_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NodalEditor)
};
