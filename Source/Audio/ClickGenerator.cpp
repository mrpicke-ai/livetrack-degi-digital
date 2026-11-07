#include "ClickGenerator.h"

namespace ldd
{
juce::AudioBuffer<float> ClickGenerator::renderClick (const Config& cfg)
{
    const auto samplesPerBeat = (int) (60.0 / juce::jmax (20.0, cfg.bpm) * cfg.sampleRate);
    const auto numSamples = samplesPerBeat * juce::jmax (1, cfg.beatsPerBar);
    juce::AudioBuffer<float> buf (1, numSamples);
    buf.clear();

    auto addTick = [&] (int startSample, bool accent)
    {
        const float freq = accent ? 2000.0f : 1500.0f;
        const float amp = cfg.volume * (accent ? 1.0f : 0.7f);
        const int tickLen = juce::jmin ((int) (cfg.sampleRate * 0.03), numSamples - startSample);
        for (int i = 0; i < tickLen; ++i)
        {
            const float env = std::exp (-5.0f * (float) i / (float) tickLen);
            buf.addSample (0, startSample + i,
                           amp * env * std::sin (juce::MathConstants<float>::twoPi * freq * (float) i / (float) cfg.sampleRate));
        }
    };

    for (int b = 0; b < cfg.beatsPerBar; ++b)
        addTick (b * samplesPerBeat, cfg.accentFirstBeat && b == 0);

    return buf;
}

juce::File ClickGenerator::generateToFile (const juce::File& target, const Config& cfg, juce::String& error)
{
    juce::WavAudioFormat wav;
    target.deleteFile();
    std::unique_ptr<juce::FileOutputStream> out (target.createOutputStream());
    if (out == nullptr)
    {
        error = "Impossibile creare il file: " + target.getFullPathName();
        return {};
    }

    std::unique_ptr<juce::AudioFormatWriter> writer (
        wav.createWriterFor (out.get(), cfg.sampleRate, 1, 16, {}, 0));
    if (writer == nullptr)
    {
        error = "Impossibile inizializzare il writer WAV.";
        return {};
    }
    out.release(); // il writer possiede lo stream

    auto oneBar = renderClick (cfg);
    const int totalBars = juce::jmax (1, cfg.numBars);
    for (int bar = 0; bar < totalBars; ++bar)
    {
        if (! writer->writeFromAudioSampleBuffer (oneBar, 0, oneBar.getNumSamples()))
        {
            error = "Errore di scrittura durante la generazione.";
            return {};
        }
    }

    writer->flush();
    return target;
}
}
