#include "PluginProcessor.h"

#include "PluginEditor.h"
#include "Presets.h"

namespace {

// The DAW's view: each section's controls in a group, every value as text with its unit,
// and typing a value back ("-12 st", "300 ms") finds the position that shows it.
juce::String groupOf(const std::string& id) {
    static const std::vector<std::pair<std::vector<std::string>, const char*>> groups = {
        {{"pitch_mode", "transpose", "fine", "glide", "quantize", "key", "fixed_note", "range_low", "range_high"}, "PITCH"},
        {{"table", "position", "track", "formant", "formant_shift"}, "TABLE"},
        {{"voices", "detune", "spread", "width"}, "UNISON"},
        {{"spray", "grain", "freeze"}, "SPRAY"},
        {{"release", "gate", "voiced", "sensitivity"}, "DYNAMICS"},
        {{"sub", "breath", "breath_mode", "tone", "drive"}, "COLOR"}};
    for (const auto& [ids, name] : groups)
        if (std::find(ids.begin(), ids.end(), id) != ids.end()) return name;
    return "OUTPUT";
}

float valueFromText(const std::function<std::string(float)>& text, const juce::String& typed) {
    const double wanted = typed.retainCharacters("+-.0123456789").getDoubleValue();
    float best = 0.0f;
    double bestDistance = 1.0e30;
    for (int i = 0; i <= 1000; ++i) {
        const float t = float(i) / 1000.0f;
        const double shown = juce::String(text(t)).retainCharacters("+-.0123456789").getDoubleValue();
        if (std::abs(shown - wanted) < bestDistance) bestDistance = std::abs(shown - wanted), best = t;
    }
    return best;
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout() {
    using namespace nodal::params;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    std::unique_ptr<juce::AudioProcessorParameterGroup> group;
    int groups = 0;
    for (const Spec& s : all()) {
        const auto name = groupOf(s.id);
        if (group == nullptr || group->getName() != name) {
            if (group != nullptr) layout.add(std::move(group));
            group = std::make_unique<juce::AudioProcessorParameterGroup>(name.toLowerCase() + juce::String(++groups), name, " | ");
        }
        const juce::ParameterID id{s.id.c_str(), 1};
        switch (s.kind) {
            case Kind::Float: {
                auto text = s.text;
                auto attributes = juce::AudioParameterFloatAttributes().withStringFromValueFunction([text](float v, int) {
                    return text ? juce::String::fromUTF8(text(v).c_str()) : juce::String(v, 2);
                });
                if (text) attributes = attributes.withValueFromStringFunction([text](const juce::String& typed) { return valueFromText(text, typed); });
                group->addChild(std::make_unique<juce::AudioParameterFloat>(id, juce::String(s.name), juce::NormalisableRange<float>(0.0f, 1.0f),
                                                                            s.defaultValue, attributes));
                break;
            }
            case Kind::Bool:
                group->addChild(std::make_unique<juce::AudioParameterBool>(id, juce::String(s.name), s.defaultValue > 0.5f));
                break;
            case Kind::Choice: {
                juce::StringArray choices;
                for (const auto& c : s.choices) choices.add(c);
                // ALIGNED / LIVE changes the latency, and every change restarts the plugin in
                // the host: not automatable (measured: automated, it stalled the audio for ms).
                const auto attributes = juce::AudioParameterChoiceAttributes().withAutomatable(s.id != "latency");
                group->addChild(std::make_unique<juce::AudioParameterChoice>(id, juce::String(s.name), choices, int(s.defaultValue), attributes));
                break;
            }
        }
    }
    if (group != nullptr) layout.add(std::move(group));
    return layout;
}

constexpr int kStateVersion = 1;
// The menu's last item ("User"). From the names: no table is built at load time.
int userTableIndex() { return int(nodal::FactoryTables::names().size()); }

juce::PropertiesFile::Options globalSettings() {
    juce::PropertiesFile::Options o;
    o.applicationName = "settings";
    o.folderName = "NODAL";
    o.filenameSuffix = ".xml";
    o.osxLibrarySubFolder = "Application Support";
    o.millisecondsBeforeSaving = -1;
    return o;
}

}  // namespace

NodalProcessor::NodalProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "NODAL", createLayout()) {
    for (const auto& s : nodal::params::all()) raw_.push_back(apvts.getRawParameterValue(s.id.c_str()));
    values_.resize(raw_.size());
    nodal::params::toSettings(values_.data());  // builds its index map here, not on the audio thread
    nodal::FactoryTables::get();                // ~30 ms once per process, never on the audio thread
    formats_.registerBasicFormats();
    folder_ = juce::File(juce::PropertiesFile(globalSettings()).getValue("tableFolder"));
    applyPreset(0);
}

NodalProcessor::~NodalProcessor() {
    cancelPendingUpdate();
    engine.setUserTable(nullptr);
}

bool NodalProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto out = layouts.getMainOutputChannelSet();
    const auto in = layouts.getMainInputChannelSet();
    return out == juce::AudioChannelSet::stereo() && (in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono());
}

void NodalProcessor::prepareToPlay(double sampleRate, int) {
    for (size_t i = 0; i < raw_.size(); ++i) values_[i] = raw_[i]->load(std::memory_order_relaxed);
    engine.prepare(float(sampleRate));
    engine.setSettings(nodal::params::toSettings(values_.data()));
    engine.reset();
    const int latency = engine.latencySamples();
    reportedLatency_ = latency;
    setLatencySamples(latency);
}

void NodalProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;
    const auto start = juce::Time::getHighResolutionTicks();
    const int numSamples = buffer.getNumSamples();
    if (getTotalNumInputChannels() == 1) buffer.copyFrom(1, 0, buffer, 0, 0, numSamples);

    for (size_t i = 0; i < raw_.size(); ++i) values_[i] = raw_[i]->load(std::memory_order_relaxed);
    engine.setSettings(nodal::params::toSettings(values_.data()));
    if (engine.latencySamples() != reportedLatency_.load(std::memory_order_relaxed)) triggerAsyncUpdate();
    engine.process(buffer.getWritePointer(0), buffer.getWritePointer(1), numSamples);

    const double elapsed = juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - start);
    const double budget = numSamples / std::max(1.0, getSampleRate());
    cpuLoad.store(cpuLoad.load(std::memory_order_relaxed) * 0.95f + float(elapsed / budget) * 0.05f, std::memory_order_relaxed);
}

void NodalProcessor::handleAsyncUpdate() {
    const bool aligned = int(std::lround(apvts.getRawParameterValue("latency")->load())) == 0;
    const int latency = engine.latencySamples(aligned);
    if (latency == reportedLatency_.load()) return;
    reportedLatency_ = latency;
    setLatencySamples(latency);
}

juce::AudioProcessorEditor* NodalProcessor::createEditor() { return new NodalEditor(*this); }

int NodalProcessor::getNumPrograms() { return int(nodal::presets::all().size()); }

const juce::String NodalProcessor::getProgramName(int index) {
    const auto& list = nodal::presets::all();
    if (index < 0 || index >= int(list.size())) return {};
    return juce::String::fromUTF8(list[size_t(index)].category) + " / " + juce::String::fromUTF8(list[size_t(index)].name);
}

void NodalProcessor::applyPreset(int index) {
    const auto& list = nodal::presets::all();
    if (index < 0 || index >= int(list.size())) return;
    const auto values = nodal::presets::valuesOf(list[size_t(index)]);
    const auto& specs = nodal::params::all();
    for (size_t i = 0; i < specs.size(); ++i)
        if (auto* param = apvts.getParameter(specs[i].id.c_str())) param->setValueNotifyingHost(param->convertTo0to1(values[i]));
    currentPreset_ = index;
    ++tableVersion;
    juce::MessageManager::callAsync([safe = juce::WeakReference<NodalProcessor>(this)] {
        if (safe != nullptr && safe->onPresetChanged) safe->onPresetChanged();
    });
}

void NodalProcessor::resetAll() {
    for (auto* param : getParameters())
        if (auto* p = dynamic_cast<juce::RangedAudioParameter*>(param)) p->setValueNotifyingHost(p->getDefaultValue());
    currentPreset_ = -1;
    ++tableVersion;
    juce::MessageManager::callAsync([safe = juce::WeakReference<NodalProcessor>(this)] {
        if (safe != nullptr && safe->onPresetChanged) safe->onPresetChanged();
    });
}

// --- Wavetables ---------------------------------------------------------------------

bool NodalProcessor::usingUserTable() const { return int(std::lround(apvts.getRawParameterValue("table")->load())) >= userTableIndex(); }

const nodal::Wavetable& NodalProcessor::currentTable() const {
    const int index = int(std::lround(apvts.getRawParameterValue("table")->load()));
    if (index >= userTableIndex()) return userTable_ ? *userTable_ : nodal::FactoryTables::get().table(0);
    return nodal::FactoryTables::get().table(index);
}

// Serum and most editors write 2048-sample frames and say so in a "clm " chunk
// ("<!>2048 ..."). Without it: 2048 when the length is a multiple, else one single cycle
// for a short file.
int NodalProcessor::detectFrameSize(const juce::File& file, int64_t length) {
    if (file.hasFileExtension("wav")) {
        juce::FileInputStream in(file);
        if (in.openedOk() && in.getTotalLength() > 12) {
            char riff[12];
            in.read(riff, 12);
            while (!in.isExhausted()) {
                char id[4];
                if (in.read(id, 4) != 4) break;
                const auto size = uint32_t(in.readInt());
                if (std::memcmp(id, "clm ", 4) == 0 && size < 256) {
                    juce::MemoryBlock text;
                    in.readIntoMemoryBlock(text, juce::ssize_t(size));
                    const auto s = text.toString();
                    const int at = s.indexOf("<!>");
                    if (at >= 0) {
                        const int frame = s.substring(at + 3).getIntValue();
                        if (frame >= 16 && frame <= 8192) return frame;
                    }
                    break;
                }
                if (!in.setPosition(in.getPosition() + int64_t(size) + (size & 1))) break;
            }
        }
    }
    if (length >= 2048 && length % 2048 == 0) return 2048;
    if (length <= 8192) return int(length);
    return 2048;
}

bool NodalProcessor::loadTableFile(const juce::File& file, juce::String& error) {
    std::unique_ptr<juce::AudioFormatReader> reader(formats_.createReaderFor(file));
    if (reader == nullptr) return error = "cannot read " + file.getFileName(), false;
    const int64_t length = std::min<int64_t>(reader->lengthInSamples, int64_t(nodal::kMaxFrames) * 8192);
    if (length < 8) return error = file.getFileName() + " is empty", false;
    juce::AudioBuffer<float> buffer(int(std::max(1u, reader->numChannels)), int(length));
    reader->read(&buffer, 0, int(length), 0, true, true);
    std::vector<float> mono(size_t(length), 0.0f);
    const float g = 1.0f / float(buffer.getNumChannels());
    for (int c = 0; c < buffer.getNumChannels(); ++c)
        for (int i = 0; i < int(length); ++i) mono[size_t(i)] += buffer.getSample(c, i) * g;
    const int frameSize = detectFrameSize(file, length);
    auto table = nodal::Wavetable::fromSamples(mono.data(), mono.size(), frameSize, file.getFileNameWithoutExtension().toStdString());
    if (table == nullptr) return error = file.getFileName() + " is too short", false;
    installUserTable(std::move(table), std::move(mono), frameSize, file.getFullPathName());
    // A file outside the browsing folder makes its own folder the new one.
    if (folder_ == juce::File() || !file.isAChildOf(folder_)) {
        folder_ = file.getParentDirectory();
        files_.clear();
    }
    if (files_.isEmpty()) rescanFolder();
    if (auto* p = apvts.getParameter("table")) p->setValueNotifyingHost(p->convertTo0to1(float(userTableIndex())));
    ++tableVersion;
    return true;
}

void NodalProcessor::installUserTable(std::unique_ptr<nodal::Wavetable> table, std::vector<float> samples, int frameSize, const juce::String& path) {
    engine.setUserTable(table.get());
    if (userTable_ != nullptr) retired_.push_back(std::move(userTable_));
    while (retired_.size() > 4) retired_.erase(retired_.begin());  // long out of the audio thread's hands
    userTable_ = std::move(table);
    userSamples_ = std::move(samples);
    userFrameSize_ = frameSize;
    userPath_ = path;
}

void NodalProcessor::rescanFolder() {
    files_.clear();
    if (!folder_.isDirectory()) return;
    auto found = folder_.findChildFiles(juce::File::findFiles, true, "*.wav;*.aif;*.aiff;*.flac");
    struct ByPath {
        juce::File root;
        int compareElements(const juce::File& a, const juce::File& b) const {
            return a.getRelativePathFrom(root).compareNatural(b.getRelativePathFrom(root));
        }
    } order{folder_};
    found.sort(order);
    for (int i = 0; i < std::min(found.size(), 20000); ++i) files_.add(found[i]);
    juce::PropertiesFile settings(globalSettings());
    settings.setValue("tableFolder", folder_.getFullPathName());
    settings.saveIfNeeded();
}

void NodalProcessor::setTableFolder(const juce::File& folder) {
    folder_ = folder;
    rescanFolder();
    if (!files_.isEmpty()) loadTableIndex(0);
    ++tableVersion;
}

int NodalProcessor::tableFileIndex() const { return userPath_.isEmpty() ? -1 : files_.indexOf(juce::File(userPath_)); }

void NodalProcessor::loadTableIndex(int index) {
    if (files_.isEmpty()) return;
    // Unreadable files are skipped, in the direction of travel.
    const int n = files_.size();
    for (int tries = 0; tries < std::min(n, 32); ++tries) {
        const int i = ((index + tries) % n + n) % n;
        juce::String error;
        if (loadTableFile(files_[i], error)) {
            lastTableError.clear();
            return;
        }
        lastTableError = error;
    }
}

void NodalProcessor::stepTable(int delta) {
    if (usingUserTable()) {
        if (files_.isEmpty()) rescanFolder();
        if (files_.isEmpty()) return;
        const int at = tableFileIndex();
        const int n = files_.size();
        int next = at < 0 ? (delta > 0 ? 0 : n - 1) : ((at + delta) % n + n) % n;
        for (int tries = 0; tries < std::min(n, 32); ++tries, next = ((next + delta) % n + n) % n) {
            juce::String error;
            if (loadTableFile(files_[next], error)) {
                lastTableError.clear();
                return;
            }
            lastTableError = error;
        }
        return;
    }
    const int count = userTableIndex();
    const int index = int(std::lround(apvts.getRawParameterValue("table")->load()));
    if (auto* p = apvts.getParameter("table")) p->setValueNotifyingHost(p->convertTo0to1(float(((index + delta) % count + count) % count)));
    ++tableVersion;
}

// --- State ----------------------------------------------------------------------------
// Parameters, the current preset, the window's zoom, and the user table itself (16-bit, so
// a session opens anywhere, even without the file), with its path and browsing folder.

void NodalProcessor::getStateInformation(juce::MemoryBlock& destData) {
    auto state = apvts.copyState();
    state.setProperty("version", kStateVersion, nullptr);
    state.setProperty("preset", currentPreset_, nullptr);
    state.setProperty("editorScale", editorScale, nullptr);
    state.setProperty("tableFolder", folder_.getFullPathName(), nullptr);
    state.removeChild(state.getChildWithName("USERTABLE"), nullptr);
    if (userTable_ != nullptr && !userSamples_.empty()) {
        float peak = 1.0e-9f;
        for (const float s : userSamples_) peak = std::max(peak, std::abs(s));
        juce::MemoryBlock pcm(userSamples_.size() * 2);
        auto* out = static_cast<int16_t*>(pcm.getData());
        for (size_t i = 0; i < userSamples_.size(); ++i) out[i] = int16_t(std::lround(userSamples_[i] / peak * 32767.0f));
        juce::ValueTree t("USERTABLE");
        t.setProperty("name", juce::String(userTable_->name), nullptr);
        t.setProperty("path", userPath_, nullptr);
        t.setProperty("frameSize", userFrameSize_, nullptr);
        t.setProperty("data", pcm.toBase64Encoding(), nullptr);
        state.addChild(t, -1, nullptr);
    }
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, destData);
}

void NodalProcessor::setStateInformation(const void* data, int sizeInBytes) {
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr || !xml->hasTagName(apvts.state.getType())) return;
    auto state = juce::ValueTree::fromXml(*xml);
    currentPreset_ = int(state.getProperty("preset", -1));
    editorScale = double(state.getProperty("editorScale", 0.0));
    const juce::String folder = state.getProperty("tableFolder").toString();
    if (folder.isNotEmpty()) {
        folder_ = juce::File(folder);
        files_.clear();  // scanned when first browsed
    }
    const auto t = state.getChildWithName("USERTABLE");
    if (t.isValid()) {
        juce::MemoryBlock pcm;
        if (pcm.fromBase64Encoding(t.getProperty("data").toString()) && pcm.getSize() >= 16) {
            const size_t n = pcm.getSize() / 2;
            const auto* in = static_cast<const int16_t*>(pcm.getData());
            std::vector<float> samples(n);
            for (size_t i = 0; i < n; ++i) samples[i] = float(in[i]) / 32767.0f;
            const int frameSize = int(t.getProperty("frameSize", 2048));
            auto table = nodal::Wavetable::fromSamples(samples.data(), n, frameSize, t.getProperty("name").toString().toStdString());
            if (table != nullptr) installUserTable(std::move(table), std::move(samples), frameSize, t.getProperty("path").toString());
        }
    }
    state.removeChild(t, nullptr);
    apvts.replaceState(state);
    ++tableVersion;
    juce::MessageManager::callAsync([safe = juce::WeakReference<NodalProcessor>(this)] {
        if (safe != nullptr && safe->onPresetChanged) safe->onPresetChanged();
    });
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new NodalProcessor(); }
