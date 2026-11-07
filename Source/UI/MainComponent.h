#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "../Audio/Engine.h"
#include "../Data/Project.h"
#include "LogoComponent.h"
#include "SpaceLookAndFeel.h"
#include "SongOverview.h"
#include "ConsoleChannel.h"
#include "AmperoStrip.h"
#include "SettingsWindow.h"
#include "PerformanceMode.h"
#include "HelpComponent.h"

namespace ldd
{
/*
 * Finestra principale "console live" (Space 1999):
 * header | colonna scaletta a sinistra (tutti i brani) | titolo brano grande
 * + trasporto | timeline overview seekabile | console a 6 canali + master
 * | trasporto in basso. I nomi traccia arrivano dai file importati
 * (rinominabili col doppio click).
 */
class MainComponent : public juce::Component,
                      public Engine::Listener,
                      private juce::Timer
{
public:
    explicit MainComponent (Engine& engine);
    ~MainComponent() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;

private:
/*
 * Pannello scaletta verticale (colonna di sinistra): intestazione, lista di
 * TUTTI i brani in un viewport scorrevole (click = carica/arma, bordo verde
 * pieno = brano corrente, tratteggiato = prossimo armato) e "+ BRANO" in fondo.
 */
class SetlistPanel : public juce::Component
{
public:
    class Row : public juce::Button
    {
    public:
        Row() : juce::Button ({}) {}

        int songIndex = -1;
        bool isCurrent = false, isNext = false;
        juce::String title, bpm;
        std::function<void (int)> onMenu;
        std::function<void (int)> onDragStart;
        std::function<void (int, int)> onDragMove;
        std::function<void (int, int)> onDrop;

        // Click destro = menu contestuale (modifica/duplica/elimina);
        // non entrare nello stato "pigiato", così il click su MouseUp non scatta.
        void mouseDown (const juce::MouseEvent& e) override
        {
            if (e.mods.isRightButtonDown())
            {
                if (onMenu) onMenu (songIndex);
                return;
            }
            dragging = false;
            juce::Button::mouseDown (e);
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (! dragging && getState() != buttonDown) return;
            if (! dragging && e.getDistanceFromDragStart() > 8)
            {
                dragging = true;
                if (onDragStart) onDragStart (songIndex);
            }
            if (dragging && onDragMove)
                onDragMove (songIndex, e.getEventRelativeTo (getParentComponent()).getPosition().y);
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            if (dragging)
            {
                dragging = false;
                setState (buttonNormal);
                if (onDrop)
                    onDrop (songIndex, e.getEventRelativeTo (getParentComponent()).getPosition().y);
                return;   // un trascinamento non deve caricare il brano al rilascio
            }
            juce::Button::mouseUp (e);
        }

        void paintButton (juce::Graphics& g, bool highlighted, bool down) override
        {
            auto b = getLocalBounds().reduced (1, 1).toFloat();
            g.setColour (theme::screen);
            g.fillRect (b);
            g.setColour (theme::background.brighter (0.2f));
            g.drawRect (b, 1.0f);
            if (isCurrent)
            {
                g.setColour (theme::primary);
                g.drawRect (b.reduced (1.5f), 1.5f);
                g.fillRect (b.withWidth (4.0f));   // cursore a margine
            }
            else if (isNext)
            {
                g.setColour (theme::warning.withAlpha (0.8f));
                juce::Path p, dashed;
                p.addRectangle (b.reduced (1.5f));
                float dash[] = { 4.0f, 4.0f };
                juce::PathStrokeType (1.2f).createDashedStroke (dashed, p, dash, 2);
                g.fillPath (dashed);
            }
            if (highlighted)
            {
                g.setColour (juce::Colours::white.withAlpha (0.06f));
                g.fillRect (b);
            }

            auto ink = isCurrent ? theme::screenText : theme::screenTextDim;
            g.setColour (ink);
            g.setFont (juce::FontOptions().withHeight (21.0f).withStyle ("Bold"));
            g.drawFittedText (title, getLocalBounds().reduced (10, 3).removeFromTop (32),
                              juce::Justification::centredLeft, 1, 0.85f);
            g.setColour (theme::screenTextDim);
            g.setFont (juce::FontOptions().withHeight (14.0f));
            g.drawText (bpm, getLocalBounds().reduced (10, 3).removeFromBottom (18),
                        juce::Justification::centredLeft);
            juce::ignoreUnused (down);
        }

    private:
        bool dragging = false;
    };

    class Content : public juce::Component
    {
    public:
        int dropY = -1;

        void paintOverChildren (juce::Graphics& g) override
        {
            if (dropY < 0) return;
            g.setColour (theme::primary);
            const int y = juce::jlimit (0, juce::jmax (0, getHeight() - 3), dropY - 1);
            g.fillRect (0, y, getWidth(), 3);
        }
    };

    SetlistPanel()
    {
        header.setText ("SCALETTA", juce::dontSendNotification);
        header.setFont (juce::FontOptions().withHeight (18.0f).withStyle ("Bold"));
        header.setColour (juce::Label::textColourId, theme::text);
        header.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (header);

        addButton.setColour (juce::TextButton::buttonColourId, theme::surfaceDark);
        addButton.setColour (juce::TextButton::textColourOffId, theme::text);
        addButton.onClick = [this] { if (onAdd) onAdd(); };
        addAndMakeVisible (addButton);

        viewport.setScrollBarsShown (true, false);
        viewport.setViewedComponent (&content, false);
        addAndMakeVisible (viewport);
    }

    std::function<void (int)> onSongSelected;
    std::function<void (int)> onMenu;
    std::function<void()> onAdd;
    std::function<void (int, int)> onReorder;

    // Ricostruzione INCREMENTALE: riusa le Row esistenti, crea solo le nuove,
    // distrugge l'eccedenza. Così aggiungere un brano non rigenera (e non
    // fa lampeggiare) tutta la lista.
    void rebuild (const std::vector<Song>& songs, int currentIndex)
    {
        const int needed = (int) songs.size();
        while ((int) rows.size() > needed) rows.pop_back();   // il distruttore stacca dal content

        for (int i = 0; i < needed; ++i)
        {
            Row* row;
            if (i < (int) rows.size())
                row = rows[(size_t) i].get();
            else
            {
                auto r = std::make_unique<Row>();
                r->setMouseClickGrabsKeyboardFocus (false);
                content.addAndMakeVisible (*r);
                rows.push_back (std::move (r));
                row = rows.back().get();
            }
            row->songIndex = i;
            row->isCurrent = i == currentIndex;
            row->isNext = currentIndex >= 0 && i == currentIndex + 1;
            row->title = juce::String (i + 1) + "  " + songs[(size_t) i].title.toUpperCase();
            row->bpm = juce::String (songs[(size_t) i].bpm, 0) + " BPM";
            row->onClick = [this, i] { if (onSongSelected) onSongSelected (i); };
            // Il callback riceve l'indice dalla riga (songIndex), non dalla
            // cattura: le righe vengono riusate e l'indice catturato qui
            // sarebbe quello della rebuild in cui erano state create.
            row->onMenu = [this] (int idx) { if (onMenu) onMenu (idx); };
            row->onDragStart = [this] (int idx)
            {
                dragFrom = idx;
                content.dropY = idx * rowH;
                content.repaint();
            };
            row->onDragMove = [this] (int, int yContent)
            {
                viewport.autoScroll (0, yContent - viewport.getViewPositionY(), 24, 12);
                dropIndex = dropIndexForY (yContent);
                content.dropY = dropIndex * rowH;
                content.repaint();
            };
            row->onDrop = [this] (int idx, int yContent)
            {
                const int from = dragFrom >= 0 ? dragFrom : idx;
                const int to = dropIndexForY (yContent);
                dragFrom = -1;
                dropIndex = -1;
                content.dropY = -1;
                content.repaint();
                if (from >= 0 && from < (int) rows.size() && to != from && to != from + 1 && onReorder)
                    onReorder (from, to);
            };
            row->repaint();
        }

        if (lastRowCount != needed)
        {
            lastRowCount = needed;
            resized();   // aggiorna altezza content e layout righe
        }
    }

    void paint (juce::Graphics& g) override
    {
        SpaceLookAndFeel::drawPanel (g, getLocalBounds());
        if (rows.empty())
        {
            g.setColour (theme::textDim);
            g.setFont (juce::FontOptions().withHeight (15.0f));
            g.drawText ("(vuota - usa + BRANO)", getLocalBounds().withTrimmedTop (34),
                        juce::Justification::horizontallyCentred);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (6);
        header.setBounds (area.removeFromTop (26));
        area.removeFromTop (2);
        addButton.setBounds (area.removeFromBottom (34).reduced (2, 1));
        area.removeFromBottom (2);
        viewport.setBounds (area);
        content.setSize (juce::jmax (1, viewport.getWidth()),
                         juce::jmax (1, (int) rows.size()) * rowH);
        auto r = content.getLocalBounds();
        for (auto& row : rows)
            row->setBounds (r.removeFromTop (rowH));
    }

private:
    static constexpr int rowH = 54;
    int lastRowCount = -1;   // -1 = layout mai calcolato
    int dragFrom = -1, dropIndex = -1;
    juce::Label header;
    juce::TextButton addButton { "+ BRANO" };
    juce::Viewport viewport;
    Content content;
    std::vector<std::unique_ptr<Row>> rows;

    int dropIndexForY (int yContent) const
    {
        return juce::jlimit (0, (int) rows.size(),
                             juce::roundToInt ((float) yContent / (float) rowH));
    }

    JUCE_DECLARE_NON_COPYABLE (SetlistPanel)
};

    //==========================================================================
    Engine& engine;
    Project project;
    juce::File projectFile;
    juce::AudioFormatManager formatManager;
    juce::AudioThumbnailCache thumbnailCache { MAX_TRACKS };

    int currentSongIndex = -1;
    std::atomic<int> stagedSongIndex { -1 };   // scritto anche dal pool thread
    bool armed = false;
    bool loading = false;

    juce::int64 loopA = -1, loopB = -1;
    double lastKnownSampleRate = 0.0;
    bool clipLatched[MAX_TRACKS] = {};
    double lastStopPressMs = -10000.0;

    juce::ThreadPool pool { 3 };

    //==========================================================================
    // Header
    SpaceLookAndFeel lookAndFeel;
    LogoComponent logo;
    juce::Label appTitle, projectNameLabel, deviceLabel, xrunLabel;
    juce::TextButton menuButton { "MENU" }, modeButton { "MODALITÀ: PROVE" },
                       performanceButton { "PERFORMANCE" };

    //==========================================================================
    // Scaletta verticale a sinistra + titolo brano grande
    SetlistPanel setlistPanel;
    juce::Label songTitle, songInfo, armedLabel;

    //==========================================================================
    // Timeline + console
    SongOverview overview;
    std::vector<std::unique_ptr<ConsoleChannel>> strips;
    juce::Slider masterFader;
    juce::Label masterLabel { "MASTER" };
    juce::Rectangle<int> masterMeterRect, masterScaleRect;
    float masterMeterLevel = 0.0f;
    bool masterClip = false;

    //==========================================================================
    // Pedaliera MIDI (Ampero): preset PC + 8 pedali on/off, stati per brano
    AmperoStrip amperoStrip { engine };

    //==========================================================================
    // Trasporto
    juce::TextButton prevButton { "<<" }, nextButton { ">>" },
                       setLoopAButton { "Imposta A" }, setLoopBButton { "Imposta B" },
                       loopToggle { "LOOP OFF" }, testToneButton { "TEST AUDIO" };
    juce::Label timecode, hintsLabel;
    juce::ProgressBar progress { progressValue };
    double progressValue = 0.0;

    // Header buttons copy
    juce::TextButton playButton { "PLAY" }, stopButton { "STOP" }, panicButton { "PANIC" };

    //==========================================================================
    // Rettangoli dei pannelli console (disegnati in paint)
    juce::Rectangle<int> headerPanel, titlePanel, overviewPanel, consolePanel, transportPanel;

    //==========================================================================
    PerformanceMode performance;
    bool performanceVisible = false;

    std::unique_ptr<SettingsWindow> settingsWindow;
    std::unique_ptr<HelpWindow> helpWindow;

    //==========================================================================
    void timerCallback() override;

    // Engine::Listener
    void engineDeviceChanged() override;
    void engineDeviceLost (const juce::String& message) override;

    //==========================================================================
    Setlist& currentSetlist();
    void refreshSetlistUI();
    void rebuildSongSlots();

    void newProject();
    void openProject();
    void saveProject (bool askPath);
    void autosave();
    juce::File defaultSessionFile() const;
    void exportSetlist();
    void importSetlist();

    juce::StringArray engineTrackNames();
    void addSong();
    void editSong (int index);
    void duplicateSong (int index);
    void reorderSong (int from, int insertIndex);
    void deleteSong (int index);
    void showSongContextMenu (int index);
    void preloadCurrentSetlist();
    void loadSongAtIndex (int index, bool autoStart);
    void onSongStaged (bool ok, const juce::String& error, int songIndex, bool autoStart);
    void precacheNextSong();
    void handleSongEnded();
    juce::String getNextSongName();
    void applyTrackNamesFromSong (const Song& song);
    void refreshOverviewMarkers (const Song& song);
    void buildMixdownOverview (const Song& song);
    void selectSongFromList (int index);

    void updateTransportButtons();
    void updateOverviewOverlays();
    void setLiveMode (bool live);

    void triggerCommand (TransportCommand c);
    void enterPerformance();
    void exitPerformance();

    void applySettingsToEngine (const ProjectSettings&);
    void collectSettingsFromEngine();

    void addClipLatchAndUpdate();

    JUCE_DECLARE_NON_COPYABLE (MainComponent)
};
}
