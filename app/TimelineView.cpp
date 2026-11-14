#include "TimelineView.h"

using namespace fuga;

void addKeyItems (juce::PopupMenu& menu, Key current, std::function<void (Key)> choose)
{
    for (auto mode : { Mode::Major, Mode::Minor })
    {
        juce::PopupMenu sub;
        for (int pc = 0; pc < 12; ++pc)
        {
            Key k { pc, mode };
            sub.addItem (keyName (k), true, k == current, [choose, k] { choose (k); });
        }
        menu.addSubMenu (mode == Mode::Major ? "Major" : "Minor", sub);
    }
}

static unsigned newSeed() { return (unsigned) juce::Random::getSystemRandom().nextInt(); }

// ================================================================ Voice header

class TimelineView::Header : public juce::Component
{
public:
    Header (TimelineView& o, int id) : owner (o), voiceId (id)
    {
        addAndMakeVisible (name);
        name.setEditable (false, true);
        name.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        name.setColour (juce::Label::textColourId, theme::text);
        name.addMouseListener (this, false);
        name.onTextChange = [this]
        {
            auto text = name.getText().trim().toStdString();
            if (! text.empty())
                owner.doc.edit ([&] (Project& p) { if (auto* v = p.findVoice (voiceId)) v->name = text; });
        };

        for (auto* b : { &mute, &solo })
        {
            addAndMakeVisible (*b);
            b->setClickingTogglesState (true);
            b->setColour (juce::TextButton::buttonColourId, theme::panel.brighter (0.1f));
        }
        mute.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffd9534f));
        solo.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffe0b93a));
        mute.setTooltip ("Mute");
        solo.setTooltip ("Solo");
        mute.onClick = [this] { owner.doc.edit ([&] (Project& p) { if (auto* v = p.findVoice (voiceId)) v->mute = mute.getToggleState(); }); };
        solo.onClick = [this] { owner.doc.edit ([&] (Project& p) { if (auto* v = p.findVoice (voiceId)) v->solo = solo.getToggleState(); }); };

        addAndMakeVisible (instrument);
        instrument.setColour (juce::TextButton::buttonColourId, theme::panel.brighter (0.1f));
        instrument.setTooltip ("Choose the instrument for this voice");
        instrument.onClick = [this] { showInstrumentMenu(); };

        addAndMakeVisible (gain);
        gain.setSliderStyle (juce::Slider::LinearBar);
        gain.setRange (0.0, 1.2, 0.01);
        gain.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        gain.setColour (juce::Slider::trackColourId, theme::gridStrong);
        gain.setTooltip ("Volume");
        gain.onDragStart = [this] { owner.doc.beginEdit(); };
        gain.onValueChange = [this]
        {
            if (auto* v = owner.doc.project.findVoice (voiceId))
            {
                v->gain = (float) gain.getValue();
                owner.doc.changed();
            }
        };
    }

    void update()
    {
        auto& doc = owner.doc;
        auto* v = doc.project.findVoice (voiceId);
        if (v == nullptr) return;
        if (! name.isBeingEdited()) name.setText (v->name, juce::dontSendNotification);
        mute.setToggleState (v->mute, juce::dontSendNotification);
        solo.setToggleState (v->solo, juce::dontSendNotification);
        gain.setValue (v->gain, juce::dontSendNotification);
        instrument.setButtonText (owner.engine.instrumentName (voiceId));
        resized();
        repaint();
    }

    void resized() override
    {
        auto* v = owner.doc.project.findVoice (voiceId);
        const int badge = v != nullptr && v->lock ? badgeWidth : 0;
        name.setBounds (12, 5, getWidth() - 12 - 60 - badge, 24);
        solo.setBounds (getWidth() - 30, 6, 24, 22);
        mute.setBounds (getWidth() - 56, 6, 24, 22);
        instrument.setBounds (12, 34, getWidth() - 12 - 62, 22);
        gain.setBounds (getWidth() - 56, 36, 50, 18);
    }

    void paint (juce::Graphics& g) override
    {
        const bool selected = owner.doc.selectedVoice == voiceId;
        g.fillAll (selected ? theme::panel.brighter (0.12f) : theme::panel);
        g.setColour (theme::voice (owner.doc.project.voiceIndex (voiceId)));
        g.fillRect (0, 0, selected ? 6 : 4, getHeight());
        if (auto* v = owner.doc.project.findVoice (voiceId); v != nullptr && v->lock)
        {
            // Lock badge: this layer only holds material of one role.
            auto b = juce::Rectangle<int> (getWidth() - 58 - badgeWidth, 8, badgeWidth - 4, 18).toFloat();
            g.setColour (theme::role (*v->lock));
            g.fillRoundedRectangle (b, 4.0f);
            g.setColour (juce::Colours::black.withAlpha (0.85f));
            // a small padlock
            const float lx = b.getX() + 5.0f, ly = b.getY() + 8.0f;
            g.fillRoundedRectangle (lx, ly, 8.0f, 6.5f, 1.0f);
            g.drawEllipse (lx + 1.5f, ly - 4.0f, 5.0f, 7.0f, 1.4f);
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            const char* text = *v->lock == Role::Countersubject ? "CS" : *v->lock == Role::Subject ? "Subj" : *v->lock == Role::Answer ? "Ans" : "Free";
            g.drawText (text, b.withTrimmedLeft (15.0f), juce::Justification::centred);
        }
        g.setColour (theme::bg);
        g.fillRect (0, getHeight() - 1, getWidth(), 1);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        owner.doc.select (voiceId, owner.doc.selectedVoice == voiceId ? owner.doc.selectedClip : -1);
        if (e.mods.isPopupMenu()) showVoiceMenu();
    }

    const int voiceId;
    static constexpr int badgeWidth = 48;

private:
    void showInstrumentMenu()
    {
        auto& doc = owner.doc;
        auto* v = doc.project.findVoice (voiceId);
        if (v == nullptr) return;
        juce::PopupMenu m;
        const int id = voiceId;
        auto* docPtr = &doc;
        m.addItem ("Built-in organ", true, v->instrumentDesc.empty(), [docPtr, id]
        {
            docPtr->edit ([&] (Project& p) { if (auto* vv = p.findVoice (id)) { vv->instrumentDesc.clear(); vv->instrumentState.clear(); } });
        });
        m.addSeparator();
        int count = 0;
        for (auto& type : owner.engine.knownPlugins.getTypes())
        {
            if (! type.isInstrument) continue;
            ++count;
            auto xml = type.createXml();
            auto desc = xml != nullptr ? xml->toString (juce::XmlElement::TextFormat().singleLine()).toStdString() : std::string();
            m.addItem (type.name + "  (" + type.pluginFormatName + ")", true, false, [docPtr, id, desc]
            {
                docPtr->edit ([&] (Project& p) { if (auto* vv = p.findVoice (id)) { vv->instrumentDesc = desc; vv->instrumentState.clear(); } });
            });
        }
        if (count == 0) m.addItem ("No VST3 or AU instruments found yet", false, false, [] {});
        m.addSeparator();
        auto* enginePtr = &owner.engine;
        m.addItem ("Open plugin window", owner.engine.hasPlugin (voiceId), false, [enginePtr, id] { enginePtr->showPluginWindow (id); });
        auto manage = owner.onManagePlugins;
        m.addItem ("Scan for plugins...", true, false, [manage] { if (manage) manage(); });
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (instrument));
    }

    void showVoiceMenu()
    {
        auto* docPtr = &owner.doc;
        const int id = voiceId;
        juce::PopupMenu m, range;
        struct Preset { const char* name; int low, high; };
        for (auto pr : { Preset { "Soprano (C4-A5)", 60, 81 }, Preset { "Alto (G3-D5)", 55, 74 }, Preset { "Tenor (C3-G4)", 48, 67 }, Preset { "Bass (E2-C4)", 40, 60 } })
        {
            auto* v = docPtr->project.findVoice (id);
            range.addItem (pr.name, true, v != nullptr && v->low == pr.low && v->high == pr.high, [docPtr, id, pr]
            {
                docPtr->edit ([&] (Project& p) { if (auto* vv = p.findVoice (id)) { vv->low = pr.low; vv->high = pr.high; } });
            });
        }
        juce::PopupMenu lock;
        {
            auto* v = docPtr->project.findVoice (id);
            auto setLock = [docPtr, id] (std::optional<Role> r)
            {
                docPtr->edit ([&] (Project& p)
                {
                    if (auto* vv = p.findVoice (id))
                    {
                        vv->lock = r;
                        if (r) for (auto& c : vv->clips) c.role = *r; // everything in a locked layer takes its role
                    }
                });
            };
            lock.addItem ("Unlocked (clips choose their own role)", true, v != nullptr && ! v->lock, [setLock] { setLock ({}); });
            lock.addSeparator();
            for (auto r : { Role::Subject, Role::Answer, Role::Countersubject })
                lock.addItem (roleName (r), true, v != nullptr && v->lock == r, [setLock, r] { setLock (r); });
        }
        m.addSubMenu ("Lock layer to role", lock);
        m.addSubMenu ("Range", range);
        const int index = docPtr->project.voiceIndex (id);
        const int n = (int) docPtr->project.voices.size();
        m.addItem ("Move up", index > 0, false, [docPtr, index]
        {
            docPtr->edit ([&] (Project& p) { std::swap (p.voices[(size_t) index], p.voices[(size_t) index - 1]); });
        });
        m.addItem ("Move down", index >= 0 && index < n - 1, false, [docPtr, index]
        {
            docPtr->edit ([&] (Project& p) { std::swap (p.voices[(size_t) index], p.voices[(size_t) index + 1]); });
        });
        m.addItem ("Rename", true, false, [safe = juce::Component::SafePointer<Header> (this)] { if (safe != nullptr) safe->name.showEditor(); });
        m.addSeparator();
        m.addItem ("Delete voice", n > 1, false, [docPtr, id]
        {
            docPtr->edit ([&] (Project& p) { std::erase_if (p.voices, [id] (const Voice& v) { return v.id == id; }); });
        });
        m.showMenuAsync (juce::PopupMenu::Options());
    }

    TimelineView& owner;
    juce::Label name;
    juce::TextButton mute { "M" }, solo { "S" }, instrument;
    juce::Slider gain;
};

// ================================================================ Content

class TimelineView::Content : public juce::Component
{
public:
    Content (TimelineView& o) : owner (o), doc (o.doc), engine (o.engine) { setWantsKeyboardFocus (true); }

    static constexpr int top = theme::rulerHeight + theme::sectionRowHeight;

    int laneAt (int y) const
    {
        if (y < top) return -1;
        int i = (y - top) / theme::laneHeight;
        return i < (int) doc.project.voices.size() ? i : -1;
    }

    juce::Rectangle<int> clipBounds (int lane, const Clip& c) const
    {
        int x0 = (int) doc.xForTick (c.start), x1 = (int) doc.xForTick (c.end());
        return { x0, top + lane * theme::laneHeight + 3, juce::jmax (4, x1 - x0), theme::laneHeight - 7 };
    }

    juce::Rectangle<int> sectionBounds (const Section& s) const
    {
        int x0 = (int) doc.xForTick (s.start), x1 = (int) doc.xForTick (s.end());
        return { x0, theme::rulerHeight + 2, juce::jmax (4, x1 - x0), theme::sectionRowHeight - 4 };
    }

    void paint (juce::Graphics& g) override
    {
        auto& p = doc.project;
        const auto clip = g.getClipBounds();
        g.fillAll (theme::bg);
        const int n = (int) p.voices.size();

        for (int i = 0; i < n; ++i)
        {
            g.setColour (p.voices[(size_t) i].id == doc.selectedVoice ? theme::laneAlt.brighter (0.06f) : (i % 2 ? theme::laneAlt : theme::lane));
            g.fillRect (clip.getX(), top + i * theme::laneHeight, clip.getWidth(), theme::laneHeight - 1);
            if (p.voices[(size_t) i].lock)
            {
                g.setColour (theme::role (*p.voices[(size_t) i].lock).withAlpha (0.09f));
                g.fillRect (clip.getX(), top + i * theme::laneHeight, clip.getWidth(), theme::laneHeight - 1);
            }
        }

        // Sections tint the lanes beneath them.
        for (auto& s : p.sections)
        {
            auto b = sectionBounds (s);
            g.setColour (theme::section (s.type).withAlpha (0.07f));
            g.fillRect (b.getX(), top, b.getWidth(), n * theme::laneHeight);
        }

        // Grid and ruler
        const Tick bar = p.barLength();
        const Tick first = std::max<Tick> (0, doc.tickForX (clip.getX()) / bar * bar);
        const Tick last = doc.tickForX (clip.getRight()) + bar;
        const bool showBeats = doc.pixelsPerBeat >= 14;
        const int barStep = doc.pixelsPerBeat * p.beatsPerBar < 28 ? 4 : 1;
        g.setFont (11.0f);
        for (Tick t = first; t <= last; t += showBeats ? PPQ : bar)
        {
            const int x = (int) doc.xForTick (t);
            const bool isBar = t % bar == 0;
            g.setColour (isBar ? theme::gridStrong.withAlpha (0.8f) : theme::grid.withAlpha (0.6f));
            g.fillRect (x, isBar ? 0 : top, 1, isBar ? getHeight() : n * theme::laneHeight);
            if (isBar && (t / bar) % barStep == 0)
            {
                g.setColour (theme::textDim);
                g.drawText (juce::String (t / bar + 1), x + 4, 2, 60, theme::rulerHeight - 4, juce::Justification::centredLeft);
            }
        }

        // Sections
        for (auto& s : p.sections)
        {
            auto b = sectionBounds (s);
            if (! b.intersects (clip)) continue;
            auto colour = theme::section (s.type);
            g.setColour (colour.withAlpha (0.85f));
            g.fillRoundedRectangle (b.toFloat(), 4.0f);
            int problems = 0;
            Severity worst = Severity::Info;
            for (auto& v : doc.violations)
                if (v.sectionId == s.id && v.severity != Severity::Info) { ++problems; worst = std::max (worst, v.severity); }
            juce::String label = juce::String (sectionTypeName (s.type)) + "  -  " + keyName (s.key);
            if (problems > 0)
            {
                g.setColour (theme::severity (worst));
                g.drawRoundedRectangle (b.toFloat().reduced (1.0f), 4.0f, 2.0f);
                label = "(!) " + label + "   " + juce::String (problems) + (problems == 1 ? " problem" : " problems");
            }
            g.setColour (juce::Colours::black.withAlpha (0.85f));
            g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            g.drawText (label, b.reduced (7, 0), juce::Justification::centredLeft, true);
        }

        // Clips
        for (int i = 0; i < n; ++i)
            for (auto& c : p.voices[(size_t) i].clips)
            {
                auto b = clipBounds (i, c);
                if (! b.intersects (clip)) continue;
                const auto colour = theme::role (c.role);
                const bool selected = c.id == doc.selectedClip && p.voices[(size_t) i].id == doc.selectedVoice;
                g.setColour (colour.withAlpha (selected ? 0.5f : 0.3f));
                g.fillRoundedRectangle (b.toFloat(), 4.0f);
                g.setColour (colour);
                g.fillRoundedRectangle (b.withHeight (14).toFloat(), 4.0f);
                g.setColour (juce::Colours::black.withAlpha (0.85f));
                g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
                g.drawText (roleName (c.role), b.withHeight (14).reduced (5, 0), juce::Justification::centredLeft, true);

                int lo = 127, hi = 0;
                for (auto& nn : c.notes) { lo = std::min (lo, nn.pitch); hi = std::max (hi, nn.pitch); }
                if (hi - lo < 11) { int mid = (hi + lo) / 2; lo = mid - 6; hi = mid + 6; }
                auto area = b.withTrimmedTop (17).reduced (0, 3);
                g.setColour (colour.brighter (0.7f));
                for (auto& nn : c.notes)
                {
                    if (nn.start >= c.length) continue;
                    const float x0 = (float) doc.xForTick (c.start + nn.start);
                    const float x1 = (float) doc.xForTick (c.start + std::min (nn.end(), c.length));
                    const float y = (float) area.getBottom() - (float) (nn.pitch - lo) / (float) (hi - lo) * (float) area.getHeight();
                    g.fillRect (x0, y - 1.5f, juce::jmax (2.0f, x1 - x0 - 1.0f), 3.0f);
                }
                g.setColour (selected ? juce::Colours::white : colour.withAlpha (0.9f));
                g.drawRoundedRectangle (b.toFloat().reduced (0.5f), 4.0f, selected ? 2.0f : 1.0f);
            }

        // Rule markers along the bottom of each lane involved
        for (auto& v : doc.violations)
        {
            const int x = (int) doc.xForTick (v.time);
            if (x < clip.getX() - 10 || x > clip.getRight() + 10) continue;
            g.setColour (theme::severity (v.severity));
            for (int id : { v.voiceA, v.voiceB })
            {
                const int lane = p.voiceIndex (id);
                if (lane < 0) continue;
                const float y = (float) (top + (lane + 1) * theme::laneHeight - 3);
                juce::Path tri;
                tri.addTriangle ((float) x - 4.5f, y, (float) x + 4.5f, y, (float) x, y - 8.0f);
                g.fillPath (tri);
            }
        }

        const int px = (int) doc.xForTick (engine.getPosition());
        g.setColour (theme::playhead);
        g.fillRect (px, 0, 1, getHeight());
        juce::Path head;
        head.addTriangle ((float) px - 5.0f, 0.0f, (float) px + 6.0f, 0.0f, (float) px + 0.5f, 8.0f);
        g.fillPath (head);
    }

    // ------------------------------------------------------------ mouse

    struct ClipHit { int voiceId = -1, clipId = -1; bool edge = false; };

    ClipHit hitClip (juce::Point<int> pos) const
    {
        const int lane = laneAt (pos.y);
        if (lane < 0) return {};
        auto& v = doc.project.voices[(size_t) lane];
        for (auto it = v.clips.rbegin(); it != v.clips.rend(); ++it)
        {
            auto b = clipBounds (lane, *it);
            if (b.contains (pos)) return { v.id, it->id, b.getWidth() > 16 && pos.x > b.getRight() - 7 };
        }
        return {};
    }

    struct SectionHit { int id = -1; int part = 0; }; // part: -1 left edge, 0 body, 1 right edge

    SectionHit hitSection (juce::Point<int> pos) const
    {
        if (pos.y < theme::rulerHeight || pos.y >= top) return {};
        for (auto it = doc.project.sections.rbegin(); it != doc.project.sections.rend(); ++it)
        {
            auto b = sectionBounds (*it);
            if (b.contains (pos))
                return { it->id, b.getWidth() > 20 ? (pos.x < b.getX() + 6 ? -1 : pos.x > b.getRight() - 7 ? 1 : 0) : 0 };
        }
        return {};
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        auto s = hitSection (e.getPosition());
        auto c = hitClip (e.getPosition());
        setMouseCursor ((s.id >= 0 && s.part != 0) || c.edge ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();
        drag = Drag::none;
        editBegun = false;
        downTick = doc.tickForX (e.x);

        if (e.y < theme::rulerHeight)
        {
            drag = Drag::playhead;
            engine.setPosition (doc.snapRound (downTick, doc.snap));
            return;
        }
        if (e.y < top)
        {
            auto hit = hitSection (e.getPosition());
            if (e.mods.isPopupMenu()) { showSectionMenu (hit.id, downTick); return; }
            if (auto* s = doc.project.findSection (hit.id))
            {
                dragId = hit.id;
                origStart = s->start;
                origLength = s->length;
                drag = hit.part < 0 ? Drag::sectionResizeL : hit.part > 0 ? Drag::sectionResizeR : Drag::sectionMove;
            }
            return;
        }

        const int lane = laneAt (e.y);
        if (lane < 0) return;
        auto hit = hitClip (e.getPosition());
        const int voiceId = doc.project.voices[(size_t) lane].id;
        doc.select (voiceId, hit.clipId);
        if (e.mods.isPopupMenu()) { showLaneMenu (voiceId, hit.clipId, downTick); return; }
        if (auto* c = doc.clip(); c != nullptr && hit.clipId >= 0)
        {
            dragId = hit.clipId;
            origStart = c->start;
            origLength = c->length;
            drag = hit.edge ? Drag::clipResize : Drag::clipMove;
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (drag == Drag::none) return;
        const Tick now = doc.tickForX (e.x);
        if (drag == Drag::playhead) { engine.setPosition (doc.snapRound (now, doc.snap)); repaint(); return; }
        if (! editBegun && std::abs (e.getDistanceFromDragStartX()) < 3 && std::abs (e.getDistanceFromDragStartY()) < 8) return;
        if (! editBegun) { doc.beginEdit(); editBegun = true; }

        auto& p = doc.project;
        const Tick bar = p.barLength();
        const Tick delta = now - downTick;

        if (drag == Drag::clipMove || drag == Drag::clipResize)
        {
            auto* v = p.findVoice (doc.selectedVoice);
            auto* c = v ? v->findClip (dragId) : nullptr;
            if (c == nullptr) return;
            if (drag == Drag::clipResize)
                c->length = std::max (doc.snap, doc.snapRound (origLength + delta, doc.snap));
            else
            {
                c->start = doc.snapRound (origStart + delta, doc.snap);
                const int lane = laneAt (e.y);
                if (lane >= 0 && p.voices[(size_t) lane].id != v->id)
                {
                    Clip moved = *c;
                    std::erase_if (v->clips, [&] (const Clip& x) { return x.id == moved.id; });
                    auto& target = p.voices[(size_t) lane];
                    if (target.lock) moved.role = *target.lock;
                    target.clips.push_back (moved);
                    doc.selectedVoice = target.id;
                    doc.selectedClip = moved.id;
                }
            }
        }
        else if (auto* s = p.findSection (dragId))
        {
            if (drag == Drag::sectionMove)
                s->start = doc.snapRound (origStart + delta, bar);
            else if (drag == Drag::sectionResizeR)
                s->length = std::max (bar, doc.snapRound (origLength + delta, bar));
            else
            {
                const Tick end = origStart + origLength;
                s->start = std::min (end - bar, doc.snapRound (origStart + delta, bar));
                s->length = end - s->start;
            }
        }
        doc.changed();
    }

    void mouseUp (const juce::MouseEvent&) override { drag = Drag::none; }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const Tick t = doc.tickForX (e.x);
        if (e.y >= theme::rulerHeight && e.y < top)
        {
            if (hitSection (e.getPosition()).id < 0)
            {
                bool hasExposition = false;
                for (auto& s : doc.project.sections) hasExposition = hasExposition || s.type == SectionType::Exposition;
                addSection (hasExposition ? SectionType::Episode : SectionType::Exposition, t);
            }
            return;
        }
        const int lane = laneAt (e.y);
        if (lane >= 0 && hitClip (e.getPosition()).clipId < 0)
            addClip (doc.project.voices[(size_t) lane].id, t, false);
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        if ((k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) && doc.clip() != nullptr)
        {
            deleteClip (doc.selectedVoice, doc.selectedClip);
            return true;
        }
        return false;
    }

    // ------------------------------------------------------------ actions

    void addSection (SectionType type, Tick at)
    {
        doc.edit ([&] (Project& p)
        {
            Section s;
            s.id = p.newId();
            s.type = type;
            s.start = doc.snapFloor (at, p.barLength());
            s.length = 4 * p.barLength();
            s.key = p.key;
            for (auto& other : p.sections) // do not run into the next section
                if (other.start > s.start) s.length = std::min (s.length, other.start - s.start);
            p.sections.push_back (s);
        });
    }

    void addClip (int voiceId, Tick at, bool generate)
    {
        int newId = -1;
        doc.edit ([&] (Project& p)
        {
            auto* v = p.findVoice (voiceId);
            if (v == nullptr) return;
            bool hasSubject = false;
            for (auto& vv : p.voices) for (auto& c : vv.clips) hasSubject = hasSubject || c.role == Role::Subject;
            Clip c;
            c.id = newId = p.newId();
            c.role = hasSubject || generate ? Role::Free : Role::Subject;
            c.start = doc.snapFloor (at, p.barLength());
            c.length = 2 * p.barLength();
            // Line the new clip up with what the other voices are doing at this point.
            Tick lo = -1, hi = -1;
            for (auto& vv : p.voices)
                if (vv.id != voiceId)
                    for (auto& o : vv.clips)
                        if (at >= o.start && at < o.end()) { lo = lo < 0 ? o.start : std::min (lo, o.start); hi = std::max (hi, o.end()); }
            if (lo >= 0) { c.start = lo; c.length = hi - lo; }
            if (v->lock)
            {
                // A locked layer writes its own kind of material.
                c.role = *v->lock;
                if (c.role != Role::Countersubject) { c.start = doc.snapFloor (at, p.barLength()); c.length = 2 * p.barLength(); }
                if (c.role != Role::Countersubject || generate)
                    c.notes = lockedLayerMaterial (p, voiceId, c.role, c.start, c.length, newSeed());
            }
            else if (generate) c.notes = generateLine (p, voiceId, c.start, c.length, { newSeed(), std::min (doc.snap * 2, PPQ / 2), false });
            v->clips.push_back (std::move (c));
        });
        doc.select (voiceId, newId);
    }

    void deleteClip (int voiceId, int clipId)
    {
        doc.edit ([&] (Project& p)
        {
            if (auto* v = p.findVoice (voiceId)) std::erase_if (v->clips, [clipId] (const Clip& c) { return c.id == clipId; });
        });
    }

    void showSectionMenu (int sectionId, Tick at)
    {
        juce::PopupMenu m;
        juce::Component::SafePointer<Content> safe (this);
        static const SectionType types[] = { SectionType::Exposition, SectionType::Episode, SectionType::MiddleEntry,
                                             SectionType::Stretto, SectionType::Pedal, SectionType::Coda };
        if (auto* s = doc.project.findSection (sectionId))
        {
            juce::PopupMenu type, key;
            for (auto t : types)
                type.addItem (sectionTypeName (t), true, s->type == t, [safe, sectionId, t]
                {
                    if (safe != nullptr) safe->doc.edit ([&] (Project& p) { if (auto* ss = p.findSection (sectionId)) ss->type = t; });
                });
            addKeyItems (key, s->key, [safe, sectionId] (Key k)
            {
                if (safe != nullptr) safe->doc.edit ([&] (Project& p) { if (auto* ss = p.findSection (sectionId)) ss->key = k; });
            });
            m.addSubMenu ("Section type", type);
            m.addSubMenu ("Key", key);
            m.addSeparator();
            m.addItem ("Delete section", true, false, [safe, sectionId]
            {
                if (safe != nullptr) safe->doc.edit ([&] (Project& p) { std::erase_if (p.sections, [sectionId] (const Section& s2) { return s2.id == sectionId; }); });
            });
        }
        else
        {
            for (auto t : types)
                m.addItem (juce::String ("Add ") + sectionTypeName (t), true, false, [safe, t, at] { if (safe != nullptr) safe->addSection (t, at); });
        }
        m.showMenuAsync (juce::PopupMenu::Options());
    }

    void showLaneMenu (int voiceId, int clipId, Tick at)
    {
        juce::PopupMenu m;
        juce::Component::SafePointer<Content> safe (this);
        auto* v = doc.project.findVoice (voiceId);
        auto* c = v ? v->findClip (clipId) : nullptr;
        if (c == nullptr)
        {
            m.addItem ("New empty clip", true, false, [safe, voiceId, at] { if (safe != nullptr) safe->addClip (voiceId, at, false); });
            m.addItem ("Generate free counterpoint here", true, false, [safe, voiceId, at] { if (safe != nullptr) safe->addClip (voiceId, at, true); });
            m.showMenuAsync (juce::PopupMenu::Options());
            return;
        }

        juce::PopupMenu role;
        for (auto r : { Role::Subject, Role::Answer, Role::Countersubject, Role::Free })
            role.addItem (roleName (r), true, c->role == r, [safe, voiceId, clipId, r]
            {
                if (safe != nullptr) safe->doc.edit ([&] (Project& p) { if (auto* cc = p.findVoice (voiceId)->findClip (clipId)) cc->role = r; });
            });
        m.addSubMenu (v->lock ? "Role (layer is locked)" : "Role", role, ! v->lock);
        if (v->lock && *v->lock != Role::Countersubject)
            m.addItem (juce::String ("Reset to match the ") + (*v->lock == Role::Subject ? "subject" : "subject's answer"),
                       referenceSubject (doc.project) != nullptr && referenceSubject (doc.project) != c, false, [safe, voiceId, clipId]
            {
                if (safe == nullptr) return;
                safe->doc.edit ([&] (Project& p)
                {
                    auto* vv = p.findVoice (voiceId);
                    auto* cc = vv ? vv->findClip (clipId) : nullptr;
                    if (cc == nullptr || ! vv->lock) return;
                    Tick len = cc->length;
                    auto notes = lockedLayerMaterial (p, voiceId, *vv->lock, cc->start, len, 0);
                    if (! notes.empty()) { cc->notes = std::move (notes); cc->length = len; }
                });
            });
        m.addSeparator();

        m.addItem ("Generate exposition from this subject", ! c->notes.empty(), false, [safe, voiceId, clipId]
        {
            if (safe == nullptr) return;
            ExpositionResult result;
            safe->doc.edit ([&] (Project& p) { result = generateExposition (p, voiceId, clipId, true, newSeed()); });
            if (! result.ok)
            {
                safe->doc.undo();
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Generate exposition", result.error);
            }
        });

        juce::PopupMenu answer, counter;
        for (auto& other : doc.project.voices)
        {
            if (other.id == voiceId) continue;
            const int target = other.id;
            for (bool tonal : { true, false })
                answer.addItem (other.name + (tonal ? "  (tonal)" : "  (real)"), ! c->notes.empty() && (! other.lock || other.lock == Role::Answer), false, [safe, voiceId, clipId, target, tonal]
                {
                    if (safe == nullptr) return;
                    int newId = -1;
                    safe->doc.edit ([&] (Project& p)
                    {
                        auto* src = p.findVoice (voiceId)->findClip (clipId);
                        auto* dst = p.findVoice (target);
                        if (src == nullptr || dst == nullptr) return;
                        Clip a;
                        a.id = newId = p.newId();
                        a.role = Role::Answer;
                        a.start = src->end();
                        a.length = src->length;
                        a.notes = fitToRange (makeAnswer (src->notes, p.keyAt (src->start), tonal), dst->low, dst->high);
                        dst->clips.push_back (std::move (a));
                    });
                    safe->doc.select (target, newId);
                });
            counter.addItem (other.name, ! other.lock || other.lock == Role::Countersubject, false, [safe, voiceId, clipId, target]
            {
                if (safe == nullptr) return;
                int newId = -1;
                safe->doc.edit ([&] (Project& p)
                {
                    auto* src = p.findVoice (voiceId)->findClip (clipId);
                    auto* dst = p.findVoice (target);
                    if (src == nullptr || dst == nullptr) return;
                    Clip cs;
                    cs.id = newId = p.newId();
                    cs.role = Role::Countersubject;
                    cs.start = src->start;
                    cs.length = src->length;
                    // Replace whatever this voice had in the same span.
                    std::erase_if (dst->clips, [&] (const Clip& x) { return x.start < cs.end() && x.end() > cs.start && ! x.isEntry(); });
                    cs.notes = generateLine (p, target, cs.start, cs.length, { newSeed(), PPQ / 2, true });
                    dst->clips.push_back (std::move (cs));
                });
                safe->doc.select (target, newId);
            });
        }
        m.addSubMenu ("Generate answer after this, in", answer);
        m.addSubMenu ("Generate countersubject against this, in", counter);
        m.addItem ("Regenerate this clip's notes", ! c->isEntry() && (! v->lock || v->lock == Role::Countersubject), false, [safe, voiceId, clipId]
        {
            if (safe == nullptr) return;
            safe->doc.edit ([&] (Project& p)
            {
                auto* vv = p.findVoice (voiceId);
                auto* cc = vv ? vv->findClip (clipId) : nullptr;
                if (cc == nullptr) return;
                cc->notes.clear();
                cc->notes = generateLine (p, voiceId, cc->start, cc->length, { newSeed(), PPQ / 2, cc->role == Role::Countersubject });
            });
        });
        m.addSeparator();
        m.addItem ("Duplicate", true, false, [safe, voiceId, clipId]
        {
            if (safe == nullptr) return;
            int newId = -1;
            safe->doc.edit ([&] (Project& p)
            {
                auto* vv = p.findVoice (voiceId);
                auto* cc = vv ? vv->findClip (clipId) : nullptr;
                if (cc == nullptr) return;
                Clip copy = *cc;
                copy.id = newId = p.newId();
                copy.start = cc->end();
                vv->clips.push_back (std::move (copy));
            });
            safe->doc.select (voiceId, newId);
        });
        m.addItem ("Delete", true, false, [safe, voiceId, clipId] { if (safe != nullptr) safe->deleteClip (voiceId, clipId); });
        m.showMenuAsync (juce::PopupMenu::Options());
    }

private:
    enum class Drag { none, playhead, clipMove, clipResize, sectionMove, sectionResizeL, sectionResizeR };
    TimelineView& owner;
    Document& doc;
    AudioEngine& engine;
    Drag drag = Drag::none;
    bool editBegun = false;
    int dragId = -1;
    Tick downTick = 0, origStart = 0, origLength = 0;
};

// ================================================================ TimelineView

TimelineView::TimelineView (Document& d, AudioEngine& e) : doc (d), engine (e)
{
    content = std::make_unique<Content> (*this);
    addAndMakeVisible (viewport);
    viewport.setViewedComponent (content.get(), false);
    viewport.setScrollBarsShown (false, true);
    doc.addChangeListener (this);
    engine.addChangeListener (this);
    refresh();
}

TimelineView::~TimelineView()
{
    doc.removeChangeListener (this);
    engine.removeChangeListener (this);
}

int TimelineView::preferredHeight() const
{
    return Content::top + (int) doc.project.voices.size() * theme::laneHeight + viewport.getScrollBarThickness();
}

void TimelineView::changeListenerCallback (juce::ChangeBroadcaster*) { refresh(); }

void TimelineView::refresh()
{
    auto& voices = doc.project.voices;
    bool same = headers.size() == voices.size();
    for (size_t i = 0; same && i < voices.size(); ++i) same = headers[i]->voiceId == voices[i].id;
    if (! same)
    {
        headers.clear();
        for (auto& v : voices)
        {
            headers.push_back (std::make_unique<Header> (*this, v.id));
            addAndMakeVisible (*headers.back());
        }
        if (auto* parent = getParentComponent()) parent->resized();
    }
    for (auto& h : headers) h->update();
    resized();
    content->repaint();
}

void TimelineView::resized()
{
    viewport.setBounds (getLocalBounds().withTrimmedLeft (theme::headerWidth));
    for (size_t i = 0; i < headers.size(); ++i)
        headers[i]->setBounds (0, Content::top + (int) i * theme::laneHeight, theme::headerWidth, theme::laneHeight);
    const Tick bar = doc.project.barLength();
    const int w = (int) doc.xForTick (std::max (doc.project.endTime() + 16 * bar, 64 * bar));
    content->setSize (juce::jmax (w, viewport.getWidth()), Content::top + (int) headers.size() * theme::laneHeight);
}

void TimelineView::paint (juce::Graphics& g)
{
    g.fillAll (theme::bg);
    g.setColour (theme::panel);
    g.fillRect (0, 0, theme::headerWidth, Content::top);
    g.setColour (theme::textDim);
    g.setFont (11.0f);
    g.drawText ("Bars", 12, 0, 100, theme::rulerHeight, juce::Justification::centredLeft);
    g.drawText ("Sections", 12, theme::rulerHeight, 100, theme::sectionRowHeight, juce::Justification::centredLeft);
}

void TimelineView::playheadMoved() { content->repaint(); }
