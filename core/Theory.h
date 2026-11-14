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

const char* modeName (Mode);   // "major", "harmonic minor", "dorian" ...
bool isMinorMode (Mode);       // natural, harmonic or melodic minor

/** True if the pitch belongs to exactly the key's scale. */
bool inScale (int pitch, Key k);

/** Scale membership as counterpoint uses it: like inScale, except that the minor scales are
    pooled so both forms of the sixth and seventh degree are available (the leading note in particular). */
bool inCounterpointScale (int pitch, Key k);

/** Bit i is set if pitch class i (C = 0) belongs to the key's scale. */
int scaleMask (Key k);

/** The nearest pitch in the scale. Ties go in the direction given (+1 up, otherwise down). */
int snapToScale (int pitch, Key k, int direction = 0);

/** The next scale pitch above (direction > 0) or below the given pitch. */
int scaleStep (int pitch, Key k, int direction);

/** The key a fifth above (the dominant key), same mode. */
inline Key dominantKey (Key k) { return { (k.tonic + 7) % 12, k.mode }; }

bool isDissonant (int intervalSemitones); // seconds, sevenths, tritone
bool isPerfect (int intervalSemitones);   // unison/octave or fifth

/** Builds a fugal answer from a subject.
    A real answer transposes everything up a fifth. A tonal answer also swaps
    tonic and dominant in the subject's head so the answer stays anchored to the home key. */
std::vector<Note> makeAnswer (const std::vector<Note>& subject, Key key, bool tonal);

std::vector<Note> transposed (std::vector<Note> notes, int semitones);

/** The clip that defines the fugue's subject: the earliest subject clip that has notes. */
const Clip* referenceSubject (const Project&);

/** True if both lines have the same rhythm and differ only by a constant transposition.
    With octavesOnly, that transposition must be a whole number of octaves. */
bool sameShape (std::vector<Note> a, std::vector<Note> b, bool octavesOnly);

/** Shifts the notes by whole octaves so they sit as well as possible inside [low, high]. */
std::vector<Note> fitToRange (std::vector<Note> notes, int low, int high);
} // namespace fuga
