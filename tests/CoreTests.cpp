#include "../core/Generate.h"
#include "../core/Rules.h"
#include "../core/Theory.h"
#include <cstdio>
#include <cstdlib>

using namespace fuga;

static int failures = 0;
#define CHECK(cond) do { if (! (cond)) { std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static Voice makeVoice (Project& p, const char* name, int low, int high)
{
    Voice v; v.id = p.newId(); v.name = name; v.low = low; v.high = high; return v;
}

static Project satb()
{
    Project p;
    p.voices.push_back (makeVoice (p, "Soprano", 60, 81));
    p.voices.push_back (makeVoice (p, "Alto", 55, 74));
    p.voices.push_back (makeVoice (p, "Tenor", 48, 67));
    p.voices.push_back (makeVoice (p, "Bass", 40, 60));
    return p;
}

static int count (const std::vector<Violation>& vs, const std::string& rule)
{
    int n = 0;
    for (auto& v : vs) n += v.rule == rule;
    return n;
}

static Clip clipOf (Project& p, Role r, Tick start, Tick len, std::vector<Note> notes)
{
    Clip c; c.id = p.newId(); c.role = r; c.start = start; c.length = len; c.notes = std::move (notes); return c;
}

static std::vector<Note> demoSubject()
{
    const Tick q = PPQ, e = PPQ / 2;
    return { { 0, q, 62 }, { q, q, 69 }, { 2 * q, e, 65 }, { 2 * q + e, e, 64 }, { 3 * q, e, 62 }, { 3 * q + e, e, 61 },
             { 4 * q, e, 62 }, { 4 * q + e, e, 64 }, { 5 * q, e, 65 }, { 5 * q + e, e, 67 }, { 6 * q, e, 69 }, { 6 * q + e, e, 67 }, { 7 * q, q, 65 } };
}

int main()
{
    // --- Answers ---
    {
        Key c { 0, Mode::Major };
        std::vector<Note> subj { { 0, PPQ, 60 }, { PPQ, PPQ, 67 }, { 2 * PPQ, PPQ, 64 }, { 3 * PPQ, PPQ, 67 } };
        auto real = makeAnswer (subj, c, false);
        CHECK (real[0].pitch == 67 && real[1].pitch == 74 && real[2].pitch == 71 && real[3].pitch == 74);
        auto tonal = makeAnswer (subj, c, true);
        CHECK (tonal[0].pitch == 67); // tonic -> dominant
        CHECK (tonal[1].pitch == 72); // dominant -> tonic
        CHECK (tonal[2].pitch == 71); // head is over, real from here
        CHECK (tonal[3].pitch == 74);
        auto fitted = fitToRange (real, 40, 60);
        CHECK (fitted[0].pitch == 43 || fitted[0].pitch == 55);
        for (auto& n : fitToRange (real, 48, 67)) CHECK (n.pitch >= 48 && n.pitch <= 67);
    }

    // --- Counterpoint checks ---
    {
        Project p = satb();
        p.voices[0].clips.push_back (clipOf (p, Role::Free, 0, 4 * PPQ, { { 0, PPQ, 67 }, { PPQ, PPQ, 69 } }));
        p.voices[1].clips.push_back (clipOf (p, Role::Free, 0, 4 * PPQ, { { 0, PPQ, 60 }, { PPQ, PPQ, 62 } }));
        auto vs = checkProject (p);
        CHECK (count (vs, "Parallel fifths") == 1);
        CHECK (count (vs, "Voice crossing") == 0);

        p.voices[1].clips[0].notes = { { 0, PPQ, 72 }, { PPQ, PPQ, 60 } };
        vs = checkProject (p);
        CHECK (count (vs, "Voice crossing") == 1);
        CHECK (count (vs, "Parallel fifths") == 0);

        p.voices[1].clips[0].notes = { { 0, PPQ, 55 }, { PPQ, PPQ, 57 } };
        CHECK (count (checkProject (p), "Parallel octaves") == 1);
        p.voices[1].clips[0].notes = { { 0, PPQ, 64 }, { PPQ, PPQ, 62 } }; // contrary, consonant
        vs = checkProject (p);
        CHECK (count (vs, "Parallel octaves") + count (vs, "Parallel fifths") + count (vs, "Voice crossing") == 0);
        p.voices[1].clips[0].notes = { { 0, PPQ, 30 } };
        CHECK (count (checkProject (p), "Out of range") == 1);
    }

    // --- Section rules ---
    {
        Project p = satb();
        p.sections.push_back ({ p.newId(), SectionType::Exposition, 0, 32 * PPQ, p.key });
        auto vs = checkProject (p);
        CHECK (count (vs, "Missing entry") == 4);

        p.sections[0].type = SectionType::Stretto;
        CHECK (count (checkProject (p), "No overlap") == 1);
        p.voices[0].clips.push_back (clipOf (p, Role::Subject, 0, 8 * PPQ, demoSubject()));
        p.voices[1].clips.push_back (clipOf (p, Role::Answer, 4 * PPQ, 8 * PPQ, demoSubject()));
        CHECK (count (checkProject (p), "No overlap") == 0);

        p.sections[0].type = SectionType::Episode;
        CHECK (count (checkProject (p), "Entry in episode") == 2);

        p.sections[0].type = SectionType::Pedal;
        CHECK (count (checkProject (p), "No pedal") == 1);
        p.voices[3].clips.push_back (clipOf (p, Role::Free, 0, 32 * PPQ, { { 0, 20 * PPQ, 45 } })); // A = dominant of D minor
        CHECK (count (checkProject (p), "No pedal") == 0);
    }

    // --- Generation ---
    for (unsigned seed = 1; seed <= 20; ++seed)
    {
        Project p = satb();
        p.voices[1].clips.push_back (clipOf (p, Role::Subject, 0, 8 * PPQ, demoSubject()));
        const int clipId = p.voices[1].clips[0].id;
        auto r = generateExposition (p, p.voices[1].id, clipId, true, seed);
        CHECK (r.ok);
        CHECK (p.sections.size() == 1 && p.sections[0].length == 32 * PPQ);
        for (auto& v : p.voices)
        {
            int entries = 0;
            for (auto& c : v.clips) { entries += c.isEntry(); CHECK (! c.notes.empty()); }
            CHECK (entries == 1);
        }
        auto vs = checkProject (p);
        int errors = 0;
        for (auto& v : vs) errors += v.severity == Severity::Error;
        CHECK (errors == 0);
        CHECK (count (vs, "Entry order") == 0);
        CHECK (count (vs, "Entry pitch") == 0);
        CHECK (count (vs, "No countersubject") == 0);
        if (seed <= 3)
            std::printf ("seed %u: %zu notes flagged (parallels %d, crossings %d, accented dissonances %d, range %d)\n", seed, vs.size(),
                         count (vs, "Parallel fifths") + count (vs, "Parallel octaves"), count (vs, "Voice crossing"),
                         count (vs, "Accented dissonance"), count (vs, "Out of range"));
    }

    // A generated line against a single voice must not create parallels or crossings.
    for (unsigned seed = 1; seed <= 30; ++seed)
    {
        Project p = satb();
        p.voices[1].clips.push_back (clipOf (p, Role::Subject, 0, 8 * PPQ, demoSubject()));
        Clip c = clipOf (p, Role::Countersubject, 0, 8 * PPQ, {});
        c.notes = generateLine (p, p.voices[2].id, 0, 8 * PPQ, { seed, PPQ / 2, true });
        CHECK (! c.notes.empty());
        Tick covered = 0;
        for (auto& n : c.notes) { covered += n.length; CHECK (inScale (n.pitch, p.key)); CHECK (n.pitch >= 48 && n.pitch <= 67); }
        CHECK (covered == 8 * PPQ);
        p.voices[2].clips.push_back (c);
        auto vs = checkProject (p);
        CHECK (count (vs, "Parallel fifths") + count (vs, "Parallel octaves") == 0);
        CHECK (count (vs, "Voice crossing") == 0);
    }

    // --- Layer locks ---
    {
        Project p = satb();
        p.voices[1].lock = Role::Subject;
        p.voices[0].lock = Role::Answer;
        p.voices[2].lock = Role::Countersubject;
        p.voices[1].clips.push_back (clipOf (p, Role::Subject, 0, 8 * PPQ, demoSubject()));
        CHECK (count (checkProject (p), "Layer lock") == 0);

        // Material produced for locked layers satisfies the lock.
        Tick len = 4 * PPQ;
        auto ans = lockedLayerMaterial (p, p.voices[0].id, Role::Answer, 8 * PPQ, len, 1);
        CHECK (len == 8 * PPQ && ! ans.empty());
        p.voices[0].clips.push_back (clipOf (p, Role::Answer, 8 * PPQ, len, ans));
        auto again = lockedLayerMaterial (p, p.voices[1].id, Role::Subject, 16 * PPQ, len, 1);
        p.voices[1].clips.push_back (clipOf (p, Role::Subject, 16 * PPQ, len, transposed (again, 12)));
        Tick csLen = 8 * PPQ;
        auto cs = lockedLayerMaterial (p, p.voices[2].id, Role::Countersubject, 8 * PPQ, csLen, 3);
        p.voices[2].clips.push_back (clipOf (p, Role::Countersubject, 8 * PPQ, csLen, cs));
        CHECK (count (checkProject (p), "Layer lock") == 0);

        // Breaking each lock is reported.
        p.voices[0].clips[0].notes[3].pitch += 1;
        CHECK (count (checkProject (p), "Layer lock") == 1);
        p.voices[1].clips[1].notes[2].start += PPQ / 4;
        CHECK (count (checkProject (p), "Layer lock") == 2);
        p.voices[2].clips[0].start = 40 * PPQ; // no entry sounds there
        CHECK (count (checkProject (p), "Layer lock") == 3);
        p.voices[2].clips[0].role = Role::Free;
        CHECK (count (checkProject (p), "Layer lock") == 3);

        // A real answer is accepted too, and an exposition refuses to run over locked layers.
        p.voices[0].clips[0].notes = fitToRange (makeAnswer (demoSubject(), p.key, false), 60, 81);
        CHECK (count (checkProject (p), "Layer lock") == 2);
        CHECK (! generateExposition (p, p.voices[1].id, p.voices[1].clips[0].id, true, 1).ok);
    }

    std::printf (failures == 0 ? "All core tests passed\n" : "%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
