#include "PianoRollView.h"
#include <set>

using namespace fuga;

static int yForPitch (int pitch) { return (theme::highPitch - pitch) * theme::rowHeight; }
static int pitchForY (int y) { return juce::jlimit (theme::lowPitch, theme::highPitch, theme::highPitch - y / theme::rowHeight); }
static bool isBlackKey (int pitch) { int pc = pitch % 12; return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10; }

class PianoRollView::Content : public juce::Component
{
public:
    Content (PianoRollView& o) : doc (o.doc), engine (o.engine)
    {
        setWantsKeyboardFocus (true);
        addChildComponent (lasso);
    }

    std::set<int> selection; // indices into the selected clip's notes

    juce::Rectangle<int> noteBounds (const Clip& c, const Note& n) const
    {
        const int x0 = (int) doc.xForTick (c.start + n.start), x1 = (int) doc.xForTick (c.start + n.end());
        return { x0, yForPitch (n.pitch), juce::jmax (3, x1 - x0), theme::rowHeight };
    }

    void paint (juce::Graphics& g) override
    {
        auto& p = doc.project;
        const auto area = g.getClipBounds();
        const Key key = p.keyAt (engine.getPosition());
        auto* voice = doc.voice();
        auto* clip = doc.clip();

        for (int pitch = theme::lowPitch; pitch <= theme::highPitch; ++pitch)
        {
            const int y = yForPitch (pitch);
            if (y > area.getBottom() || y + theme::rowHeight < area.getY()) continue;
            auto c = isBlackKey (pitch) ? theme::lane.darker (0.25f) : theme::lane;
            if (! inScale (pitch, key)) c = c.darker (0.35f);
            if (relPc (pitch, key) == 0) c = c.brighter (0.12f);
            g.setColour (c);
            g.fillRect (area.getX(), y, area.getWidth(), theme::rowHeight);
            g.setColour (theme::bg.withAlpha (0.6f));
            g.fillRect (area.getX(), y + theme::rowHeight - 1, area.getWidth(), 1);
        }

        // Grid
        const Tick bar = p.barLength();
        Tick step = doc.snap;
        while (doc.xForTick (step) < 7) step *= 2;
        const Tick first = std::max<Tick> (0, doc.tickForX (area.getX()) / bar * bar);
        const Tick last = doc.tickForX (area.getRight()) + step;
        for (Tick t = first; t <= last; t += step)
        {
            g.setColour (t % bar == 0 ? theme::gridStrong : t % PPQ == 0 ? theme::grid : theme::grid.withAlpha (0.35f));
            g.fillRect ((int) doc.xForTick (t), area.getY(), 1, area.getHeight());
        }

        // The voice's comfortable range
        if (voice != nullptr)
        {
            g.setColour (theme::accent.withAlpha (0.35f));
            g.fillRect (area.getX(), yForPitch (voice->high), area.getWidth(), 1);
            g.fillRect (area.getX(), yForPitch (voice->low) + theme::rowHeight - 1, area.getWidth(), 1);
        }

        // Everything outside the selected clip is dimmed; notes can only be written inside it.
        if (clip != nullptr)
        {
            const int x0 = (int) doc.xForTick (clip->start), x1 = (int) doc.xForTick (clip->end());
            g.setColour (juce::Colours::black.withAlpha (0.38f));
            g.fillRect (area.getX(), area.getY(), juce::jmax (0, x0 - area.getX()), area.getHeight());
            g.fillRect (x1, area.getY(), juce::jmax (0, area.getRight() - x1), area.getHeight());
            g.setColour (theme::role (clip->role));
            g.fillRect (x0 - 1, area.getY(), 2, area.getHeight());
            g.fillRect (x1 - 1, area.getY(), 2, area.getHeight());
        }
        else
        {
            g.setColour (juce::Colours::black.withAlpha (0.38f));
            g.fillRect (area);
        }

        // Ghost notes: other voices, then this voice's other clips
        for (size_t i = 0; i < p.voices.size(); ++i)
        {
            auto& v = p.voices[i];
            const bool own = voice != nullptr && v.id == voice->id;
            for (auto& c : v.clips)
            {
                if (own && clip != nullptr && c.id == clip->id) continue;
                for (auto& n : c.notes)
                {
                    if (n.start >= c.length) continue;
                    auto b = noteBounds (c, n);
                    b.setRight (juce::jmin (b.getRight(), (int) doc.xForTick (c.end())));
                    if (! b.intersects (area)) continue;
                    g.setColour (theme::voice ((int) i).withAlpha (own ? 0.55f : 0.32f));
                    g.fillRect (b.reduced (0, own ? 1 : 3));
                }
            }
        }

        if (clip != nullptr)
        {
            const auto colour = theme::role (clip->role);
            for (size_t i = 0; i < clip->notes.size(); ++i)
            {
                auto& n = clip->notes[i];
                auto b = noteBounds (*clip, n);
                if (! b.intersects (area)) continue;
                const bool selected = selection.count ((int) i) > 0;
                const bool outside = n.start >= clip->length;
                g.setColour ((selected ? colour.brighter (0.5f) : colour).withAlpha (outside ? 0.3f : 1.0f));
                g.fillRoundedRectangle (b.toFloat().reduced (0.0f, 0.5f), 2.0f);
                g.setColour (selected ? juce::Colours::white : colour.darker (0.6f));
                g.drawRoundedRectangle (b.toFloat().reduced (0.5f, 1.0f), 2.0f, 1.0f);
                if (b.getWidth() > 26)
                {
                    g.setColour (juce::Colours::black.withAlpha (0.75f));
                    g.setFont (9.5f);
                    g.drawText (pitchName (n.pitch), b.reduced (3, 0), juce::Justification::centredLeft, false);
                }
            }
        }

        // Rule violations that involve this voice: ring the notes sounding there.
        if (voice != nullptr && clip != nullptr)
            for (auto& v : doc.violations)
            {
                if (v.voiceA != voice->id && v.voiceB != voice->id) continue;
                g.setColour (theme::severity (v.severity));
                for (auto& n : clip->notes)
                {
                    const Tick s = clip->start + n.start, e = clip->start + n.end();
                    if (s < v.time + std::max<Tick> (1, v.length) && e > v.time)
                        g.drawRoundedRectangle (noteBounds (*clip, n).toFloat().expanded (1.5f), 3.0f, 1.6f);
                }
            }

        if (clip == nullptr)
        {
            g.setColour (theme::textDim);
            g.setFont (14.0f);
            auto vis = getParentComponent() != nullptr ? getParentComponent()->getLocalBounds() + juce::Point<int> (-getX(), -getY()) : getLocalBounds();
            g.drawText ("Select a clip in the timeline to edit its notes, or double-click a voice lane to create one.",
                        vis, juce::Justification::centred, true);
        }

        g.setColour (theme::playhead);
        g.fillRect ((int) doc.xForTick (engine.getPosition()), area.getY(), 1, area.getHeight());
    }

    // ------------------------------------------------------------ mouse

    int noteAt (juce::Point<int> pos, bool& onEdge) const
    {
        auto* clip = const_cast<Document&> (doc).clip();
        onEdge = false;
        if (clip == nullptr) return -1;
        for (int i = (int) clip->notes.size(); --i >= 0;)
        {
            auto b = noteBounds (*clip, clip->notes[(size_t) i]);
            if (b.contains (pos))
            {
                onEdge = b.getWidth() > 10 && pos.x > b.getRight() - 6;
                return i;
            }
        }
        return -1;
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        bool edge = false;
        noteAt (e.getPosition(), edge);
        setMouseCursor (edge ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
    }

    void preview (int pitch)
    {
        if (pitch == previewPitch) return;
        stopPreview();
        previewPitch = pitch;
        if (! engine.isPlaying()) engine.keyboardState.noteOn (1, pitch, 0.7f);
        else previewPitch = -1;
    }

    void stopPreview()
    {
        if (previewPitch >= 0) engine.keyboardState.noteOff (1, previewPitch, 0.0f);
        previewPitch = -1;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();
        mode = Mode::none;
        editBegun = false;
        auto* clip = doc.clip();
        const Tick t = doc.tickForX (e.x);

        if (clip == nullptr || t < clip->start || t >= clip->end())
        {
            // Clicking another clip of this voice selects it.
            if (auto* v = doc.voice())
                for (auto& c : v->clips)
                    if (t >= c.start && t < c.end()) { selection.clear(); doc.select (v->id, c.id); return; }
            bool edge;
            if (clip == nullptr || noteAt (e.getPosition(), edge) < 0) return;
        }

        bool edge = false;
        const int hit = noteAt (e.getPosition(), edge);
        if (hit >= 0)
        {
            if (e.mods.isShiftDown())
            {
                if (selection.count (hit)) selection.erase (hit); else selection.insert (hit);
            }
            else if (selection.count (hit) == 0)
            {
                selection = { hit };
            }
            mode = edge ? Mode::resize : Mode::move;
            original = clip->notes;
            downTick = t;
            downPitch = pitchForY (e.y);
            preview (clip->notes[(size_t) hit].pitch);
            repaint();
            return;
        }

        if (e.mods.isShiftDown() || e.mods.isCommandDown() || e.mods.isPopupMenu())
        {
            mode = Mode::lasso;
            lassoStart = e.getPosition();
            if (! e.mods.isShiftDown()) selection.clear();
            repaint();
            return;
        }

        // Write a new note, then keep dragging to set its length.
        doc.beginEdit();
        editBegun = true;
        Note n;
        n.start = doc.snapFloor (t - clip->start, doc.snap);
        n.length = std::min (lastLength, clip->length - n.start);
        n.pitch = pitchForY (e.y);
        clip->notes.push_back (n);
        selection = { (int) clip->notes.size() - 1 };
        original = clip->notes;
        downTick = t;
        downPitch = n.pitch;
        mode = Mode::resize;
        newNote = true;
        preview (n.pitch);
        doc.changed();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto* clip = doc.clip();
        if (clip == nullptr || mode == Mode::none) return;

        if (mode == Mode::lasso)
        {
            auto r = juce::Rectangle<int> (lassoStart, e.getPosition());
            lasso.setBounds (r);
            lasso.setVisible (true);
            if (! e.mods.isShiftDown()) selection.clear();
            for (size_t i = 0; i < clip->notes.size(); ++i)
                if (noteBounds (*clip, clip->notes[i]).intersects (r)) selection.insert ((int) i);
            repaint();
            return;
        }

        if (original.size() != clip->notes.size()) return;
        const Tick delta = doc.tickForX (e.x) - downTick;
        const int dPitch = pitchForY (e.y) - downPitch;
        if (! editBegun)
        {
            if (std::abs (e.getDistanceFromDragStartX()) < 3 && dPitch == 0) return;
            doc.beginEdit();
            editBegun = true;
        }

        for (int i : selection)
        {
            if (i < 0 || i >= (int) original.size()) continue;
            auto& n = clip->notes[(size_t) i];
            const auto& o = original[(size_t) i];
            if (mode == Mode::move)
            {
                n.start = juce::jlimit<Tick> (0, std::max<Tick> (0, clip->length - doc.snap), doc.snapRound (o.start + delta, doc.snap));
                n.pitch = juce::jlimit (theme::lowPitch, theme::highPitch, o.pitch + dPitch);
            }
            else
            {
                if (newNote)
                {
                    // Drag out the length of a freshly written note; a plain click keeps the last length used.
                    if (std::abs (e.getDistanceFromDragStartX()) < 4) continue;
                    const Tick raw = doc.tickForX (e.x) - (clip->start + o.start);
                    n.length = std::max (doc.snap, ((raw + doc.snap - 1) / doc.snap) * doc.snap);
                }
                else
                    n.length = std::max (doc.snap, doc.snapRound (o.length + delta, doc.snap));
            }
        }
        if (mode == Mode::move && ! selection.empty())
        {
            const int i = *selection.begin();
            if (i < (int) clip->notes.size()) preview (clip->notes[(size_t) i].pitch);
        }
        doc.changed();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        stopPreview();
        if (auto* clip = doc.clip(); clip != nullptr && mode == Mode::resize && selection.size() == 1)
        {
            const int i = *selection.begin();
            if (i < (int) clip->notes.size()) lastLength = clip->notes[(size_t) i].length;
        }
        lasso.setVisible (false);
        mode = Mode::none;
        newNote = false;
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        bool edge;
        const int hit = noteAt (e.getPosition(), edge);
        if (hit < 0) return;
        // The first click of the double-click may have added a note; either way remove what is here.
        selection = { hit };
        deleteSelection();
    }

    void deleteSelection()
    {
        if (doc.clip() == nullptr || selection.empty()) return;
        auto sel = selection;
        selection.clear();
        doc.edit ([&] (Project&)
        {
            auto& notes = doc.clip()->notes;
            for (auto it = sel.rbegin(); it != sel.rend(); ++it)
                if (*it >= 0 && *it < (int) notes.size()) notes.erase (notes.begin() + *it);
        });
    }

    void nudge (Tick dt, int dp)
    {
        auto* clip = doc.clip();
        if (clip == nullptr || selection.empty()) return;
        doc.edit ([&] (Project&)
        {
            for (int i : selection)
                if (i >= 0 && i < (int) clip->notes.size())
                {
                    auto& n = clip->notes[(size_t) i];
                    n.start = juce::jlimit<Tick> (0, std::max<Tick> (0, clip->length - doc.snap), n.start + dt);
                    n.pitch = juce::jlimit (theme::lowPitch, theme::highPitch, n.pitch + dp);
                }
        });
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        auto* clip = doc.clip();
        if (clip == nullptr) return false;
        if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey)
        {
            if (selection.empty()) return false;
            deleteSelection();
            return true;
        }
        if (k == juce::KeyPress ('a', juce::ModifierKeys::commandModifier, 0))
        {
            selection.clear();
            for (int i = 0; i < (int) clip->notes.size(); ++i) selection.insert (i);
            repaint();
            return true;
        }
        if (selection.empty()) return false;
        const bool shift = k.getModifiers().isShiftDown();
        if (k.isKeyCode (juce::KeyPress::upKey)) { nudge (0, shift ? 12 : 1); return true; }
        if (k.isKeyCode (juce::KeyPress::downKey)) { nudge (0, shift ? -12 : -1); return true; }
        if (k.isKeyCode (juce::KeyPress::leftKey)) { nudge (-doc.snap, 0); return true; }
        if (k.isKeyCode (juce::KeyPress::rightKey)) { nudge (doc.snap, 0); return true; }
        return false;
    }

private:
    enum class Mode { none, move, resize, lasso };
    Document& doc;
    AudioEngine& engine;
    Mode mode = Mode::none;
    bool editBegun = false, newNote = false;
    std::vector<Note> original;
    Tick downTick = 0, lastLength = PPQ / 2;
    int downPitch = 60, previewPitch = -1;
    juce::Point<int> lassoStart;

    struct Lasso : juce::Component
    {
        Lasso() { setInterceptsMouseClicks (false, false); }
        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colours::white.withAlpha (0.12f));
            g.setColour (juce::Colours::white.withAlpha (0.6f));
            g.drawRect (getLocalBounds());
        }
    } lasso;
};

// ================================================================

PianoRollView::PianoRollView (Document& d, AudioEngine& e) : doc (d), engine (e)
{
    content = std::make_unique<Content> (*this);
    addAndMakeVisible (viewport);
    viewport.setViewedComponent (content.get(), false);
    viewport.setScrollBarsShown (true, false);
    doc.addChangeListener (this);
}

PianoRollView::~PianoRollView() { doc.removeChangeListener (this); }

void PianoRollView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (doc.selectedVoice != lastVoice || doc.selectedClip != lastClip)
    {
        content->selection.clear();
        const bool voiceChanged = doc.selectedVoice != lastVoice;
        lastVoice = doc.selectedVoice;
        lastClip = doc.selectedClip;
        if (voiceChanged) centreOnVoice();
    }
    if (auto* c = doc.clip())
        std::erase_if (content->selection, [&] (int i) { return i >= (int) c->notes.size(); });
    resized();
    content->repaint();
    repaint();
}

void PianoRollView::centreOnVoice()
{
    if (auto* v = doc.voice())
    {
        const int mid = yForPitch ((v->low + v->high) / 2);
        viewport.setViewPosition (viewport.getViewPositionX(), juce::jmax (0, mid - viewport.getHeight() / 2));
    }
}

void PianoRollView::resized()
{
    viewport.setBounds (getLocalBounds().withTrimmedLeft (theme::headerWidth));
    const fuga::Tick bar = doc.project.barLength();
    const int w = (int) doc.xForTick (std::max (doc.project.endTime() + 16 * bar, 64 * bar));
    const int h = (theme::highPitch - theme::lowPitch + 1) * theme::rowHeight;
    if (content->getWidth() != juce::jmax (w, viewport.getWidth()) || content->getHeight() != h)
        content->setSize (juce::jmax (w, viewport.getWidth()), h);
    if (lastVoice == -2 && viewport.getHeight() > 0)
    {
        lastVoice = doc.selectedVoice;
        lastClip = doc.selectedClip;
        centreOnVoice();
    }
}

void PianoRollView::paint (juce::Graphics& g)
{
    // The keyboard column, drawn to follow the viewport's vertical scroll.
    g.fillAll (theme::panel);
    const int keyW = 54, x0 = theme::headerWidth - keyW;
    const int offset = viewport.getViewPositionY();
    const Key key = doc.project.keyAt (engine.getPosition());
    for (int pitch = theme::lowPitch; pitch <= theme::highPitch; ++pitch)
    {
        const int y = yForPitch (pitch) - offset;
        if (y > getHeight() || y + theme::rowHeight < 0) continue;
        g.setColour (isBlackKey (pitch) ? juce::Colour (0xff2a2a2e) : juce::Colour (0xffd8d8dc));
        g.fillRect (x0, y, keyW, theme::rowHeight - 1);
        if (pitch % 12 == 0 || relPc (pitch, key) == 0)
        {
            g.setColour (isBlackKey (pitch) ? juce::Colours::white : juce::Colours::black);
            g.setFont (9.5f);
            g.drawText (pitchName (pitch), x0, y, keyW - 4, theme::rowHeight - 1, juce::Justification::centredRight);
        }
    }

    g.setColour (theme::panel);
    g.fillRect (0, 0, x0, getHeight());
    auto info = juce::Rectangle<int> (12, 8, x0 - 18, 80);
    if (auto* v = doc.voice())
    {
        g.setColour (theme::voice (doc.project.voiceIndex (v->id)));
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawText (v->name, info.removeFromTop (20), juce::Justification::centredLeft);
        g.setFont (11.5f);
        if (auto* c = doc.clip())
        {
            g.setColour (theme::role (c->role));
            g.drawText (roleName (c->role), info.removeFromTop (16), juce::Justification::centredLeft);
        }
        g.setColour (theme::textDim);
        g.drawText ("Range " + pitchName (v->low) + " to " + pitchName (v->high), info.removeFromTop (16), juce::Justification::centredLeft);
        g.drawText (keyName (key), info.removeFromTop (16), juce::Justification::centredLeft);
    }
}

void PianoRollView::playheadMoved() { content->repaint(); }
