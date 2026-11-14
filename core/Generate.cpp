#include "Generate.h"
#include "Theory.h"
#include <cmath>
#include <cstdlib>
#include <limits>
#include <random>

namespace fuga
{
namespace
{
struct Context
{
    std::vector<std::vector<Note>> flat; // other voices, absolute time
    std::vector<bool> isBelow;           // true if that voice sits below the generated one
};

struct Slot { Tick time; Tick length; };

std::vector<Slot> makeRhythm (const Context& ctx, Tick start, Tick length, Tick grid, std::mt19937& rng)
{
    std::uniform_real_distribution<double> u (0.0, 1.0);
    bool anyContext = false;
    for (auto& f : ctx.flat)
        for (auto& n : f) anyContext = anyContext || (n.end() > start && n.start < start + length);

    std::vector<Tick> onsets { 0 };
    for (Tick t = grid; t < length; t += grid)
    {
        bool ctxOnset = false;
        for (auto& f : ctx.flat) ctxOnset = ctxOnset || hasOnsetAt (f, start + t);
        const bool onBeat = t % PPQ == 0;
        // Complementary rhythm: move when the others hold, hold when they move.
        double p = ! anyContext ? (onBeat ? 0.8 : 0.45) : (ctxOnset ? (onBeat ? 0.55 : 0.3) : (onBeat ? 0.95 : 0.8));
        if (t - onsets.back() >= 4 * PPQ) p = 1.0;
        if (u (rng) < p) onsets.push_back (t);
    }
    std::vector<Slot> slots;
    for (size_t i = 0; i < onsets.size(); ++i)
        slots.push_back ({ onsets[i], (i + 1 < onsets.size() ? onsets[i + 1] : length) - onsets[i] });
    return slots;
}

double verticalCost (const Context& ctx, Tick t, int p, bool strong, bool invertible)
{
    double cost = 0;
    int lowest = p;
    for (auto& f : ctx.flat) { int c = pitchAt (f, t); if (c >= 0) lowest = std::min (lowest, c); }

    for (size_t v = 0; v < ctx.flat.size(); ++v)
    {
        int c = pitchAt (ctx.flat[v], t);
        if (c < 0) continue;
        int ic = std::abs (p - c) % 12;
        if (isDissonant (p - c)) cost += strong ? 12 : 4;
        else if (ic == 5) cost += (std::min (p, c) == lowest) ? (strong ? 8 : 3) : 1;
        else if (ic == 0) cost += p == c ? 6 : 3;
        else if (ic == 7) cost += invertible ? (strong ? 6 : 2) : 1.5;

        if (ctx.isBelow[v] ? p < c : p > c) cost += 10;              // crossing
        if (std::abs (p - c) < 3 && p != c) cost += 1;                 // crowding
    }
    return cost;
}

bool dissonantAgainst (const Context& ctx, Tick t, int p)
{
    for (auto& f : ctx.flat) { int c = pitchAt (f, t); if (c >= 0 && isDissonant (p - c)) return true; }
    return false;
}

double melodicCost (int p0, int p1, Key key)
{
    int d = std::abs (p1 - p0);
    double cost = d == 0 ? 3 : d <= 2 ? 0 : d <= 4 ? 1.5 : d == 5 ? 2.5 : d == 7 ? 3.5 : d == 12 ? 4.5 : (d == 8 || d == 9) ? 6 : 20;
    if (d == 6) cost = 18;
    if (key.mode == Mode::Minor)
    {
        int r0 = relPc (p0, key), r1 = relPc (p1, key);
        if (r0 == 11 && p1 - p0 != 1) cost += 6;                  // leading note wants to rise
        if (r0 == 10 && p1 > p0) cost += 4;                        // natural 7th should not rise
        if ((r0 == 8 && r1 == 11) || (r0 == 11 && r1 == 8)) cost += 20; // augmented second
        if (r1 == 11 && p1 < p0) cost += 3;
    }
    return cost;
}
} // namespace

std::vector<Note> generateLine (const Project& project, int voiceId, Tick start, Tick length, const GenOptions& opt)
{
    const int vi = project.voiceIndex (voiceId);
    if (vi < 0 || length <= 0) return {};
    const Voice& voice = project.voices[(size_t) vi];
    const Key key = project.keyAt (start);
    std::mt19937 rng (opt.seed * 2654435761u + 12345u);
    std::uniform_real_distribution<double> noise (0.0, 1.5);

    Context ctx;
    for (size_t i = 0; i < project.voices.size(); ++i)
        if ((int) i != vi)
        {
            ctx.flat.push_back (flatten (project.voices[i]));
            ctx.isBelow.push_back ((int) i > vi);
        }

    // Where this voice was just before, so the new line joins on smoothly.
    int previousPitch = -1;
    {
        auto own = flatten (voice);
        for (auto& n : own) if (n.end() <= start && start - n.end() <= 2 * PPQ) previousPitch = n.pitch;
    }

    const Tick grid = std::max<Tick> (opt.grid, PPQ / 4);
    auto slots = makeRhythm (ctx, start, length, grid, rng);

    std::vector<int> pitches;
    for (int p = voice.low; p <= voice.high; ++p) if (inScale (p, key)) pitches.push_back (p);
    if (pitches.empty() || slots.empty()) return {};

    const size_t N = slots.size(), P = pitches.size();
    const double centre = (voice.low + voice.high) / 2.0;
    const double inf = std::numeric_limits<double>::infinity();
    std::vector<std::vector<double>> best (N, std::vector<double> (P, inf));
    std::vector<std::vector<int>> from (N, std::vector<int> (P, -1));

    auto unary = [&] (size_t i, int p)
    {
        const Tick t = start + slots[i].time;
        const bool strong = slots[i].time % PPQ == 0;
        double c = verticalCost (ctx, t, p, strong, opt.invertible) + noise (rng) + 0.06 * std::abs (p - centre);
        // The held note also has to survive what the other voices do underneath it.
        for (Tick s = slots[i].time + grid; s < slots[i].time + slots[i].length; s += grid)
            c += 0.35 * verticalCost (ctx, start + s, p, false, false);
        if (i + 1 == N)
        {
            int rel = relPc (p, key);
            if (rel != 0 && rel != 7 && rel != 3 && rel != 4) c += 3; // settle on a chord tone
        }
        return c;
    };

    for (size_t j = 0; j < P; ++j)
    {
        best[0][j] = unary (0, pitches[j]);
        if (previousPitch >= 0) best[0][j] += melodicCost (previousPitch, pitches[j], key);
    }

    for (size_t i = 1; i < N; ++i)
    {
        const Tick t0 = start + slots[i - 1].time, t1 = start + slots[i].time;
        for (size_t j = 0; j < P; ++j)
        {
            const int p1 = pitches[j];
            const double u = unary (i, p1);
            for (size_t k = 0; k < P; ++k)
            {
                if (best[i - 1][k] == inf) continue;
                const int p0 = pitches[k];
                double c = best[i - 1][k] + u + melodicCost (p0, p1, key);

                for (auto& f : ctx.flat)
                {
                    int c0 = pitchAt (f, t0), c1 = pitchAt (f, t1);
                    if (c0 < 0 || c1 < 0 || p0 == p1) continue;
                    const bool similar = c0 != c1 && ((p1 - p0) > 0) == ((c1 - c0) > 0);
                    if (similar && isPerfect (p1 - c1))
                        c += (std::abs (p1 - c1) % 12 == std::abs (p0 - c0) % 12) ? 40 : (std::abs (p1 - p0) > 2 ? 4 : 0);
                    if (c0 != c1 && ! similar) c -= 0.5; // contrary motion
                }
                if (dissonantAgainst (ctx, t0, p0) && std::abs (p1 - p0) > 2) c += 6; // dissonances leave by step
                if (dissonantAgainst (ctx, t1, p1) && std::abs (p1 - p0) > 2) c += 4; // and arrive by step

                if (c < best[i][j]) { best[i][j] = c; from[i][j] = (int) k; }
            }
        }
    }

    size_t j = 0;
    for (size_t k = 1; k < P; ++k) if (best[N - 1][k] < best[N - 1][j]) j = k;

    std::vector<Note> out (N);
    for (size_t i = N; i-- > 0;)
    {
        out[i] = { slots[i].time, slots[i].length, pitches[j], 90 };
        if (i > 0) j = (size_t) from[i][j];
    }
    // Merge repeated pitches into longer notes so the line breathes.
    std::vector<Note> merged;
    for (auto& n : out)
    {
        if (! merged.empty() && merged.back().pitch == n.pitch && merged.back().length + n.length <= 2 * PPQ)
            merged.back().length += n.length;
        else
            merged.push_back (n);
    }
    return merged;
}

ExpositionResult generateExposition (Project& p, int subjectVoiceId, int subjectClipId, bool tonalAnswer, unsigned seed)
{
    ExpositionResult result;
    Voice* sv = p.findVoice (subjectVoiceId);
    Clip* sc = sv ? sv->findClip (subjectClipId) : nullptr;
    if (sc == nullptr || sc->notes.empty()) { result.error = "Select a clip that contains the subject first."; return result; }
    if (p.voices.size() < 2) { result.error = "An exposition needs at least two voices."; return result; }

    sc->role = Role::Subject;
    const Clip subject = *sc;
    const Tick len = subject.length, t0 = subject.start;
    const Key key = p.keyAt (t0);
    const int n = (int) p.voices.size();
    const int startIndex = p.voiceIndex (subjectVoiceId);

    // Entry order: the subject's voice, then the voices above it nearest first, then those below.
    std::vector<int> order { startIndex };
    for (int i = startIndex - 1; i >= 0; --i) order.push_back (i);
    for (int i = startIndex + 1; i < n; ++i) order.push_back (i);

    const Tick total = len * n;
    // Clear everything else in the span.
    for (auto& v : p.voices)
        std::erase_if (v.clips, [&] (const Clip& c) { return c.id != subject.id && c.start < t0 + total && c.end() > t0; });
    std::erase_if (p.sections, [&] (const Section& s) { return s.start < t0 + total && s.end() > t0; });

    Section sec;
    sec.id = p.newId();
    sec.type = SectionType::Exposition;
    sec.start = t0;
    sec.length = total;
    sec.key = key;
    p.sections.push_back (sec);
    std::sort (p.sections.begin(), p.sections.end(), [] (const Section& a, const Section& b) { return a.start < b.start; });
    result.sectionId = sec.id;

    const auto answerNotes = makeAnswer (subject.notes, key, tonalAnswer);
    std::vector<Note> countersubject; // as first written, against the answer

    auto addClip = [&] (int voiceIdx, Role role, Tick start, std::vector<Note> notes)
    {
        Clip c;
        c.id = p.newId();
        c.role = role;
        c.start = start;
        c.length = len;
        c.notes = std::move (notes);
        p.voices[(size_t) voiceIdx].clips.push_back (std::move (c));
    };

    Tick grid = PPQ / 2;
    for (auto& nn : subject.notes) if (nn.length >= PPQ / 4) grid = std::min (grid, nn.length);

    for (int k = 1; k < n; ++k)
    {
        const Tick start = t0 + len * k;
        const int vIdx = order[(size_t) k];
        const Voice& v = p.voices[(size_t) vIdx];
        const bool isAnswer = k % 2 == 1;
        addClip (vIdx, isAnswer ? Role::Answer : Role::Subject, start,
                 fitToRange (isAnswer ? answerNotes : subject.notes, v.low, v.high));

        // The previous entrant takes the countersubject.
        const int csIdx = order[(size_t) k - 1];
        const Voice& csVoice = p.voices[(size_t) csIdx];
        if (k == 1)
        {
            GenOptions o { seed, grid, true };
            countersubject = generateLine (p, csVoice.id, start, len, o);
            addClip (csIdx, Role::Countersubject, start, countersubject);
        }
        else
        {
            // Against a subject entry the countersubject comes back down a fifth.
            auto cs = isAnswer ? countersubject : transposed (countersubject, -7);
            addClip (csIdx, Role::Countersubject, start, fitToRange (std::move (cs), csVoice.low, csVoice.high));
        }

        // Everyone who entered earlier continues in free counterpoint.
        for (int e = 0; e + 1 < k; ++e)
        {
            const int fIdx = order[(size_t) e];
            GenOptions o { seed + (unsigned) (k * 31 + e * 7), grid, false };
            auto line = generateLine (p, p.voices[(size_t) fIdx].id, start, len, o);
            addClip (fIdx, Role::Free, start, std::move (line));
        }
    }

    for (auto& v : p.voices)
        std::sort (v.clips.begin(), v.clips.end(), [] (const Clip& a, const Clip& b) { return a.start < b.start; });
    result.ok = true;
    return result;
}
} // namespace fuga
