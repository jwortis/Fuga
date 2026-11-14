#include "MainComponent.h"

using namespace fuga;

static const Tick snapValues[] = { PPQ, PPQ / 2, PPQ / 4, PPQ / 8, PPQ / 3, PPQ / 6 };
static const char* const snapNames[] = { "1/4", "1/8", "1/16", "1/32", "1/8 triplet", "1/16 triplet" };

MainComponent::MainComponent()
{
    setWantsKeyboardFocus (true);
    getLookAndFeel().setColour (juce::ResizableWindow::backgroundColourId, theme::bg);

    for (auto* b : { &playButton, &recordButton, &rewindButton, &metronomeButton, &generateButton, &addVoiceButton, &zoomOut, &zoomIn })
    {
        addAndMakeVisible (*b);
        b->setColour (juce::TextButton::buttonColourId, theme::panel.brighter (0.12f));
        b->setWantsKeyboardFocus (false);
    }
    playButton.setTooltip ("Play or stop (Space)");
    playButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff3d9a5f));
    playButton.onClick = [this] { togglePlay(); };
    recordButton.setTooltip ("Record from your MIDI keyboard into the selected voice (R)");
    recordButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffd9534f));
    recordButton.onClick = [this] { record(); };
    rewindButton.setTooltip ("Back to the start (Return)");
    rewindButton.onClick = [this] { engine.setPosition (0); scrollToTick (0); };
    metronomeButton.setClickingTogglesState (true);
    metronomeButton.setColour (juce::TextButton::buttonOnColourId, theme::accent.darker (0.3f));
    metronomeButton.setTooltip ("Metronome click during playback and recording");
    metronomeButton.onClick = [this] { engine.setMetronome (metronomeButton.getToggleState()); };
    generateButton.setTooltip ("Build a full exposition from the selected subject clip: answers, countersubject and free counterpoint in every voice");
    generateButton.onClick = [this] { generateExpositionFromSelection (true); };
    addVoiceButton.onClick = [this] { addVoice(); };
    zoomOut.onClick = [this] { setZoom (doc.pixelsPerBeat / 1.3); };
    zoomIn.onClick = [this] { setZoom (doc.pixelsPerBeat * 1.3); };

    auto setupLabel = [this] (juce::Label& l, const char* text)
    {
        addAndMakeVisible (l);
        l.setText (text, juce::dontSendNotification);
        l.setColour (juce::Label::textColourId, theme::textDim);
        l.setFont (juce::FontOptions (12.0f));
        l.setJustificationType (juce::Justification::centredRight);
    };
    setupLabel (tempoLabel, "Tempo");
    setupLabel (keyLabel, "Key");
    setupLabel (snapLabel, "Snap");
    setupLabel (zoomLabel, "Zoom");

    addAndMakeVisible (tempo);
    tempo.setSliderStyle (juce::Slider::IncDecButtons);
    tempo.setIncDecButtonsMode (juce::Slider::incDecButtonsDraggable_Vertical);
    tempo.setRange (30, 240, 1);
    tempo.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 44, 24);
    tempo.setWantsKeyboardFocus (false);
    tempo.onValueChange = [this]
    {
        if (std::abs (doc.project.bpm - tempo.getValue()) > 0.01)
            doc.edit ([&] (Project& p) { p.bpm = tempo.getValue(); });
    };

    addAndMakeVisible (keyBox);
    for (int i = 0; i < 24; ++i)
        keyBox.addItem (keyName ({ i % 12, i < 12 ? Mode::Major : Mode::Minor }), i + 1);
    keyBox.setWantsKeyboardFocus (false);
    keyBox.onChange = [this]
    {
        const int i = keyBox.getSelectedId() - 1;
        if (i < 0) return;
        Key k { i % 12, i < 12 ? Mode::Major : Mode::Minor };
        if (! (k == doc.project.key)) doc.edit ([&] (Project& p) { p.key = k; });
    };

    addAndMakeVisible (snapBox);
    for (int i = 0; i < 6; ++i) snapBox.addItem (snapNames[i], i + 1);
    snapBox.setSelectedId (3, juce::dontSendNotification);
    snapBox.setWantsKeyboardFocus (false);
    snapBox.onChange = [this]
    {
        doc.snap = snapValues[juce::jlimit (0, 5, snapBox.getSelectedId() - 1)];
        doc.sendSynchronousChangeMessage();
    };

    addAndMakeVisible (timeline);
    addAndMakeVisible (pianoRoll);
    addAndMakeVisible (keyboard);
    keyboard.setAvailableRange (24, 108);
    keyboard.setWantsKeyboardFocus (false);
    keyboard.setOctaveForMiddleC (4);

    addAndMakeVisible (problemsTitle);
    problemsTitle.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    problemsTitle.setColour (juce::Label::textColourId, theme::text);
    addAndMakeVisible (problems);
    problems.setModel (this);
    problems.setRowHeight (40);
    problems.setColour (juce::ListBox::backgroundColourId, theme::panel);
    addAndMakeVisible (status);
    status.setColour (juce::Label::textColourId, theme::textDim);
    status.setFont (juce::FontOptions (12.0f));

    timeline.onManagePlugins = [this] { showPluginManager(); };

    // Keep both editors on the same time axis.
    timeline.viewport.onScroll = [this]
    {
        if (syncingScroll) return;
        juce::ScopedValueSetter<bool> guard (syncingScroll, true);
        pianoRoll.viewport.setViewPosition (timeline.viewport.getViewPositionX(), pianoRoll.viewport.getViewPositionY());
    };
    pianoRoll.viewport.onScroll = [this]
    {
        pianoRoll.repaint();
        if (syncingScroll) return;
        juce::ScopedValueSetter<bool> guard (syncingScroll, true);
        timeline.viewport.setViewPosition (pianoRoll.viewport.getViewPositionX(), 0);
    };

    doc.addChangeListener (this);
    engine.syncProject (doc.project);
    engine.setLiveVoice (doc.selectedVoice);
    updateToolbar();
    setSize (1320, 860);
    startTimerHz (30);
   #if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu (this);
   #endif
}

MainComponent::~MainComponent()
{
   #if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu (nullptr);
   #endif
    doc.removeChangeListener (this);
    problems.setModel (nullptr);
}

void MainComponent::paint (juce::Graphics& g) { g.fillAll (theme::bg); }

void MainComponent::resized()
{
    auto r = getLocalBounds();
    auto bar = r.removeFromTop (44).reduced (10, 8);
    rewindButton.setBounds (bar.removeFromLeft (36));
    bar.removeFromLeft (4);
    playButton.setBounds (bar.removeFromLeft (64));
    bar.removeFromLeft (4);
    recordButton.setBounds (bar.removeFromLeft (70));
    bar.removeFromLeft (4);
    metronomeButton.setBounds (bar.removeFromLeft (52));
    tempoLabel.setBounds (bar.removeFromLeft (52));
    tempo.setBounds (bar.removeFromLeft (96));
    keyLabel.setBounds (bar.removeFromLeft (36));
    keyBox.setBounds (bar.removeFromLeft (104));
    snapLabel.setBounds (bar.removeFromLeft (42));
    snapBox.setBounds (bar.removeFromLeft (112));
    zoomLabel.setBounds (bar.removeFromLeft (46));
    zoomOut.setBounds (bar.removeFromLeft (28));
    bar.removeFromLeft (2);
    zoomIn.setBounds (bar.removeFromLeft (28));
    generateButton.setBounds (bar.removeFromRight (160));
    bar.removeFromRight (6);
    addVoiceButton.setBounds (bar.removeFromRight (70));

    status.setBounds (r.removeFromBottom (22).reduced (10, 0));
    keyboard.setBounds (r.removeFromBottom (58).withTrimmedLeft (theme::headerWidth));
    timeline.setBounds (r.removeFromTop (juce::jmin (timeline.preferredHeight(), r.getHeight() / 2 + 60)));
    r.removeFromTop (3);
    auto side = r.removeFromRight (310);
    problemsTitle.setBounds (side.removeFromTop (26).reduced (8, 0));
    problems.setBounds (side);
    pianoRoll.setBounds (r);
}

// ---------------------------------------------------------------- updates

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    engine.syncProject (doc.project);
    engine.setLiveVoice (doc.selectedVoice);
    updateToolbar();
    problems.updateContent();
    problems.repaint();
    if (timeline.getHeight() != juce::jmin (timeline.preferredHeight(), (getHeight() - 124) / 2 + 60)) resized();
}

void MainComponent::updateToolbar()
{
    tempo.setValue (doc.project.bpm, juce::dontSendNotification);
    keyBox.setSelectedId (doc.project.key.tonic + (doc.project.key.mode == Mode::Major ? 0 : 12) + 1, juce::dontSendNotification);
    auto* c = doc.clip();
    generateButton.setEnabled (c != nullptr && ! c->notes.empty());

    int errors = 0, warnings = 0, infos = 0;
    for (auto& v : doc.violations)
        (v.severity == Severity::Error ? errors : v.severity == Severity::Warning ? warnings : infos)++;
    problemsTitle.setText (doc.violations.empty() ? "Fugue check: all clear"
                               : "Fugue check: " + juce::String (errors) + " errors, " + juce::String (warnings) + " warnings, " + juce::String (infos) + " notes",
                           juce::dontSendNotification);
    updateTitle();
}

void MainComponent::updateTitle()
{
    if (onTitleChanged)
        onTitleChanged ((doc.file == juce::File() ? juce::String ("Untitled") : doc.file.getFileNameWithoutExtension()) + (doc.dirty ? " *" : "") + " - Fuga");
}

void MainComponent::timerCallback()
{
    const bool playing = engine.isPlaying();
    if (playing || wasPlaying)
    {
        timeline.playheadMoved();
        pianoRoll.playheadMoved();
    }
    if (playing)
    {
        // Follow the playhead.
        const int x = (int) doc.xForTick (engine.getPosition());
        auto& vp = timeline.viewport;
        if (x > vp.getViewPositionX() + vp.getViewWidth() - 40 || x < vp.getViewPositionX())
            vp.setViewPosition (juce::jmax (0, x - 60), 0);
    }
    if (wasPlaying && ! playing) stopAndCollectRecording(); // playback ran off the end
    wasPlaying = playing;
    playButton.setToggleState (playing, juce::dontSendNotification);
    playButton.setButtonText (playing ? "Stop" : "Play");
    recordButton.setToggleState (engine.isRecording(), juce::dontSendNotification);

    const Tick t = engine.getPosition();
    const Tick bar = doc.project.barLength();
    auto midi = engine.midiInputNames();
    status.setText ("Bar " + juce::String (t / bar + 1) + ", beat " + juce::String ((t % bar) / PPQ + 1)
                        + "      MIDI in: " + (midi.isEmpty() ? juce::String ("none connected (click the on-screen keyboard to play)") : midi.joinIntoString (", "))
                        + "      Live voice: " + (doc.voice() != nullptr ? juce::String (doc.voice()->name) : juce::String ("-")),
                    juce::dontSendNotification);
}

// ---------------------------------------------------------------- transport

void MainComponent::togglePlay()
{
    if (engine.isPlaying())
    {
        engine.stop();
        stopAndCollectRecording();
        wasPlaying = false;
    }
    else
    {
        if (engine.getPosition() >= doc.project.endTime()) engine.setPosition (0);
        engine.play();
    }
}

void MainComponent::record()
{
    if (engine.isPlaying()) { togglePlay(); return; }
    recordStart = engine.getPosition();
    engine.startRecording();
}

void MainComponent::stopAndCollectRecording()
{
    auto recorded = engine.takeRecording();
    if (recorded.empty() || doc.voice() == nullptr) return;

    const Tick grid = doc.snap;
    const Tick bar = doc.project.barLength();
    Tick first = recorded.front().start, last = 0;
    for (auto& n : recorded)
    {
        n.start = doc.snapRound (n.start, grid);
        n.length = std::max (grid, doc.snapRound (n.length, grid));
        first = std::min (first, n.start);
        last = std::max (last, n.start + n.length);
    }
    int newId = -1;
    const int voiceId = doc.selectedVoice;
    doc.edit ([&] (Project& p)
    {
        bool hasSubject = false;
        for (auto& v : p.voices) for (auto& c : v.clips) hasSubject = hasSubject || c.role == Role::Subject;
        Clip c;
        c.id = newId = p.newId();
        c.role = hasSubject ? Role::Free : Role::Subject;
        if (auto* lv = p.findVoice (voiceId); lv != nullptr && lv->lock) c.role = *lv->lock;
        c.start = (first / bar) * bar;
        c.length = std::max (bar, ((last - c.start + bar - 1) / bar) * bar);
        for (auto& n : recorded) c.notes.push_back ({ n.start - c.start, n.length, n.pitch, juce::jlimit (1, 127, n.velocity) });
        p.findVoice (voiceId)->clips.push_back (std::move (c));
    });
    doc.select (voiceId, newId);
}

// ---------------------------------------------------------------- commands

void MainComponent::generateExpositionFromSelection (bool tonal)
{
    auto* c = doc.clip();
    if (c == nullptr)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Generate exposition",
                                                "Select the clip that holds your subject first.");
        return;
    }
    ExpositionResult result;
    const int voiceId = doc.selectedVoice, clipId = doc.selectedClip;
    doc.edit ([&] (Project& p) { result = generateExposition (p, voiceId, clipId, tonal, (unsigned) juce::Random::getSystemRandom().nextInt()); });
    if (! result.ok)
    {
        doc.undo();
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Generate exposition", result.error);
    }
}

void MainComponent::addVoice()
{
    doc.edit ([&] (Project& p)
    {
        Voice v;
        v.id = p.newId();
        v.name = "Voice " + std::to_string (p.voices.size() + 1);
        v.low = 48;
        v.high = 72;
        p.voices.push_back (v);
    });
}

void MainComponent::setZoom (double ppb)
{
    auto& vp = timeline.viewport;
    const Tick centre = doc.tickForX (vp.getViewPositionX() + vp.getViewWidth() / 2);
    doc.pixelsPerBeat = juce::jlimit (4.0, 160.0, ppb);
    doc.sendSynchronousChangeMessage();
    vp.setViewPosition (juce::jmax (0, (int) doc.xForTick (centre) - vp.getViewWidth() / 2), 0);
}

void MainComponent::scrollToTick (Tick t)
{
    auto& vp = timeline.viewport;
    const int x = (int) doc.xForTick (t);
    if (x < vp.getViewPositionX() || x > vp.getViewPositionX() + vp.getViewWidth() - 80)
        vp.setViewPosition (juce::jmax (0, x - vp.getViewWidth() / 3), 0);
}

void MainComponent::confirmDiscard (std::function<void()> then)
{
    if (! doc.dirty) { then(); return; }
    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                      .withIconType (juce::MessageBoxIconType::QuestionIcon)
                                      .withTitle ("Unsaved changes")
                                      .withMessage ("Save your changes to this fugue first?")
                                      .withButton ("Save")
                                      .withButton ("Discard")
                                      .withButton ("Cancel")
                                      .withAssociatedComponent (this),
                                  [this, then] (int result)
                                  {
                                      if (result == 1) save (false, then);
                                      else if (result == 2) then();
                                  });
}

void MainComponent::newProject()
{
    confirmDiscard ([this] { engine.stop(); engine.setPosition (0); doc.newProject(); });
}

void MainComponent::open()
{
    confirmDiscard ([this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Open a fugue", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.fuga");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f == juce::File()) return;
            engine.stop();
            engine.setPosition (0);
            if (! doc.load (f))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Open", "That file could not be read as a Fuga project.");
        });
    });
}

void MainComponent::save (bool saveAs, std::function<void()> then)
{
    auto write = [this, then] (const juce::File& f)
    {
        engine.captureStates (doc.project);
        if (! doc.save (f))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Save", "The file could not be written.");
        else if (then)
            then();
    };
    if (! saveAs && doc.file != juce::File()) { write (doc.file); return; }
    chooser = std::make_unique<juce::FileChooser> ("Save fugue", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Untitled.fuga"), "*.fuga");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                          [write] (const juce::FileChooser& fc)
                          {
                              auto f = fc.getResult();
                              if (f != juce::File()) write (f.withFileExtension ("fuga"));
                          });
}

void MainComponent::showAudioSettings()
{
    auto* selector = new juce::AudioDeviceSelectorComponent (engine.deviceManager, 0, 0, 2, 2, true, false, true, false);
    selector->setSize (520, 420);
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (selector);
    o.dialogTitle = "Audio and MIDI settings";
    o.dialogBackgroundColour = theme::panel;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.launchAsync();
}

void MainComponent::showPluginManager()
{
    auto deadMans = engine.settings->getFile().getSiblingFile ("PluginScanCrashGuard");
    auto* list = new juce::PluginListComponent (engine.formatManager, engine.knownPlugins, deadMans, engine.settings.get(), true);
    list->setSize (720, 480);
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (list);
    o.dialogTitle = "Plugins (use Options to scan for VST3 and AU instruments)";
    o.dialogBackgroundColour = theme::panel;
    o.useNativeTitleBar = true;
    o.resizable = true;
    o.launchAsync();
}

// ---------------------------------------------------------------- menus and keys

juce::StringArray MainComponent::getMenuBarNames() { return { "File", "Edit", "Fugue", "Settings" }; }

juce::PopupMenu MainComponent::getMenuForIndex (int index, const juce::String&)
{
    juce::PopupMenu m;
    if (index == 0)
    {
        m.addItem ("New", [this] { newProject(); });
        m.addItem ("Open...", [this] { open(); });
        m.addSeparator();
        m.addItem ("Save", [this] { save (false); });
        m.addItem ("Save As...", [this] { save (true); });
    }
    else if (index == 1)
    {
        m.addItem ("Undo", doc.canUndo(), false, [this] { doc.undo(); });
        m.addItem ("Redo", doc.canRedo(), false, [this] { doc.redo(); });
    }
    else if (index == 2)
    {
        const bool hasClip = doc.clip() != nullptr;
        m.addItem ("Generate exposition from selected subject (tonal answer)", hasClip, false, [this] { generateExpositionFromSelection (true); });
        m.addItem ("Generate exposition from selected subject (real answer)", hasClip, false, [this] { generateExpositionFromSelection (false); });
        m.addSeparator();
        m.addItem ("Add voice", [this] { addVoice(); });
    }
    else
    {
        m.addItem ("Audio and MIDI...", [this] { showAudioSettings(); });
        m.addItem ("Plugins...", [this] { showPluginManager(); });
    }
    return m;
}

bool MainComponent::keyPressed (const juce::KeyPress& k)
{
    const auto cmd = juce::ModifierKeys::commandModifier;
    if (k == juce::KeyPress::spaceKey) { togglePlay(); return true; }
    if (k == juce::KeyPress::returnKey) { engine.setPosition (0); scrollToTick (0); timeline.playheadMoved(); pianoRoll.playheadMoved(); return true; }
    if (k == juce::KeyPress ('z', cmd | juce::ModifierKeys::shiftModifier, 0)) { doc.redo(); return true; }
    if (k == juce::KeyPress ('z', cmd, 0)) { doc.undo(); return true; }
    if (k == juce::KeyPress ('s', cmd | juce::ModifierKeys::shiftModifier, 0)) { save (true); return true; }
    if (k == juce::KeyPress ('s', cmd, 0)) { save (false); return true; }
    if (k == juce::KeyPress ('o', cmd, 0)) { open(); return true; }
    if (k == juce::KeyPress ('n', cmd, 0)) { newProject(); return true; }
    if (k == juce::KeyPress ('r', 0, 0)) { record(); return true; }
    if (k == juce::KeyPress ('=', cmd, 0)) { setZoom (doc.pixelsPerBeat * 1.3); return true; }
    if (k == juce::KeyPress ('-', cmd, 0)) { setZoom (doc.pixelsPerBeat / 1.3); return true; }
    return false;
}

// ---------------------------------------------------------------- problems list

int MainComponent::getNumRows() { return (int) doc.violations.size(); }

void MainComponent::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= (int) doc.violations.size()) return;
    auto& v = doc.violations[(size_t) row];
    if (selected) g.fillAll (theme::panel.brighter (0.15f));
    g.setColour (theme::severity (v.severity));
    g.fillEllipse (9.0f, 8.0f, 8.0f, 8.0f);
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
    const Tick bar = doc.project.barLength();
    g.drawText (juce::String (v.rule) + "   bar " + juce::String (v.time / bar + 1), 24, 3, w - 28, 16, juce::Justification::centredLeft, true);
    g.setColour (theme::textDim);
    g.setFont (11.5f);
    g.drawText (v.message, 24, 19, w - 28, 16, juce::Justification::centredLeft, true);
    g.setColour (theme::bg);
    g.fillRect (0, h - 1, w, 1);
}

void MainComponent::selectedRowsChanged (int row)
{
    if (row < 0 || row >= (int) doc.violations.size()) return;
    const auto v = doc.violations[(size_t) row];
    engine.setPosition (v.time);
    scrollToTick (v.time);
    if (v.voiceA >= 0)
        if (auto* voice = doc.project.findVoice (v.voiceA))
        {
            int clipId = -1;
            for (auto& c : voice->clips) if (v.time >= c.start && v.time < c.end()) clipId = c.id;
            doc.select (v.voiceA, clipId);
        }
    timeline.playheadMoved();
    pianoRoll.playheadMoved();
}

void MainComponent::runDemo()
{
    if (juce::JUCEApplication::getCommandLineParameters().contains ("--locks"))
    {
        // Three locked layers, each filled with its own kind of material, plus one broken answer.
        doc.edit ([&] (Project& p)
        {
            p.voices[1].lock = Role::Subject;
            p.voices[0].lock = Role::Answer;
            p.voices[2].lock = Role::Countersubject;
            auto add = [&] (size_t vi, Tick start, bool spoil)
            {
                Clip c;
                c.id = p.newId();
                c.role = *p.voices[vi].lock;
                c.start = start;
                c.length = 8 * PPQ;
                c.notes = lockedLayerMaterial (p, p.voices[vi].id, c.role, c.start, c.length, 5);
                if (spoil) c.notes[4].pitch += 2;
                p.voices[vi].clips.push_back (c);
            };
            add (0, 8 * PPQ, false);
            add (2, 8 * PPQ, false);
            add (1, 16 * PPQ, false);
            add (0, 24 * PPQ, true);
        });
        doc.dirty = false;
        return;
    }
    for (auto& v : doc.project.voices)
        for (auto& c : v.clips)
            if (c.role == Role::Subject)
            {
                doc.select (v.id, c.id);
                ExpositionResult r;
                doc.edit ([&] (Project& p) { r = generateExposition (p, v.id, c.id, true, 7); });
                doc.dirty = false;
                // Round-trip through the file format as a sanity check.
                Project copy;
                jassert (Document::fromJson (Document::toJson (doc.project), copy));
                jassert (Document::toJson (copy) == Document::toJson (doc.project));
                return;
            }
}
