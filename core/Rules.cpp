#include "Rules.h"
#include "Theory.h"
#include <cstdlib>
#include <set>

namespace fuga
{
namespace
{
struct Entry { const Voice* voice; const Clip* clip; };

std::string barBeat (const Project& p, Tick t)
{
    Tick bar = t / p.barLength();
    Tick beat = (t % p.barLength()) / PPQ;
    return "bar " + std::to_string (bar + 1) + " beat " + std::to_string (beat + 1);
}

int firstPitch (const Clip& c)
{
    const Note* first = nullptr;
    for (auto& n : c.notes) if (first == nullptr || n.start < first->start) first = &n;
    return first ? first->pitch : -1;
}

void checkCounterpoint (const Project& p, const std::vector<std::vector<Note>>& flat, std::vector<Violation>& out)
{
    const size_t nv = p.voices.size();

    // Range
    for (size_t i = 0; i < nv; ++i)
        for (auto& n : flat[i])
            if (n.pitch < p.voices[i].low || n.pitch > p.voices[i].high)
                out.push_back ({ Severity::Info, "Out of range",
                                 p.voices[i].name + " goes outside its range with " + pitchName (n.pitch) + " at " + barBeat (p, n.start) + ".",
                                 n.start, n.length, p.voices[i].id, -1, -1 });

    std::set<Tick> allTimes;
    for (auto& f : flat) for (auto& n : f) allTimes.insert (n.start);

    for (size_t a = 0; a < nv; ++a)
        for (size_t b = a + 1; b < nv; ++b)
        {
            std::set<Tick> times;
            for (auto& n : flat[a]) times.insert (n.start);
            for (auto& n : flat[b]) times.insert (n.start);

            bool wasCrossed = false;
            Tick prevT = -1;
            int a0 = -1, b0 = -1;
            for (Tick t : times)
            {
                int a1 = pitchAt (flat[a], t), b1 = pitchAt (flat[b], t);
                const bool both = a1 >= 0 && b1 >= 0;

                // Voice crossing: voice a is the higher voice.
                bool crossed = both && a1 < b1;
                if (crossed && ! wasCrossed)
                    out.push_back ({ Severity::Warning, "Voice crossing",
                                     p.voices[a].name + " drops below " + p.voices[b].name + " at " + barBeat (p, t) + ".",
                                     t, PPQ, p.voices[a].id, p.voices[b].id, -1 });
                wasCrossed = crossed;

                // Parallel perfect intervals
                if (both && prevT >= 0 && a0 >= 0 && b0 >= 0 && a0 != a1 && b0 != b1
                    && ((a1 - a0) > 0) == ((b1 - b0) > 0)
                    && isPerfect (a1 - b1) && std::abs (a1 - b1) % 12 == std::abs (a0 - b0) % 12)
                {
                    const bool fifth = std::abs (a1 - b1) % 12 == 7;
                    out.push_back ({ Severity::Warning, fifth ? "Parallel fifths" : "Parallel octaves",
                                     std::string (fifth ? "Parallel fifths" : "Parallel octaves") + " between " + p.voices[a].name
                                         + " and " + p.voices[b].name + " into " + barBeat (p, t) + ".",
                                     prevT, t - prevT + PPQ / 2, p.voices[a].id, p.voices[b].id, -1 });
                }

                // Accented dissonance: both voices strike a dissonance together on a beat.
                if (both && t % PPQ == 0 && hasOnsetAt (flat[a], t) && hasOnsetAt (flat[b], t))
                {
                    bool dissonant = isDissonant (a1 - b1);
                    if (! dissonant && std::abs (a1 - b1) % 12 == 5)
                    {
                        // A fourth is dissonant only against the lowest sounding voice.
                        int lowest = 128;
                        for (auto& f : flat) { int q = pitchAt (f, t); if (q >= 0) lowest = std::min (lowest, q); }
                        dissonant = std::min (a1, b1) == lowest;
                    }
                    if (dissonant)
                        out.push_back ({ Severity::Info, "Accented dissonance",
                                         p.voices[a].name + " and " + p.voices[b].name + " strike " + pitchName (a1) + " against "
                                             + pitchName (b1) + " together on " + barBeat (p, t) + ".",
                                         t, PPQ / 2, p.voices[a].id, p.voices[b].id, -1 });
                }

                prevT = t; a0 = a1; b0 = b1;
            }
        }
}

void checkSection (const Project& p, const Section& s, const std::vector<std::vector<Note>>& flat, std::vector<Violation>& out)
{
    std::vector<Entry> entries;
    for (auto& v : p.voices)
        for (auto& c : v.clips)
            if (c.isEntry() && c.start >= s.start && c.start < s.end())
                entries.push_back ({ &v, &c });
    std::sort (entries.begin(), entries.end(), [] (const Entry& a, const Entry& b) { return a.clip->start < b.clip->start; });

    auto add = [&] (Severity sev, std::string rule, std::string msg, Tick t, Tick len, int va = -1, int vb = -1)
    {
        out.push_back ({ sev, std::move (rule), std::string (sectionTypeName (s.type)) + ": " + msg, t, len, va, vb, s.id });
    };

    switch (s.type)
    {
        case SectionType::Exposition:
        {
            for (auto& v : p.voices)
            {
                bool found = false;
                for (auto& e : entries) found = found || e.voice == &v;
                if (! found)
                    add (Severity::Error, "Missing entry", v.name + " never enters with the subject or answer.", s.start, s.length, v.id);
            }
            for (size_t i = 0; i < entries.size(); ++i)
            {
                auto& e = entries[i];
                Role expected = i % 2 == 0 ? Role::Subject : Role::Answer;
                if (e.clip->role != expected)
                    add (i == 0 ? Severity::Error : Severity::Warning, "Entry order",
                         "entry " + std::to_string (i + 1) + " (" + e.voice->name + ") should be the "
                             + (expected == Role::Subject ? "subject" : "answer") + ", since entries alternate subject and answer.",
                         e.clip->start, e.clip->length, e.voice->id);

                if (i > 0)
                {
                    auto& prev = entries[i - 1];
                    if (e.clip->start < prev.clip->end())
                        add (Severity::Warning, "Overlapping entries",
                             e.voice->name + " enters before " + prev.voice->name + " has finished. Overlapping entries belong in a stretto.",
                             e.clip->start, prev.clip->end() - e.clip->start, e.voice->id, prev.voice->id);

                    // The voice that just finished its entry should carry on with the countersubject.
                    bool hasCs = false;
                    for (auto& c : prev.voice->clips)
                        hasCs = hasCs || (c.role == Role::Countersubject && c.start < e.clip->end() && c.end() > e.clip->start);
                    if (! hasCs)
                        add (Severity::Info, "No countersubject",
                             prev.voice->name + " has no countersubject against the entry in " + e.voice->name + ".",
                             e.clip->start, e.clip->length, prev.voice->id);

                    // Pitch level relative to the opening subject.
                    int ref = firstPitch (*entries[0].clip), mine = firstPitch (*e.clip);
                    if (ref >= 0 && mine >= 0)
                    {
                        int diff = ((mine - ref) % 12 + 12) % 12;
                        if (e.clip->role == Role::Subject && diff != 0)
                            add (Severity::Warning, "Entry pitch", "the subject entry in " + e.voice->name + " is not at the tonic level of the opening subject.",
                                 e.clip->start, e.clip->length, e.voice->id);
                        if (e.clip->role == Role::Answer && diff != 7 && diff != 5)
                            add (Severity::Warning, "Entry pitch", "the answer in " + e.voice->name + " is not at the dominant level.",
                                 e.clip->start, e.clip->length, e.voice->id);
                    }
                }
            }
            break;
        }
        case SectionType::Episode:
            for (auto& e : entries)
                add (Severity::Warning, "Entry in episode",
                     e.voice->name + " states the full " + (e.clip->role == Role::Subject ? "subject" : "answer")
                         + ". Episodes develop fragments and avoid complete entries.",
                     e.clip->start, e.clip->length, e.voice->id);
            break;
        case SectionType::MiddleEntry:
            if (entries.empty())
                add (Severity::Error, "Missing entry", "no voice states the subject or answer.", s.start, s.length);
            break;
        case SectionType::Stretto:
        {
            bool overlap = false;
            for (size_t i = 0; i + 1 < entries.size(); ++i)
                overlap = overlap || (entries[i + 1].clip->start < entries[i].clip->end() && entries[i + 1].voice != entries[i].voice);
            if (! overlap)
                add (Severity::Error, "No overlap", "needs at least two subject or answer entries that overlap in different voices.", s.start, s.length);
            break;
        }
        case SectionType::Pedal:
        {
            bool found = false;
            for (auto& f : flat)
                for (auto& n : f)
                {
                    Tick a = std::max (n.start, s.start), b = std::min (n.end(), s.end());
                    int rel = relPc (n.pitch, s.key);
                    found = found || (b - a >= s.length / 2 && (rel == 0 || rel == 7));
                }
            if (! found)
                add (Severity::Error, "No pedal", "no voice holds the tonic or dominant of " + keyName (s.key) + " for at least half the section.", s.start, s.length);
            break;
        }
        case SectionType::Coda:
        {
            // The last sonority should rest on the tonic in the lowest voice.
            Tick lastEnd = -1;
            for (auto& f : flat)
                for (auto& n : f)
                    if (n.start >= s.start && n.start < s.end()) lastEnd = std::max (lastEnd, std::min (n.end(), s.end()));
            if (lastEnd < 0) break;
            int lowest = 128;
            for (auto& f : flat) { int q = pitchAt (f, lastEnd - 1); if (q >= 0) lowest = std::min (lowest, q); }
            if (lowest < 128 && relPc (lowest, s.key) != 0)
                add (Severity::Warning, "Final cadence", "the piece ends with " + pitchName (lowest) + " in the bass, not the tonic of " + keyName (s.key) + ".",
                     lastEnd - PPQ, PPQ);
            break;
        }
    }
}
void checkLocks (const Project& p, std::vector<Violation>& out)
{
    const Clip* subject = referenceSubject (p);
    for (auto& v : p.voices)
    {
        if (! v.lock) continue;
        const Role lock = *v.lock;
        const std::string layer = v.name + " is locked to " + roleName (lock);
        const Clip* firstCs = nullptr;
        for (auto& c : v.clips)
            if (lock == Role::Countersubject && ! c.notes.empty() && (firstCs == nullptr || c.start < firstCs->start)) firstCs = &c;

        for (auto& c : v.clips)
        {
            auto add = [&] (Severity sev, const std::string& msg)
            {
                out.push_back ({ sev, "Layer lock", layer + ", but " + msg, c.start, c.length, v.id, -1, -1 });
            };
            const std::string where = "the clip at bar " + std::to_string (c.start / p.barLength() + 1);
            if (c.role != lock) { add (Severity::Warning, where + " is marked " + roleName (c.role) + "."); continue; }
            if (c.notes.empty()) continue;

            if (lock == Role::Subject && subject != nullptr && &c != subject && ! sameShape (subject->notes, c.notes, true))
                add (Severity::Warning, where + " is not a statement of the subject (same notes, any octave).");

            if (lock == Role::Answer)
            {
                if (subject == nullptr)
                    add (Severity::Warning, "there is no subject yet for it to answer.");
                else
                {
                    const Key key = p.keyAt (subject->start);
                    if (! sameShape (makeAnswer (subject->notes, key, true), c.notes, true)
                        && ! sameShape (makeAnswer (subject->notes, key, false), c.notes, true))
                        add (Severity::Warning, where + " is not a real or tonal answer to the subject.");
                }
            }

            if (lock == Role::Countersubject)
            {
                bool againstEntry = false;
                for (auto& other : p.voices)
                    if (other.id != v.id)
                        for (auto& e : other.clips)
                            againstEntry = againstEntry || (e.isEntry() && e.start < c.end() && e.end() > c.start);
                if (! againstEntry)
                    add (Severity::Warning, where + " has no subject or answer sounding against it in another voice.");
                if (firstCs != nullptr && &c != firstCs && ! sameShape (firstCs->notes, c.notes, false))
                    add (Severity::Info, where + " differs from the layer's first countersubject. A countersubject normally returns unchanged apart from transposition.");
            }
        }
    }
}
} // namespace

std::vector<Violation> checkProject (const Project& p)
{
    std::vector<Violation> out;
    std::vector<std::vector<Note>> flat;
    for (auto& v : p.voices) flat.push_back (flatten (v));

    checkCounterpoint (p, flat, out);
    for (auto& s : p.sections) checkSection (p, s, flat, out);
    checkLocks (p, out);

    std::stable_sort (out.begin(), out.end(), [] (const Violation& a, const Violation& b) { return a.time < b.time; });
    return out;
}
} // namespace fuga
