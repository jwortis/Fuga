// Fuga core score model. Pure C++20, no UI or audio dependencies.
#pragma once
#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace fuga
{
using Tick = std::int64_t;
constexpr Tick PPQ = 480; // ticks per quarter note

struct Note
{
    Tick start = 0;   // relative to the owning clip's start
    Tick length = PPQ;
    int pitch = 60;   // MIDI note number
    int velocity = 96;
    Tick end() const { return start + length; }
    bool operator== (const Note&) const = default;
};

/** The contrapuntal function of a clip of material within a voice. */
enum class Role { Subject, Answer, Countersubject, Free };

struct Clip
{
    int id = 0;
    Role role = Role::Free;
    Tick start = 0;      // absolute project time
    Tick length = 4 * PPQ;
    std::vector<Note> notes;
    Tick end() const { return start + length; }
    bool isEntry() const { return role == Role::Subject || role == Role::Answer; }
};

/** One layer of the timeline. Voices are ordered highest (index 0) to lowest. */
struct Voice
{
    int id = 0;
    std::string name;
    int low = 48, high = 72; // comfortable range, MIDI note numbers
    bool mute = false, solo = false;
    float gain = 0.8f;
    std::string instrumentDesc;  // opaque: empty = built-in synth, else plugin description XML
    std::string instrumentState; // opaque: base64 plugin state
    /** Role lock: when set, every clip in this layer has this role and must obey its constraint
        (a subject layer only holds statements of the subject, an answer layer only answers, and so on). */
    std::optional<Role> lock;
    std::vector<Clip> clips;

    Clip* findClip (int clipId)
    {
        for (auto& c : clips) if (c.id == clipId) return &c;
        return nullptr;
    }
    const Clip* findClip (int clipId) const { return const_cast<Voice*> (this)->findClip (clipId); }
};

enum class Mode { Major, Minor };

struct Key
{
    int tonic = 0; // pitch class 0-11
    Mode mode = Mode::Major;
    bool operator== (const Key&) const = default;
};

enum class SectionType { Exposition, Episode, MiddleEntry, Stretto, Pedal, Coda };

struct Section
{
    int id = 0;
    SectionType type = SectionType::Exposition;
    Tick start = 0;
    Tick length = 16 * PPQ;
    Key key;
    Tick end() const { return start + length; }
};

struct Project
{
    double bpm = 80.0;
    int beatsPerBar = 4;
    Key key { 2, Mode::Minor };
    std::vector<Voice> voices;
    std::vector<Section> sections;
    int nextId = 1;

    int newId() { return nextId++; }
    Tick barLength() const { return PPQ * beatsPerBar; }

    Voice* findVoice (int voiceId)
    {
        for (auto& v : voices) if (v.id == voiceId) return &v;
        return nullptr;
    }
    const Voice* findVoice (int voiceId) const { return const_cast<Project*> (this)->findVoice (voiceId); }

    int voiceIndex (int voiceId) const
    {
        for (size_t i = 0; i < voices.size(); ++i) if (voices[i].id == voiceId) return (int) i;
        return -1;
    }

    Section* findSection (int sectionId)
    {
        for (auto& s : sections) if (s.id == sectionId) return &s;
        return nullptr;
    }

    /** The section covering the given time, or nullptr. */
    const Section* sectionAt (Tick t) const
    {
        for (auto& s : sections) if (t >= s.start && t < s.end()) return &s;
        return nullptr;
    }

    Key keyAt (Tick t) const
    {
        if (auto* s = sectionAt (t)) return s->key;
        return key;
    }

    Tick endTime() const
    {
        Tick e = 0;
        for (auto& v : voices) for (auto& c : v.clips) e = std::max (e, c.end());
        for (auto& s : sections) e = std::max (e, s.end());
        return e;
    }
};

/** All notes of a voice in absolute time, clipped to their clip bounds and sorted by start. */
inline std::vector<Note> flatten (const Voice& v)
{
    std::vector<Note> out;
    for (auto& c : v.clips)
        for (auto n : c.notes)
        {
            if (n.start >= c.length || n.end() <= 0) continue;
            n.length = std::min (n.end(), c.length) - std::max<Tick> (n.start, 0);
            n.start = std::max<Tick> (n.start, 0) + c.start;
            out.push_back (n);
        }
    std::sort (out.begin(), out.end(), [] (const Note& a, const Note& b)
               { return a.start != b.start ? a.start < b.start : a.pitch < b.pitch; });
    return out;
}

/** Pitch sounding at time t in a flattened, sorted note list, or -1 for a rest. */
inline int pitchAt (const std::vector<Note>& notes, Tick t)
{
    int p = -1;
    for (auto& n : notes)
    {
        if (n.start > t) break;
        if (t < n.end()) p = n.pitch; // latest onset wins
    }
    return p;
}

inline bool hasOnsetAt (const std::vector<Note>& notes, Tick t)
{
    for (auto& n : notes)
    {
        if (n.start > t) break;
        if (n.start == t) return true;
    }
    return false;
}
} // namespace fuga
