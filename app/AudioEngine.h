#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "../core/Score.h"

/** Playback, live MIDI input, recording and per-voice instruments (built-in synth or VST3/AU plugin). */
class AudioEngine : public juce::AudioIODeviceCallback,
                    public juce::MidiInputCallback,
                    public juce::ChangeBroadcaster,
                    private juce::Timer
{
public:
    AudioEngine();
    ~AudioEngine() override;

    juce::AudioDeviceManager deviceManager;
    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPlugins;
    juce::MidiKeyboardState keyboardState;
    std::unique_ptr<juce::PropertiesFile> settings;

    /** Mirrors the project into the engine: note events, gains, instruments. Message thread. */
    void syncProject (const fuga::Project&);
    /** Writes each plugin's current state into the project so it can be saved. */
    void captureStates (fuga::Project&);

    void play();
    void stop();
    bool isPlaying() const { return playing.load(); }
    void setPosition (fuga::Tick);
    fuga::Tick getPosition() const { return (fuga::Tick) position.load(); }

    void startRecording();
    bool isRecording() const { return recording.load(); }
    struct RecordedNote { fuga::Tick start, length; int pitch, velocity; };
    /** Call after stop(): the notes played while recording, in project time. */
    std::vector<RecordedNote> takeRecording();

    /** The voice that live MIDI input plays. */
    void setLiveVoice (int voiceId) { liveVoice.store (voiceId); }
    void setMetronome (bool on) { metronome.store (on); }

    juce::String instrumentName (int voiceId) const;
    bool hasPlugin (int voiceId) const;
    void showPluginWindow (int voiceId);
    juce::StringArray midiInputNames() const;

    void audioDeviceIOCallbackWithContext (const float* const*, int, float* const*, int, int, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override {}
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;

private:
    struct Event { fuga::Tick tick; bool on; int pitch, velocity; };
    struct Track
    {
        int voiceId = 0;
        juce::Synthesiser synth;
        std::unique_ptr<juce::AudioPluginInstance> plugin;
        std::unique_ptr<juce::DocumentWindow> window; // declared after plugin: destroyed first
        std::string desc;    // the instrument this track has, or is loading
        bool loading = false, failed = false;
        float gain = 0.8f;
        bool audible = true;
        std::vector<Event> events;
        size_t cursor = 0;
        juce::MidiBuffer midi;
    };

    void timerCallback() override;
    Track* findTrack (int voiceId) const;
    void loadInstrument (Track&, const std::string& desc, const std::string& state);
    void prepareTrack (Track&);
    void processChunk (float* const* out, int numOut, int n);

    juce::CriticalSection lock; // guards tracks and everything below, shared with the audio thread
    std::vector<std::unique_ptr<Track>> tracks;
    juce::AudioBuffer<float> scratch;
    juce::MidiMessageCollector collector;
    juce::MidiBuffer liveMidi;
    double sampleRate = 44100.0;
    int blockSize = 512;
    double bpm = 80.0;
    fuga::Tick endTick = 0;
    int beatsPerBar = 4;
    bool cursorsValid = false, flushNotes = false;
    double clickPhase = 0, clickFreq = 0;
    int clickSamplesLeft = 0;

    std::atomic<bool> playing { false }, recording { false }, metronome { false };
    std::atomic<double> position { 0.0 };
    std::atomic<int> liveVoice { -1 };

    struct RecEvent { double tick; int pitch, velocity; bool on; };
    juce::AbstractFifo recFifo { 8192 };
    std::vector<RecEvent> recBuffer;

    JUCE_DECLARE_WEAK_REFERENCEABLE (AudioEngine)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};
