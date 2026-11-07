/*
 * AudioSmokeTest: test automatico del motore audio.
 * Apre il dispositivo come farebbe il programma, carica un file di test,
 * suona per 3 secondi e misura: callback attivi, posizione avanzata,
 * livello massimo in uscita. Risponde alla domanda "esce audio?".
 */
#include <juce_audio_utils/juce_audio_utils.h>
#include "Audio/Engine.h"
#include "Data/Project.h"

using namespace ldd;

int testToneCheck (Engine& engine);
int windowsAudioSwitchCheck (Engine& engine);
int channel34Check (Engine& engine, const juce::File& testFile);

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    Engine engine;
    engine.initialise();

    ProjectSettings s;
    s.audioDeviceType = "ASIO";
    s.outputDeviceName = "UMC ASIO Driver";
    s.sampleRate = 48000.0;
    s.bufferSize = 256;
    engine.applyDeviceSettings (s);

    std::cout << "Dispositivo: " << engine.getDeviceSummary() << "\n";
    std::cout << "Uscite: " << engine.getNumOutputChannels() << "  nomi: "
              << engine.getOutputChannelNames().joinIntoString (" | ") << "\n";
    std::cout << "Sample rate motore: " << engine.getDeviceSampleRate()
              << "  buffer: " << engine.getBufferSize() << "\n";

    // File di test: sinusoide 440 Hz, 3 s, stereo 44.1 kHz.
    auto testFile = juce::File::getSpecialLocation (juce::File::tempDirectory)
                        .getChildFile ("ldd_smoketest.wav");
    {
        juce::WavAudioFormat wav;
        auto os = testFile.createOutputStream();
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (
            os.get(), 44100.0, 2, 16, {}, 0));
        if (writer == nullptr) { std::cout << "ERRORE: non riesco a creare il WAV\n"; return 1; }
        os.release();
        juce::AudioBuffer<float> b (2, 44100 * 3);
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            float v = 0.5f * std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * i / 44100.0);
            b.setSample (0, i, v);
            b.setSample (1, i, v);
        }
        writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }

    Song song;
    song.title = "Smoke test";
    song.filePaths[0] = testFile.getFullPathName();
    song.gains.fill (1.0f);
    engine.setNumActiveTracks (1);
    engine.setTrackRouting (0, 0, 2);

    juce::String err;
    if (! engine.stageSong (song, err))
    {
        std::cout << "ERRORE stageSong: " << err << "\n";
        return 1;
    }
    engine.commitStagedSong (false);
    engine.play();

    float maxLevel = 0.0f;
    auto t0 = juce::Time::getMillisecondCounter();
    while (juce::Time::getMillisecondCounter() - t0 < 3000)
    {
        juce::Thread::sleep (50);
        maxLevel = juce::jmax (maxLevel, engine.masterLevel.load(),
                               engine.trackLevel[0].load());
    }

    const auto pos = engine.getPositionSamples();
    std::cout << "Posizione dopo 3 s: " << pos << " campioni (atteso ~144000)"
              << "  playing: " << (engine.isPlaying() ? "si" : "NO") << "\n";
    std::cout << "Livello massimo misurato: " << maxLevel
              << (maxLevel > 0.01f ? "  -> AUDIO IN USCITA" : "  -> SILENZIO!") << "\n";
    std::cout << "Xrun: " << engine.xrunCount.load() << "\n";
    std::cout << "Callback: numOut=" << engine.diagNumOut.load()
              << "  canali null (maschera bit): " << engine.diagNullMask.load() << "\n";
    std::cout << "Render: numOut=" << engine.diagRenderNumOut.load()
              << " tracks=" << engine.diagRenderActiveTracks.load()
              << " loaded0=" << engine.diagRenderLoaded0.load()
              << " startCh0=" << engine.diagRenderStartCh.load()
              << " ptr=" << engine.diagRenderPtr.load() << "\n";

    const int toneResult = testToneCheck (engine);
    const int ch34Result = channel34Check (engine, testFile);

    // Cambio driver come fa il programma quando in Impostazioni si sceglie
    // "Windows Audio": riproduce il crash segnalato ("se uso audio di
    // windows crasha").
    const int switchResult = windowsAudioSwitchCheck (engine);

    engine.shutdown();
    return (maxLevel > 0.01f && toneResult == 0 && switchResult == 0
            && ch34Result == 0) ? 0 : 2;
}

// Verifica ESPLICITA dei canali fisici 3 e 4: carica il file di test con
// routing sulla coppia 3-4 e misura il livello su OGNI canale di uscita.
// Risponde a "non esce audio dai canali 3 e 4": il problema e' nel
// software o a monte (cuffie/mixer/cavi)?
int channel34Check (Engine& engine, const juce::File& testFile)
{
    std::cout << "--- verifica canali 3-4 ---\n";
    const int nOut = engine.getNumOutputChannels();
    std::cout << "Canali fisici disponibili: " << nOut << "\n";
    if (nOut < 4) { std::cout << "DISPOSITIVO APERTO SOLO A " << nOut << " CANALI!\n"; return 4; }

    engine.stop();
    engine.setNumActiveTracks (1);
    engine.setTrackRouting (0, 2, 2);   // traccia -> uscite fisiche 3-4

    Song song;
    song.title = "Check 3-4";
    song.filePaths[0] = testFile.getFullPathName();
    song.gains.fill (1.0f);
    juce::String err;
    if (! engine.stageSong (song, err)) { std::cout << "ERRORE stageSong: " << err << "\n"; return 4; }
    engine.commitStagedSong (false);
    engine.play();

    float peak[6] = {};
    auto t0 = juce::Time::getMillisecondCounter();
    while (juce::Time::getMillisecondCounter() - t0 < 2000)
    {
        juce::Thread::sleep (50);
        for (int c = 0; c < nOut && c < 6; ++c)
            peak[c] = juce::jmax (peak[c], engine.outputLevel[(size_t) c].load());
    }
    engine.stop();

    for (int c = 0; c < nOut && c < 6; ++c)
        std::cout << "Uscita " << (c + 1) << ": picco " << peak[c] << "\n";

    const bool ok34 = peak[2] > 0.01f && peak[3] > 0.01f;
    const bool silent12 = peak[0] < 0.001f && peak[1] < 0.001f;
    std::cout << "Canali 3-4 con segnale: " << (ok34 ? "SI" : "NO")
              << "   |   1-2 silenziose: " << (silent12 ? "SI" : "no") << "\n";
    return (ok34 && silent12) ? 0 : 4;
}

// Riproduce lo scenario del crash: dispositivo ASIO attivo, poi applica
// impostazioni "Windows Audio" (preferAsio=false) e torna ad ASIO.
int windowsAudioSwitchCheck (Engine& engine)
{
    std::cout << "--- switch Windows Audio ---\n";

    ProjectSettings wasapi;
    wasapi.audioDeviceType = "Windows Audio";
    wasapi.outputDeviceName = "OUT 1-4 (BEHRINGER UMC 404HD 192k)";
    wasapi.preferAsio = false;
    wasapi.sampleRate = 48000.0;
    wasapi.bufferSize = 256;
    engine.applyDeviceSettings (wasapi);
    std::cout << "Dopo switch WASAPI: " << engine.getDeviceSummary()
              << "  uscite: " << engine.getNumOutputChannels() << "\n";
    juce::Thread::sleep (500);
    engine.play();
    juce::Thread::sleep (1500);
    engine.stop();
    std::cout << "Switch WASAPI sopravvissuto, pos: " << engine.getPositionSamples() << "\n";

    ProjectSettings back;
    back.audioDeviceType = "ASIO";
    back.outputDeviceName = "UMC ASIO Driver";
    back.preferAsio = true;
    back.sampleRate = 48000.0;
    back.bufferSize = 256;
    engine.applyDeviceSettings (back);
    juce::Thread::sleep (500);
    std::cout << "Tornato ad ASIO: " << engine.getDeviceSummary() << "\n";
    return 0;
}

// Verifica rapida del test tone: tono su tutte le uscite anche da fermo.
int testToneCheck (Engine& engine)
{
    engine.setTestTone (true);
    juce::Thread::sleep (800);
    float lvl = 0.0f, ch[6] = {};
    for (int i = 0; i < 20; ++i)
    {
        juce::Thread::sleep (25);
        lvl = juce::jmax (lvl, engine.masterLevel.load());
        for (int c = 0; c < 6; ++c)
            ch[c] = juce::jmax (ch[c], engine.outputLevel[(size_t) c].load());
    }
    engine.setTestTone (false);
    std::cout << "Test tone livello: " << lvl
              << (lvl > 0.05f ? "  -> OK" : "  -> NON SUONA!") << "\n";
    for (int c = 0; c < juce::jmin (6, engine.getNumOutputChannels()); ++c)
        std::cout << "  Test tone uscita " << (c + 1) << ": " << ch[c]
                  << (ch[c] > 0.05f ? "  OK" : "  SILENZIOSA!") << "\n";
    return lvl > 0.05f ? 0 : 3;
}
