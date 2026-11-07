#pragma once
#include <juce_core/juce_core.h>
#include "../AppData.h"

namespace ldd
{
// Interruttore della pedaliera MIDI (Ampero): nome mostrato sul tasto,
// numero CC che accende/spegne il blocco effetto sul dispositivo, stato on/off
// memorizzato PER BRANO (ogni brano richiama il suo preset coi pedali voluti).
struct PedalSwitch
{
    juce::String name;
    int cc = 0;
    bool on = false;
};

inline constexpr int NUM_PEDALS = 8;

// Mappa CC FISSA di fabbrica dell'Ampero II Stage (MIDI Control Information
// List Hotone, non riassegnabile sul dispositivo):
//   FS1-FS7 = CC 79-85  -> replica dei footswitch dello Stage in STOMP mode
//   slot A1-A6 = CC 48-53, slot B1-B6 = CC 54-59 (on/off del singolo slot)
//   tuner = CC 60, tap tempo = CC 76, scene 1-5 = CC 25 (valore 1-5)
// Default: i 7 pedali replicano i footswitch FS1-FS7, l'ottavo e' il tuner.
inline void applyAmperoDefaultPedals (std::array<PedalSwitch, NUM_PEDALS>& pedals)
{
    for (int i = 0; i < NUM_PEDALS; ++i)
    {
        auto& p = pedals[(size_t) i];
        p.on = false;
        if (i < 7) { p.name = "FS " + juce::String (i + 1); p.cc = 79 + i; }
        else       { p.name = "TUNER"; p.cc = 60; }
    }
}

// Cambio di stato di un pedale in un punto preciso del brano (automazione):
// al passaggio della testina oltre timeSec il pedale viene acceso/spento.
struct PedalEvent
{
    double timeSec = 0.0;
    int pedal = 0;   // 0..NUM_PEDALS-1
    bool on = false;
};

struct Song
{
    juce::String title = "Nuovo brano";
    juce::String artist;
    juce::String notes;
    double bpm = 120.0;
    int timeSigNumerator = 4;
    int timeSigDenominator = 4;

    std::array<juce::String, MAX_TRACKS> filePaths;
    std::array<float, MAX_TRACKS> gains;
    std::array<bool, MAX_TRACKS> mutes;
    std::array<bool, MAX_TRACKS> solos;
    std::array<int, MAX_TRACKS> outputs;   // canale fisico, -1 = non assegnata

    // Pedaliera MIDI: predefinito A60-1 (patch assoluta 296); -1 = non inviare.
    int amperoPreset = 296;
    std::array<PedalSwitch, NUM_PEDALS> amperoSwitches;
    // Automazione pedali: cambi di stato durante il brano (secondi dal via).
    std::vector<PedalEvent> pedalEvents;

    // Marker con etichetta (secondi) — usati anche come cue visivi.
    std::vector<std::pair<double, juce::String>> markers;

    Song();

    juce::var toVar() const;
    static Song fromVar (const juce::var&);
    bool hasAnyFile() const;
};

struct Setlist
{
    juce::String name = "Nuova scaletta";
    std::vector<Song> songs;

    juce::var toVar() const;
    static Setlist fromVar (const juce::var&);
    double totalSeconds() const;
};

struct ProjectSettings
{
    juce::String audioDeviceType = "ASIO";
    juce::String outputDeviceName;
    double sampleRate = 48000.0;
    int bufferSize = 256;
    bool stereoLinked = false;
    bool eightTracks = false;
    bool autoStartNext = false;   // live: parte subito il brano successivo a fine brano
    bool preferAsio = true;       // passa ad ASIO quando esiste il driver dedicato
    std::vector<MidiMapping> midiMappings;

    // Pedaliera MIDI in uscita (es. Hotone Ampero): identificativo dispositivo
    // (vuoto = nessuno) e canale MIDI 1-16 su cui parlare con il dispositivo.
    juce::String midiOutputId;
    int midiChannel = 1;

    // Nomi traccia personalizzati (vuoto = default "Traccia N").
    std::array<juce::String, MAX_TRACKS> trackNames;
    // Routing globale: canale iniziale (0-based, -1 = non assegnata) e numero
    // di canali (1 = singolo, 2 = coppia, 4 = gruppo tipo 1-2-3-4).
    std::array<int, MAX_TRACKS> trackOutStart;
    std::array<int, MAX_TRACKS> trackOutCount;

    ProjectSettings()
    {
        trackOutStart.fill (-1);
        trackOutCount.fill (1);
        for (int i = 0; i < MAX_TRACKS; ++i) trackOutStart[(size_t) i] = i;
    }

    juce::var toVar() const;
    static ProjectSettings fromVar (const juce::var&);
};

struct Project
{
    juce::String name = "Nuovo progetto";
    std::vector<Setlist> setlists;
    int currentSetlist = 0;
    ProjectSettings settings;

    juce::var toVar() const;
    static Project fromVar (const juce::var&);

    bool saveToFile (const juce::File& f, juce::String& error) const;
    static Project loadFromFile (const juce::File& f, juce::String& error);
};
}
