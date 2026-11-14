#include "AudioEngine.h"

using namespace fuga;

namespace
{
struct OrganSound : juce::SynthesiserSound
{
    bool appliesToNote (int) override { return true; }
    bool appliesToChannel (int) override { return true; }
};

/** A plain additive organ tone. Clear attacks and no decay, so every line of the counterpoint stays audible. */
struct OrganVoice : juce::SynthesiserVoice
{
    bool canPlaySound (juce::SynthesiserSound* s) override { return dynamic_cast<OrganSound*> (s) != nullptr; }

    void startNote (int note, float velocity, juce::SynthesiserSound*, int) override
    {
        phase = 0.0;
        delta = juce::MathConstants<double>::twoPi * juce::MidiMessage::getMidiNoteInHertz (note) / getSampleRate();
        level = 0.08f + 0.12f * velocity;
        adsr.setSampleRate (getSampleRate());
        adsr.setParameters ({ 0.006f, 0.08f, 0.85f, 0.09f });
        adsr.noteOn();
        active = true;
    }

    void stopNote (float, bool allowTailOff) override
    {
        if (allowTailOff) { adsr.noteOff(); return; }
        adsr.reset();
        active = false;
        clearCurrentNote();
    }

    void renderNextBlock (juce::AudioBuffer<float>& out, int start, int num) override
    {
        if (! active) return;
        for (int i = 0; i < num; ++i)
        {
            const double s = std::sin (phase) + 0.5 * std::sin (2 * phase) + 0.28 * std::sin (3 * phase)
                           + 0.12 * std::sin (4 * phase) + 0.06 * std::sin (6 * phase);
            const float v = (float) s * level * adsr.getNextSample();
            for (int ch = 0; ch < out.getNumChannels() && ch < 2; ++ch) out.addSample (ch, start + i, v);
            phase += delta;
            if (phase > juce::MathConstants<double>::twoPi * 64) phase -= juce::MathConstants<double>::twoPi * 64;
        }
        if (! adsr.isActive()) { active = false; clearCurrentNote(); }
    }

    void pitchWheelMoved (int) override {}
    void controllerMoved (int, int) override {}

    double phase = 0, delta = 0;
    float level = 0;
    bool active = false;
    juce::ADSR adsr;
};

struct PluginWindow : juce::DocumentWindow
{
    PluginWindow (const juce::String& name, juce::AudioProcessorEditor* editor)
        : DocumentWindow (name, juce::Colours::darkgrey, DocumentWindow::closeButton | DocumentWindow::minimiseButton)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (editor, true);
        setResizable (editor->isResizable(), false);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }
    void closeButtonPressed() override { setVisible (false); }
};
} // namespace

AudioEngine::AudioEngine()
{
    juce::PropertiesFile::Options opts;
    opts.applicationName = "Fuga";
    opts.filenameSuffix = "settings";
    opts.osxLibrarySubFolder = "Application Support";
    settings = std::make_unique<juce::PropertiesFile> (opts);

    juce::addDefaultFormatsToManager (formatManager);
    if (auto xml = settings->getXmlValue ("knownPlugins"))
        knownPlugins.recreateFromXml (*xml);

    recBuffer.resize ((size_t) recFifo.getTotalSize());
    keyboardState.addListener (&collector);
    collector.reset (sampleRate);

    auto saved = settings->getXmlValue ("audioDevice");
    deviceManager.initialise (0, 2, saved.get(), true);
    deviceManager.addAudioCallback (this);
    deviceManager.addMidiInputDeviceCallback ({}, this);
    timerCallback();
    startTimer (2000);
}

AudioEngine::~AudioEngine()
{
    stopTimer();
    if (auto xml = deviceManager.createStateXml()) settings->setValue ("audioDevice", xml.get());
    if (auto xml = knownPlugins.createXml()) settings->setValue ("knownPlugins", xml.get());
    settings->saveIfNeeded();

    deviceManager.removeMidiInputDeviceCallback ({}, this);
    deviceManager.removeAudioCallback (this);
    keyboardState.removeListener (&collector);
    tracks.clear();
}

void AudioEngine::timerCallback()
{
    // Pick up MIDI keyboards as they are plugged in.
    for (auto& d : juce::MidiInput::getAvailableDevices())
        if (! deviceManager.isMidiInputDeviceEnabled (d.identifier))
            deviceManager.setMidiInputDeviceEnabled (d.identifier, true);
}

juce::StringArray AudioEngine::midiInputNames() const
{
    juce::StringArray names;
    for (auto& d : juce::MidiInput::getAvailableDevices()) names.add (d.name);
    return names;
}

AudioEngine::Track* AudioEngine::findTrack (int voiceId) const
{
    for (auto& t : tracks) if (t->voiceId == voiceId) return t.get();
    return nullptr;
}

void AudioEngine::prepareTrack (Track& t)
{
    t.synth.setCurrentPlaybackSampleRate (sampleRate);
    if (t.plugin != nullptr)
    {
        t.plugin->releaseResources();
        t.plugin->setRateAndBufferSizeDetails (sampleRate, blockSize);
        t.plugin->prepareToPlay (sampleRate, blockSize);
    }
}

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    const juce::ScopedLock sl (lock);
    sampleRate = device->getCurrentSampleRate();
    blockSize = juce::jmax (64, device->getCurrentBufferSizeSamples());
    collector.reset (sampleRate);
    int chans = 2;
    for (auto& t : tracks)
    {
        prepareTrack (*t);
        if (t->plugin != nullptr)
            chans = juce::jmax (chans, t->plugin->getTotalNumInputChannels(), t->plugin->getTotalNumOutputChannels());
    }
    scratch.setSize (chans, blockSize);
    cursorsValid = false;
}

void AudioEngine::syncProject (const Project& p)
{
    // Build everything off the lock, then swap in.
    std::vector<std::pair<int, std::vector<Event>>> built;
    bool anySolo = false;
    for (auto& v : p.voices) anySolo = anySolo || v.solo;

    for (auto& v : p.voices)
    {
        std::vector<Event> ev;
        for (auto& n : flatten (v))
        {
            ev.push_back ({ n.start, true, n.pitch, n.velocity });
            ev.push_back ({ n.end(), false, n.pitch, 0 });
        }
        std::sort (ev.begin(), ev.end(), [] (const Event& a, const Event& b)
                   { return a.tick != b.tick ? a.tick < b.tick : (! a.on && b.on); });
        built.emplace_back (v.id, std::move (ev));
    }

    std::vector<std::unique_ptr<Track>> removed;
    std::vector<std::unique_ptr<Track>> created;
    for (auto& v : p.voices)
        if (findTrack (v.id) == nullptr)
        {
            auto t = std::make_unique<Track>();
            t->voiceId = v.id;
            for (int i = 0; i < 12; ++i) t->synth.addVoice (new OrganVoice());
            t->synth.addSound (new OrganSound());
            t->synth.setCurrentPlaybackSampleRate (sampleRate);
            created.push_back (std::move (t));
        }

    {
        const juce::ScopedLock sl (lock);
        for (auto& t : created) tracks.push_back (std::move (t));
        for (auto it = tracks.begin(); it != tracks.end();)
        {
            if (p.findVoice ((*it)->voiceId) == nullptr) { removed.push_back (std::move (*it)); it = tracks.erase (it); }
            else ++it;
        }
        bool eventsChanged = false;
        for (auto& [id, ev] : built)
        {
            auto* t = findTrack (id);
            auto* v = p.findVoice (id);
            t->gain = v->gain;
            t->audible = ! v->mute && (! anySolo || v->solo);
            const bool same = t->events.size() == ev.size()
                              && std::equal (ev.begin(), ev.end(), t->events.begin(), [] (const Event& a, const Event& b)
                                             { return a.tick == b.tick && a.on == b.on && a.pitch == b.pitch; });
            if (! same) { std::swap (t->events, ev); eventsChanged = true; }
        }
        bpm = p.bpm;
        beatsPerBar = p.beatsPerBar;
        endTick = p.endTime();
        loopStart = p.loopOn && p.hasLoop() ? p.loopStart : 0;
        loopEnd = p.loopOn && p.hasLoop() ? p.loopEnd : 0;
        if (eventsChanged)
        {
            cursorsValid = false;
            if (playing.load()) flushNotes = true;
        }
    }
    removed.clear();

    for (auto& v : p.voices)
        if (auto* t = findTrack (v.id); t != nullptr && t->desc != v.instrumentDesc)
            loadInstrument (*t, v.instrumentDesc, v.instrumentState);
}

void AudioEngine::loadInstrument (Track& t, const std::string& desc, const std::string& state)
{
    t.desc = desc;
    t.failed = false;
    t.window.reset();

    if (desc.empty())
    {
        std::unique_ptr<juce::AudioPluginInstance> old;
        {
            const juce::ScopedLock sl (lock);
            std::swap (old, t.plugin);
            flushNotes = true;
        }
        t.loading = false;
        sendChangeMessage();
        return;
    }

    juce::PluginDescription pd;
    auto xml = juce::parseXML (juce::String (desc));
    if (xml == nullptr || ! pd.loadFromXml (*xml))
    {
        t.failed = true;
        sendChangeMessage();
        return;
    }

    t.loading = true;
    sendChangeMessage();
    const int voiceId = t.voiceId;
    juce::WeakReference<AudioEngine> weak (this);
    formatManager.createPluginInstanceAsync (pd, sampleRate, blockSize,
        [weak, voiceId, desc, state] (std::unique_ptr<juce::AudioPluginInstance> inst, const juce::String&)
        {
            if (weak == nullptr) return;
            auto& self = *weak;
            auto* track = self.findTrack (voiceId);
            if (track == nullptr || track->desc != desc) return; // superseded
            track->loading = false;
            if (inst == nullptr)
            {
                track->failed = true;
                self.sendChangeMessage();
                return;
            }
            if (! state.empty())
            {
                juce::MemoryBlock mb;
                if (mb.fromBase64Encoding (juce::String (state)))
                    inst->setStateInformation (mb.getData(), (int) mb.getSize());
            }
            inst->setRateAndBufferSizeDetails (self.sampleRate, self.blockSize);
            inst->prepareToPlay (self.sampleRate, self.blockSize);
            {
                const juce::ScopedLock sl (self.lock);
                const int chans = juce::jmax (self.scratch.getNumChannels(), inst->getTotalNumInputChannels(), inst->getTotalNumOutputChannels());
                if (chans > self.scratch.getNumChannels() || self.scratch.getNumSamples() < self.blockSize)
                    self.scratch.setSize (chans, self.blockSize);
                std::swap (track->plugin, inst);
            }
            self.sendChangeMessage();
        });
}

void AudioEngine::captureStates (Project& p)
{
    for (auto& t : tracks)
        if (auto* v = p.findVoice (t->voiceId); v != nullptr && t->plugin != nullptr)
        {
            juce::MemoryBlock mb;
            t->plugin->getStateInformation (mb);
            v->instrumentState = mb.toBase64Encoding().toStdString();
        }
}

juce::String AudioEngine::instrumentName (int voiceId) const
{
    if (auto* t = findTrack (voiceId))
    {
        if (t->plugin != nullptr) return t->plugin->getName();
        if (t->loading) return "Loading...";
        if (t->failed) return "Plugin missing";
    }
    return "Built-in organ";
}

bool AudioEngine::hasPlugin (int voiceId) const
{
    auto* t = findTrack (voiceId);
    return t != nullptr && t->plugin != nullptr;
}

void AudioEngine::showPluginWindow (int voiceId)
{
    auto* t = findTrack (voiceId);
    if (t == nullptr || t->plugin == nullptr) return;
    if (t->window == nullptr)
    {
        auto* editor = t->plugin->createEditorIfNeeded();
        if (editor == nullptr) editor = new juce::GenericAudioProcessorEditor (*t->plugin);
        t->window = std::make_unique<PluginWindow> (t->plugin->getName(), editor);
    }
    t->window->setVisible (true);
    t->window->toFront (true);
}

// ---------------------------------------------------------------- transport

void AudioEngine::play()
{
    const juce::ScopedLock sl (lock);
    cursorsValid = false;
    playing.store (true);
}

void AudioEngine::stop()
{
    const juce::ScopedLock sl (lock);
    playing.store (false);
    recording.store (false);
    flushNotes = true;
}

void AudioEngine::setPosition (Tick t)
{
    const juce::ScopedLock sl (lock);
    position.store ((double) std::max<Tick> (0, t));
    cursorsValid = false;
    if (playing.load()) flushNotes = true;
}

void AudioEngine::startRecording()
{
    recFifo.reset();
    recording.store (true);
    play();
}

std::vector<AudioEngine::RecordedNote> AudioEngine::takeRecording()
{
    std::vector<RecEvent> events;
    {
        int s1, n1, s2, n2;
        recFifo.prepareToRead (recFifo.getNumReady(), s1, n1, s2, n2);
        for (int i = 0; i < n1; ++i) events.push_back (recBuffer[(size_t) (s1 + i)]);
        for (int i = 0; i < n2; ++i) events.push_back (recBuffer[(size_t) (s2 + i)]);
        recFifo.finishedRead (n1 + n2);
    }
    std::vector<RecordedNote> notes;
    const double now = position.load();
    for (size_t i = 0; i < events.size(); ++i)
    {
        if (! events[i].on) continue;
        double end = now;
        for (size_t j = i + 1; j < events.size(); ++j)
            if (events[j].pitch == events[i].pitch) { end = events[j].tick; break; }
        notes.push_back ({ (Tick) events[i].tick, std::max<Tick> (1, (Tick) (end - events[i].tick)), events[i].pitch, events[i].velocity });
    }
    return notes;
}

void AudioEngine::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m)
{
    // Scale filter: drop note-ons outside the scale. Note-offs always pass so nothing can hang.
    if (m.isNoteOn() && (inputMask.load() & (1 << (m.getNoteNumber() % 12))) == 0)
        return;
    if (m.isNoteOnOrOff())
        keyboardState.processNextMidiEvent (m); // shows on the on-screen keyboard and reaches the collector
    else
        collector.addMessageToQueue (m);
}

// ---------------------------------------------------------------- audio thread

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const*, int, float* const* out, int numOut, int numSamples,
                                                    const juce::AudioIODeviceCallbackContext&)
{
    juce::ScopedNoDenormals noDenormals;
    for (int ch = 0; ch < numOut; ++ch)
        if (out[ch] != nullptr) juce::FloatVectorOperations::clear (out[ch], numSamples);

    const juce::ScopedTryLock sl (lock);
    if (! sl.isLocked() || numOut <= 0 || scratch.getNumSamples() == 0) return;

    liveMidi.clear();
    collector.removeNextBlockOfMessages (liveMidi, numSamples);

    float* chans[2] = { out[0], out[numOut > 1 ? 1 : 0] };
    const double ticksPerSample = bpm * (double) PPQ / (60.0 * sampleRate);
    for (int done = 0; done < numSamples;)
    {
        int n = juce::jmin (scratch.getNumSamples(), numSamples - done);

        // When looping, end this chunk exactly on the loop end and jump back to the loop start.
        const double pos = position.load();
        const bool looping = playing.load() && ! recording.load() && loopEnd > loopStart && pos < (double) loopEnd;
        if (looping)
            n = juce::jlimit (1, n, (int) std::ceil (((double) loopEnd - pos) / ticksPerSample));

        float* offset[2] = { chans[0] + done, chans[1] + done };
        processChunk (offset, numOut > 1 ? 2 : 1, done, n);
        done += n;

        if (looping && playing.load() && position.load() >= (double) loopEnd)
        {
            position.store ((double) loopStart);
            cursorsValid = false;
            flushNotes = true;
        }
    }
}

void AudioEngine::processChunk (float* const* out, int numOut, int offset, int n)
{
    const double ticksPerSample = bpm * (double) PPQ / (60.0 * sampleRate);
    const bool isPlaying = playing.load();
    const double startTick = position.load();
    const double endOfBlock = startTick + n * ticksPerSample;
    const int live = liveVoice.load();
    const bool flush = flushNotes;
    flushNotes = false;

    if (isPlaying && recording.load())
        for (const auto meta : liveMidi)
        {
            const auto m = meta.getMessage();
            if (! m.isNoteOnOrOff() || meta.samplePosition < offset || meta.samplePosition >= offset + n) continue;
            int s1, n1, s2, n2;
            recFifo.prepareToWrite (1, s1, n1, s2, n2);
            if (n1 + n2 > 0)
            {
                recBuffer[(size_t) (n1 > 0 ? s1 : s2)] = { startTick + (meta.samplePosition - offset) * ticksPerSample, m.getNoteNumber(), (int) m.getVelocity(), m.isNoteOn() };
                recFifo.finishedWrite (1);
            }
        }

    for (auto& tp : tracks)
    {
        auto& t = *tp;
        t.midi.clear();
        if (flush)
        {
            t.midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
            t.midi.addEvent (juce::MidiMessage::allSoundOff (1), 0);
        }
        if (t.voiceId == live) t.midi.addEvents (liveMidi, offset, n, -offset);

        if (isPlaying)
        {
            if (! cursorsValid)
                t.cursor = (size_t) (std::lower_bound (t.events.begin(), t.events.end(), startTick,
                                                       [] (const Event& e, double tick) { return (double) e.tick < tick; }) - t.events.begin());
            while (t.cursor < t.events.size() && (double) t.events[t.cursor].tick < endOfBlock)
            {
                auto& e = t.events[t.cursor++];
                const int pos = juce::jlimit (0, n - 1, (int) (((double) e.tick - startTick) / ticksPerSample));
                t.midi.addEvent (e.on ? juce::MidiMessage::noteOn (1, e.pitch, (juce::uint8) e.velocity)
                                      : juce::MidiMessage::noteOff (1, e.pitch), pos);
            }
        }

        const int numCh = t.plugin != nullptr ? juce::jmax (2, t.plugin->getTotalNumInputChannels(), t.plugin->getTotalNumOutputChannels()) : 2;
        if (numCh > scratch.getNumChannels()) continue;
        juce::AudioBuffer<float> buf (scratch.getArrayOfWritePointers(), numCh, n);
        buf.clear();
        int outCh = 2;
        if (t.plugin != nullptr)
        {
            t.plugin->processBlock (buf, t.midi);
            outCh = juce::jlimit (1, 2, t.plugin->getTotalNumOutputChannels());
        }
        else
        {
            t.synth.renderNextBlock (buf, t.midi, 0, n);
        }
        if (t.audible)
            for (int ch = 0; ch < numOut; ++ch)
                juce::FloatVectorOperations::addWithMultiply (out[ch], buf.getReadPointer (juce::jmin (ch, outCh - 1)), t.gain, n);
    }
    cursorsValid = isPlaying;

    if (isPlaying)
    {
        if (metronome.load())
        {
            const double firstBeat = std::ceil (startTick / (double) PPQ);
            for (double b = firstBeat; b * PPQ < endOfBlock; b += 1.0)
            {
                clickSamplesLeft = (int) (sampleRate * 0.03);
                clickFreq = ((juce::int64) b % beatsPerBar == 0) ? 1760.0 : 1100.0;
                clickPhase = 0;
            }
            for (int i = 0; i < n && clickSamplesLeft > 0; ++i, --clickSamplesLeft)
            {
                const float v = 0.25f * (float) std::sin (clickPhase) * (float) clickSamplesLeft / (float) (sampleRate * 0.03);
                clickPhase += juce::MathConstants<double>::twoPi * clickFreq / sampleRate;
                for (int ch = 0; ch < numOut; ++ch) out[ch][i] += v;
            }
        }

        position.store (endOfBlock);
        // Stop a bar after the last note, unless we are recording.
        if (! recording.load() && endOfBlock > (double) (endTick + PPQ * beatsPerBar) && ! (loopEnd > loopStart && startTick < (double) loopEnd))
        {
            playing.store (false);
            flushNotes = true;
        }
    }
}
