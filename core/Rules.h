#pragma once
#include "Score.h"

namespace fuga
{
enum class Severity { Info, Warning, Error };

struct Violation
{
    Severity severity = Severity::Warning;
    std::string rule;    // short name, e.g. "Parallel fifths"
    std::string message; // full sentence for the user
    Tick time = 0;
    Tick length = 0;
    int voiceA = -1, voiceB = -1; // voice ids involved, or -1
    int sectionId = -1;           // section the rule belongs to, or -1
};

/** Runs every counterpoint and section check over the project. Results are sorted by time. */
std::vector<Violation> checkProject (const Project&);
} // namespace fuga
