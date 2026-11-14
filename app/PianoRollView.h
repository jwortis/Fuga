#pragma once
#include "TimelineView.h"

/** Note editor for the selected clip. Shares the timeline's time axis, and shows the other
    voices as ghost notes so the counterpoint is visible while you write. */
class PianoRollView : public juce::Component, private juce::ChangeListener
{
public:
    PianoRollView (Document&, AudioEngine&);
    ~PianoRollView() override;

    LinkedViewport viewport;
    void playheadMoved();
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    class Content;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void centreOnVoice();

    Document& doc;
    AudioEngine& engine;
    std::unique_ptr<Content> content;
    int lastVoice = -2, lastClip = -2;
};
