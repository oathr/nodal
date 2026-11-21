#include "PluginEditor.h"

#include <BinaryData.h>

#include "Presets.h"

namespace {

constexpr int kPageWidth = 1280, kPageHeight = 800;  // the page's layout size

std::optional<juce::WebBrowserComponent::Resource> getResource(const juce::String& url) {
    const auto make = [](const char* data, int size, const char* mime) {
        const auto* bytes = reinterpret_cast<const std::byte*>(data);
        return juce::WebBrowserComponent::Resource{std::vector<std::byte>(bytes, bytes + size), mime};
    };
    const auto path = url.upToFirstOccurrenceOf("?", false, false);
    if (path == "/" || path == "/index.html") return make(BinaryData::ui_html, BinaryData::ui_htmlSize, "text/html");
    if (path == "/juce/index.js") return make(BinaryData::index_js, BinaryData::index_jsSize, "text/javascript");
    if (path == "/juce/check_native_interop.js")
        return make(BinaryData::check_native_interop_js, BinaryData::check_native_interop_jsSize, "text/javascript");
    if (path.startsWith("/fonts/")) {  // by original file name
        const auto file = path.fromLastOccurrenceOf("/", false, false);
        for (int i = 0; i < BinaryData::namedResourceListSize; ++i) {
            if (file != BinaryData::originalFilenames[i]) continue;
            int size = 0;
            if (const char* data = BinaryData::getNamedResource(BinaryData::namedResourceList[i], size)) return make(data, size, "font/woff2");
        }
    }
    return std::nullopt;
}

juce::String utf8(const char* s) { return juce::String::fromUTF8(s); }

juce::WebBrowserComponent::Options webOptions() {
    return juce::WebBrowserComponent::Options{}
        .withBackend(juce::WebBrowserComponent::Options::Backend::webview2)
        .withWinWebView2Options(juce::WebBrowserComponent::Options::WinWebView2{}.withUserDataFolder(
            juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("NODAL")))
        .withNativeIntegrationEnabled()
        .withResourceProvider(getResource);
}

}  // namespace

// The page in "plate" view (ui.html?view=plate): the visualiser only, filling a borderless
// window over the whole monitor. Escape or a double-click asks to close (plateClose).
struct NodalEditor::PlateWindow : juce::Component {
    juce::WebBrowserComponent web;

    explicit PlateWindow(std::function<void()> onClose)
        : web(webOptions().withNativeFunction("plateClose", [onClose](const juce::Array<juce::var>&, auto complete) {
              complete(juce::var());
              juce::MessageManager::callAsync(onClose);  // never delete the web view from inside its own call
          })) {
        setOpaque(true);
        addAndMakeVisible(web);
        web.goToURL(juce::WebBrowserComponent::getResourceProviderRoot() + "?view=plate");
    }
    void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(0xff050505)); }
    void resized() override { web.setBounds(getLocalBounds()); }
};

void NodalEditor::togglePlateWindow() {
    if (plateWindow_ != nullptr) return closePlateWindow();
    juce::Component::SafePointer<NodalEditor> self(this);
    plateWindow_ = std::make_unique<PlateWindow>([self] {
        if (self != nullptr) self->closePlateWindow();
    });
    const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(getScreenBounds());
    const auto area = display != nullptr ? display->totalArea : juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->totalArea;
    plateWindow_->setBounds(area);
    plateWindow_->addToDesktop(juce::ComponentPeer::windowAppearsOnTaskbar);
    plateWindow_->setAlwaysOnTop(true);
    plateWindow_->setVisible(true);
    plateWindow_->toFront(true);
    plateWindow_->web.grabKeyboardFocus();
    webView_.emitEventIfBrowserIsVisible("nodal_plate", true);  // the page's own plate rests meanwhile
}

void NodalEditor::closePlateWindow() {
    plateWindow_.reset();
    webView_.emitEventIfBrowserIsVisible("nodal_plate", false);
}

NodalEditor::Relays NodalEditor::createRelays() {
    using namespace nodal::params;
    Relays r;
    for (const Spec& s : all()) {
        switch (s.kind) {
            case Kind::Float: r.sliders.push_back(std::make_unique<juce::WebSliderRelay>(s.id.c_str())); break;
            case Kind::Bool: r.toggles.push_back(std::make_unique<juce::WebToggleButtonRelay>(s.id.c_str())); break;
            case Kind::Choice: r.combos.push_back(std::make_unique<juce::WebComboBoxRelay>(s.id.c_str())); break;
        }
    }
    return r;
}

juce::WebBrowserComponent::Options NodalEditor::createOptions(Relays& relays) {
    auto options = webOptions()
                       .withNativeFunction("getPresets", [this](const juce::Array<juce::var>&, auto complete) {
                           juce::Array<juce::var> list;
                           for (const auto& p : nodal::presets::all()) {
                               auto* item = new juce::DynamicObject();
                               item->setProperty("category", utf8(p.category));
                               item->setProperty("name", utf8(p.name));
                               item->setProperty("note", utf8(p.note));
                               list.add(juce::var(item));
                           }
                           auto* result = new juce::DynamicObject();
                           result->setProperty("list", list);
                           result->setProperty("current", processor_.currentPresetOrInit());
                           complete(juce::var(result));
                       })
                       .withNativeFunction("loadPreset", [this](const juce::Array<juce::var>& args, auto complete) {
                           if (!args.isEmpty()) processor_.applyPreset(int(args[0]));
                           complete(juce::var());
                       })
                       .withNativeFunction("resetAll", [this](const juce::Array<juce::var>&, auto complete) {
                           processor_.resetAll();
                           complete(juce::var());
                       })
                       .withNativeFunction("tableStep", [this](const juce::Array<juce::var>& args, auto complete) {
                           processor_.stepTable(args.isEmpty() ? 1 : int(args[0]));
                           sendTable();
                           complete(juce::var());
                       })
                       .withNativeFunction("tableLoadIndex", [this](const juce::Array<juce::var>& args, auto complete) {
                           if (!args.isEmpty()) processor_.loadTableIndex(int(args[0]));
                           sendTable();
                           complete(juce::var());
                       })
                       .withNativeFunction("tableFolder", [this](const juce::Array<juce::var>&, auto complete) {
                           chooseFolder();
                           complete(juce::var());
                       })
                       .withNativeFunction("tableFile", [this](const juce::Array<juce::var>&, auto complete) {
                           chooseFile();
                           complete(juce::var());
                       })
                       .withNativeFunction("plateFullscreen", [this](const juce::Array<juce::var>&, auto complete) {
                           complete(juce::var());
                           juce::Component::SafePointer<NodalEditor> self(this);
                           juce::MessageManager::callAsync([self] { if (self != nullptr) self->togglePlateWindow(); });
                       })
                       .withNativeFunction("getTableList", [this](const juce::Array<juce::var>&, auto complete) { complete(tableList()); })
                       // The page is up: send it every label and the table once.
                       .withNativeFunction("ready", [this](const juce::Array<juce::var>&, auto complete) {
                           pageReady_ = true;
                           sendTexts(true);
                           sendTable();
                           complete(juce::var());
                       });
    for (auto& s : relays.sliders) options = options.withOptionsFrom(*s);
    for (auto& t : relays.toggles) options = options.withOptionsFrom(*t);
    for (auto& c : relays.combos) options = options.withOptionsFrom(*c);
    return options;
}

NodalEditor::NodalEditor(NodalProcessor& p)
    : AudioProcessorEditor(p), processor_(p), relays_(createRelays()), webView_(createOptions(relays_)) {
    using namespace nodal::params;
    size_t si = 0, ti = 0, ci = 0;
    for (const Spec& s : all()) {
        auto& param = *p.apvts.getParameter(s.id.c_str());
        switch (s.kind) {
            case Kind::Float: sliderAttachments_.push_back(std::make_unique<juce::WebSliderParameterAttachment>(param, *relays_.sliders[si++], nullptr)); break;
            case Kind::Bool: toggleAttachments_.push_back(std::make_unique<juce::WebToggleButtonParameterAttachment>(param, *relays_.toggles[ti++], nullptr)); break;
            case Kind::Choice: comboAttachments_.push_back(std::make_unique<juce::WebComboBoxParameterAttachment>(param, *relays_.combos[ci++], nullptr)); break;
        }
    }
    shownValues_.assign(all().size(), -1.0f);

    processor_.onPresetChanged = [this] { webView_.emitEventIfBrowserIsVisible("nodal_preset", processor_.currentPresetOrInit()); };

    addAndMakeVisible(webView_);
    webView_.goToURL(juce::WebBrowserComponent::getResourceProviderRoot());

    double scale = processor_.editorScale;
    if (scale <= 0.0) scale = juce::PropertiesFile(settingsOptions()).getDoubleValue("editorScale", 0.85);
    scale = juce::jlimit(0.5, 1.5, scale);
    setResizable(true, true);
    setResizeLimits(kPageWidth / 2, kPageHeight / 2, kPageWidth * 3 / 2, kPageHeight * 3 / 2);
    getConstrainer()->setFixedAspectRatio(double(kPageWidth) / kPageHeight);
    setSize(int(kPageWidth * scale), int(kPageHeight * scale));
    startTimerHz(30);
}

NodalEditor::~NodalEditor() {
    stopTimer();
    plateWindow_.reset();
    if (processor_.editorScale > 0.0) {
        juce::PropertiesFile settings(settingsOptions());
        settings.setValue("editorScale", processor_.editorScale);
        settings.saveIfNeeded();
    }
    processor_.onPresetChanged = nullptr;
}

void NodalEditor::resized() {
    webView_.setBounds(getLocalBounds());
    processor_.editorScale = double(getWidth()) / kPageWidth;
}

juce::PropertiesFile::Options NodalEditor::settingsOptions() {
    juce::PropertiesFile::Options o;
    o.applicationName = "settings";
    o.folderName = "NODAL";
    o.filenameSuffix = ".xml";
    o.osxLibrarySubFolder = "Application Support";
    o.millisecondsBeforeSaving = -1;
    return o;
}

void NodalEditor::chooseFolder() {
    auto start = processor_.tableFolder();
    if (!start.isDirectory()) start = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
    chooser_ = std::make_unique<juce::FileChooser>("Wavetable folder", start);
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this](const juce::FileChooser& fc) {
        const auto folder = fc.getResult();
        if (folder.isDirectory()) processor_.setTableFolder(folder);
        sendTable();
    });
}

void NodalEditor::chooseFile() {
    auto start = processor_.tableFolder();
    if (!start.isDirectory()) start = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
    chooser_ = std::make_unique<juce::FileChooser>("Wavetable", start, "*.wav;*.aif;*.aiff;*.flac");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
        const auto file = fc.getResult();
        if (file.existsAsFile()) {
            juce::String error;
            if (!processor_.loadTableFile(file, error)) processor_.lastTableError = error;
            else processor_.lastTableError.clear();
        }
        sendTable();
    });
}

juce::var NodalEditor::tableList() const {
    auto* result = new juce::DynamicObject();
    const auto folder = processor_.tableFolder();
    result->setProperty("folder", folder.getFullPathName());
    juce::Array<juce::var> files;
    for (const auto& f : processor_.tableFiles()) files.add(f.getRelativePathFrom(folder).replaceCharacter('\\', '/'));
    result->setProperty("files", files);
    result->setProperty("current", processor_.tableFileIndex());
    return juce::var(result);
}

// The table on screen: its name, where it comes from, and up to 64 frames of its waveform.
void NodalEditor::sendTable() {
    const auto& t = processor_.currentTable();
    const bool user = processor_.usingUserTable();
    auto* info = new juce::DynamicObject();
    info->setProperty("name", user && processor_.userTableName().isEmpty() ? juce::String("(no file)") : juce::String::fromUTF8(t.name.c_str()));
    info->setProperty("source", user ? "user" : "factory");
    const auto folder = processor_.tableFolder();
    info->setProperty("folder", folder.getFileName());
    info->setProperty("path", user ? juce::File(processor_.userTablePath()).getRelativePathFrom(folder).replaceCharacter('\\', '/') : juce::String());
    info->setProperty("index", user ? processor_.tableFileIndex() : int(std::lround(processor_.apvts.getRawParameterValue("table")->load())));
    info->setProperty("count", user ? processor_.tableFiles().size() : nodal::FactoryTables::get().count());
    info->setProperty("frames", t.frames);
    info->setProperty("error", processor_.lastTableError);
    const int shown = std::min(64, t.frames);
    juce::Array<juce::var> preview;
    for (int f = 0; f < shown; ++f) {
        const int source = shown == t.frames ? f : f * (t.frames - 1) / std::max(1, shown - 1);
        for (int i = 0; i < nodal::kPreviewPoints; i += 2)
            preview.add(std::round(t.preview[size_t(source) * nodal::kPreviewPoints + size_t(i)] * 1000.0f) / 1000.0f);
    }
    info->setProperty("shown", shown);
    info->setProperty("preview", preview);
    webView_.emitEventIfBrowserIsVisible("nodal_table", juce::var(info));
    shownTableVersion_ = processor_.tableVersion.load();
    shownTableIndex_ = int(std::lround(processor_.apvts.getRawParameterValue("table")->load()));
}

// The DAW's label of every parameter whose value moved (all of them on request).
void NodalEditor::sendTexts(bool all) {
    const auto& specs = nodal::params::all();
    auto* texts = new juce::DynamicObject();
    bool any = false;
    for (size_t i = 0; i < specs.size(); ++i) {
        auto* param = processor_.apvts.getParameter(specs[i].id.c_str());
        const float v = param->getValue();
        if (!all && v == shownValues_[i]) continue;
        shownValues_[i] = v;
        texts->setProperty(juce::Identifier(specs[i].id.c_str()), param->getCurrentValueAsText());
        any = true;
    }
    if (any) webView_.emitEventIfBrowserIsVisible("nodal_text", juce::var(texts));
    else delete texts;
}

void NodalEditor::timerCallback() {
    if (!pageReady_) return;
    sendTexts(false);
    if (processor_.tableVersion.load() != shownTableVersion_ ||
        int(std::lround(processor_.apvts.getRawParameterValue("table")->load())) != shownTableIndex_)
        sendTable();

    const auto& r = processor_.engine.readout();
    auto* live = new juce::DynamicObject();
    live->setProperty("f0", r.f0.load());
    live->setProperty("conf", r.confidence.load());
    live->setProperty("level", r.levelDb.load());
    live->setProperty("bright", r.brightness.load());
    live->setProperty("out", r.outLevel.load());
    const int voices = r.voices.load();
    live->setProperty("voices", voices);
    juce::Array<juce::var> midi, pos, lag, gain, harm;
    for (int v = 0; v < voices; ++v) {
        midi.add(r.midi[size_t(v)].load());
        pos.add(r.position[size_t(v)].load());
        lag.add(r.lag[size_t(v)].load());
        gain.add(r.gain[size_t(v)].load());
    }
    for (int k = 0; k < 8; ++k) harm.add(r.harmonics[size_t(k)].load());
    live->setProperty("midi", midi);
    live->setProperty("pos", pos);
    live->setProperty("lag", lag);
    live->setProperty("gain", gain);
    live->setProperty("harm", harm);
    if (++ticks_ % 2 == 0) {
        juce::Array<juce::var> env;
        for (int i = 0; i < nodal::kEnvPoints; i += 2) env.add(std::round(r.env[size_t(i)].load() * 10.0f) / 10.0f);
        live->setProperty("env", env);
    }
    if (ticks_ % 10 == 0) {
        live->setProperty("cpu", processor_.cpuLoad.load() * 100.0f);
        live->setProperty("sampleRate", processor_.getSampleRate());
        live->setProperty("latency", processor_.getLatencySamples());
    }
    // The plate view has no relays: it gets the two values its drawing reads.
    live->setProperty("freeze", processor_.apvts.getRawParameterValue("freeze")->load());
    live->setProperty("spray", processor_.apvts.getParameter("spray")->getValue());
    const juce::var liveVar(live);
    webView_.emitEventIfBrowserIsVisible("nodal_live", liveVar);
    if (plateWindow_ != nullptr) plateWindow_->web.emitEventIfBrowserIsVisible("nodal_live", liveVar);
}
