#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../core/Rules.h"

namespace theme
{
constexpr int headerWidth = 190;
constexpr int rulerHeight = 22;
constexpr int sectionRowHeight = 26;
constexpr int laneHeight = 64;
constexpr int rowHeight = 12;   // piano roll row
constexpr int lowPitch = 24;    // C1
constexpr int highPitch = 108;  // C8

inline const juce::Colour bg { 0xff1b1c21 };
inline const juce::Colour panel { 0xff23252c };
inline const juce::Colour lane { 0xff272931 };
inline const juce::Colour laneAlt { 0xff2c2e37 };
inline const juce::Colour grid { 0xff3a3d48 };
inline const juce::Colour gridStrong { 0xff555968 };
inline const juce::Colour text { 0xffe4e6ee };
inline const juce::Colour textDim { 0xff9a9eae };
inline const juce::Colour accent { 0xfff0b445 };
inline const juce::Colour playhead { 0xffffffff };

inline juce::Colour role (fuga::Role r)
{
    switch (r)
    {
        case fuga::Role::Subject: return juce::Colour (0xffe8853d);
        case fuga::Role::Answer: return juce::Colour (0xff3da5e8);
        case fuga::Role::Countersubject: return juce::Colour (0xffb06be0);
        case fuga::Role::Free: return juce::Colour (0xff6fae7a);
    }
    return juce::Colours::grey;
}

inline juce::Colour section (fuga::SectionType t)
{
    switch (t)
    {
        case fuga::SectionType::Exposition: return juce::Colour (0xffc9a227);
        case fuga::SectionType::Episode: return juce::Colour (0xff4f9d9d);
        case fuga::SectionType::MiddleEntry: return juce::Colour (0xffd0703c);
        case fuga::SectionType::Stretto: return juce::Colour (0xffc2456b);
        case fuga::SectionType::Pedal: return juce::Colour (0xff7a6bd0);
        case fuga::SectionType::Coda: return juce::Colour (0xff7d8a99);
    }
    return juce::Colours::grey;
}

inline juce::Colour voice (int index)
{
    static const juce::uint32 c[] = { 0xffef6f6c, 0xfff2c14e, 0xff5fc9a8, 0xff6c9bef, 0xffc77dff, 0xffff9f6b };
    return juce::Colour (c[((index % 6) + 6) % 6]);
}

inline juce::Colour severity (fuga::Severity s)
{
    switch (s)
    {
        case fuga::Severity::Info: return juce::Colour (0xff7fa8d8);
        case fuga::Severity::Warning: return juce::Colour (0xfff0b445);
        case fuga::Severity::Error: return juce::Colour (0xffef5350);
    }
    return juce::Colours::grey;
}
} // namespace theme
