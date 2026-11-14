#pragma once
#include "Score.h"

namespace fuga
{
const char* noteName (int pitchClass);
std::string pitchName (int midiPitch);
std::string keyName (Key);
const char* roleName (Role);
const char* sectionTypeName (SectionType);

/** Semitone offset of the pitch above the key's tonic, 0-11. */
inline int relPc (int pitch, Key k) { return ((pitch - k.tonic) % 12 + 12) % 12; }

/** True if the pitch belongs to the key's scale. Minor allows both the natural and the raised seventh. */
bool inScale (int pitch, Key k);

/** The key a fifth above (the dominant key), same mode. */
inline Key dominantKey (Key k) { return { (k.tonic + 7) % 12, k.mode }; }

bool isDissonant (int intervalSemitones); // seconds, sevenths, tritone
bool isPerfect (int intervalSemitones);   // unison/octave or fifth

/** Builds a fugal answer from a subject.
    A real answer transposes everything up a fifth. A tonal answer also swaps
    tonic and dominant in the subject's head so the answer stays anchored to the home key. */
std::vector<Note> makeAnswer (const std::vector<Note>& subject, Key key, bool tonal);

std::vector<Note> transposed (std::vector<Note> notes, int semitones);

/** Shifts the notes by whole octaves so they sit as well as possible inside [low, high]. */
std::vector<Note> fitToRange (std::vector<Note> notes, int low, int high);
} // namespace fuga
