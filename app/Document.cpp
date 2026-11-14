#include "Document.h"

using namespace fuga;

Document::Document()
{
    project = makeDefaultProject();
    fixSelection();
    violations = checkProject (project);
}

void Document::beginEdit()
{
    undoStack.push_back (project);
    if (undoStack.size() > 200) undoStack.erase (undoStack.begin());
    redoStack.clear();
    dirty = true;
}

void Document::changed()
{
    fixSelection();
    violations = checkProject (project);
    sendSynchronousChangeMessage();
}

void Document::select (int voiceId, int clipId)
{
    if (selectedVoice == voiceId && selectedClip == clipId) return;
    selectedVoice = voiceId;
    selectedClip = clipId;
    fixSelection();
    sendSynchronousChangeMessage();
}

void Document::fixSelection()
{
    if (project.findVoice (selectedVoice) == nullptr)
    {
        selectedVoice = project.voices.empty() ? -1 : project.voices.front().id;
        selectedClip = -1;
    }
    if (auto* v = voice(); v != nullptr && v->findClip (selectedClip) == nullptr)
        selectedClip = -1;
}

void Document::undo()
{
    if (undoStack.empty()) return;
    redoStack.push_back (std::move (project));
    project = std::move (undoStack.back());
    undoStack.pop_back();
    dirty = true;
    changed();
}

void Document::redo()
{
    if (redoStack.empty()) return;
    undoStack.push_back (std::move (project));
    project = std::move (redoStack.back());
    redoStack.pop_back();
    dirty = true;
    changed();
}

void Document::newProject()
{
    project = makeDefaultProject();
    undoStack.clear();
    redoStack.clear();
    file = juce::File();
    dirty = false;
    selectedVoice = selectedClip = -1;
    changed();
}

Project Document::makeDefaultProject()
{
    Project p;
    p.bpm = 80;
    p.key = { 2, Mode::Minor };
    auto add = [&] (const char* name, int low, int high)
    {
        Voice v;
        v.id = p.newId();
        v.name = name;
        v.low = low;
        v.high = high;
        p.voices.push_back (v);
    };
    add ("Soprano", 60, 81);
    add ("Alto", 55, 74);
    add ("Tenor", 48, 67);
    add ("Bass", 40, 60);

    // A short starter subject in the alto so there is something to hear and generate from.
    const Tick q = PPQ, e = PPQ / 2;
    Clip c;
    c.id = p.newId();
    c.role = Role::Subject;
    c.start = 0;
    c.length = 8 * q;
    c.notes = { { 0, q, 62 }, { q, q, 69 }, { 2 * q, e, 65 }, { 2 * q + e, e, 64 }, { 3 * q, e, 62 }, { 3 * q + e, e, 61 },
                { 4 * q, e, 62 }, { 4 * q + e, e, 64 }, { 5 * q, e, 65 }, { 5 * q + e, e, 67 }, { 6 * q, e, 69 }, { 6 * q + e, e, 67 }, { 7 * q, q, 65 } };
    p.voices[1].clips.push_back (c);
    return p;
}

// ---------------------------------------------------------------- JSON

static const char* const roleIds[] = { "subject", "answer", "countersubject", "free" };
static const char* const sectionIds[] = { "exposition", "episode", "middleEntry", "stretto", "pedal", "coda" };

static juce::var keyToVar (Key k)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("tonic", k.tonic);
    o->setProperty ("mode", k.mode == Mode::Major ? "major" : "minor");
    return o;
}

static Key keyFromVar (const juce::var& v)
{
    return { juce::jlimit (0, 11, (int) v["tonic"]), v["mode"].toString() == "major" ? Mode::Major : Mode::Minor };
}

juce::String Document::toJson (const Project& p)
{
    auto* root = new juce::DynamicObject();
    juce::var rootVar (root);
    root->setProperty ("format", "fuga");
    root->setProperty ("version", 1);
    root->setProperty ("bpm", p.bpm);
    root->setProperty ("beatsPerBar", p.beatsPerBar);
    root->setProperty ("key", keyToVar (p.key));
    root->setProperty ("nextId", p.nextId);

    juce::Array<juce::var> voices;
    for (auto& v : p.voices)
    {
        auto* vo = new juce::DynamicObject();
        vo->setProperty ("id", v.id);
        vo->setProperty ("name", juce::String (v.name));
        vo->setProperty ("low", v.low);
        vo->setProperty ("high", v.high);
        vo->setProperty ("mute", v.mute);
        vo->setProperty ("solo", v.solo);
        vo->setProperty ("gain", v.gain);
        vo->setProperty ("instrument", juce::String (v.instrumentDesc));
        vo->setProperty ("instrumentState", juce::String (v.instrumentState));
        juce::Array<juce::var> clips;
        for (auto& c : v.clips)
        {
            auto* co = new juce::DynamicObject();
            co->setProperty ("id", c.id);
            co->setProperty ("role", roleIds[(int) c.role]);
            co->setProperty ("start", (juce::int64) c.start);
            co->setProperty ("length", (juce::int64) c.length);
            juce::Array<juce::var> notes;
            for (auto& n : c.notes)
                notes.add (juce::Array<juce::var> { (juce::int64) n.start, (juce::int64) n.length, n.pitch, n.velocity });
            co->setProperty ("notes", notes);
            clips.add (co);
        }
        vo->setProperty ("clips", clips);
        voices.add (vo);
    }
    root->setProperty ("voices", voices);

    juce::Array<juce::var> sections;
    for (auto& s : p.sections)
    {
        auto* so = new juce::DynamicObject();
        so->setProperty ("id", s.id);
        so->setProperty ("type", sectionIds[(int) s.type]);
        so->setProperty ("start", (juce::int64) s.start);
        so->setProperty ("length", (juce::int64) s.length);
        so->setProperty ("key", keyToVar (s.key));
        sections.add (so);
    }
    root->setProperty ("sections", sections);
    return juce::JSON::toString (rootVar);
}

template <size_t N>
static int indexOfId (const char* const (&ids)[N], const juce::String& s)
{
    for (size_t i = 0; i < N; ++i) if (s == ids[i]) return (int) i;
    return (int) N - 1;
}

bool Document::fromJson (const juce::String& text, Project& p)
{
    auto root = juce::JSON::parse (text);
    if (! root.isObject() || root["format"].toString() != "fuga") return false;
    p = Project();
    p.bpm = juce::jlimit (20.0, 300.0, (double) root["bpm"]);
    p.beatsPerBar = juce::jlimit (1, 12, (int) root["beatsPerBar"]);
    p.key = keyFromVar (root["key"]);
    int maxId = 0;

    if (auto* voices = root["voices"].getArray())
        for (auto& vv : *voices)
        {
            Voice v;
            v.id = vv["id"];
            v.name = vv["name"].toString().toStdString();
            v.low = vv["low"];
            v.high = vv["high"];
            v.mute = vv["mute"];
            v.solo = vv["solo"];
            v.gain = (float) (double) vv["gain"];
            v.instrumentDesc = vv["instrument"].toString().toStdString();
            v.instrumentState = vv["instrumentState"].toString().toStdString();
            maxId = std::max (maxId, v.id);
            if (auto* clips = vv["clips"].getArray())
                for (auto& cv : *clips)
                {
                    Clip c;
                    c.id = cv["id"];
                    c.role = (Role) indexOfId (roleIds, cv["role"].toString());
                    c.start = (juce::int64) cv["start"];
                    c.length = std::max<Tick> (1, (juce::int64) cv["length"]);
                    maxId = std::max (maxId, c.id);
                    if (auto* notes = cv["notes"].getArray())
                        for (auto& nv : *notes)
                            if (auto* a = nv.getArray(); a != nullptr && a->size() >= 3)
                                c.notes.push_back ({ (juce::int64) (*a)[0], std::max<Tick> (1, (juce::int64) (*a)[1]),
                                                     juce::jlimit (0, 127, (int) (*a)[2]), a->size() > 3 ? juce::jlimit (1, 127, (int) (*a)[3]) : 96 });
                    v.clips.push_back (std::move (c));
                }
            p.voices.push_back (std::move (v));
        }

    if (auto* sections = root["sections"].getArray())
        for (auto& sv : *sections)
        {
            Section s;
            s.id = sv["id"];
            s.type = (SectionType) indexOfId (sectionIds, sv["type"].toString());
            s.start = (juce::int64) sv["start"];
            s.length = std::max<Tick> (1, (juce::int64) sv["length"]);
            s.key = keyFromVar (sv["key"]);
            maxId = std::max (maxId, s.id);
            p.sections.push_back (s);
        }

    p.nextId = std::max ((int) root["nextId"], maxId + 1);
    return true;
}

bool Document::save (const juce::File& f)
{
    if (! f.replaceWithText (toJson (project))) return false;
    file = f;
    dirty = false;
    sendSynchronousChangeMessage();
    return true;
}

bool Document::load (const juce::File& f)
{
    Project p;
    if (! fromJson (f.loadFileAsString(), p)) return false;
    project = std::move (p);
    undoStack.clear();
    redoStack.clear();
    file = f;
    dirty = false;
    selectedVoice = selectedClip = -1;
    changed();
    return true;
}
