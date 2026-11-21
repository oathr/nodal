#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include "Params.h"

class NodalProcessor : public juce::AudioProcessor, private juce::AsyncUpdater {
public:
    NodalProcessor();
    ~NodalProcessor() override;

    void prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 1.5; }

    // Factory presets (Presets.h) are the host's programs too.
    int getNumPrograms() override;
    int getCurrentProgram() override { return std::max(0, currentPreset_); }
    int currentPresetOrInit() const { return currentPreset_; }  // -1 = INIT
    void setCurrentProgram(int index) override { applyPreset(index); }
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}
    void applyPreset(int index);
    void resetAll();
    std::function<void()> onPresetChanged;  // message thread, set by the editor

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // --- Wavetables -----------------------------------------------------------------
    // A user table comes from a file; its folder (subfolders included) is the browsing list,
    // stepped through with the arrows. Factory tables are stepped through when one of them
    // is in use. All on the message thread.
    bool loadTableFile(const juce::File& file, juce::String& error);
    void setTableFolder(const juce::File& folder);  // scans it and loads its first table
    void stepTable(int delta);
    void loadTableIndex(int index);
    bool usingUserTable() const;
    const nodal::Wavetable& currentTable() const;
    juce::File tableFolder() const { return folder_; }
    const juce::Array<juce::File>& tableFiles() const { return files_; }
    int tableFileIndex() const;
    juce::String userTableName() const { return userTable_ ? juce::String(userTable_->name) : juce::String(); }
    juce::String userTablePath() const { return userPath_; }
    std::atomic<int> tableVersion{0};  // bumped on every table change (the editor redraws)
    juce::String lastTableError;

    double editorScale = 0.0;  // the window's zoom, saved with the project (0 = not set)
    juce::AudioProcessorValueTreeState apvts;
    nodal::Engine engine;
    std::atomic<float> cpuLoad{0.0f};

private:
    void handleAsyncUpdate() override;  // latency changes, reported from the message thread
    void installUserTable(std::unique_ptr<nodal::Wavetable> table, std::vector<float> samples, int frameSize, const juce::String& path);
    void rescanFolder();
    static int detectFrameSize(const juce::File& file, int64_t length);

    std::vector<std::atomic<float>*> raw_;
    std::vector<float> values_;
    int currentPreset_ = 0;
    std::atomic<int> reportedLatency_{-1};

    std::unique_ptr<nodal::Wavetable> userTable_;
    std::vector<std::unique_ptr<nodal::Wavetable>> retired_;  // freed a load later, never under the audio thread
    std::vector<float> userSamples_;  // the source, saved in the session
    int userFrameSize_ = 2048;
    juce::String userPath_;
    juce::File folder_;
    juce::Array<juce::File> files_;
    juce::AudioFormatManager formats_;

    JUCE_DECLARE_WEAK_REFERENCEABLE(NodalProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NodalProcessor)
};
