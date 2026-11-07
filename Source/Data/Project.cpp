#include "Project.h"

namespace ldd
{
namespace
{
juce::var filesToVar (const std::array<juce::String, MAX_TRACKS>& files)
{
    juce::Array<juce::var> a;
    for (auto& f : files) a.add (f);
    return juce::var (a);
}

juce::var floatsToVar (const std::array<float, MAX_TRACKS>& v)
{
    juce::Array<juce::var> a;
    for (auto x : v) a.add (x);
    return juce::var (a);
}

juce::var boolsToVar (const std::array<bool, MAX_TRACKS>& v)
{
    juce::Array<juce::var> a;
    for (auto x : v) a.add (x);
    return juce::var (a);
}

juce::var intsToVar (const std::array<int, MAX_TRACKS>& v)
{
    juce::Array<juce::var> a;
    for (auto x : v) a.add (x);
    return juce::var (a);
}

template <typename T, size_t N>
void readArray (const juce::var& v, std::array<T, N>& out, T fallback)
{
    if (auto* a = v.getArray())
        for (size_t i = 0; i < N && i < (size_t) a->size(); ++i)
        {
            if constexpr (std::is_same_v<T, juce::String>)
                out[i] = (*a)[(int) i].toString();
            else
                out[i] = static_cast<T> ((*a)[(int) i]);
        }
    else
        out.fill (fallback);
}
}

Song::Song()
{
    gains.fill (1.0f);
    mutes.fill (false);
    solos.fill (false);
    for (int i = 0; i < MAX_TRACKS; ++i)
        outputs[i] = i;   // default: traccia i -> uscita fisica i
    applyAmperoDefaultPedals (amperoSwitches);
}

namespace
{
juce::var pedalsToVar (const std::array<PedalSwitch, NUM_PEDALS>& pedals)
{
    juce::Array<juce::var> a;
    for (auto& p : pedals)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("name", p.name);
        o->setProperty ("cc", p.cc);
        o->setProperty ("on", p.on);
        a.add (juce::var (o));
    }
    return juce::var (a);
}

void readPedals (const juce::var& v, std::array<PedalSwitch, NUM_PEDALS>& out)
{
    if (auto* a = v.getArray())
        for (int i = 0; i < NUM_PEDALS && i < a->size(); ++i)
        {
            auto mo = (*a)[i];   // juce::Array restituisce per valore
            out[(size_t) i].name = mo["name"].toString();
            out[(size_t) i].cc = (int) mo["cc"];
            out[(size_t) i].on = (bool) mo["on"];
        }
}
}

juce::var Song::toVar() const
{
    auto* v = new juce::DynamicObject();
    v->setProperty ("title", title);
    v->setProperty ("artist", artist);
    v->setProperty ("notes", notes);
    v->setProperty ("bpm", bpm);
    v->setProperty ("timeSig", juce::String (timeSigNumerator) + "/" + juce::String (timeSigDenominator));
    v->setProperty ("files", filesToVar (filePaths));
    v->setProperty ("gains", floatsToVar (gains));
    v->setProperty ("mutes", boolsToVar (mutes));
    v->setProperty ("solos", boolsToVar (solos));
    v->setProperty ("outputs", intsToVar (outputs));
    v->setProperty ("amperoPreset", amperoPreset);
    v->setProperty ("amperoSwitches", pedalsToVar (amperoSwitches));

    juce::Array<juce::var> evs;
    for (auto& e : pedalEvents)
    {
        auto* eo = new juce::DynamicObject();
        eo->setProperty ("t", e.timeSec);
        eo->setProperty ("pedal", e.pedal);
        eo->setProperty ("on", e.on);
        evs.add (juce::var (eo));
    }
    v->setProperty ("pedalEvents", juce::var (evs));

    juce::Array<juce::var> marks;
    for (auto& m : markers)
    {
        auto* mo = new juce::DynamicObject();
        mo->setProperty ("pos", m.first);
        mo->setProperty ("label", m.second);
        marks.add (juce::var (mo));
    }
    v->setProperty ("markers", juce::var (marks));
    return juce::var (v);
}

Song Song::fromVar (const juce::var& v)
{
    Song s;
    s.title = v["title"].toString();
    s.artist = v["artist"].toString();
    s.notes = v["notes"].toString();
    s.bpm = v["bpm"];
    auto ts = v["timeSig"].toString();
    if (ts.contains ("/"))
    {
        s.timeSigNumerator = ts.upToFirstOccurrenceOf ("/", false, false).getIntValue();
        s.timeSigDenominator = ts.fromFirstOccurrenceOf ("/", false, false).getIntValue();
    }
    readArray (v["files"], s.filePaths, juce::String());
    readArray (v["gains"], s.gains, 1.0f);
    readArray (v["mutes"], s.mutes, false);
    readArray (v["solos"], s.solos, false);
    readArray (v["outputs"], s.outputs, -1);
    s.amperoPreset = (int) v["amperoPreset"];
    readPedals (v["amperoSwitches"], s.amperoSwitches);
    // Migrazione: i brani salvati col vecchio default CC 21-28 (mai ritoccati)
    // passano alla mappa ufficiale Ampero II Stage, conservando gli stati on/off.
    {
        bool legacy = true;
        for (int i = 0; i < NUM_PEDALS; ++i)
            if (s.amperoSwitches[(size_t) i].cc != 21 + i) { legacy = false; break; }
        if (legacy)
        {
            std::array<bool, NUM_PEDALS> onStates;
            for (int i = 0; i < NUM_PEDALS; ++i)
                onStates[(size_t) i] = s.amperoSwitches[(size_t) i].on;
            applyAmperoDefaultPedals (s.amperoSwitches);
            for (int i = 0; i < NUM_PEDALS; ++i)
                s.amperoSwitches[(size_t) i].on = onStates[(size_t) i];
        }
    }
    if (auto* a = v["pedalEvents"].getArray())
        for (int i = 0; i < a->size(); ++i)
        {
            auto ev = (*a)[i];
            PedalEvent e;
            e.timeSec = (double) ev["t"];
            e.pedal = juce::jlimit (0, NUM_PEDALS - 1, (int) ev["pedal"]);
            e.on = (bool) ev["on"];
            if (e.timeSec >= 0.0) s.pedalEvents.push_back (e);
        }
    if (auto* a = v["markers"].getArray())
        for (auto& mo : *a)
            s.markers.emplace_back ((double) mo["pos"], mo["label"].toString());
    return s;
}

bool Song::hasAnyFile() const
{
    for (auto& f : filePaths)
        if (f.isNotEmpty()) return true;
    return false;
}

juce::var Setlist::toVar() const
{
    auto* v = new juce::DynamicObject();
    v->setProperty ("name", name);
    juce::Array<juce::var> arr;
    for (auto& s : songs) arr.add (s.toVar());
    v->setProperty ("songs", juce::var (arr));
    return juce::var (v);
}

Setlist Setlist::fromVar (const juce::var& v)
{
    Setlist sl;
    sl.name = v["name"].toString();
    if (auto* a = v["songs"].getArray())
        for (auto& sv : *a)
            sl.songs.push_back (Song::fromVar (sv));
    return sl;
}

double Setlist::totalSeconds() const
{
    // Stima: durata non nota finché i file non sono caricati; i marker/durate
    // precise vengono calcolate a caricamento. Qui restituiamo 0.
    juce::ignoreUnused (this);
    return 0.0;
}

juce::var ProjectSettings::toVar() const
{
    auto* v = new juce::DynamicObject();
    v->setProperty ("audioDeviceType", audioDeviceType);
    v->setProperty ("outputDeviceName", outputDeviceName);
    v->setProperty ("sampleRate", sampleRate);
    v->setProperty ("bufferSize", bufferSize);
    v->setProperty ("stereoLinked", stereoLinked);
    v->setProperty ("eightTracks", eightTracks);
    v->setProperty ("autoStartNext", autoStartNext);
    v->setProperty ("preferAsio", preferAsio);
    v->setProperty ("midiOutputId", midiOutputId);
    v->setProperty ("midiChannel", midiChannel);

    juce::Array<juce::var> maps;
    for (auto& m : midiMappings)
    {
        auto* mo = new juce::DynamicObject();
        mo->setProperty ("command", m.command);
        mo->setProperty ("type", m.type);
        mo->setProperty ("channel", m.channel);
        mo->setProperty ("number", m.number);
        maps.add (juce::var (mo));
    }
    v->setProperty ("midiMappings", juce::var (maps));

    v->setProperty ("trackNames", filesToVar (trackNames));
    v->setProperty ("trackOutStart", intsToVar (trackOutStart));
    v->setProperty ("trackOutCount", intsToVar (trackOutCount));
    return juce::var (v);
}

ProjectSettings ProjectSettings::fromVar (const juce::var& v)
{
    ProjectSettings s;
    s.audioDeviceType = v["audioDeviceType"].toString();
    s.outputDeviceName = v["outputDeviceName"].toString();
    s.sampleRate = v["sampleRate"];
    s.bufferSize = (int) v["bufferSize"];
    s.stereoLinked = (bool) v["stereoLinked"];
    s.eightTracks = (bool) v["eightTracks"];
    s.autoStartNext = (bool) v["autoStartNext"];
    if (v["preferAsio"].isBool()) s.preferAsio = (bool) v["preferAsio"];
    s.midiOutputId = v["midiOutputId"].toString();
    s.midiChannel = juce::jlimit (1, 16, (int) v["midiChannel"]);
    if (auto* a = v["midiMappings"].getArray())
        for (auto& mo : *a)
            s.midiMappings.push_back ({ (int) mo["command"], (int) mo["type"],
                                        (int) mo["channel"], (int) mo["number"] });

    // Compatibilità con progetti salvati prima dell'introduzione di nomi/routing:
    // se le chiavi mancano mantieni i default del costruttore.
    if (v["trackNames"].getArray() != nullptr)
        readArray (v["trackNames"], s.trackNames, juce::String());
    if (v["trackOutStart"].getArray() != nullptr)
        readArray (v["trackOutStart"], s.trackOutStart, -1);
    if (v["trackOutCount"].getArray() != nullptr)
        readArray (v["trackOutCount"], s.trackOutCount, 1);
    return s;
}

juce::var Project::toVar() const
{
    auto* v = new juce::DynamicObject();
    v->setProperty ("app", "LivetrackDegiDigital");
    v->setProperty ("format", 1);
    v->setProperty ("name", name);
    v->setProperty ("currentSetlist", currentSetlist);
    juce::Array<juce::var> arr;
    for (auto& sl : setlists) arr.add (sl.toVar());
    v->setProperty ("setlists", juce::var (arr));
    v->setProperty ("settings", settings.toVar());
    return juce::var (v);
}

Project Project::fromVar (const juce::var& v)
{
    Project p;
    p.name = v["name"].toString();
    p.currentSetlist = (int) v["currentSetlist"];
    if (auto* a = v["setlists"].getArray())
        for (auto& slv : *a)
            p.setlists.push_back (Setlist::fromVar (slv));
    p.settings = ProjectSettings::fromVar (v["settings"]);
    if (p.setlists.empty()) p.setlists.emplace_back();
    return p;
}

bool Project::saveToFile (const juce::File& f, juce::String& error) const
{
    auto json = juce::JSON::toString (toVar(), true);
    if (f.replaceWithText (json)) return true;
    error = "Impossibile scrivere il file " + f.getFullPathName();
    return false;
}

Project Project::loadFromFile (const juce::File& f, juce::String& error)
{
    auto json = juce::JSON::parse (f.loadFileAsString());
    if (! json.isObject())
    {
        error = "File progetto non valido: " + f.getFullPathName();
        return {};
    }
    return fromVar (json);
}
}
