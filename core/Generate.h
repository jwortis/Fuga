#pragma once
#include "Score.h"

namespace fuga
{
struct GenOptions
{
    unsigned seed = 1;
    Tick grid = PPQ / 2;     // smallest rhythmic step
    bool invertible = false; // avoid fifths on beats so the line still works when the voices swap
};

/** Composes a line for one voice over [start, start + length) against whatever the other
    voices are already playing there. Returned notes are relative to `start`.
    Uses a dynamic-programming search that minimises a counterpoint cost: consonance on beats,
    stepwise motion, no parallel fifths or octaves, no voice crossing. */
std::vector<Note> generateLine (const Project&, int voiceId, Tick start, Tick length, const GenOptions&);

struct ExpositionResult
{
    bool ok = false;
    std::string error;
    int sectionId = -1;
};

/** Builds a complete exposition from one subject clip: every voice enters in turn with the
    subject or a (tonal) answer, the previous voice takes the countersubject, and earlier voices
    continue in free counterpoint. Existing material in the affected span is replaced. */
ExpositionResult generateExposition (Project&, int subjectVoiceId, int subjectClipId, bool tonalAnswer, unsigned seed);
} // namespace fuga
