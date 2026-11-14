#pragma once
#include "PianoRollView.h"

class MainComponent : public juce::Component,
                      public juce::MenuBarModel,
                      private juce::ChangeListener,
                      private juce::Timer,
                      private juce::ListBoxModel
{
public:
    MainComponent();
    ~MainComponent() override;

    void resized() override;
    void paint (juce::Graphics&) override;
    bool keyPressed (const juce::KeyPress&) override;

    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int, const juce::String&) override;
    void menuItemSelected (int, int) override {}

    /** Asks about unsaved changes, then calls `then` if it is fine to carry on. */
    void confirmDiscard (std::function<void()> then);
    std::function<void (const juce::String&)> onTitleChanged;
    /** Selects the first subject and builds an exposition from it. Used for snapshots. */
    void runDemo();

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    int getNumRows() override;
    void paintListBoxItem (int, juce::Graphics&, int, int, bool) override;
    void selectedRowsChanged (int) override;

    void togglePlay();
    void stopAndCollectRecording();
    void record();
    void newProject();
    void open();
    void save (bool saveAs, std::function<void()> then = {});
    void generateExpositionFromSelection (bool tonal);
    void addVoice();
    void toggleLoop();
    void showAudioSettings();
    void showPluginManager();
    void setZoom (double pixelsPerBeat);
    void scrollToTick (fuga::Tick);
    void updateToolbar();
    void updateTitle();

    Document doc;
    AudioEngine engine;
    TimelineView timeline { doc, engine };
    PianoRollView pianoRoll { doc, engine };
    juce::ListBox problems;
    juce::Label problemsTitle, status;
    juce::MidiKeyboardComponent keyboard { engine.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard };
    juce::TextButton loopButton { "Loop" };
    juce::TextButton playButton { "Play" }, recordButton { "Record" }, rewindButton { "|<" }, metronomeButton { "Click" };
    juce::TextButton generateButton { "Generate exposition" }, addVoiceButton { "+ Voice" }, zoomOut { "-" }, zoomIn { "+" };
    juce::Slider tempo;
    juce::ComboBox keyBox, scaleBox, snapBox;
    juce::TextButton scaleLockButton { "Scale lock" }, midiFilterButton { "MIDI filter" };
    juce::Label tempoLabel, keyLabel, snapLabel, zoomLabel;
    juce::TooltipWindow tooltips { this, 600 };
    std::unique_ptr<juce::FileChooser> chooser;
    bool syncingScroll = false;
    bool wasPlaying = false;
    fuga::Tick recordStart = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
