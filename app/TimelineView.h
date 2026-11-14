#pragma once
#include "AudioEngine.h"
#include "Document.h"
#include "Theme.h"

/** A viewport that reports scrolling, so the timeline and piano roll can stay lined up. */
class LinkedViewport : public juce::Viewport
{
public:
    std::function<void()> onScroll;
    void visibleAreaChanged (const juce::Rectangle<int>&) override { if (onScroll) onScroll(); }
};

/** The arrangement view: a ruler, a row of fugue sections, and one lane per voice holding typed clips. */
class TimelineView : public juce::Component, private juce::ChangeListener
{
public:
    TimelineView (Document&, AudioEngine&);
    ~TimelineView() override;

    LinkedViewport viewport;
    std::function<void()> onManagePlugins;

    int preferredHeight() const;
    void playheadMoved();
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    class Content;
    class Header;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refresh();

    Document& doc;
    AudioEngine& engine;
    std::unique_ptr<Content> content;
    std::vector<std::unique_ptr<Header>> headers;
};

/** Fills a menu with all 24 keys. */
void addKeyItems (juce::PopupMenu&, fuga::Key current, std::function<void (fuga::Key)> choose);
