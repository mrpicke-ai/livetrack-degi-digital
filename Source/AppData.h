#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace ldd
{
// 6 tracce indipendenti; ogni traccia esce su singolo canale, coppia o gruppo.
inline constexpr int MAX_TRACKS = 6;
inline constexpr int DEFAULT_TRACKS = 6;

inline juce::StringArray getDefaultTrackNames()
{
    juce::StringArray names;
    for (int i = 1; i <= MAX_TRACKS; ++i)
        names.add ("Traccia " + juce::String (i));
    return names;
}

inline juce::Colour getTrackColour (int index)
{
    static const juce::uint32 cols[] =
    {
        0xff3ce96b,  // fosforo verde
        0xff38d6c8,  // ciano
        0xffe8a020,  // ambra
        0xffb86ef0,  // viola
        0xff6ea8ff,  // blu
        0xffff7a59,  // corallo
        0xff9fe04a,  // lime
        0xffffd24a,  // giallo
        0xffff6ea0   // rosa
    };
    return juce::Colour (cols[index % MAX_TRACKS]);
}

// Comandi richiamabili da tastiera, MIDI e interfaccia performance.
enum class TransportCommand
{
    playPause = 0,
    stop,
    next,
    prev,
    panic,
    count
};

inline juce::String getCommandName (TransportCommand c)
{
    switch (c)
    {
        case TransportCommand::playPause: return "Play / Pause";
        case TransportCommand::stop:      return "Stop";
        case TransportCommand::next:        return "Brano successivo";
        case TransportCommand::prev:        return "Brano precedente";
        case TransportCommand::panic:       return "PANIC";
        default:                          return {};
    }
}

// Mappatura MIDI: tipo 0 = Control Change, 1 = Note, 2 = Program Change.
struct MidiMapping
{
    int command = -1;
    int type = 0;
    int channel = -1;   // -1 = qualsiasi
    int number = -1;    // CC number / nota / program
};

// Palette "Space: 1999" — console Moonbase Alpha: pannelli tortora con rivetti,
// schermi incassati a fosforo, tasti squadrati, allarmi rossi.
namespace theme
{
    inline const juce::Colour background    { 0xff16140e };  // quasi nero caldo
    inline const juce::Colour surface          { 0xff8f8674 };  // pannello tortora
    inline const juce::Colour surfaceDark      { 0xff6b6355 };  // pannello in ombra
    inline const juce::Colour surfaceLight     { 0xffb3a992 };  // bevel chiaro
    inline const juce::Colour text             { 0xffe8e2d4 };  // testo chiaro (su scuro)
    inline const juce::Colour textOnPanel      { 0xff1d1a14 };  // testo scuro (su pannelli)
    inline const juce::Colour textDim          { 0xff8a8271 };  // etichette spente
    inline const juce::Colour primary          { 0xff3ce96b };  // fosforo verde
    inline const juce::Colour danger           { 0xffe03a28 };  // rosso allarme
    inline const juce::Colour warning          { 0xffe8a020 };  // ambra
    inline const juce::Colour screen           { 0xff0b120b };  // schermo incassato
    inline const juce::Colour screenText       { 0xff49f07e };  // testo fosforescente
    inline const juce::Colour screenTextDim    { 0xff2a7a44 };  // fosforescente spento
}
}
