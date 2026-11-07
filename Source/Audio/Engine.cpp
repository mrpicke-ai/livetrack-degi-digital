#include "Engine.h"

namespace ldd
{
Engine::Engine()
{
    auto names = getDefaultTrackNames();
    for (int i = 0; i < MAX_TRACKS; ++i)
    {
        tracks[i].name = names[i];
        tracks[i].colour = getTrackColour (i);
        tracks[i].outputStart = i;
        tracks[i].outputCount = 1;
        tracks[i].gain = 1.0f;
        audioThreadGain[i] = 1.0f;
        trackLevel[i] = 0.0f;
    }
}

Engine::~Engine()
{
    shutdown();
}

void Engine::initialise()
{
    formatManager.registerBasicFormats();

    // Stereo all'avvio: chiedere subito 9 canali fallisce sulle schede a 2/4
    // uscite e lascia il motore senza dispositivo attivo (play bloccato).
    // Il numero pieno di canali viene richiesto in applyDeviceSettings().
    deviceManager.initialise (0, 2, nullptr, true);

    // ASIO è la modalità prioritaria; se assente resta il default di JUCE.
    for (auto* type : deviceManager.getAvailableDeviceTypes())
        if (type->getTypeName() == "ASIO")
        {
            deviceManager.setCurrentAudioDeviceType ("ASIO", true);
            break;
        }

    deviceManager.addAudioCallback (this);

    for (auto& d : juce::MidiInput::getAvailableDevices())
        deviceManager.setMidiInputDeviceEnabled (d.identifier, false);

    deviceManager.addMidiInputDeviceCallback ({}, this);
}

void Engine::shutdown()
{
    deviceManager.removeAudioCallback (this);
    deviceManager.removeMidiInputDeviceCallback ({}, this);
    deviceManager.closeAudioDevice();
}

//==============================================================================
juce::String Engine::getDeviceSummary() const
{
    if (auto* d = deviceManager.getCurrentAudioDevice())
        return d->getName() + "  |  " + juce::String ((int) d->getCurrentSampleRate()) + " Hz  |  "
             + juce::String (d->getCurrentBufferSizeSamples()) + " campioni";
    return "Nessun dispositivo attivo";
}

void Engine::setPreferredDeviceType (const juce::String& typeName)
{
    for (auto* type : deviceManager.getAvailableDeviceTypes())
        if (type->getTypeName() == typeName)
        {
            if (deviceManager.getCurrentDeviceTypeObject() != type)
                deviceManager.setCurrentAudioDeviceType (typeName, true);
            return;
        }
}

void Engine::applyDeviceSettings (const ProjectSettings& s)
{
    auto log = [] (const juce::String& line)
    {
        juce::File (juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                        .getChildFile ("DEGI Digital")
                        .getChildFile ("device_log.txt"))
            .appendText (line + "\n");
    };

    // Salva lo stato attuale PRIMA di toccare qualsiasi cosa: se il nuovo
    // dispositivo non si apre (es. WASAPI sulla UMC mentre l'ASIO tiene la
    // scheda occupata) si ripristina quello funzionante. In live non si
    // resta mai senza audio.
    juce::AudioDeviceManager::AudioDeviceSetup previousSetup;
    deviceManager.getAudioDeviceSetup (previousSetup);
    previousSetup.useDefaultInputChannels = false;
    previousSetup.useDefaultOutputChannels = false;
    const juce::String previousType = deviceManager.getCurrentAudioDeviceType();
    const bool hadDevice = deviceManager.getCurrentAudioDevice() != nullptr;

    setPreferredDeviceType (s.audioDeviceType);
    // Tipo cambiato: dai al driver precedente il tempo di rilasciare
    // l'hardware. L'ASIO della UMC libera la scheda in modo asincrono e
    // un'apertura WASAPI immediata fallisce (o va in crash nel driver).
    if (deviceManager.getCurrentAudioDeviceType() != previousType)
        juce::Thread::sleep (500);

    auto* type = deviceManager.getCurrentDeviceTypeObject();
    if (type == nullptr)
    {
        log ("applyDeviceSettings: nessun tipo di dispositivo corrente");
        return;
    }

    log ("=== applyDeviceSettings ===");
    log ("tipo corrente: " + type->getTypeName());
    log ("nome salvato: " + s.outputDeviceName);
    log ("nomi disponibili: " + type->getDeviceNames (false).joinIntoString (" | "));

    // Risolvi il nome dispositivo tra quelli esposti dal driver. Il nome
    // salvato puo' differire (es. "OUT 1-2 (BEHRINGER UMC 404HD 192k)" vs
    // "BEHRINGER UMC 404HD 192k"): cerchiamo prima la corrispondenza esatta,
    // poi quella che contiene il nome salvato, poi il primo disponibile.
    juce::String deviceName = s.outputDeviceName;
    auto names = type->getDeviceNames (false);
    if (! names.contains (deviceName))
    {
        juce::String resolved;
        for (auto& n : names)
            if (deviceName.isNotEmpty() && n.containsIgnoreCase (deviceName))
            {
                resolved = n;
                break;
            }
        if (resolved.isEmpty())
            for (auto& n : names)
                if (deviceName.containsIgnoreCase (n))
                {
                    resolved = n;
                    break;
                }
        if (resolved.isEmpty() && names.size() > 0) resolved = names[0];
        if (resolved.isNotEmpty()) deviceName = resolved;
    }

    // Token hardware (parte tra parentesi del nome, es. "BEHRINGER UMC 404HD
    // 192k"): serve a riconoscere endpoint/driver che si riferiscono alla
    // stessa scheda fisica.
    juce::String hwToken = deviceName.fromFirstOccurrenceOf ("(", false, false)
                                .upToFirstOccurrenceOf (")", false, false)
                                .trim();
    if (hwToken.isEmpty()) hwToken = deviceName;

    // Se il tipo corrente non e' ASIO ma esiste un driver ASIO riferito alla
    // stessa scheda (es. "UMC ASIO Driver" per la Behringer UMC), passa ad
    // ASIO: latenza minore e canali fisici nativi senza endpoint WASAPI
    // separati. Si puo' disattivare dall'apposita spunta in Impostazioni.
    // Parole generiche ("audio", "device", "driver"...) non contano.
    if (s.preferAsio && type->getTypeName() != "ASIO")
    {
        for (auto* t : deviceManager.getAvailableDeviceTypes())
        {
            if (t->getTypeName() != "ASIO") continue;
            juce::String asioName;
            for (auto& n : t->getDeviceNames (false))
            {
                for (auto word : juce::StringArray::fromTokens (hwToken, " ()", ""))
                {
                    if (word.length() < 3) continue;
                    auto w = word.toLowerCase();
                    if (w == "audio" || w == "device" || w == "speakers" || w == "driver") continue;
                    if (n.containsIgnoreCase (word)) { asioName = n; break; }
                }
                if (asioName.isNotEmpty()) break;
            }
            if (asioName.isNotEmpty())
            {
                deviceManager.setCurrentAudioDeviceType ("ASIO", true);
                type = deviceManager.getCurrentDeviceTypeObject();
                deviceName = asioName;
                names = type->getDeviceNames (false);
                log ("upgrade ad ASIO: " + asioName);
                break;
            }
        }
    }

    // WASAPI espone la stessa scheda come endpoint separati ("OUT 1-2",
    // "OUT 3-4", "OUT 1-4"): tra quelli della stessa scheda scegli quello con
    // PIU' canali, cosi' la UMC404HD apre tutte e 4 le uscite fisiche.

    // Quanti canali fisici ha DAVVERO questo dispositivo? La risposta corretta
    // viene dal tipo di driver (es. UMC404HD = 4), non da una richiesta fissa.
    int maxOut = 2;
    juce::String bestName = deviceName;
    if (auto dev = std::unique_ptr<juce::AudioIODevice> (type->createDevice (deviceName, juce::String())))
        maxOut = juce::jlimit (2, MAX_TRACKS, dev->getOutputChannelNames().size());
    for (auto& n : names)
    {
        if (! n.containsIgnoreCase (hwToken)) continue;
        if (auto d2 = std::unique_ptr<juce::AudioIODevice> (type->createDevice (n, juce::String())))
        {
            const int c = juce::jlimit (2, MAX_TRACKS, d2->getOutputChannelNames().size());
            if (c > maxOut)
            {
                maxOut = c;
                bestName = n;
            }
        }
    }
    deviceName = bestName;
    log ("nome risolto: " + deviceName + "  maxOut: " + juce::String (maxOut));

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager.getAudioDeviceSetup (setup);
    setup.outputDeviceName = deviceName;
    // FONDAMENTALE: con useDefault*Channels = true (il default) JUCE ignora
    // i bit sotto e riapre il dispositivo con i canali fissati all'avvio
    // (initialise(0, 2, ...)) = sempre 2 uscite, le 3-4 restano mute.
    setup.useDefaultInputChannels = false;
    setup.useDefaultOutputChannels = false;
    if (s.sampleRate > 0) setup.sampleRate = s.sampleRate;

    // Apri TUTTE le uscite fisiche: il patchbay deve offrire 1..4 sulla UMC.
    setup.outputChannels.clear();
    for (int i = 0; i < maxOut; ++i) setup.outputChannels.setBit (i);
    if (s.bufferSize > 0) setup.bufferSize = s.bufferSize;

    log ("canali richiesti: " + juce::String (setup.outputChannels.countNumberOfSetBits())
         + "  useDefaultOut=" + (setup.useDefaultOutputChannels ? "si" : "no"));
    try
    {
        deviceManager.setAudioDeviceSetup (setup, true);
    }
    catch (...)
    {
        log ("ECCEZIONE in setAudioDeviceSetup");
    }
    if (auto* opened = deviceManager.getCurrentAudioDevice())
        log ("aperto: " + opened->getName() + "  canali uscita: "
             + juce::String (opened->getOutputChannelNames().size())
             + "  ATTIVI: " + juce::String (opened->getActiveOutputChannels().countNumberOfSetBits()));
    else
        log ("apertura fallita, passo al fallback stereo");
    if (deviceManager.getCurrentAudioDevice() == nullptr)
    {
        // Ultimo tentativo: stereo. Meglio un dispositivo attivo con 2 canali
        // che nessun dispositivo.
        setup.outputChannels.clear();
        setup.outputChannels.setBit (0);
        setup.outputChannels.setBit (1);
        try
        {
            deviceManager.setAudioDeviceSetup (setup, true);
        }
        catch (...)
        {
            log ("ECCEZIONE nel fallback stereo");
        }
        if (auto* opened = deviceManager.getCurrentAudioDevice())
            log ("fallback: " + opened->getName() + "  canali uscita: "
                 + juce::String (opened->getOutputChannelNames().size()));
    }

    // Anche il fallback e' fallito e prima c'era un dispositivo attivo:
    // ripristina tipo e setup precedenti. In live e' preferibile tornare
    // al dispositivo di prima piuttosto che restare in silenzio.
    if (deviceManager.getCurrentAudioDevice() == nullptr && hadDevice)
    {
        log ("ripristino dispositivo precedente: " + previousType + " / "
             + previousSetup.outputDeviceName);
        deviceManager.setCurrentAudioDeviceType (previousType, true);
        juce::Thread::sleep (500);
        try
        {
            deviceManager.setAudioDeviceSetup (previousSetup, true);
        }
        catch (...)
        {
            log ("ECCEZIONE nel ripristino");
        }
        if (auto* opened = deviceManager.getCurrentAudioDevice())
            log ("ripristinato: " + opened->getName());
        else
            log ("ripristino fallito");
    }
}

void Engine::setTrackName (int track, const juce::String& name)
{
    if (juce::isPositiveAndBelow (track, MAX_TRACKS) && name.isNotEmpty())
        tracks[(size_t) track].name = name;
}

void Engine::setTrackRouting (int track, int startChannel, int channelCount)
{
    if (! juce::isPositiveAndBelow (track, MAX_TRACKS)) return;
    tracks[(size_t) track].outputStart = juce::jmax (-1, juce::jmin (startChannel, MAX_TRACKS - 1));
    tracks[(size_t) track].outputCount = juce::jlimit (1, 4, channelCount);
}

//==============================================================================
void Engine::play()
{
    pushCommand ({ Command::play, 0 });
}

void Engine::pause()
{
    pushCommand ({ Command::pause, 0 });
}

void Engine::togglePlayPause()
{
    if (isPlaying()) pause(); else play();
}

void Engine::stop()
{
    pushCommand ({ Command::stop, 0 });
}

void Engine::panic()
{
    pushCommand ({ Command::panicCmd, 0 });
}

void Engine::seek (int64_t sample)
{
    auto len = songLength.load();
    if (len > 0) sample = juce::jlimit<int64_t> (0, juce::jmax<int64_t> (0, len - 1), sample);
    else sample = 0;
    pushCommand ({ Command::seekTo, sample });
}

void Engine::setLiveMode (bool live)
{
    liveMode = live;
    if (live) loopEnabled = false;
}

void Engine::setLoopRegion (int64_t start, int64_t end)
{
    if (end > start)
    {
        auto len = songLength.load();
        loopStart = juce::jmax<int64_t> (0, start);
        loopEnd = juce::jmin (end, len > 0 ? len : end);
    }
}

void Engine::setNumActiveTracks (int n)
{
    numActiveTracks = juce::jlimit (1, MAX_TRACKS, n);
}

void Engine::applySongMixer (const Song& song)
{
    for (int i = 0; i < MAX_TRACKS; ++i)
    {
        tracks[(size_t) i].gain = song.gains[(size_t) i];
        tracks[(size_t) i].muted = song.mutes[(size_t) i];
        tracks[(size_t) i].soloed = song.solos[(size_t) i];
        // Il routing è globale (Impostazioni / strip del mixer), non per-brano.
    }
}

void Engine::clearClippingFlags()
{
    // I flag di clipping vengono gestiti dalla GUI con latch; qui resettiamo i picchi.
    for (int i = 0; i < MAX_TRACKS; ++i) trackLevel[i].store (0.0f);
    masterLevel.store (0.0f);
}

//==============================================================================
juce::String Engine::songCacheKey (const Song& song) const
{
    juce::String key = juce::String (deviceSampleRate) + "|";
    for (const auto& path : song.filePaths)
        key << path << "|";
    return key;
}

std::shared_ptr<Engine::SongBuffers> Engine::decodeSongBuffers (const Song& song,
                                                                juce::String& error)
{
    // Decodifica serializzata: AudioFormatManager e buffer di lavoro non
    // vengono mai condivisi da due thread di caricamento.
    bool freeFlag = false;
    if (! stagingBusy.compare_exchange_strong (freeFlag, true))
    {
        error = "busy";
        return {};
    }

    auto buffers = std::make_shared<SongBuffers>();
    buffers->sampleRate = (int) deviceSampleRate;
    error.clear();

    for (int i = 0; i < MAX_TRACKS; ++i)
    {
        const auto& path = song.filePaths[(size_t) i];
        if (path.isEmpty()) continue;

        juce::File f (path);
        if (! f.existsAsFile())
        {
            error << "Manca il file della traccia " << tracks[(size_t) i].name << ": " << path << "\n";
            continue;
        }

        juce::String decodeError;
        if (decodeFile (formatManager, f, deviceSampleRate, buffers->buf[(size_t) i], decodeError))
            buffers->loaded[(size_t) i] = true;
        else
            error << "Traccia " << tracks[(size_t) i].name << ": " << decodeError << "\n";
    }

    int64_t len = 0;
    for (int i = 0; i < MAX_TRACKS; ++i)
        if (buffers->loaded[(size_t) i])
            len = juce::jmax (len, (int64_t) buffers->buf[(size_t) i].getNumSamples());

    stagingBusy = false;
    if (len == 0)
    {
        if (error.isEmpty()) error = "Nessun file audio valido nel brano.";
        return {};
    }

    buffers->length = len;
    return buffers;
}

bool Engine::preloadSong (const Song& song, juce::String& error)
{
    const auto key = songCacheKey (song);
    {
        juce::ScopedLock sl (preloadedSongsLock);
        if (preloadedSongs.find (key) != preloadedSongs.end())
        {
            error.clear();
            return true;
        }
    }

    auto buffers = decodeSongBuffers (song, error);
    if (! buffers) return false;
    juce::ScopedLock sl (preloadedSongsLock);
    preloadedSongs[key] = std::move (buffers);
    return true;
}

bool Engine::isSongPreloaded (const Song& song) const
{
    juce::ScopedLock sl (preloadedSongsLock);
    return preloadedSongs.find (songCacheKey (song)) != preloadedSongs.end();
}

void Engine::clearPreloadedSongs()
{
    juce::ScopedLock sl (preloadedSongsLock);
    preloadedSongs.clear();
}

bool Engine::stageSong (const Song& song, juce::String& error, int index)
{
    auto buffers = decodeSongBuffers (song, error);
    if (! buffers) return false;
    stagedSongCopy = song;
    stagedBuffers = std::move (buffers);
    stagedIndex.store (index, std::memory_order_release);
    return true;
}

bool Engine::commitBuffers (const Song& song, std::shared_ptr<SongBuffers> buffers,
                            bool keepTransport)
{
    if (! buffers) return false;
    // Rilascio posticipato: i buffer del brano precedente restano vivi fino
    // al PROSSIMO commit/unload, così il callback audio in volo non tocca
    // memoria già liberata (un callback dura ms, i commit sono distanti).
    retiringBuffers = currentBuffers;
    currentBuffers = std::move (buffers);
    currentBuffersPtr.store (currentBuffers.get(), std::memory_order_release);
    loadedSong = song;
    songLength = currentBuffers->length;
    loopEnabled = false;
    position = 0;
    paused = false;
    playing = keepTransport;
    if (keepTransport) panicSilence = false;
    return true;
}

bool Engine::commitPreloadedSong (const Song& song, bool keepTransport)
{
    std::shared_ptr<SongBuffers> buffers;
    {
        juce::ScopedLock sl (preloadedSongsLock);
        const auto it = preloadedSongs.find (songCacheKey (song));
        if (it == preloadedSongs.end()) return false;
        buffers = it->second;
    }
    stagedBuffers.reset();
    stagedIndex.store (-2, std::memory_order_release);
    return commitBuffers (song, std::move (buffers), keepTransport);
}

bool Engine::commitStagedSong (bool keepTransport, int expectedIndex)
{
    const int si = stagedIndex.load (std::memory_order_acquire);
    if (! stagedBuffers || si == -2) return false;
    if (expectedIndex >= 0 && si >= 0 && si != expectedIndex)
    {
        // I buffer staged non sono il brano atteso (un caricamento manuale
        // ha sovrascritto la precache): scartali senza toccare il trasporto.
        stagedBuffers.reset();
        stagedIndex.store (-2, std::memory_order_release);
        return false;
    }
    const auto song = stagedSongCopy;
    auto buffers = stagedBuffers;
    stagedBuffers.reset();
    stagedIndex.store (-2, std::memory_order_release);
    return commitBuffers (song, std::move (buffers), keepTransport);
}

void Engine::unloadSong()
{
    retiringBuffers = currentBuffers;   // rilascio posticipato (vedi commit)
    currentBuffers.reset();
    currentBuffersPtr.store (nullptr, std::memory_order_release);
    stagedBuffers.reset();
    stagedIndex.store (-2, std::memory_order_release);
    songLength = 0;
    position = 0;
    playing = false;
    paused = false;
    loadedSong = Song();
}

bool Engine::consumeSongEndedFlag()
{
    return songEndedFlag.exchange (false);
}

//==============================================================================
bool Engine::pushCommand (Command c)
{
    auto w = commandWrite.load (std::memory_order_relaxed);
    auto next = (w + 1) % COMMAND_QUEUE_SIZE;
    if (next == commandRead.load (std::memory_order_acquire)) return false; // piena: ignora
    commandQueue[w] = c;
    commandWrite.store (next, std::memory_order_release);
    return true;
}

void Engine::processCommands()
{
    for (;;)
    {
        auto r = commandRead.load (std::memory_order_relaxed);
        if (r == commandWrite.load (std::memory_order_acquire)) break;
        auto c = commandQueue[r];
        commandRead.store ((r + 1) % COMMAND_QUEUE_SIZE, std::memory_order_release);

        switch (c.type)
        {
            case Command::play:
                if (currentBuffers != nullptr)
                {
                    if (position.load() >= songLength) position = 0;
                    panicSilence = false;
                    fadeOutRemaining = 0;
                    playing = true;
                    paused = false;
                }
                break;
            case Command::pause:
                if (playing) { playing = false; paused = true; fadeOutRemaining = fadeOutLength; }
                break;
            case Command::stop:
                playing = false; paused = false; position = 0; fadeOutRemaining = fadeOutLength;
                break;
            case Command::panicCmd:
                playing = false; paused = false; position = 0; fadeOutRemaining = fadeOutLength;
                pendingPanicSilence = true;
                break;
            case Command::seekTo:
                position = c.value;
                break;
        }
    }
}

//==============================================================================
void Engine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    deviceSampleRate = device->getCurrentSampleRate();
    currentBufferSize = device->getCurrentBufferSizeSamples();
    numOutputChannels = device->getOutputChannelNames().size();
    outputChannelNames = device->getOutputChannelNames();
    deviceOpen = true;
    deviceLostFlag = false;
    fadeOutLength = juce::jmax (16, currentBufferSize / 4);
    lastCallbackMs = 0.0;

    // Avvisa l'UI: canali/sample rate possono essere cambiati.
    listeners.call ([] (Listener& l) { l.engineDeviceChanged(); });
}

void Engine::audioDeviceStopped()
{
    deviceOpen = false;
    if (playing) deviceLostFlag = true;
    playing = false;
    paused = false;
}

void Engine::audioDeviceError (const juce::String& errorMessage)
{
    deviceLostFlag = true;
    // Interruzione sicura: nessun ripristino automatico di riproduzione.
    playing = false;
    paused = false;
    position = 0;
    panicSilence = true;
    juce::Logger::writeToLog ("Errore dispositivo audio: " + errorMessage);
}

bool Engine::isDevicePresent() const
{
    return deviceOpen.load() && ! deviceLostFlag.load();
}

void Engine::audioDeviceIOCallbackWithContext (const float* const*, int,
                                               float* const* outputChannelData,
                                               int numOut, int numSamples,
                                               const juce::AudioIODeviceCallbackContext&)
{
    // Rilevazione underrun: se dal callback precedente è passato più del triplo
    // del tempo previsto, conteggia un xrun.
    auto now = juce::Time::getMillisecondCounterHiRes();
    auto last = lastCallbackMs.exchange (now, std::memory_order_relaxed);
    if (last > 0.0 && deviceSampleRate > 0.0)
    {
        auto expectedMs = 1000.0 * (double) numSamples / deviceSampleRate;
        if (now - last > expectedMs * 3.0) ++xrunCount;
    }

    if (diagNumOut.load() == -1)
    {
        int mask = 0;
        for (int c = 0; c < numOut; ++c)
            if (outputChannelData[c] == nullptr) mask |= (1 << c);
        diagNullMask.store (mask);
        diagNumOut.store (numOut);
    }

    renderBlock (outputChannelData, numOut, numSamples);
}

void Engine::renderBlock (float* const* out, int numOut, int n)
{
    processCommands();

    // Puntatore grezzo atomico: nessuna copia di shared_ptr sul thread audio.
    SongBuffers* bufs = currentBuffersPtr.load (std::memory_order_acquire);
    const bool active = playing.load() && ! paused.load() && bufs != nullptr;
    const bool fadeOut = ! active && fadeOutRemaining > 0;

    if (active && diagRenderNumOut.load (std::memory_order_relaxed) == -1)
    {
        diagRenderNumOut.store (numOut);
        diagRenderActiveTracks.store (numActiveTracks);
        diagRenderPtr.store (bufs != nullptr ? 1 : 0);
        if (bufs != nullptr)
        {
            diagRenderLoaded0.store (bufs->loaded[0] ? 1 : 0);
            diagRenderStartCh.store (tracks[0].outputStart.load());
        }
    }

    for (int c = 0; c < numOut; ++c)
        if (out[c] != nullptr) juce::FloatVectorOperations::clear (out[c], n);

    // Test tone: suona su tutte le uscite fisiche anche da fermo, per
    // verificare cavi, mixer e monitor senza bisogno di un brano caricato.
    if (testTone.load())
    {
        const double inc = juce::MathConstants<double>::twoPi * 440.0
                           / juce::jmax (1.0, deviceSampleRate);
        for (int s = 0; s < n; ++s)
        {
            testPhase += inc;
            if (testPhase > juce::MathConstants<double>::twoPi) testPhase -= juce::MathConstants<double>::twoPi;
            const float v = 0.22f * (float) std::sin (testPhase);
            for (int c = 0; c < numOut; ++c)
                if (out[c] != nullptr) out[c][s] += v;
        }
        masterLevel.store (0.22f);
        for (int c = 0; c < MAX_TRACKS; ++c)
            outputLevel[(size_t) c].store (c < numOut ? 0.22f : 0.0f);
        return;
    }

    if (! active && ! fadeOut)
    {
        for (int i = 0; i < numActiveTracks; ++i) trackLevel[i].store (0.0f);
        masterLevel.store (0.0f);
        for (int c = 0; c < MAX_TRACKS; ++c) outputLevel[(size_t) c].store (0.0f);
        return;
    }

    const int64_t pos = position.load (std::memory_order_relaxed);

    bool anySolo = false;
    for (int i = 0; i < numActiveTracks; ++i)
        if (tracks[(size_t) i].soloed.load()) anySolo = true;

    const float mTarget = panicSilence || fadeOut ? 0.0f : masterGain.load();
    const float mStart = audioThreadMasterGain;
    const float mStep = (mTarget - mStart) / (float) n;

    for (int i = 0; i < numActiveTracks; ++i)
    {
        if (! bufs->loaded[(size_t) i]) continue;
        const int startCh = tracks[(size_t) i].outputStart.load();
        const int reqCount = tracks[(size_t) i].outputCount.load();
        if (startCh < 0 || startCh >= numOut || reqCount <= 0) continue;
        const int destCount = juce::jmin (reqCount, numOut - startCh, 4);

        const auto& src = bufs->buf[(size_t) i];
        const int srcLen = src.getNumSamples();
        const int srcNumCh = src.getNumChannels();
        float* dst[4] = {};
        const float* srcCh[4] = {};
        for (int d = 0; d < destCount; ++d)
        {
            dst[d] = out[startCh + d];
            srcCh[d] = src.getReadPointer (d % srcNumCh);
        }

        float t = tracks[(size_t) i].gain.load();
        if (fadeOut || panicSilence) t = 0.0f;
        else if (tracks[(size_t) i].muted.load() || (anySolo && ! tracks[(size_t) i].soloed.load())) t = 0.0f;

        const float gStart = audioThreadGain[i];
        const float gStep = (t - gStart) / (float) n;

        float peak = 0.0f;
        int64_t idx = pos;
        for (int s = 0; s < n; ++s, ++idx)
        {
            const float g = (gStart + gStep * (float) s) * (mStart + mStep * (float) s);
            for (int d = 0; d < destCount; ++d)
            {
                const float v = idx < srcLen ? srcCh[d][idx] : 0.0f;
                dst[d][s] += v * g;
                peak = juce::jmax (peak, std::abs (v * g));
            }
        }
        audioThreadGain[i] = t;
        trackLevel[i].store (peak);
    }
    audioThreadMasterGain = mTarget;

    // Avanzamento posizione esclusivamente campione-per-campione.
    if (active)
    {
        int64_t newPos = pos + n;
        if (loopEnabled.load() && loopEnd.load() > loopStart.load() && newPos >= loopEnd.load())
        {
            const int64_t span = loopEnd.load() - loopStart.load();
            newPos = loopStart.load() + (newPos - loopStart.load()) % span;
        }
        else if (songLength > 0 && newPos >= songLength)
        {
            playing = false;
            paused = false;
            songEndedFlag = true;
            newPos = songLength;
        }
        position.store (newPos, std::memory_order_relaxed);
    }
    else if (fadeOut)
    {
        fadeOutRemaining = 0;
        if (pendingPanicSilence) { panicSilence = true; pendingPanicSilence = false; }
    }

    // Meter master + meter per canale fisico.
    float mPeak = 0.0f;
    for (int c = 0; c < numOut; ++c)
    {
        float chPeak = 0.0f;
        if (out[c] != nullptr)
            for (int s = 0; s < n; ++s)
                chPeak = juce::jmax (chPeak, std::abs (out[c][s]));
        mPeak = juce::jmax (mPeak, chPeak);
        if (c < MAX_TRACKS) outputLevel[(size_t) c].store (chPeak);
    }
    for (int c = numOut; c < MAX_TRACKS; ++c) outputLevel[(size_t) c].store (0.0f);
    masterLevel.store (mPeak);
}

//==============================================================================
void Engine::setMidiMappings (std::vector<MidiMapping> m)
{
    juce::ScopedLock sl (midiMappingsLock);
    midiMappings = std::move (m);
}

bool Engine::decodeFile (juce::AudioFormatManager& afm, const juce::File& file,
                         double targetSampleRate, juce::AudioBuffer<float>& out,
                         juce::String& error)
{
    std::unique_ptr<juce::AudioFormatReader> reader (afm.createReaderFor (file));
    if (reader == nullptr)
    {
        error = "Formato non supportato: " + file.getFileName();
        return false;
    }

    const int numCh = juce::jmin (2, (int) reader->numChannels);
    const int64_t inLen = reader->lengthInSamples;
    if (inLen <= 0)
    {
        error = "File vuoto o corrotto: " + file.getFileName();
        return false;
    }

    const double inRate = reader->sampleRate;
    juce::AudioBuffer<float> in (numCh, (int) inLen + 8);
    in.clear();
    reader->read (&in, 0, (int) inLen, 0, true, true);
    // FONDAMENTALE: il reader scrive nei puntatori grezzi e non tocca il flag
    // interno "cleared" lasciato da clear(): senza questo, nel ramo senza
    // ricampionamento makeCopyOf() vede hasBeenCleared()==true e AZZERA
    // l'output (ogni file col sample rate uguale al dispositivo suonava
    // in silenzio).
    in.setNotClear();

    if (std::abs (inRate - targetSampleRate) < 1.0)
    {
        // Stesso sample rate: copia diretta. ATTENZIONE: qui setSize DEVE
        // tenere il contenuto (keepExistingContent=true): con false azzera
        // tutto il buffer appena copiato (bug del silenzio a sample rate
        // uguale al dispositivo).
        out.makeCopyOf (in);
        out.setSize (numCh, (int) inLen, true, true, true);
        return true;
    }

    const auto ratio = inRate / targetSampleRate;
    const int outLen = (int) ((double) inLen / ratio) + 1;
    out.setSize (numCh, outLen, false, true, true);

    for (int ch = 0; ch < numCh; ++ch)
    {
        juce::LagrangeInterpolator interp;
        interp.reset();
        interp.process (ratio, in.getReadPointer (ch), out.getWritePointer (ch), outLen);
    }
    return true;
}

//==============================================================================
int Engine::consumeLearnedMessage()
{
    auto v = lastLearned.exchange (0);
    if (v != 0 && midiLearnCommand >= 0)
        addMidiMapping ({ midiLearnCommand, v >> 24, (v >> 16) & 0xff, v & 0xffff });
    return v;
}

void Engine::addMidiMapping (const MidiMapping& m)
{
    juce::ScopedLock sl (midiMappingsLock);
    for (auto& x : midiMappings)
        if (x.command == m.command) { x = m; return; }
    midiMappings.push_back (m);
}

//==============================================================================
juce::StringArray Engine::getMidiOutputDeviceIds()
{
    juce::StringArray ids;
    for (auto& d : juce::MidiOutput::getAvailableDevices())
        ids.add (d.identifier);
    return ids;
}

juce::StringArray Engine::getMidiOutputDeviceNames()
{
    juce::StringArray names;
    for (auto& d : juce::MidiOutput::getAvailableDevices())
        names.add (d.name);
    return names;
}

bool Engine::setMidiOutputDevice (const juce::String& identifier)
{
    // Apertura sul thread GUI (puo' bloccare per qualche 10 ms).
    std::unique_ptr<juce::MidiOutput> newOut;
    if (identifier.isNotEmpty())
    {
        newOut = juce::MidiOutput::openDevice (identifier);
        if (newOut == nullptr)
            return false;
    }

    juce::ScopedLock sl (midiOutLock);
    midiOut = std::move (newOut);
    midiOutputId = midiOut != nullptr ? identifier : juce::String();
    return true;
}

bool Engine::isMidiOutputOpen() const
{
    juce::ScopedLock sl (midiOutLock);
    return midiOut != nullptr;
}

void Engine::sendProgramChange (int channel, int program)
{
    juce::ScopedLock sl (midiOutLock);
    if (midiOut != nullptr)
        midiOut->sendMessageNow (juce::MidiMessage::programChange (
            juce::jlimit (1, 16, channel), juce::jlimit (0, 127, program)));
    juce::ScopedLock dl (midiDiagLock);
    lastMidiOutDesc = midiOut != nullptr
        ? "PC " + juce::String (program) + " (ch " + juce::String (channel) + ")"
        : juce::String ("uscita MIDI non aperta!");
}

void Engine::sendControlChange (int channel, int controllerNumber, int value)
{
    juce::ScopedLock sl (midiOutLock);
    if (midiOut != nullptr)
        midiOut->sendMessageNow (juce::MidiMessage::controllerEvent (
            juce::jlimit (1, 16, channel), juce::jlimit (0, 127, controllerNumber),
            juce::jlimit (0, 127, value)));
    juce::ScopedLock dl (midiDiagLock);
    lastMidiOutDesc = midiOut != nullptr
        ? "CC " + juce::String (controllerNumber) + "=" + juce::String (value)
          + " (ch " + juce::String (channel) + ")"
        : juce::String ("uscita MIDI non aperta!");
}

bool Engine::openMidiTestInput (const juce::String& matchName)
{
    midiTestIn.reset();
    auto devs = juce::MidiInput::getAvailableDevices();
    if (devs.isEmpty()) return false;
    auto chosen = devs.getFirst();
    if (matchName.isNotEmpty())
        for (auto& d : devs)
            if (d.name.containsIgnoreCase (matchName)) { chosen = d; break; }
    midiTestIn = juce::MidiInput::openDevice (chosen.identifier, this);
    return midiTestIn != nullptr;
}

juce::String Engine::getLastMidiOutDesc() const
{
    juce::ScopedLock dl (midiDiagLock);
    return lastMidiOutDesc;
}

juce::String Engine::getLastMidiInDesc() const
{
    juce::ScopedLock dl (midiDiagLock);
    return lastMidiInDesc;
}

void Engine::sendTestSequence (int channel)
{
    sendControlChange (channel, 0, 0);      // bank MSB 0
    sendProgramChange (channel, 0);         // patch 1 (P01-1)
    sendControlChange (channel, 79, 127);   // FS1 on
    sendControlChange (channel, 79, 0);     // FS1 off
    juce::ScopedLock dl (midiDiagLock);
    lastMidiOutDesc = "TEST: bank0+PC0+CC79 on/off (ch " + juce::String (channel) + ")";
}

void Engine::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& msg)
{
    // Diagnostica RX: registra sempre l'ultimo messaggio visto in ingresso.
    {
        juce::String d;
        if (msg.isProgramChange())
            d = "PC " + juce::String (msg.getProgramChangeNumber());
        else if (msg.isController())
            d = "CC " + juce::String (msg.getControllerNumber()) + "="
                + juce::String (msg.getControllerValue());
        else if (msg.isNoteOnOrOff())
            d = "nota " + juce::String (msg.getNoteNumber());
        else
            d = "msg 0x" + juce::String::toHexString (msg.getRawData()[0]);
        d += " (ch " + juce::String (msg.getChannel()) + ")";
        juce::ScopedLock dl (midiDiagLock);
        lastMidiInDesc = d;
    }

    if (midiLearnActive)
    {
        int type = -1, number = -1, chan = msg.getChannel();
        if (msg.isController()) { type = 0; number = msg.getControllerNumber(); }
        else if (msg.isNoteOn()) { type = 1; number = msg.getNoteNumber(); }
        else if (msg.isProgramChange()) { type = 2; number = msg.getProgramChangeNumber(); }
        if (type >= 0)
            lastLearned.store ((type << 24) | (chan << 16) | number);
        return;
    }

    juce::Array<MidiMapping> snapshot;
    {
        juce::ScopedLock sl (midiMappingsLock);
        snapshot.addArray (midiMappings.data(), (int) midiMappings.size());
    }

    for (auto& m : snapshot)
    {
        bool match = false;
        if (m.type == 0 && msg.isController() && msg.getControllerNumber() == m.number) match = true;
        if (m.type == 1 && msg.isNoteOn() && msg.getNoteNumber() == m.number) match = true;
        if (m.type == 2 && msg.isProgramChange() && msg.getProgramChangeNumber() == m.number) match = true;
        if (! match) continue;
        if (m.channel > 0 && msg.getChannel() != m.channel) continue;

        if (m.command >= 0 && m.command < (int) TransportCommand::count)
            dispatchCommand ((TransportCommand) m.command);
    }
}

void Engine::dispatchCommand (TransportCommand c)
{
    switch (c)
    {
        case TransportCommand::playPause: togglePlayPause(); break;
        case TransportCommand::stop: stop(); break;
        case TransportCommand::panic: panic(); break;
        case TransportCommand::next:
        case TransportCommand::prev: break; // gestito dalla GUI (navigazione setlist)
    }
}
}
