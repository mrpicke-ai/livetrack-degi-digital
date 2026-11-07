#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

namespace ldd
{
// Genera un file WAV di click metronomico (accento sul primo battito del gruppo).
// Suoni: sinusoidale 2 kHz (accento) / 1,5 kHz (battito), decadimento esponenziale.
class ClickGenerator
{
public:
    struct Config
    {
        double bpm = 120.0;
        int beatsPerBar = 4;
        int numBars = 100;         // lunghezza del file generato (in misure)
        double sampleRate = 48000.0;
        float volume = 0.8f;
        bool accentFirstBeat = true;
    };

    // Scrive il WAV e ritorna il file creato; empty in caso di errore.
    static juce::File generateToFile (const juce::File& target, const Config&, juce::String& error);

    // Ritorna i campioni in memoria (una misura) — utile per test.
    static juce::AudioBuffer<float> renderClick (const Config&);
};
}
