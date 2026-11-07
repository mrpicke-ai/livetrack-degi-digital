#include "MainComponent.h"
#include "SongEditor.h"
#include "../Audio/ClickGenerator.h"

namespace ldd
{
static juce::String formatTime (double seconds)
{
    if (seconds < 0.0) seconds = 0.0;
    auto m = (int) (seconds / 60.0);
    auto s = (int) seconds % 60;
    auto ms = (int) (seconds * 1000.0) % 1000;
    return juce::String::formatted ("%02d:%02d.%03d", m, s, ms);
}

class SetlistPreloadThread : public juce::ThreadWithProgressWindow
{
public:
    SetlistPreloadThread (Engine& e, std::vector<Song> songsToLoad)
        : juce::ThreadWithProgressWindow ("Caricamento scaletta", true, false),
          engine (e), songs (std::move (songsToLoad)) {}

    void run() override
    {
        for (int i = 0; i < (int) songs.size(); ++i)
        {
            const auto& song = songs[(size_t) i];
            setStatusMessage ("Brano " + juce::String (i + 1) + "/"
                              + juce::String ((int) songs.size()) + ": " + song.title);

            juce::String error;
            bool ok = false;
            for (int attempt = 0; attempt < 40 && ! ok; ++attempt)
            {
                ok = engine.preloadSong (song, error);
                if (ok || error != "busy") break;
                juce::Thread::sleep (100);
            }
            if (error.isNotEmpty())
                errors << song.title << ":\n" << error << "\n";
            setProgress ((i + 1) / (double) songs.size());
        }
    }

    juce::String errors;

private:
    Engine& engine;
    std::vector<Song> songs;
};

MainComponent::MainComponent (Engine& e)
    : engine (e)
{
    juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
    setWantsKeyboardFocus (true);
    formatManager.registerBasicFormats();

    addAndMakeVisible (logo);
    appTitle.setText ("LIVETRACK DEGI DIGITAL", juce::dontSendNotification);
    appTitle.setFont (juce::FontOptions().withHeight (20.0f).withStyle ("Bold"));
    appTitle.setColour (juce::Label::textColourId, theme::text);
    addAndMakeVisible (appTitle);

    projectNameLabel.setFont (juce::FontOptions().withHeight (15.0f));
    projectNameLabel.setColour (juce::Label::textColourId, theme::primary);
    addAndMakeVisible (projectNameLabel);

    deviceLabel.setFont (juce::FontOptions().withHeight (15.0f));
    deviceLabel.setColour (juce::Label::textColourId, theme::screenText);
    addAndMakeVisible (deviceLabel);

    xrunLabel.setFont (juce::FontOptions().withHeight (15.0f));
    xrunLabel.setColour (juce::Label::textColourId, theme::screenTextDim);
    addAndMakeVisible (xrunLabel);

    for (auto* b : { &menuButton, &modeButton, &performanceButton })
    {
        b->setColour (juce::TextButton::buttonColourId, theme::surface);
        b->setColour (juce::TextButton::textColourOffId, theme::textOnPanel);
    }

    menuButton.onClick = [this]
    {
        juce::PopupMenu m;
        m.addItem ("Apri progetto…", [this] { openProject(); });
        m.addItem ("Salva progetto…", [this] { saveProject (true); });
        m.addItem ("Nuovo progetto", [this]
        {
            newProject();
            refreshSetlistUI();
            rebuildSongSlots();
        });
        m.addSeparator();
        auto setlistMenu = juce::PopupMenu();
        setlistMenu.addItem ("Nuova scaletta", [this]
        {
            project.setlists.push_back (Setlist());
            project.currentSetlist = (int) project.setlists.size() - 1;
            currentSongIndex = -1;
            refreshSetlistUI(); rebuildSongSlots(); autosave();
        });
        setlistMenu.addItem ("Rinomina scaletta…", [this]
        {
            auto& sl = currentSetlist();
            juce::AlertWindow w ("Rinomina scaletta", "Nome:", juce::AlertWindow::NoIcon);
            w.addTextEditor ("name", sl.name);
            w.addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
            w.addButton ("Annulla", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            if (w.runModalLoop() == 1 && w.getTextEditorContents ("name").isNotEmpty())
            {
                sl.name = w.getTextEditorContents ("name");
                refreshSetlistUI(); autosave();
            }
        });
        if (project.setlists.size() > 1)
            setlistMenu.addItem ("Elimina scaletta corrente", [this]
            {
                if (project.setlists.size() <= 1) return;
                engine.stop(); engine.unloadSong();
                currentSongIndex = -1;
                project.setlists.erase (project.setlists.begin() + project.currentSetlist);
                project.currentSetlist = juce::jlimit (0, (int) project.setlists.size() - 1,
                                                       project.currentSetlist);
                refreshSetlistUI(); rebuildSongSlots(); autosave();
            });
        m.addSubMenu ("Scaletta", setlistMenu);
        m.addItem ("Esporta scaletta su file…", [this] { exportSetlist(); });
        m.addItem ("Importa scaletta…", [this] { importSetlist(); });
        m.addSeparator();
        m.addItem ("Aggiungi brano…", [this] { addSong(); });
        if (currentSongIndex >= 0)
        {
            m.addItem ("Modifica brano…", [this] { editSong (currentSongIndex); });
            m.addItem ("Duplica brano", [this] { duplicateSong (currentSongIndex); });
            m.addItem ("Elimina brano", [this] { deleteSong (currentSongIndex); });
        }
        m.addSeparator();
        m.addItem ("Impostazioni…", [this]
        {
            settingsWindow = std::make_unique<SettingsWindow> (engine, project.settings,
                [this] (const ProjectSettings& s)
                {
                    applySettingsToEngine (s);
                    project.settings = s;
                    collectSettingsFromEngine();
                    autosave();
                });
            settingsWindow->onClose = [this]
            {
                collectSettingsFromEngine();
                autosave();
                settingsWindow.reset();
            };
            settingsWindow->addToDesktop();
            settingsWindow->setVisible (true);
        });
        m.addItem ("Guida…", [this]
        {
            if (helpWindow == nullptr)
            {
                helpWindow = std::make_unique<HelpWindow>();
                helpWindow->addToDesktop();
            }
            helpWindow->setVisible (true);
            helpWindow->toFront (true);
        });
        m.addSeparator();
        m.addItem ("Livetrack Degi Digital 1.0.0 - DEGISOFT", false, false, [] {});
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&menuButton));
    };
    addAndMakeVisible (menuButton);

    modeButton.onClick = [this] { setLiveMode (! engine.isLiveMode()); };
    performanceButton.onClick = [this] { enterPerformance(); };
    addAndMakeVisible (modeButton);
    addAndMakeVisible (performanceButton);

    //==========================================================================
    // Titolo brano in esecuzione (grande e ben leggibile)
    songTitle.setFont (juce::FontOptions().withHeight (56.0f).withStyle ("Bold"));
    songTitle.setColour (juce::Label::textColourId, theme::text);
    songTitle.setJustificationType (juce::Justification::centredLeft);
    songTitle.setText ("NESSUN BRANO", juce::dontSendNotification);
    addAndMakeVisible (songTitle);

    songInfo.setFont (juce::FontOptions().withHeight (17.0f));
    songInfo.setColour (juce::Label::textColourId, theme::textDim);
    songInfo.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (songInfo);

    armedLabel.setFont (juce::FontOptions().withHeight (18.0f).withStyle ("Bold"));
    armedLabel.setColour (juce::Label::textColourId, theme::warning);
    armedLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (armedLabel);

    //==========================================================================
    // Scaletta verticale a sinistra
    setlistPanel.onSongSelected = [this] (int i) { loadSongAtIndex (i, false); };
    setlistPanel.onMenu = [this] (int i) { showSongContextMenu (i); };
    setlistPanel.onAdd = [this] { addSong(); };
    setlistPanel.onReorder = [this] (int from, int to) { reorderSong (from, to); };
    addAndMakeVisible (setlistPanel);

    //==========================================================================
    // Timeline overview
    addAndMakeVisible (overview);
    overview.onSeek = [this] (double fraction)
    {
        if (engine.getLengthSamples() > 0)
            engine.seek ((int64_t) (fraction * engine.getLengthSamples()));
    };

    //==========================================================================
    // Console
    masterFader.setSliderStyle (juce::Slider::LinearVertical);
    masterFader.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    masterFader.setRange (0.0, 1.25, 0.0);
    masterFader.setValue (1.0, juce::dontSendNotification);
    masterFader.setColour (juce::Slider::thumbColourId, theme::primary);
    masterFader.onValueChange = [this] { engine.setMasterGain ((float) masterFader.getValue()); };
    masterLabel.setColour (juce::Label::textColourId, theme::textOnPanel);
    masterLabel.setJustificationType (juce::Justification::centred);
    masterLabel.setFont (juce::FontOptions().withHeight (15.0f).withStyle ("Bold"));
    addAndMakeVisible (masterFader);
    addAndMakeVisible (masterLabel);

    //==========================================================================
    // Trasporto (pannello in basso)
    prevButton.onClick = [this] { triggerCommand (TransportCommand::prev); };
    nextButton.onClick = [this] { triggerCommand (TransportCommand::next); };
    for (auto* b : { &prevButton, &nextButton })
    {
        b->setColour (juce::TextButton::buttonColourId, theme::surface);
        b->setColour (juce::TextButton::textColourOffId, theme::textOnPanel);
        addAndMakeVisible (*b);
    }

    setLoopAButton.onClick = [this]
    {
        loopA = engine.getPositionSamples();
        if (loopB <= loopA) loopB = loopA + (int64_t) (engine.getDeviceSampleRate() * 4);
        engine.setLoopRegion (loopA, loopB);
        updateOverviewOverlays();
    };
    setLoopBButton.onClick = [this]
    {
        loopB = engine.getPositionSamples();
        if (loopB <= loopA) loopA = 0;
        engine.setLoopRegion (loopA, loopB);
        updateOverviewOverlays();
    };
    loopToggle.setClickingTogglesState (true);
    loopToggle.onClick = [this]
    {
        engine.setLoopEnabled (loopToggle.getToggleState());
        loopToggle.setButtonText (loopToggle.getToggleState() ? "LOOP ON" : "LOOP OFF");
        updateOverviewOverlays();
    };
    for (auto* b : { &setLoopAButton, &setLoopBButton, &loopToggle })
    {
        b->setColour (juce::TextButton::buttonColourId, theme::surfaceDark);
        b->setColour (juce::TextButton::textColourOffId, theme::text);
        addAndMakeVisible (*b);
    }

    // TEST AUDIO: tono di prova su tutte le uscite fisiche (anche da fermo).
    testToneButton.setClickingTogglesState (true);
    testToneButton.setColour (juce::TextButton::buttonColourId, theme::surfaceDark);
    testToneButton.setColour (juce::TextButton::buttonOnColourId, theme::warning);
    testToneButton.setColour (juce::TextButton::textColourOffId, theme::text);
    testToneButton.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    testToneButton.onClick = [this]
    {
        const bool on = testToneButton.getToggleState();
        engine.setTestTone (on);
        testToneButton.setButtonText (on ? "TEST ON" : "TEST AUDIO");
    };
    addAndMakeVisible (testToneButton);

    timecode.setFont (juce::FontOptions().withHeight (24.0f));
    timecode.setColour (juce::Label::textColourId, theme::screenText);
    timecode.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (timecode);
    addAndMakeVisible (progress);

    hintsLabel.setFont (juce::FontOptions().withHeight (16.0f));
    hintsLabel.setColour (juce::Label::textColourId, theme::textDim);
    hintsLabel.setText ("SPAZIO play/pausa  ·  FRECCE brano prec/succ  ·  doppio click nome traccia = rinomina",
                        juce::dontSendNotification);
    addAndMakeVisible (hintsLabel);

    // Tasti trasporto nella riga scaletta (grandi)
    playButton.onClick = [this] { triggerCommand (TransportCommand::playPause); };
    stopButton.onClick = [this] { triggerCommand (TransportCommand::stop); };
    panicButton.onClick = [this] { triggerCommand (TransportCommand::panic); };
    panicButton.setColour (juce::TextButton::buttonColourId, theme::danger);
    panicButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    playButton.setColour (juce::TextButton::buttonColourId, theme::primary);
    playButton.setColour (juce::TextButton::textColourOffId, theme::screen);
    stopButton.setColour (juce::TextButton::buttonColourId, theme::surface);
    stopButton.setColour (juce::TextButton::textColourOffId, theme::textOnPanel);
    for (auto* b : { &playButton, &stopButton, &panicButton })
        addAndMakeVisible (*b);

    // Performance overlay
    addChildComponent (performance);
    performance.onCommand = [this] (TransportCommand c) { triggerCommand (c); };
    performance.onExit = [this] { exitPerformance(); };

    //==========================================================================
    // Striscia pedaliera MIDI (Ampero): stati per brano, invio automatico
    addAndMakeVisible (amperoStrip);
    amperoStrip.onSongEdited = [this] (const Song& s)
    {
        // La striscia lavora su una copia: riversa SOLO i campi pedaliera
        // nel brano corrente della scaletta e autosalva.
        if (currentSongIndex >= 0)
        {
            auto& songs = currentSetlist().songs;
            if (currentSongIndex < (int) songs.size())
            {
                songs[(size_t) currentSongIndex].amperoPreset = s.amperoPreset;
                songs[(size_t) currentSongIndex].amperoSwitches = s.amperoSwitches;
                songs[(size_t) currentSongIndex].pedalEvents = s.pedalEvents;
                refreshOverviewMarkers (s);
                autosave();
            }
        }
    };

    //==========================================================================
    // Sessione precedente
    projectFile = defaultSessionFile();
    if (projectFile.existsAsFile())
    {
        juce::String err;
        project = Project::loadFromFile (projectFile, err);
    }
    if (project.setlists.empty()) project.setlists.emplace_back();

    applySettingsToEngine (project.settings);
    collectSettingsFromEngine();
    refreshSetlistUI();
    rebuildSongSlots();
    preloadCurrentSetlist();
    setLiveMode (false); // modalita' prove di default

    engine.addListener (this);
    lastKnownSampleRate = engine.getDeviceSampleRate();
    startTimerHz (30);
}

MainComponent::~MainComponent()
{
    engine.removeListener (this);
    pool.removeAllJobs (true, 2000);
    helpWindow.reset();
    settingsWindow.reset();
}

juce::File MainComponent::defaultSessionFile() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("DEGI Digital")
        .getChildFile ("last_session.liveset");
}

Setlist& MainComponent::currentSetlist()
{
    project.currentSetlist = juce::jlimit (0, (int) project.setlists.size() - 1,
                                           project.currentSetlist);
    return project.setlists[(size_t) project.currentSetlist];
}

void MainComponent::refreshSetlistUI()
{
    projectNameLabel.setText (project.name + "  -  " + currentSetlist().name,
                              juce::dontSendNotification);
}

void MainComponent::rebuildSongSlots()
{
    setlistPanel.rebuild (currentSetlist().songs, currentSongIndex);
}

//==========================================================================
void MainComponent::newProject()
{
    engine.stop();
    engine.unloadSong();
    engine.clearPreloadedSongs();
    project = Project();
    if (project.setlists.empty()) project.setlists.emplace_back();
    project.currentSetlist = 0;
    currentSongIndex = -1;
    armed = false;
    overview.clear();
    songTitle.setText ("NESSUN BRANO", juce::dontSendNotification);
    songInfo.setText ({}, juce::dontSendNotification);
    applySettingsToEngine (project.settings);
    collectSettingsFromEngine();
}

void MainComponent::openProject()
{
    juce::FileChooser fc ("Apri progetto", {}, "*.liveset");
    if (fc.browseForFileToOpen())
    {
        juce::String err;
        auto p = Project::loadFromFile (fc.getResult(), err);
        if (err.isNotEmpty())
        {
            juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                                                    "Errore apertura", err);
            return;
        }
        project = std::move (p);
        projectFile = fc.getResult();
        engine.unloadSong();
        engine.clearPreloadedSongs();
        currentSongIndex = -1;
        armed = false;
        overview.clear();
        applySettingsToEngine (project.settings);
        collectSettingsFromEngine();
        refreshSetlistUI();
        rebuildSongSlots();
        preloadCurrentSetlist();
    }
}

void MainComponent::saveProject (bool askPath)
{
    juce::File target = projectFile;
    if (askPath || ! target.existsAsFile())
    {
        juce::FileChooser fc ("Salva progetto", target, "*.liveset");
        if (! fc.browseForFileToSave (true)) return;
        target = fc.getResult().withFileExtension ("liveset");
    }
    juce::String err;
    if (project.saveToFile (target, err))
        projectFile = target;
    else
        juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                                                "Errore salvataggio", err);
}

void MainComponent::autosave()
{
    projectFile.getParentDirectory().createDirectory();
    projectFile.deleteFile();
    juce::String err;
    project.saveToFile (projectFile, err);
}

void MainComponent::exportSetlist()
{
    juce::FileChooser fc ("Esporta scaletta", {}, "*.ldscaletta");
    if (! fc.browseForFileToSave (true)) return;
    auto target = fc.getResult().withFileExtension ("ldscaletta");
    auto json = juce::JSON::toString (currentSetlist().toVar(), true);
    if (! target.replaceWithText (json))
        juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                                                "Esportazione scaletta",
                                                "Impossibile scrivere il file:\n" + target.getFullPathName());
}

void MainComponent::importSetlist()
{
    juce::FileChooser fc ("Importa scaletta", {}, "*.ldscaletta");
    if (! fc.browseForFileToOpen()) return;
    auto content = fc.getResult().loadFileAsString();
    auto v = juce::JSON::parse (content);
    if (! v.isObject())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                                                "Importazione scaletta",
                                                "File non valido o danneggiato.");
        return;
    }
    Setlist sl = Setlist::fromVar (v);
    if (sl.songs.empty())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                                                "Importazione scaletta",
                                                "La scaletta non contiene brani.");
        return;
    }
    if (sl.name == "Nuova scaletta")
        sl.name = fc.getResult().getFileNameWithoutExtension();
    project.setlists.push_back (std::move (sl));
    project.currentSetlist = (int) project.setlists.size() - 1;
    currentSongIndex = -1;
    refreshSetlistUI();
    rebuildSongSlots();
    preloadCurrentSetlist();
    autosave();
}

//==========================================================================
juce::StringArray MainComponent::engineTrackNames()
{
    juce::StringArray names;
    for (int i = 0; i < engine.getNumActiveTracks(); ++i)
        names.add (engine.getTrack (i).name);
    return names;
}

void MainComponent::addSong()
{
    Song song;
    SongEditorWindow w (song, engine.getNumActiveTracks(), engine.getDeviceSampleRate(),
                        engineTrackNames());
    if (w.runModalLoop() != 0 && song.hasAnyFile())
    {
        currentSetlist().songs.push_back (song);
        rebuildSongSlots();
        preloadCurrentSetlist();
        autosave();
    }
}

void MainComponent::editSong (int index)
{
    auto& songs = currentSetlist().songs;
    if (! juce::isPositiveAndBelow (index, (int) songs.size())) return;
    Song copy = songs[(size_t) index];
    SongEditorWindow w (copy, engine.getNumActiveTracks(), engine.getDeviceSampleRate(),
                        engineTrackNames());
    if (w.runModalLoop() != 0)
    {
        songs[(size_t) index] = copy;
        rebuildSongSlots();
        preloadCurrentSetlist();
        if (index == currentSongIndex) loadSongAtIndex (index, false);
        autosave();
    }
}

void MainComponent::duplicateSong (int index)
{
    auto& songs = currentSetlist().songs;
    if (! juce::isPositiveAndBelow (index, (int) songs.size())) return;
    auto copy = songs[(size_t) index];
    copy.title += " copia";
    songs.insert (songs.begin() + index + 1, copy);
    rebuildSongSlots();
    preloadCurrentSetlist();
    autosave();
}

void MainComponent::reorderSong (int from, int insertIndex)
{
    auto& songs = currentSetlist().songs;
    if (! juce::isPositiveAndBelow (from, (int) songs.size())) return;
    insertIndex = juce::jlimit (0, (int) songs.size(), insertIndex);
    if (insertIndex == from || insertIndex == from + 1) return;

    auto song = songs[(size_t) from];
    songs.erase (songs.begin() + from);
    if (insertIndex > from) --insertIndex;
    songs.insert (songs.begin() + insertIndex, std::move (song));

    // Il brano in riproduzione resta lo stesso: cambia solo il suo indice.
    if (currentSongIndex == from)
        currentSongIndex = insertIndex;
    else if (currentSongIndex > from && currentSongIndex <= insertIndex)
        --currentSongIndex;
    else if (currentSongIndex < from && currentSongIndex >= insertIndex)
        ++currentSongIndex;

    rebuildSongSlots();
    preloadCurrentSetlist();
    autosave();
}

void MainComponent::deleteSong (int index)
{
    auto& songs = currentSetlist().songs;
    if (! juce::isPositiveAndBelow (index, (int) songs.size())) return;
    songs.erase (songs.begin() + index);
    if (index == currentSongIndex)
    {
        engine.stop();
        engine.unloadSong();
        currentSongIndex = -1;
        armed = false;
        overview.clear();
        songTitle.setText ("NESSUN BRANO", juce::dontSendNotification);
        songInfo.setText ({}, juce::dontSendNotification);
        amperoStrip.loadSong (Song(), false);
    }
    rebuildSongSlots();
    preloadCurrentSetlist();
    autosave();
}

void MainComponent::showSongContextMenu (int index)
{
    auto& songs = currentSetlist().songs;
    if (! juce::isPositiveAndBelow (index, (int) songs.size())) return;

    juce::PopupMenu m;
    m.addItem ("Modifica brano…", [this, index] { editSong (index); });
    m.addItem ("Duplica", [this, index] { duplicateSong (index); });
    m.addSeparator();
    m.addItem ("Elimina", [this, index]
    {
        // Conferma: l'eliminazione e' definitiva (l'autosave persiste subito).
        const auto title = currentSetlist().songs[(size_t) index].title;
        juce::AlertWindow::showOkCancelBox (
            juce::AlertWindow::WarningIcon, "Elimina brano",
            "Eliminare \"" + title + "\" dalla scaletta?",
            "Elimina", "Annulla", nullptr,
            juce::ModalCallbackFunction::create (
                [this, index] (int r) { if (r != 0) deleteSong (index); }));
    });
    m.showMenuAsync (juce::PopupMenu::Options());   // appare sotto il puntatore
}

void MainComponent::selectSongFromList (int index)
{
    loadSongAtIndex (index, false);
}

//==========================================================================
void MainComponent::preloadCurrentSetlist()
{
    const auto songs = currentSetlist().songs;
    if (songs.empty()) return;

    // Finestra modale senza Annulla: finché tutti i brani non sono in RAM,
    // menu, trasporto e comandi della scaletta restano bloccati.
    SetlistPreloadThread loader (engine, songs);
    loader.runThread();
    if (loader.errors.isNotEmpty())
        juce::AlertWindow::showMessageBoxAsync (
            juce::AlertWindow::WarningIcon, "Caricamento scaletta",
            "Controlla questi file:\n\n" + loader.errors);
}

void MainComponent::loadSongAtIndex (int index, bool autoStart)
{
    auto& songs = currentSetlist().songs;
    if (! juce::isPositiveAndBelow (index, (int) songs.size())) return;
    if (loading) return;

    const Song songCopy = songs[(size_t) index];
    stagedSongIndex = index;
    if (engine.isSongPreloaded (songCopy))
    {
        onSongStaged (true, {}, index, autoStart);
        return;
    }

    loading = true;

    pool.addJob (
        std::function<juce::ThreadPoolJob::JobStatus()> (
            [this, songCopy, index, autoStart]
            {
                juce::String error;
                // Un precache può occupare lo staging: ritenta per un
                // massimo di ~4 s invece di fallire subito.
                bool ok = false;
                for (int attempt = 0; attempt < 40 && ! ok; ++attempt)
                {
                    ok = engine.stageSong (songCopy, error, index);
                    if (! ok && error == "busy")
                    {
                        error.clear();
                        juce::Thread::sleep (100);
                    }
                    else
                        break;
                }
                juce::MessageManager::callAsync (
                    [this, ok, error, index, autoStart]
                    { onSongStaged (ok, error, index, autoStart); });
                return juce::ThreadPoolJob::jobHasFinished;
            }));
}

void MainComponent::onSongStaged (bool ok, const juce::String& error, int songIndex,
                                  bool autoStart)
{
    loading = false;
    currentSongIndex = songIndex;
    armed = false;

    if (! ok)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                                                "Caricamento brano", error);
        rebuildSongSlots();
        return;
    }
    if (error.isNotEmpty())
        juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::InfoIcon,
                                                "Avvisi sui file", error);

    auto& song = currentSetlist().songs[(size_t) songIndex];

    // Mixer del brano sul motore (thread-safe), poi swap atomico dei buffer.
    engine.applySongMixer (song);
    if (! engine.commitPreloadedSong (song, autoStart))
        engine.commitStagedSong (autoStart, songIndex);
    if (! autoStart) armed = true;

    applyTrackNamesFromSong (song);

    // Mini-waveform dei file nelle strip
    for (int i = 0; i < engine.getNumActiveTracks(); ++i)
        strips[(size_t) i]->setTrackFile (song.filePaths[(size_t) i]);

    // Timeline overview: mixdown + marker
    buildMixdownOverview (song);

    loopA = -1; loopB = -1;
    loopToggle.setToggleState (false, juce::dontSendNotification);
    loopToggle.setButtonText ("LOOP OFF");
    engine.setLoopEnabled (false);
    masterClip = false;
    for (auto& c : clipLatched) c = false;
    updateOverviewOverlays();

    songTitle.setText (song.title.toUpperCase(), juce::dontSendNotification);
    songInfo.setText (song.artist + "   -   " + juce::String (song.bpm, 1) + " BPM"
                          + (song.notes.isNotEmpty() ? "   -   " + song.notes : juce::String()),
                      juce::dontSendNotification);

    // Sync strip mixer (gain/mute/solo del brano; il routing resta globale).
    // dontSendNotification: niente write-back/autosave ridondanti a ogni load.
    for (int i = 0; i < (int) strips.size() && i < engine.getNumActiveTracks(); ++i)
    {
        strips[(size_t) i]->setGainValue (song.gains[(size_t) i], juce::dontSendNotification);
        strips[(size_t) i]->setMuteState (song.mutes[(size_t) i], juce::dontSendNotification);
        strips[(size_t) i]->setSoloState (song.solos[(size_t) i], juce::dontSendNotification);
    }

    // Pedaliera MIDI: aggiorna la striscia e invia PC + CC alla Ampero,
    // cosi' la pedaliera si porta da sola sul preset del brano armato.
    amperoStrip.setMidiChannel (project.settings.midiChannel);
    amperoStrip.loadSong (song, true);

    rebuildSongSlots();
    updateTransportButtons();

    // In live precarica il brano successivo per il cambio gapless.
    if (engine.isLiveMode()) precacheNextSong();
}

// Titolo sopra il fader = SEMPRE il nome del file importato nel brano
// corrente; il nome personalizzato vale solo per le tracce vuote.
void MainComponent::applyTrackNamesFromSong (const Song& song)
{
    auto defaults = getDefaultTrackNames();
    for (int i = 0; i < engine.getNumActiveTracks(); ++i)
    {
        const auto& fp = song.filePaths[(size_t) i];
        juce::String n;
        if (fp.isNotEmpty())
            n = juce::File (fp).getFileNameWithoutExtension();
        else
        {
            n = project.settings.trackNames[(size_t) i];
            if (n.isEmpty()) n = defaults[i];
        }
        engine.setTrackName (i, n);
        if (i < (int) strips.size()) strips[(size_t) i]->setName (n);
    }
}

void MainComponent::refreshOverviewMarkers (const Song& song)
{
    auto marks = song.markers;
    for (const auto& e : song.pedalEvents)
        if (juce::isPositiveAndBelow (e.pedal, NUM_PEDALS))
        {
            const auto& sw = song.amperoSwitches[(size_t) e.pedal];
            marks.emplace_back (e.timeSec,
                                sw.name.toUpperCase() + (e.on ? " ON" : " OFF"));
        }

    const double lenSec = engine.getLengthSamples()
                          / juce::jmax (1.0, engine.getDeviceSampleRate());
    overview.setMarkers (marks, lenSec);
}

void MainComponent::buildMixdownOverview (const Song& song)
{
    auto bufs = engine.getCurrentSongBuffers();
    if (! bufs || bufs->length <= 0)
    {
        overview.clear();
        return;
    }

    const double sr = bufs->sampleRate > 0 ? (double) bufs->sampleRate : 48000.0;
    const double lenSec = bufs->length / sr;

    // Picchi calcolati DIRETTAMENTE dai buffer delle tracce: niente mixdown
    // intermedio da centinaia di MB copiato e scansionato sul thread UI
    // (con brani lunghi bloccava l'interfaccia a ogni caricamento).
    constexpr int N = 2048;
    std::vector<std::pair<float, float>> peaks ((size_t) N, { 0.0f, 0.0f });

    for (int t = 0; t < engine.getNumActiveTracks(); ++t)
    {
        if (! bufs->loaded[(size_t) t]) continue;
        const auto& tb = bufs->buf[(size_t) t];
        const int n = tb.getNumSamples();
        const int ch = juce::jmin (2, tb.getNumChannels());
        if (n <= 0 || ch <= 0) continue;
        for (int c = 0; c < ch; ++c)
        {
            const float* d = tb.getReadPointer (c);
            for (int i = 0; i < N; ++i)
            {
                int s0 = (int) ((int64_t) i * n / N);
                int s1 = (int) (((int64_t) (i + 1) * n) / N);
                if (s1 <= s0) s1 = s0 + 1;
                float lo = peaks[(size_t) i].first, hi = peaks[(size_t) i].second;
                for (int s = s0; s < juce::jmin (s1, n); ++s)
                {
                    lo = juce::jmin (lo, d[s]);
                    hi = juce::jmax (hi, d[s]);
                }
                peaks[(size_t) i] = { lo, hi };
            }
        }
    }

    refreshOverviewMarkers (song);
    overview.setPeaks (std::move (peaks), lenSec);
}

void MainComponent::precacheNextSong()
{
    auto& songs = currentSetlist().songs;
    const int nextIndex = currentSongIndex + 1;
    if (! juce::isPositiveAndBelow (nextIndex, (int) songs.size())) return;
    if (engine.hasStagedSong()) return;

    const Song next = songs[(size_t) nextIndex];
    if (engine.isSongPreloaded (next))
    {
        stagedSongIndex = nextIndex;
        return;   // già in RAM: nessuna seconda decodifica
    }
    pool.addJob (
        std::function<juce::ThreadPoolJob::JobStatus()> (
            [this, next, nextIndex]
            {
                juce::String err;
                if (engine.stageSong (next, err, nextIndex))
                    stagedSongIndex = nextIndex;   // precache pronta per il gapless
                return juce::ThreadPoolJob::jobHasFinished;
            }));
}

void MainComponent::handleSongEnded()
{
    if (engine.isLiveMode())
    {
        auto& songs = currentSetlist().songs;
        int nextIndex = currentSongIndex + 1;
        if (juce::isPositiveAndBelow (nextIndex, (int) songs.size()))
        {
            currentSongIndex = nextIndex;
            armed = false;
            auto& next = songs[(size_t) nextIndex];
            engine.applySongMixer (next);
            bool gapless = engine.getAutoStartNext();
            bool committed = false;
            if (engine.isSongPreloaded (next))
                committed = engine.commitPreloadedSong (next, gapless);
            else
            {
                gapless = gapless && engine.hasStagedSong() && stagedSongIndex == nextIndex;
                committed = engine.commitStagedSong (gapless, nextIndex);
            }
            if (! committed)
            {
                // Precache assente o sovrascritta da un caricamento manuale:
                // non committare il brano sbagliato: ricaricalo al volo.
                loadSongAtIndex (nextIndex, false);
                updateTransportButtons();
                return;
            }
            armed = ! gapless;

            applyTrackNamesFromSong (next);
            amperoStrip.setMidiChannel (project.settings.midiChannel);
            amperoStrip.loadSong (next, true);
            // Sync visivo dei fader sul mixer del NUOVO brano: senza questo le
            // strip restavano ai valori del brano precedente e, appena
            // toccavi un fader, il valore vecchio veniva salvato nel brano
            // nuovo (effetto "memoria del fader in tutte le tracce").
            for (int i = 0; i < (int) strips.size() && i < engine.getNumActiveTracks(); ++i)
            {
                strips[(size_t) i]->setGainValue (next.gains[(size_t) i], juce::dontSendNotification);
                strips[(size_t) i]->setMuteState (next.mutes[(size_t) i], juce::dontSendNotification);
                strips[(size_t) i]->setSoloState (next.solos[(size_t) i], juce::dontSendNotification);
            }
            for (int i = 0; i < engine.getNumActiveTracks(); ++i)
                strips[(size_t) i]->setTrackFile (next.filePaths[(size_t) i]);
            buildMixdownOverview (next);

            songTitle.setText (next.title.toUpperCase(), juce::dontSendNotification);
            songInfo.setText (next.artist + "   -   " + juce::String (next.bpm, 1) + " BPM",
                              juce::dontSendNotification);
            rebuildSongSlots();
            precacheNextSong();
        }
        else
        {
            armed = false; // fine scaletta
        }
    }
    updateTransportButtons();
}

juce::String MainComponent::getNextSongName()
{
    auto& songs = currentSetlist().songs;
    if (juce::isPositiveAndBelow (currentSongIndex + 1, (int) songs.size()))
        return songs[(size_t) currentSongIndex + 1].title;
    return {};
}

//==========================================================================
void MainComponent::triggerCommand (TransportCommand c)
{
    switch (c)
    {
        case TransportCommand::playPause:
            if (! engine.isPlaying())
            {
                if (engine.getDeviceManager().getCurrentAudioDevice() == nullptr)
                {
                    juce::AlertWindow::showMessageBoxAsync (
                        juce::AlertWindow::WarningIcon, "Nessun dispositivo audio",
                        "Attiva un dispositivo in Impostazioni -> Audio prima di avviare la riproduzione.");
                    break;
                }
                if (currentSongIndex < 0 && ! currentSetlist().songs.empty())
                {
                    loadSongAtIndex (0, false);
                    return;
                }
                engine.play();
                armed = false;
            }
            else engine.pause();
            break;

        case TransportCommand::stop:
            if (engine.isLiveMode())
            {
                // Protezione dalle pressioni accidentali: doppia pressione entro 2 s.
                auto now = juce::Time::getMillisecondCounter();
                if (now - lastStopPressMs < 2000) { engine.stop(); lastStopPressMs = 0; }
                else lastStopPressMs = now;
            }
            else engine.stop();
            break;

        case TransportCommand::panic:
            engine.panic();
            engine.setTestTone (false);
            testToneButton.setToggleState (false, juce::dontSendNotification);
            testToneButton.setButtonText ("TEST AUDIO");
            armed = false;
            break;

        case TransportCommand::next:
        case TransportCommand::prev:
        {
            int delta = c == TransportCommand::next ? 1 : -1;
            int target = currentSongIndex < 0 ? 0 : currentSongIndex + delta;
            auto& songs = currentSetlist().songs;
            if (engine.isLiveMode())
            {
                if (juce::isPositiveAndBelow (target, (int) songs.size()))
                {
                    engine.stop();
                    loadSongAtIndex (target, false);
                }
            }
            else if (juce::isPositiveAndBelow (target, (int) songs.size()))
                loadSongAtIndex (target, engine.isPlaying());
            break;
        }
    }
    updateTransportButtons();
}

void MainComponent::setLiveMode (bool live)
{
    engine.setLiveMode (live);
    modeButton.setButtonText (live ? "MODALITÀ: LIVE" : "MODALITÀ: PROVE");
    modeButton.setColour (juce::TextButton::buttonColourId, live ? theme::danger : theme::surface);
    modeButton.setColour (juce::TextButton::textColourOffId,
                          live ? juce::Colours::white : theme::textOnPanel);
    bool loopVisible = ! live;
    setLoopAButton.setVisible (loopVisible);
    setLoopBButton.setVisible (loopVisible);
    loopToggle.setVisible (loopVisible);
    if (live) { engine.setLoopEnabled (false); loopToggle.setToggleState (false, juce::dontSendNotification); }
    updateOverviewOverlays();
}

void MainComponent::updateTransportButtons()
{
    playButton.setButtonText (engine.isPlaying() ? "PAUSA" : "PLAY");
    if (engine.isLiveMode() && engine.isPlaying())
        stopButton.setButtonText ("STOP (x2)");
    else stopButton.setButtonText ("STOP");
    armed = armed && ! engine.isPlaying();
}

void MainComponent::updateOverviewOverlays()
{
    auto len = engine.getLengthSamples();
    double a = loopA >= 0 && len > 0 ? (double) loopA / len : 0.0;
    double b = loopB > loopA && len > 0 ? (double) loopB / len : 0.0;
    overview.setLoopRegion (a, b, engine.isLoopEnabled());
}

void MainComponent::addClipLatchAndUpdate() {}

//==========================================================================
void MainComponent::engineDeviceChanged()
{
    // Il numero di canali fisici può essere cambiato (es. UMC404HD: 2 -> 4):
    // aggiorna le opzioni di routing di ogni strip.
    auto outNames = engine.getOutputChannelNames();
    for (auto& strip : strips)
        strip->setAvailableOutputs (outNames);

    if (engine.getDeviceSampleRate() != lastKnownSampleRate)
    {
        // Il sample rate è cambiato: ricarica TUTTA la scaletta al nuovo rate.
        engine.stop();
        engine.unloadSong();
        engine.clearPreloadedSongs();
        lastKnownSampleRate = engine.getDeviceSampleRate();
        preloadCurrentSetlist();
        if (currentSongIndex >= 0) loadSongAtIndex (currentSongIndex, false);
    }
    lastKnownSampleRate = engine.getDeviceSampleRate();
}

void MainComponent::engineDeviceLost (const juce::String& message)
{
    juce::AlertWindow::showMessageBoxAsync (
        juce::AlertWindow::WarningIcon, "Dispositivo audio perso",
        "La riproduzione è stata interrotta in sicurezza.\n\n" + message
            + "\n\nRicollega il dispositivo e riapri Impostazioni. Il programma NON "
              "cambia dispositivo da solo e NON riparte in riproduzione.");
}

//==========================================================================
void MainComponent::timerCallback()
{
    // Stato dispositivo
    deviceLabel.setText (engine.getDeviceSummary(), juce::dontSendNotification);
    xrunLabel.setText ("xrun: " + juce::String (engine.xrunCount.load()),
                       juce::dontSendNotification);
    xrunLabel.setColour (juce::Label::textColourId,
                         engine.xrunCount.load() > 0 ? theme::warning : theme::textDim);

    if (engine.consumeDeviceLostFlag())
        engineDeviceLost (engine.getDeviceSummary());

    // Fine brano
    if (engine.consumeSongEndedFlag()) handleSongEnded();

    // Live: se non c'è brano caricato e c'è una scaletta, arma il primo
    if (engine.isLiveMode() && ! engine.hasSong() && ! currentSetlist().songs.empty()
        && ! loading && currentSongIndex < 0)
        loadSongAtIndex (0, false);

    // Playhead e timecode
    auto pos = engine.getPositionSamples();
    auto len = engine.getLengthSamples();
    auto rate = engine.getDeviceSampleRate();
    double posSec = rate > 0 ? pos / rate : 0.0;
    double lenSec = rate > 0 ? len / rate : 0.0;
    timecode.setText (formatTime (posSec) + "  /  " + formatTime (lenSec),
                      juce::dontSendNotification);
    progressValue = len > 0 ? (double) pos / len : 0.0;
    overview.setPlayheadFraction (progressValue);

    // Stato armato
    armedLabel.setText (armed && ! engine.isPlaying() ? "ARMATO - SPAZIO PER PARTIRE"
                                                       : (engine.isPlaying() ? "IN RIPRODUZIONE" : ""),
                        juce::dontSendNotification);

    // Meter strip + master
    for (int i = 0; i < (int) strips.size(); ++i)
    {
        float lvl = engine.trackLevel[i].load();
        if (lvl >= 0.999f) clipLatched[i] = true;
        strips[i]->setLevel (lvl, clipLatched[i]);
    }
    masterMeterLevel = engine.masterLevel.load();
    if (masterMeterLevel >= 0.999f)
    {
        masterClip = true;
        masterLabel.setColour (juce::Label::textColourId, theme::danger);
    }
    else masterLabel.setColour (juce::Label::textColourId, theme::textOnPanel);
    repaint (masterMeterRect);

    // Performance overlay
    if (performanceVisible)
        performance.update (engine.getLoadedSongTitle(), getNextSongName(),
                            engine.isPlaying(), armed, posSec,
                            juce::jmax (0.0, lenSec - posSec), pos, len);
}

//==========================================================================
void MainComponent::applySettingsToEngine (const ProjectSettings& s)
{
    engine.applyDeviceSettings (s);
    engine.setNumActiveTracks (MAX_TRACKS);
    engine.setStereoLinked (s.stereoLinked);
    engine.setAutoStartNext (s.autoStartNext);
    engine.setMidiMappings (s.midiMappings);
    engine.setMidiOutputDevice (s.midiOutputId);
    amperoStrip.setMidiChannel (s.midiChannel);

    // Azzera i routing puntati oltre le uscite fisiche disponibili
    // (scalette salvate con piu' canali del dispositivo corrente).
    for (int i = 0; i < MAX_TRACKS; ++i)
    {
        int rs = project.settings.trackOutStart[(size_t) i];
        int rc = juce::jmax (1, project.settings.trackOutCount[(size_t) i]);
        if (rs >= 0 && (rs >= engine.getNumOutputChannels() || rs + rc > engine.getNumOutputChannels()))
        {
            project.settings.trackOutStart[(size_t) i] = -1;
            project.settings.trackOutCount[(size_t) i] = 1;
        }
    }

    // Nomi e routing globali delle tracce.
    auto defaultNames = getDefaultTrackNames();
    for (int i = 0; i < MAX_TRACKS; ++i)
    {
        engine.setTrackName (i, s.trackNames[(size_t) i].isNotEmpty() ? s.trackNames[(size_t) i]
                                                                      : defaultNames[i]);
        engine.setTrackRouting (i, s.trackOutStart[(size_t) i], s.trackOutCount[(size_t) i]);
    }
}

void MainComponent::collectSettingsFromEngine()
{
    // Ricostruisci i canali della console
    strips.clear();
    auto outNames = engine.getOutputChannelNames();
    for (int i = 0; i < engine.getNumActiveTracks(); ++i)
    {
        auto strip = std::make_unique<ConsoleChannel> (i, outNames, formatManager,
                                                       thumbnailCache);
        strip->setName (engine.getTrack (i).name);
        strip->setRouting (project.settings.trackOutStart[(size_t) i],
                           project.settings.trackOutCount[(size_t) i]);
        strip->onGainChanged = [this, i] (float g)
        {
            engine.getTrack (i).gain = g;
            if (engine.isStereoLinked() && i < 2)
            {
                engine.getTrack (1 - i).gain = g;
                if (i < 2) strips[(size_t) (1 - i)]->setGainValue (g, juce::dontSendNotification);
            }
            // Il livello del fader e' del BRANO: salvalo nella scaletta.
            if (currentSongIndex >= 0)
            {
                auto& songs = currentSetlist().songs;
                if (currentSongIndex < (int) songs.size())
                {
                    songs[(size_t) currentSongIndex].gains[(size_t) i] = g;
                    if (engine.isStereoLinked() && i < 2)
                        songs[(size_t) currentSongIndex].gains[(size_t) (1 - i)] = g;
                    autosave();
                }
            }
        };
        strip->onMuteChanged = [this, i] (bool m)
        {
            engine.getTrack (i).muted = m;
            if (currentSongIndex >= 0)
            {
                auto& songs = currentSetlist().songs;
                if (currentSongIndex < (int) songs.size())
                {
                    songs[(size_t) currentSongIndex].mutes[(size_t) i] = m;
                    autosave();
                }
            }
        };
        strip->onSoloChanged = [this, i] (bool s2)
        {
            engine.getTrack (i).soloed = s2;
            if (currentSongIndex >= 0)
            {
                auto& songs = currentSetlist().songs;
                if (currentSongIndex < (int) songs.size())
                {
                    songs[(size_t) currentSongIndex].solos[(size_t) i] = s2;
                    autosave();
                }
            }
        };
        strip->onNameChanged = [this, i] (const juce::String& n)
        {
            engine.setTrackName (i, n);
            project.settings.trackNames[(size_t) i] = n;   // personalizzato: persiste
            autosave();
        };
        strip->onRoutingChanged = [this, i] (int start, int count)
        {
            engine.setTrackRouting (i, start, count);
            project.settings.trackOutStart[(size_t) i] = start;
            project.settings.trackOutCount[(size_t) i] = count;
            autosave();
        };
        addAndMakeVisible (*strip);
        strips.push_back (std::move (strip));
    }
    resized();
}

//==========================================================================
void MainComponent::enterPerformance()
{
    performanceVisible = true;
    performance.setVisible (true);
    performance.toFront (true);   // i canali della console sono figli aggiunti dopo:
                                  // senza questo coprono l'overlay
    setWantsKeyboardFocus (false);
    performance.grabKeyboardFocus();
    if (auto* dw = dynamic_cast<juce::ResizableWindow*> (getTopLevelComponent()))
        dw->setFullScreen (true);
    resized();
}

void MainComponent::exitPerformance()
{
    performanceVisible = false;
    performance.setVisible (false);
    setWantsKeyboardFocus (true);
    grabKeyboardFocus();
    if (auto* dw = dynamic_cast<juce::ResizableWindow*> (getTopLevelComponent()))
        dw->setFullScreen (false);
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    if (performanceVisible)
    {
        if (key == juce::KeyPress::escapeKey) { exitPerformance(); return true; }
        return false;
    }
    if (key == juce::KeyPress::spaceKey) { triggerCommand (TransportCommand::playPause); return true; }
    if (key == juce::KeyPress::rightKey) { triggerCommand (TransportCommand::next); return true; }
    if (key == juce::KeyPress::leftKey) { triggerCommand (TransportCommand::prev); return true; }
    return false;
}

//==========================================================================
void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);

    // Pannelli console con bevel e rivetti (la scaletta si disegna da sola)
    SpaceLookAndFeel::drawPanel (g, headerPanel);
    SpaceLookAndFeel::drawPanel (g, titlePanel);
    SpaceLookAndFeel::drawPanel (g, overviewPanel);
    SpaceLookAndFeel::drawPanel (g, consolePanel);
    SpaceLookAndFeel::drawPanel (g, transportPanel);

    // Schermi incassati a fosforo: stato dispositivo
    SpaceLookAndFeel::drawPanel (g, deviceLabel.getBounds().expanded (8, 5).getUnion (
                                        xrunLabel.getBounds().expanded (8, 5)), true);

    // Meter master (livello post-fader) incassato a sinistra del fader
    auto mm = masterMeterRect.toFloat().reduced (0.5f);
    g.setColour (theme::background.brighter (0.2f));
    g.fillRect (mm.expanded (1.5f));
    g.setColour (theme::screen);
    g.fillRect (mm);
    const float mh = (float) masterMeterRect.getHeight()
                     * juce::jlimit (0.0f, 1.0f, masterMeterLevel);
    auto mc = masterClip ? theme::danger
                         : (masterMeterLevel > 0.85f ? theme::warning : theme::primary);
    g.setColour (mc.withAlpha (masterClip ? 1.0f : 0.9f));
    g.fillRect (mm.getX() + 1.0f, mm.getBottom() - mh - 1.0f, mm.getWidth() - 2.0f, mh);

    // Scala graduata del master: il livello 1.0 (= 0 dB) e' evidenziato da una
    // riga continua attraverso meter e fader, come sulle strip dei canali.
    if (masterScaleRect.getHeight() > 40 && masterFader.getWidth() > 0)
    {
        auto tr = masterFader.getBounds().toFloat();
        struct Tick { float v; const char* label; };
        const Tick ticks[] = { { 0.25f, "-12" }, { 0.5f, "-6" }, { 0.75f, "-3" },
                               { 1.0f, "0" },   { 1.25f, "+2" } };
        g.setFont (juce::FontOptions().withHeight (12.0f).withStyle ("Bold"));
        for (auto& t : ticks)
        {
            const float y = tr.getBottom() - (t.v / 1.25f) * tr.getHeight();
            const bool unity = (t.v == 1.0f);
            juce::Colour c = unity ? theme::primary : theme::text;

            if (unity)
            {
                g.setColour (c.withAlpha (0.35f));
                g.drawLine ((float) masterMeterRect.getX(), y, tr.getRight() + 2.0f, y, 1.2f);
            }
            g.setColour (c);
            g.drawText (t.label, masterScaleRect.getX(), (int) y - 7,
                        masterScaleRect.getWidth(), 14, juce::Justification::centred);
        }
    }
}

void MainComponent::resized()
{
    auto area = getLocalBounds();

    //==========================================================================
    // Header (64 px)
    headerPanel = area.removeFromTop (64).reduced (4, 4);
    auto header = headerPanel.reduced (6, 2);
    logo.setBounds (header.removeFromLeft (56));

    auto titles = header.removeFromLeft (juce::jmin (300, juce::jmax (180, header.getWidth() / 3)));
    appTitle.setBounds (titles.removeFromTop (32));
    projectNameLabel.setBounds (titles);

    // Pulsanti a destra
    auto buttons = header.removeFromRight (juce::jmin (header.getWidth(), 370));
    menuButton.setBounds (buttons.removeFromRight (40).reduced (3));
    performanceButton.setBounds (buttons.removeFromRight (152).reduced (3));
    modeButton.setBounds (buttons.removeFromRight (142).reduced (3));
    buttons.removeFromRight (8);

    // Stato dispositivo: tutto lo spazio rimanente
    auto status = header.reduced (4, 0);
    deviceLabel.setBounds (status.removeFromTop (30));
    xrunLabel.setBounds (status);

    //==========================================================================
    // Colonna sinistra: scaletta verticale con tutti i brani
    setlistPanel.setBounds (area.removeFromLeft (280).reduced (4));

    //==========================================================================
    // Riga titolo + trasporto (130 px)
    titlePanel = area.removeFromTop (130).reduced (4, 4);
    auto row = titlePanel.reduced (10, 6);

    // Destra: trasporto grande
    auto tpb = row.removeFromRight (300);
    panicButton.setBounds (tpb.removeFromRight (92).reduced (2));
    stopButton.setBounds (tpb.removeFromRight (86).reduced (2));
    playButton.setBounds (tpb.reduced (2));
    row.removeFromRight (12);

    // Titolo brano in esecuzione: il piu' leggibile possibile
    songTitle.setBounds (row.removeFromTop (62));
    songInfo.setBounds (row.removeFromTop (22));
    armedLabel.setBounds (row.removeFromTop (20));

    //==========================================================================
    // Timeline overview (170 px)
    overviewPanel = area.removeFromTop (170).reduced (4, 4);
    overview.setBounds (overviewPanel.reduced (6, 6));

    //==========================================================================
    // Trasporto in basso (64 px)
    transportPanel = area.removeFromBottom (64).reduced (4, 4);
    auto transport = transportPanel.reduced (10, 6);

    // Destra: timecode e barra di avanzamento
    timecode.setBounds (transport.removeFromRight (200));
    if (transport.getWidth() > 700)
        progress.setBounds (transport.removeFromRight (128).reduced (4));
    else
        progress.setBounds ({});
    transport.removeFromRight (8);
    hintsLabel.setBounds (transport.removeFromRight (juce::jmin (480, transport.getWidth() / 2)));

    // Blocco navigazione + loop
    prevButton.setBounds (transport.removeFromLeft (52).reduced (2));
    nextButton.setBounds (transport.removeFromLeft (52).reduced (2));
    transport.removeFromLeft (8);

    const bool loopsVisible = ! engine.isLiveMode();
    setLoopAButton.setVisible (loopsVisible);
    setLoopBButton.setVisible (loopsVisible);
    loopToggle.setVisible (loopsVisible);
    if (loopsVisible && transport.getWidth() > 260)
    {
        auto loops = transport.removeFromLeft (juce::jmin (transport.getWidth(), 260));
        setLoopAButton.setBounds (loops.removeFromLeft (loops.getWidth() / 3).reduced (2));
        setLoopBButton.setBounds (loops.removeFromLeft (loops.getWidth() / 2).reduced (2));
        loopToggle.setBounds (loops.reduced (2));
    }
    else
    {
        setLoopAButton.setBounds ({});
        setLoopBButton.setBounds ({});
        loopToggle.setBounds ({});
    }

    // Test tone: sempre visibile, anche in live
    if (transport.getWidth() > 110)
        testToneButton.setBounds (transport.removeFromLeft (104).reduced (2));
    else
        testToneButton.setBounds ({});

    //==========================================================================
    // Striscia pedaliera MIDI (104 px, sopra il trasporto)
    amperoStrip.setBounds (area.removeFromBottom (104).reduced (4, 4));

    //==========================================================================
    // Console canali: tutto lo spazio rimanente
    consolePanel = area.reduced (4, 4);
    auto console = consolePanel.reduced (8, 6);

    auto masterArea = console.removeFromRight (96);
    masterLabel.setBounds (masterArea.removeFromBottom (22));
    auto mRow = masterArea.reduced (6, 2);
    masterMeterRect = mRow.removeFromLeft (12);
    mRow.removeFromLeft (4);
    masterFader.setBounds (mRow.removeFromLeft (38));
    mRow.removeFromLeft (4);
    masterScaleRect = mRow;

    const int nCh = juce::jmax (1, (int) strips.size());
    const int chW = juce::jmax (92, console.getWidth() / nCh);
    auto chArea = console;
    for (auto& strip : strips)
        strip->setBounds (chArea.removeFromLeft (juce::jmin (chW, chArea.getWidth())).reduced (3, 0));

    //==========================================================================
    // Overlay performance a tutto schermo
    performance.setBounds (getLocalBounds());
}
}
