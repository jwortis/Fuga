#include "Theory.h"
#include <cstdlib>

namespace fuga
{
static const char* const names[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };

const char* noteName (int pc) { return names[((pc % 12) + 12) % 12]; }

std::string pitchName (int p) { return std::string (noteName (p)) + std::to_string (p / 12 - 1); }

std::string keyName (Key k) { return std::string (noteName (k.tonic)) + " " + modeName (k.mode); }

const char* modeName (Mode m)
{
    static const char* const n[numModes] = { "major", "minor", "harmonic minor", "melodic minor", "dorian", "phrygian", "lydian", "mixolydian", "locrian" };
    return n[(int) m];
}

bool isMinorMode (Mode m) { return m == Mode::Minor || m == Mode::HarmonicMinor || m == Mode::MelodicMinor; }

const char* roleName (Role r)
{
    switch (r)
    {
        case Role::Subject: return "Subject";
        case Role::Answer: return "Answer";
        case Role::Countersubject: return "Countersubject";
        case Role::Free: return "Free";
    }
    return "";
}

const char* sectionTypeName (SectionType t)
{
    switch (t)
    {
        case SectionType::Exposition: return "Exposition";
        case SectionType::Episode: return "Episode";
        case SectionType::MiddleEntry: return "Middle entry";
        case SectionType::Stretto: return "Stretto";
        case SectionType::Pedal: return "Pedal point";
        case SectionType::Coda: return "Coda";
    }
    return "";
}

bool inScale (int pitch, Key k)
{
    //                                         C  .  D  .  E  F  .  G  .  A  .  B   (relative to the tonic)
    static const bool scales[numModes][12] = { { 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1 },   // major
                                               { 1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 0 },   // natural minor
                                               { 1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 0, 1 },   // harmonic minor
                                               { 1, 0, 1, 1, 0, 1, 0, 1, 0, 1, 0, 1 },   // melodic minor (ascending)
                                               { 1, 0, 1, 1, 0, 1, 0, 1, 0, 1, 1, 0 },   // dorian
                                               { 1, 1, 0, 1, 0, 1, 0, 1, 1, 0, 1, 0 },   // phrygian
                                               { 1, 0, 1, 0, 1, 0, 1, 1, 0, 1, 0, 1 },   // lydian
                                               { 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 1, 0 },   // mixolydian
                                               { 1, 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0 } }; // locrian
    return scales[(int) k.mode][relPc (pitch, k)];
}

bool inCounterpointScale (int pitch, Key k)
{
    if (! isMinorMode (k.mode)) return inScale (pitch, k);
    static const bool minor[12] = { 1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 1 }; // natural minor plus the leading note
    return minor[relPc (pitch, k)];
}

int scaleMask (Key k)
{
    int mask = 0;
    for (int pc = 0; pc < 12; ++pc) if (inScale (pc, k)) mask |= 1 << pc;
    return mask;
}

int snapToScale (int pitch, Key k, int direction)
{
    for (int d = 0; d < 12; ++d)
    {
        const int first = direction > 0 ? pitch + d : pitch - d, second = direction > 0 ? pitch - d : pitch + d;
        if (inScale (first, k)) return first;
        if (inScale (second, k)) return second;
    }
    return pitch;
}

int scaleStep (int pitch, Key k, int direction)
{
    const int step = direction > 0 ? 1 : -1;
    for (int p = pitch + step; std::abs (p - pitch) <= 12; p += step)
        if (inScale (p, k)) return p;
    return pitch;
}

bool isDissonant (int interval)
{
    int ic = std::abs (interval) % 12;
    return ic == 1 || ic == 2 || ic == 6 || ic == 10 || ic == 11;
}

bool isPerfect (int interval)
{
    int ic = std::abs (interval) % 12;
    return ic == 0 || ic == 7;
}

std::vector<Note> transposed (std::vector<Note> notes, int semitones)
{
    for (auto& n : notes) n.pitch += semitones;
    return notes;
}

const Clip* referenceSubject (const Project& p)
{
    const Clip* best = nullptr;
    for (auto& v : p.voices)
        for (auto& c : v.clips)
            if (c.role == Role::Subject && ! c.notes.empty() && (best == nullptr || c.start < best->start))
                best = &c;
    return best;
}

bool sameShape (std::vector<Note> a, std::vector<Note> b, bool octavesOnly)
{
    if (a.size() != b.size() || a.empty()) return false;
    auto byStart = [] (const Note& x, const Note& y) { return x.start != y.start ? x.start < y.start : x.pitch < y.pitch; };
    std::sort (a.begin(), a.end(), byStart);
    std::sort (b.begin(), b.end(), byStart);
    const int shift = b[0].pitch - a[0].pitch;
    if (octavesOnly && shift % 12 != 0) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].start != b[i].start || a[i].length != b[i].length || b[i].pitch - a[i].pitch != shift) return false;
    return true;
}

std::vector<Note> makeAnswer (const std::vector<Note>& subject, Key key, bool tonal)
{
    auto out = subject;
    std::sort (out.begin(), out.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
    bool head = tonal;
    for (auto& n : out)
    {
        int shift = 7;
        if (head)
        {
            int rel = relPc (n.pitch, key);
            if (rel == 7) shift = 5;       // dominant is answered by the tonic
            else if (rel != 0) head = false; // the head ends at the first note off tonic/dominant
        }
        n.pitch += shift;
    }
    return out;
}

std::vector<Note> fitToRange (std::vector<Note> notes, int low, int high)
{
    if (notes.empty()) return notes;
    int lo = 127, hi = 0;
    for (auto& n : notes) { lo = std::min (lo, n.pitch); hi = std::max (hi, n.pitch); }
    int best = 0;
    long bestCost = -1;
    for (int oct = -5; oct <= 5; ++oct)
    {
        int l = lo + oct * 12, h = hi + oct * 12;
        long over = std::max (0, low - l) + std::max (0, h - high);
        long centre = std::abs ((l + h) - (low + high));
        long cost = over * 100 + centre;
        if (bestCost < 0 || cost < bestCost) { bestCost = cost; best = oct; }
    }
    return transposed (std::move (notes), best * 12);
}
} // namespace fuga
