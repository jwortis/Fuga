#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../core/Generate.h"
#include "../core/Rules.h"
#include "../core/Theory.h"

/** The open project plus editing state shared by every view. Message thread only. */
class Document : public juce::ChangeBroadcaster
{
public:
    Document();

    fuga::Project project;
    std::vector<fuga::Violation> violations;

    int selectedVoice = -1, selectedClip = -1;
    fuga::Tick snap = fuga::PPQ / 4;
    double pixelsPerBeat = 30.0;
    juce::File file;
    bool dirty = false;

    /** Saves an undo snapshot. Call once before a gesture, then mutate `project` and call changed(). */
    void beginEdit();
    /** Re-runs the rule checks and notifies listeners. */
    void changed();

    template <typename Fn>
    void edit (Fn&& fn)
    {
        beginEdit();
        fn (project);
        changed();
    }

    void select (int voiceId, int clipId);
    fuga::Voice* voice() { return project.findVoice (selectedVoice); }
    /** The key that applies to the clip being edited (its section's key, else the project key). */
    fuga::Key editKey() { auto* c = clip(); return c ? project.keyAt (c->start) : project.key; }
    fuga::Clip* clip() { auto* v = voice(); return v ? v->findClip (selectedClip) : nullptr; }

    bool canUndo() const { return ! undoStack.empty(); }
    bool canRedo() const { return ! redoStack.empty(); }
    void undo();
    void redo();

    void newProject();
    bool save (const juce::File&);
    bool load (const juce::File&);

    double xForTick (fuga::Tick t) const { return (double) t * pixelsPerBeat / (double) fuga::PPQ; }
    fuga::Tick tickForX (double x) const { return (fuga::Tick) std::llround (x * (double) fuga::PPQ / pixelsPerBeat); }
    fuga::Tick snapFloor (fuga::Tick t, fuga::Tick grid) const { return t < 0 ? 0 : (t / grid) * grid; }
    fuga::Tick snapRound (fuga::Tick t, fuga::Tick grid) const { return t < 0 ? 0 : ((t + grid / 2) / grid) * grid; }

    static fuga::Project makeDefaultProject();
    static juce::String toJson (const fuga::Project&);
    static bool fromJson (const juce::String&, fuga::Project&);

private:
    std::vector<fuga::Project> undoStack, redoStack;
    void fixSelection();
};
