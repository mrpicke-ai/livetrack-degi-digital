#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <atomic>
#include <memory>
#include <map>
#include "../AppData.h"
#include "../Data/Project.h"

namespace ldd
{
/*
 * Motore audio: unico punto di verità per riproduzione, routing e temporizzazione.
 * Il clock è quello del dispositivo: la posizione avanza di numSamples per blocco,
 * mai da timer grafici. Nessuna allocazione né accesso a disco nel callback audio:
 * i file vengono decodificati interamente in memoria (su altro thread) prima del
 * passaggio di mano atomico, quindi il callback fa solo letture da buffer pronti.
 */
class Engine : public juce::AudioIODeviceCallback,
               public juce::MidiInputCallback
{
public:
    struct SongBuffers
    {
        std::array<juce::AudioBuffer<float>, MAX_TRACKS> buf;
        std::array<bool, MAX_TRACKS> loaded {};
        int64_t length = 0;   // durata del brano = durata massima delle tracce
        int sampleRate = 0;
    };

    struct TrackState
    {
        std::atomic<float> gain { 1.0f };
        std::atomic<bool> muted { false };
        std::atomic<bool> soloed { false };
        // Routing: count canali fisici (1, 2 o 4) a partire da outputStart.
        // Esempi: singolo 3 -> start=2,count=1 | coppia 1-2 -> start=0,count=2 | gruppo 1-2-3-4 -> start=0,count=4
        std::atomic<int> outputStart { 0 };
        std::atomic<int> outputCount { 1 };
        juce::String name;
        juce::Colour colour;
    };

    class Listener
    {
    public:
        virtual ~Listener() = default;
        virtual void engineSongEnded() {}
        virtual void engineSongStaged (bool ok, const juce::String& error) {}
        virtual void engineDeviceChanged() {}
        virtual void engineDeviceLost (const juce::String& message) {}
    };

    Engine();
    ~Engine() override;

    // Da chiamare sul message thread all'avvio.
    void initialise();
    void shutdown();

    //==============================================================================
    // Dispositivo
    juce::AudioDeviceManager& getDeviceManager() { return deviceManager; }
    int getNumOutputChannels() const { return numOutputChannels; }
    juce::StringArray getOutputChannelNames() const { return outputChannelNames; }
    double getDeviceSampleRate() const { return deviceSampleRate; }
    int getBufferSize() const { return currentBufferSize; }
    bool isDeviceOpen() const { return deviceOpen; }
    bool isDevicePresent() const;
    juce::String getDeviceSummary() const;
    void applyDeviceSettings (const ProjectSettings&);
    void setPreferredDeviceType (const juce::String& typeName);
    void setTrackName (int track, const juce::String& name);
    void setTrackRouting (int track, int startChannel, int channelCount);

    //==============================================================================
    // Trasporto (thread-safe)
    void play();
    void pause();
    void togglePlayPause();
    void stop();
    void panic();
    void seek (int64_t sample);

    bool isPlaying() const { return playing.load(); }
    bool isPaused() const { return paused.load(); }
    bool isStopped() const { return ! playing.load() && ! paused.load(); }
    int64_t getPositionSamples() const { return position.load(); }
    int64_t getLengthSamples() const { return songLength; }
    bool hasSong() const { return currentBuffers != nullptr; }
    juce::String getLoadedSongTitle() const { return loadedSong.title; }
    const Song& getLoadedSong() const { return loadedSong; }
    // Buffer del brano caricati (sola lettura GUI: serve per la waveform overview).
    std::shared_ptr<const SongBuffers> getCurrentSongBuffers() const { return currentBuffers; }

    void setLiveMode (bool live);
    bool isLiveMode() const { return liveMode.load(); }
    void setAutoStartNext (bool b) { autoStartNext = b; }
    bool getAutoStartNext() const { return autoStartNext.load(); }

    // Loop A/B (sala prove)
    void setLoopRegion (int64_t start, int64_t end);
    void setLoopEnabled (bool b) { loopEnabled = b; }
    bool isLoopEnabled() const { return loopEnabled.load(); }
    int64_t getLoopStart() const { return loopStart.load(); }
    int64_t getLoopEnd() const { return loopEnd.load(); }

    //==============================================================================
    // Tracce (thread-safe)
    TrackState& getTrack (int i) { return tracks[(size_t) i]; }
    void setNumActiveTracks (int n);
    int getNumActiveTracks() const { return numActiveTracks; }
    void setStereoLinked (bool b) { stereoLinked = b; }
    bool isStereoLinked() const { return stereoLinked.load(); }
    void setMasterGain (float g) { masterGain = g; }
    float getMasterGain() const { return masterGain.load(); }
    // Applica sul motore i gain/mute/solo salvati nel brano (il routing è globale).
    void applySongMixer (const Song&);

    // Test tone: sinusoide su TUTTE le uscite fisiche, per verificare cavi e
    // monitor indipendentemente dai brani (si attiva anche da fermo).
    void setTestTone (bool b) { testTone = b; }
    bool isTestToneActive() const { return testTone.load(); }

    // Meter (scritti dal thread audio, letti dalla GUI)
    std::atomic<float> trackLevel[MAX_TRACKS];
    std::atomic<float> masterLevel { 0.0f };
    std::atomic<float> outputLevel[MAX_TRACKS];   // livello per CANALE FISICO di uscita
    // Diagnostica one-shot: layout buffer ricevuto dal callback.
    std::atomic<int> diagNumOut { -1 };
    std::atomic<int> diagNullMask { 0 };
    // Diagnostica one-shot: stato del loop tracce durante la riproduzione.
    std::atomic<int> diagRenderNumOut { -1 };
    std::atomic<int> diagRenderActiveTracks { -1 };
    std::atomic<int> diagRenderLoaded0 { -1 };
    std::atomic<int> diagRenderStartCh { -999 };
    std::atomic<int> diagRenderPtr { -1 };
    std::atomic<int> xrunCount { 0 };
    void clearClippingFlags();

    //==============================================================================
    // Caricamento brani: stage (decodifica in background) -> commit (swap atomico).
    // stageSong è bloccante: va chiamata da un thread di lavoro, non dalla GUI.
    // Solo UNO staging alla volta: se un precache è in corso, stageSong
    // restituisce subito false con error = "busy" (la GUI riprova dopo).
    bool stageSong (const Song& song, juce::String& error, int index = -1);
    bool hasStagedSong() const { return stagedIndex.load (std::memory_order_acquire) != -2; }
    void discardStagedSong()
    {
        stagedSongCopy = Song();
        stagedBuffers.reset();
        stagedIndex.store (-2, std::memory_order_release);
    }
    // Indice (nella scaletta) del brano attualmente staged: -1 = nessuno.
    int getStagedSongIndex() const { return stagedIndex.load(); }
    // Commit gapless: se keepTransport=true la riproduzione continua senza soluzione.
    // Se keepTransport=false il trasporto si ferma e il brano resta "armato".
    // expectedIndex >= 0: committa SOLO se il brano staged è quello atteso
    // (protegge da precache sovrascritto da un caricamento manuale).
    // Restituisce false se non c'era nulla da committare o l'indice non coincide
    // (i buffer staged vengono scartati in quel caso).
    bool commitStagedSong (bool keepTransport, int expectedIndex = -1);

    // Cache dell'intera scaletta: precaricamento su thread di lavoro, commit
    // immediato sul thread GUI senza decodifica né accessi al disco.
    bool preloadSong (const Song& song, juce::String& error);
    bool isSongPreloaded (const Song& song) const;
    bool commitPreloadedSong (const Song& song, bool keepTransport);
    void clearPreloadedSongs();
    void unloadSong();

    //==============================================================================
    // MIDI
    void setMidiMappings (std::vector<MidiMapping> m);
    const std::vector<MidiMapping>& getMidiMappings() const { return midiMappings; }
    void setMidiLearn (bool active, int learnCommand = -1)
    {
        midiLearnActive = active;
        midiLearnCommand = learnCommand;
    }
    // Ultimo messaggio MIDI catturato in modalità learn (0 = nessuno).
    // Encoding: type<<24 | channel<<16 | number.
    int consumeLearnedMessage();
    void addMidiMapping (const MidiMapping& m);

    //==============================================================================
    // MIDI OUT (pedaliera esterna, es. Hotone Ampero)
    // Apertura/chiusura dal thread GUI; invii thread-safe con lock breve.
    static juce::StringArray getMidiOutputDeviceIds();
    static juce::StringArray getMidiOutputDeviceNames();
    // identifier vuoto = chiudi uscita. Restituisce false se l'apertura fallisce.
    bool setMidiOutputDevice (const juce::String& identifier);
    juce::String getMidiOutputDeviceId() const { return midiOutputId; }
    bool isMidiOutputOpen() const;
    // channel 1-16. program 0-127.
    void sendProgramChange (int channel, int program);
    // value 0-127 (0 = off, 127 = on).
    void sendControlChange (int channel, int controllerNumber, int value);

    //==============================================================================
    // Diagnostica MIDI: monitor degli ultimi messaggi TX/RX e ingresso di test
    // per il loopback (cavo OUT->IN sulla stessa scheda). Apertura dal thread GUI.
    bool openMidiTestInput (const juce::String& matchName);
    bool isMidiTestInputOpen() const { return midiTestIn != nullptr; }
    juce::String getLastMidiOutDesc() const;
    juce::String getLastMidiInDesc() const;
    // Sequenza di prova: bank 0 + PC 0 + CC 79 on/off (ch = canale 1-16).
    void sendTestSequence (int channel);

    // Fine brano rilevata dal thread audio: la GUI la consuma col timer.
    bool consumeSongEndedFlag();
    bool consumeDeviceLostFlag() { return deviceLostFlag.exchange (false); }

    void addListener (Listener* l) { listeners.add (l); }
    void removeListener (Listener* l) { listeners.remove (l); }

    // Decodifica statica e testabile di un file verso il sample rate del dispositivo.
    static bool decodeFile (juce::AudioFormatManager& afm, const juce::File& file,
                            double targetSampleRate, juce::AudioBuffer<float>& out,
                            juce::String& error);

private:
    //==============================================================================
    juce::String songCacheKey (const Song& song) const;
    std::shared_ptr<SongBuffers> decodeSongBuffers (const Song& song, juce::String& error);
    bool commitBuffers (const Song& song, std::shared_ptr<SongBuffers> buffers, bool keepTransport);

    struct Command
    {
        enum Type { play, pause, stop, panicCmd, seekTo } type;
        int64_t value = 0;
    };
    static constexpr int COMMAND_QUEUE_SIZE = 64;
    Command commandQueue[COMMAND_QUEUE_SIZE];
    std::atomic<int> commandRead { 0 }, commandWrite { 0 };
    bool pushCommand (Command);
    void processCommands();   // solo thread audio

    // AudioIODeviceCallback
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceIOCallbackWithContext (const float* const*, int, float* const*,
                                           int, int, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceStopped() override;
    void audioDeviceError (const juce::String&) override;

    void renderBlock (float* const* outputChannelData, int numOut, int numSamples);

    // MidiInputCallback
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;
    void dispatchCommand (TransportCommand);

    //==============================================================================
    juce::AudioDeviceManager deviceManager;
    juce::AudioFormatManager formatManager;

    std::map<juce::String, std::shared_ptr<SongBuffers>> preloadedSongs;
    mutable juce::CriticalSection preloadedSongsLock;
    std::shared_ptr<SongBuffers> currentBuffers, stagedBuffers;
    // Ultimo set di buffer "pensionati": trattenuti un commit in più per
    // non liberare memoria ancora in uso dal callback audio in volo.
    std::shared_ptr<SongBuffers> retiringBuffers;
    // Puntatore grezzo specchiato di currentBuffers, letto dal thread audio:
    // il control block del shared_ptr resta toccato SOLO dal thread GUI,
    // così il callback audio non fa mai una copia racy del shared_ptr.
    std::atomic<SongBuffers*> currentBuffersPtr { nullptr };
    // Staging: un solo stage alla volta (pool multithread), con l'indice
    // del brano staged per evitare commit del brano sbagliato.
    std::atomic<bool> stagingBusy { false };
    std::atomic<int> stagedIndex { -2 };   // -2 = nessuno | -1 = sconosciuto | >= 0 = indice scaletta
    Song loadedSong, stagedSongCopy;
    std::atomic<int64_t> songLength { 0 };

    std::atomic<int64_t> position { 0 };
    std::atomic<bool> playing { false }, paused { false };
    std::atomic<bool> loopEnabled { false };
    std::atomic<int64_t> loopStart { 0 }, loopEnd { 0 };
    std::atomic<bool> liveMode { false }, autoStartNext { false };
    std::atomic<float> masterGain { 1.0f };
    std::atomic<bool> stereoLinked { false };

    double deviceSampleRate = 48000.0;
    int currentBufferSize = 256;
    int numOutputChannels = 0;
    juce::StringArray outputChannelNames;
    std::atomic<bool> deviceOpen { false };
    std::atomic<bool> deviceLostFlag { false };
    std::atomic<bool> songEndedFlag { false };
    std::atomic<double> lastCallbackMs { 0.0 };
    std::atomic<bool> testTone { false };
    double testPhase = 0.0;   // solo thread audio

    TrackState tracks[MAX_TRACKS];
    int numActiveTracks = DEFAULT_TRACKS;
    float audioThreadGain[MAX_TRACKS] = {};   // stato smoothing lato audio
    float audioThreadMasterGain = 1.0f;
    int fadeOutRemaining = 0;                 // campioni di fade residui (pause/stop/panic)
    int fadeOutLength = 64;                 // durata del fade calibrata sul buffer
    bool pendingPanicSilence = false;

    std::vector<MidiMapping> midiMappings;
    juce::CriticalSection midiMappingsLock;
    std::atomic<bool> midiLearnActive { false };
    std::atomic<int> midiLearnCommand { -1 };
    std::atomic<int> lastLearned { 0 };

    // Uscita MIDI (pedaliera): aperta sul thread GUI, invii protetti da lock.
    std::unique_ptr<juce::MidiOutput> midiOut;
    juce::CriticalSection midiOutLock;
    juce::String midiOutputId;

    // Diagnostica MIDI: ingresso di test (loopback) e ultime descrizioni TX/RX.
    std::unique_ptr<juce::MidiInput> midiTestIn;
    mutable juce::CriticalSection midiDiagLock;
    juce::String lastMidiOutDesc { "--" }, lastMidiInDesc { "--" };

    juce::ListenerList<Listener> listeners;
    bool panicSilence = false;   // dopo il fade di panic, blocca l'uscita finché non riparte

    JUCE_DECLARE_NON_COPYABLE (Engine)
};
}
