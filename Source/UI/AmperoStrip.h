#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include "../Audio/Engine.h"
#include "../Data/Project.h"
#include "SpaceLookAndFeel.h"

namespace ldd
{
/*
 * Striscia pedaliera MIDI (Hotone Ampero): Program Change del preset del
 * brano + 8 interruttori on/off (Control Change) per accendere/spegnere i
 * pedali del preset usato in modalita' stomp.
 *
 * Due livelli di controllo:
 * 1. STATO BASE (per brano): inviato a caricamento/armo del brano.
 * 2. EVENTI A TEMPO: cambi di stato in punti precisi del brano
 *    (es. "al secondo 92 accendi il DRIVE"). La testina li esegue da sola
 *    durante la riproduzione; uno spostamento della testina riapplica lo
 *    stato corretto alla nuova posizione.
 *
 * REC EVENTI: da fermo o in play, armando REC ogni click su un pedale viene
 * inviato subito e memorizzato come evento nella posizione attuale del brano.
 * Click normale (REC spento) = cambio immediato + salva nello stato base.
 * Doppio click sul pedale = rinomina e cambia numero CC. EVENTI... apre la
 * lista degli eventi del brano per rivederli o eliminarli.
 *
 * I messaggi escono sull'uscita MIDI configurata in
 * MENU -> Impostazioni -> scheda MIDI (dispositivo + canale).
 */
class AmperoStrip : public juce::Component,
                    private juce::Timer
{
public:
    explicit AmperoStrip (Engine& e) : engine (e)
    {
        titleLabel.setFont (juce::FontOptions().withHeight (16.0f).withStyle ("Bold"));
        titleLabel.setColour (juce::Label::textColourId, theme::textOnPanel);
        titleLabel.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (titleLabel);

        // Diagnostica MIDI: TEST invia bank+PC+CC e apre l'ingresso corrispondente
        // all'uscita attiva (per loopback OUT->IN o per vedere cosa rientra).
        testButton.setColour (juce::TextButton::buttonColourId, theme::surfaceDark);
        testButton.setColour (juce::TextButton::textColourOffId, theme::warning);
        testButton.onClick = [this]
        {
            if (! engine.isMidiTestInputOpen())
            {
                juce::String outName;
                auto ids = Engine::getMidiOutputDeviceIds();
                auto names = Engine::getMidiOutputDeviceNames();
                const auto cur = engine.getMidiOutputDeviceId();
                for (int i = 0; i < ids.size(); ++i)
                    if (ids[i] == cur) { outName = names[i]; break; }
                engine.openMidiTestInput (outName);
            }
            engine.sendTestSequence (channel);
        };
        addAndMakeVisible (testButton);

        diagLabel.setFont (juce::FontOptions().withHeight (11.5f));
        diagLabel.setColour (juce::Label::textColourId, theme::screenTextDim);
        diagLabel.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (diagLabel);

        pcLabel.setFont (juce::FontOptions().withHeight (14.0f));
        pcLabel.setColour (juce::Label::textColourId, theme::textOnPanel);
        pcLabel.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (pcLabel);

        pcSlider.setSliderStyle (juce::Slider::IncDecButtons);
        pcSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 56, 24);
        pcSlider.setRange (-1.0, 300.0, 1.0);
        pcSlider.setValue (-1.0, juce::dontSendNotification);
        pcSlider.textFromValueFunction = [] (double v)
        {
            return v < 0.0 ? juce::String ("--") : juce::String ((int) v);
        };
        pcSlider.setColour (juce::Slider::textBoxTextColourId, theme::screenText);
        pcSlider.setColour (juce::Slider::textBoxBackgroundColourId, theme::screen);
        pcSlider.setColour (juce::Slider::textBoxOutlineColourId, theme::surfaceDark);
        pcSlider.onValueChange = [this]
        {
            working.amperoPreset = (int) pcSlider.getValue();
            notifyEdited();
        };
        addAndMakeVisible (pcSlider);

        for (auto* b : { &sendButton, &recButton, &eventsButton })
        {
            b->setColour (juce::TextButton::buttonColourId, theme::surfaceDark);
            b->setColour (juce::TextButton::textColourOffId, theme::text);
            addAndMakeVisible (*b);
        }
        sendButton.onClick = [this] { sendCurrentState(); flashHint(); };

        recButton.setClickingTogglesState (true);
        recButton.setColour (juce::TextButton::buttonOnColourId, theme::danger);
        recButton.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        recButton.onClick = [this]
        {
            recording = recButton.getToggleState();
            recButton.setButtonText (recording ? "REC ON" : "REC EVENTI");
        };

        eventsButton.onClick = [this] { showEventsEditor(); };

        for (int i = 0; i < NUM_PEDALS; ++i)
        {
            auto b = std::make_unique<PedalButton>();
            b->index = i;
            b->setClickingTogglesState (true);
            const int idx = i;
            b->nameGetter = [this, idx] { return working.amperoSwitches[(size_t) idx].name; };
            b->ccGetter = [this, idx] { return working.amperoSwitches[(size_t) idx].cc; };
            b->onClick = [this, idx] { pedalClicked (idx); };
            b->onEdit = [this] (int i2) { editPedal (i2); };
            addAndMakeVisible (*b);
            pedals.push_back (std::move (b));
        }

        hintLabel.setFont (juce::FontOptions().withHeight (13.0f));
        hintLabel.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (hintLabel);

        refreshHint();
        startTimerHz (30);   // esecuzione eventi a tempo + messaggio guida
    }

    std::function<void (const Song&)> onSongEdited;   // persistenza (autosave lato MainComponent)

    // Aggiorna la striscia coi dati del brano; sendMidi=true invia PC + CC
    // alla pedaliera (caricamento brano / armo).
    void loadSong (const Song& song, bool sendMidi)
    {
        working = song;
        hasSong = true;
        lastPosSec = 0.0;
        firedEvents.assign (working.pedalEvents.size(), false);
        pcSlider.setValue (working.amperoPreset, juce::dontSendNotification);
        for (int i = 0; i < NUM_PEDALS; ++i)
        {
            // Ogni brano parte SEMPRE da pedali spenti; solo gli eventi a tempo
            // potranno attivarli durante la riproduzione.
            working.amperoSwitches[(size_t) i].on = false;
            pedals[(size_t) i]->setToggleState (false, juce::dontSendNotification);
        }
        if (sendMidi)
        {
            sendCurrentState();
            applyEventsUpTo (0.0);
        }
    }

    void setMidiChannel (int ch) { channel = juce::jlimit (1, 16, ch); }

    void paint (juce::Graphics& g) override
    {
        SpaceLookAndFeel::drawPanel (g, getLocalBounds());
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (6, 5);

        auto left = area.removeFromLeft (286);
        auto titleRow = left.removeFromTop (18);
        testButton.setBounds (titleRow.removeFromRight (52));
        titleLabel.setBounds (titleRow);
        auto pcRow = left.removeFromTop (26);
        pcLabel.setBounds (pcRow.removeFromLeft (52));
        pcSlider.setBounds (pcRow.reduced (0, 1));
        auto btnRow = left.removeFromTop (24);
        sendButton.setBounds (btnRow.removeFromLeft (92).reduced (0, 1));
        btnRow.removeFromLeft (5);
        recButton.setBounds (btnRow.removeFromLeft (92).reduced (0, 1));
        btnRow.removeFromLeft (5);
        eventsButton.setBounds (btnRow.removeFromLeft (92).reduced (0, 1));
        diagLabel.setBounds (left.removeFromTop (13));
        hintLabel.setBounds (left);

        area.removeFromLeft (10);
        const int n = NUM_PEDALS;
        const int gap = 6;
        const int w = juce::jmax (40, (area.getWidth() - gap * (n - 1)) / n);
        for (int i = 0; i < n && area.getWidth() >= w; ++i)
        {
            pedals[(size_t) i]->setBounds (area.removeFromLeft (w));
            if (i < n - 1) area.removeFromLeft (gap);
        }
    }

private:
    //==============================================================================
    class PedalButton : public juce::Button
    {
    public:
        PedalButton() : juce::Button ({}) {}
        int index = 0;
        std::function<juce::String()> nameGetter;
        std::function<int()> ccGetter;
        std::function<void (int)> onEdit;

    protected:
        void mouseDoubleClick (const juce::MouseEvent&) override
        {
            if (onEdit) onEdit (index);
        }

        void paintButton (juce::Graphics& g, bool highlighted, bool down) override
        {
            auto b = getLocalBounds().reduced (1).toFloat();
            const bool on = getToggleState();

            g.setColour (on ? theme::primary : theme::screen);
            g.fillRect (b);
            g.setColour (theme::background.brighter (0.2f));
            g.drawRect (b, 1.0f);
            if (highlighted || down)
            {
                g.setColour (juce::Colours::white.withAlpha (0.08f));
                g.fillRect (b);
            }

            const auto name = nameGetter ? nameGetter() : juce::String ("Pedale");
            const int cc = ccGetter ? ccGetter() : 0;
            g.setColour (on ? theme::screen : theme::screenTextDim);
            g.setFont (juce::FontOptions().withHeight (16.0f).withStyle ("Bold"));
            g.drawFittedText (name, getLocalBounds().reduced (4).removeFromTop (26),
                              juce::Justification::centred, 1, 0.8f);
            g.setFont (juce::FontOptions().withHeight (13.0f));
            g.drawText (on ? "ON" : "off", getLocalBounds().removeFromBottom (18),
                        juce::Justification::centred);
            g.setFont (juce::FontOptions().withHeight (11.0f));
            g.setColour (on ? theme::screen.withAlpha (0.7f)
                            : theme::screenTextDim.withAlpha (0.7f));
            g.drawText ("CC " + juce::String (cc), getLocalBounds().reduced (3),
                        juce::Justification::bottomRight);
        }
    };

    //==============================================================================
    // Lista eventi del brano: rivedi, aggiungi a mano ed elimina. Finestra
    // non modale: la riproduzione continua mentre la consulti.
    class EventsEditorContent : public juce::Component
    {
    public:
        explicit EventsEditorContent (Song& s) : song (s)
        {
            addRowLabel.setText ("Aggiungi:", juce::dontSendNotification);
            addRowLabel.setColour (juce::Label::textColourId, theme::text);
            addAndMakeVisible (addRowLabel);

            timeEditor.setInputRestrictions (10, "0123456789.:");
            timeEditor.setTextToShowWhenEmpty ("mm:ss.s", theme::textDim);
            timeEditor.setColour (juce::TextEditor::backgroundColourId, theme::screen);
            timeEditor.setColour (juce::TextEditor::textColourId, theme::screenText);
            timeEditor.setColour (juce::TextEditor::outlineColourId, theme::surfaceDark);
            addAndMakeVisible (timeEditor);

            onOffToggle.setButtonText ("ACCESO");
            onOffToggle.setClickingTogglesState (true);
            onOffToggle.setColour (juce::ToggleButton::textColourId, theme::text);
            onOffToggle.setColour (juce::ToggleButton::tickColourId, theme::primary);
            onOffToggle.onClick = [this]
            {
                onOffToggle.setButtonText (onOffToggle.getToggleState() ? "ACCESO" : "SPENTO");
            };
            addAndMakeVisible (onOffToggle);

            addButton.setButtonText ("Aggiungi");
            addButton.setColour (juce::TextButton::buttonColourId, theme::surfaceDark);
            addButton.setColour (juce::TextButton::textColourOffId, theme::primary);
            addButton.onClick = [this] { addEvent(); };
            addAndMakeVisible (addButton);
            addAndMakeVisible (pedalBox);

            listViewport.setScrollBarsShown (true, false);
            listViewport.setViewedComponent (&listContent, false);
            addAndMakeVisible (listViewport);

            for (auto* b : { &clearAllButton, &closeButton })
            {
                b->setColour (juce::TextButton::buttonColourId, theme::surfaceDark);
                b->setColour (juce::TextButton::textColourOffId, theme::text);
                addAndMakeVisible (*b);
            }
            clearAllButton.onClick = [this]
            {
                song.pedalEvents.clear();
                refresh();
                if (onChanged) onChanged();
            };
            closeButton.onClick = [this]
            {
                if (auto* dw = dynamic_cast<juce::DocumentWindow*> (getTopLevelComponent()))
                    dw->closeButtonPressed();
            };
            refresh();
        }

        std::function<void()> onChanged;

        void refresh()
        {
            // Menu pedali coi nomi attuali.
            const int sel = pedalBox.getSelectedItemIndex();
            pedalBox.clear (juce::dontSendNotification);
            for (int i = 0; i < NUM_PEDALS; ++i)
                pedalBox.addItem (song.amperoSwitches[(size_t) i].name, i + 1);
            pedalBox.setSelectedItemIndex (juce::jlimit (0, NUM_PEDALS - 1, sel),
                                           juce::dontSendNotification);

            rows.clear();
            for (int i = 0; i < (int) song.pedalEvents.size(); ++i)
            {
                auto& e = song.pedalEvents[(size_t) i];
                auto row = std::make_unique<Row>();
                row->text = formatTime (e.timeSec) + "   "
                            + song.amperoSwitches[(size_t) e.pedal].name.toUpperCase()
                            + (e.on ? "   ACCESO" : "   SPENTO");
                row->onDelete = [this, i]
                {
                    song.pedalEvents.erase (song.pedalEvents.begin() + i);
                    refresh();
                    if (onChanged) onChanged();
                };
                listContent.addAndMakeVisible (*row);
                rows.push_back (std::move (row));
            }
            listLabel.setText (rows.empty() ? "Nessun evento: arm REC EVENTI e clicca i pedali in play, oppure aggiungi qui sotto"
                                             : juce::String ((int) rows.size()) + " eventi",
                               juce::dontSendNotification);
            resized();
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (theme::background);
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced (8);
            listLabel.setBounds (area.removeFromTop (20));

            auto addRow = area.removeFromTop (30);
            addRowLabel.setBounds (addRow.removeFromLeft (70));
            timeEditor.setBounds (addRow.removeFromLeft (80).reduced (2));
            addRow.removeFromLeft (6);
            pedalBox.setBounds (addRow.removeFromLeft (150).reduced (2));
            addRow.removeFromLeft (6);
            onOffToggle.setBounds (addRow.removeFromLeft (90).reduced (2));
            addRow.removeFromLeft (6);
            addButton.setBounds (addRow.removeFromLeft (90).reduced (2));

            area.removeFromTop (6);
            listViewport.setBounds (area.removeFromTop (juce::jmax (100, area.getHeight() - 38)));
            auto bottom = area.removeFromBottom (34);
            clearAllButton.setBounds (bottom.removeFromRight (140).reduced (2));
            closeButton.setBounds (bottom.removeFromRight (100).reduced (2));
            listContent.setSize (juce::jmax (100, listViewport.getMaximumVisibleWidth()),
                                 (int) rows.size() * 30 + 4);
            auto r = listContent.getLocalBounds().reduced (2);
            for (auto& row : rows)
                row->setBounds (r.removeFromTop (30).reduced (0, 2));
        }

    private:
        // "mm:ss.s" oppure secondi puri ("90.5") -> secondi. -1 = non valido.
        static double parseTime (const juce::String& txt)
        {
            auto t = txt.trim();
            if (t.isEmpty()) return -1.0;
            if (t.contains (":"))
            {
                auto m = t.upToFirstOccurrenceOf (":", false, false).getDoubleValue();
                auto s = t.fromFirstOccurrenceOf (":", false, false).getDoubleValue();
                if (s >= 60.0) return -1.0;
                return m * 60.0 + s;
            }
            return t.getDoubleValue();
        }

        void addEvent()
        {
            const double t = parseTime (timeEditor.getText());
            if (t < 0.0)
            {
                juce::AlertWindow::showMessageBoxAsync (
                    juce::AlertWindow::WarningIcon, "Tempo non valido",
                    "Inserisci il tempo come mm:ss.s (es. 1:32.5) o secondi (es. 90.5).");
                return;
            }
            PedalEvent e;
            e.timeSec = t;
            e.pedal = juce::jlimit (0, NUM_PEDALS - 1, pedalBox.getSelectedItemIndex());
            e.on = onOffToggle.getToggleState();
            auto& evs = song.pedalEvents;
            evs.insert (std::upper_bound (evs.begin(), evs.end(), e,
                                          [] (const PedalEvent& a, const PedalEvent& b)
                                          { return a.timeSec < b.timeSec; }),
                        e);
            timeEditor.clear();
            refresh();
            if (onChanged) onChanged();
        }

        static juce::String formatTime (double sec)
        {
            const int m = (int) (sec / 60.0);
            const double s = sec - m * 60.0;
            return juce::String::formatted ("%02d:%04.1f", m, s);
        }

        class Row : public juce::Component
        {
        public:
            Row()
            {
                del.setButtonText ("Elimina");
                del.setColour (juce::TextButton::buttonColourId, theme::surfaceDark);
                del.setColour (juce::TextButton::textColourOffId, theme::warning);
                del.onClick = [this] { if (onDelete) onDelete(); };
                addAndMakeVisible (del);
            }
            juce::String text;
            std::function<void()> onDelete;
            void paint (juce::Graphics& g) override
            {
                g.setColour (theme::screen);
                g.fillRect (getLocalBounds().reduced (1));
                g.setColour (theme::screenText);
                g.setFont (juce::FontOptions().withHeight (15.0f));
                g.drawText (text, getLocalBounds().reduced (8, 0),
                            juce::Justification::centredLeft);
            }
            void resized() override
            {
                del.setBounds (getLocalBounds().removeFromRight (90).reduced (2));
            }
        private:
            juce::TextButton del;
        };

        Song& song;
        juce::Label listLabel, addRowLabel { "add", "Aggiungi:" };
        juce::TextEditor timeEditor;
        juce::ComboBox pedalBox;
        juce::ToggleButton onOffToggle;
        juce::TextButton addButton;
        juce::Viewport listViewport;
        juce::Component listContent;
        std::vector<std::unique_ptr<Row>> rows;
        juce::TextButton clearAllButton { "Elimina tutti" }, closeButton { "Chiudi" };

        JUCE_DECLARE_NON_COPYABLE (EventsEditorContent)
    };

    //==============================================================================
    double currentPosSec() const
    {
        return engine.getPositionSamples() / juce::jmax (1.0, engine.getDeviceSampleRate());
    }

    void timerCallback() override
    {
        // Invio CC differito: dopo un PC i dispositivi impiegano qualche 10 ms
        // a caricare il preset; i CC inviati subito verrebbero persi.
        if (pendingCcTicks > 0 && --pendingCcTicks == 0)
        {
            for (int i = 0; i < NUM_PEDALS; ++i)
                engine.sendControlChange (channel, working.amperoSwitches[(size_t) i].cc,
                                          working.amperoSwitches[(size_t) i].on ? 127 : 0);
        }

        // STOP riporta la testina a zero: anche gli eventi devono tornare
        // pronti per una nuova esecuzione. In PAUSA i flag restano invariati.
        const double posSec = currentPosSec();
        if (! engine.isPlaying() && hasSong && engine.isStopped() && posSec <= 0.001)
        {
            std::fill (firedEvents.begin(), firedEvents.end(), false);
        }
        // Esecuzione eventi a tempo durante la riproduzione.
        else if (engine.isPlaying() && hasSong && ! working.pedalEvents.empty())
        {
            // Salto di posizione (seek / nuovo giro): riapplica lo stato
            // corretto per la nuova posizione invece di eseguire a raffica.
            if (posSec + 0.35 < lastPosSec || posSec > lastPosSec + 1.0)
            {
                firedEvents.assign (working.pedalEvents.size(), false);
                applyEventsUpTo (posSec);
            }
            else
            {
                for (size_t i = 0; i < working.pedalEvents.size(); ++i)
                    if (! firedEvents[i] && working.pedalEvents[(size_t) i].timeSec <= posSec)
                    {
                        firedEvents[i] = true;
                        sendPedalState (working.pedalEvents[(size_t) i].pedal,
                                        working.pedalEvents[(size_t) i].on);
                    }
            }
        }
        lastPosSec = posSec;

        // Monitor TX/RX (aggiornato solo se il testo cambia, per non ripaintare).
        const auto diag = "TX: " + engine.getLastMidiOutDesc()
                          + "  |  RX: " + engine.getLastMidiInDesc();
        if (diag != lastDiag)
        {
            diagLabel.setText (diag, juce::dontSendNotification);
            lastDiag = diag;
        }

        refreshHint();
    }

    // Esegue (e marca come sparati) tutti gli eventi fino a posSec incluso.
    void applyEventsUpTo (double posSec)
    {
        for (size_t i = 0; i < working.pedalEvents.size(); ++i)
            if (! firedEvents[i] && working.pedalEvents[i].timeSec <= posSec)
            {
                firedEvents[i] = true;
                sendPedalState (working.pedalEvents[i].pedal, working.pedalEvents[i].on);
            }
    }

    void pedalClicked (int idx)
    {
        const bool newState = pedals[(size_t) idx]->getToggleState();

        // REC armato: il click diventa un EVENTO alla posizione della testina,
        // sia in play sia da fermo. Lo stato base del pedale non viene toccato.
        if (recording)
        {
            const double posSec = currentPosSec();
            sendPedalState (idx, newState);
            PedalEvent e { posSec, idx, newState };
            auto& evs = working.pedalEvents;
            evs.insert (std::upper_bound (evs.begin(), evs.end(), e,
                                          [] (const PedalEvent& a, const PedalEvent& b)
                                          { return a.timeSec < b.timeSec; }),
                        e);
            // Ricostruisci i flag: gli eventi gia' passati (incluso quello
            // appena registrato, inviato direttamente sopra) risultano eseguiti.
            firedEvents.assign (evs.size(), false);
            for (size_t k = 0; k < evs.size(); ++k)
                if (evs[k].timeSec <= posSec) firedEvents[k] = true;
            // Il toggle conserva newState: durante REC deve mostrare lo stato
            // appena inviato, non tornare subito allo stato base del brano.
            notifyEdited();
            return;
        }

        working.amperoSwitches[(size_t) idx].on = newState;
        sendPedal (idx);
        notifyEdited();
    }

    void sendCurrentState()
    {
        if (! engine.isMidiOutputOpen()) { refreshHint(); return; }
        if (working.amperoPreset >= 1)
        {
            // Ampero II Stage: 300 patch in 60 banchi. Selezione assoluta =
            // Bank MSB (CC 0) + Program Change, NON il solo PC (che resterebbe
            // nel banco corrente). PRESET = numero patch 1-300 come sul display.
            const int p = working.amperoPreset - 1;
            engine.sendControlChange (channel, 0, p / 128);
            engine.sendProgramChange (channel, p % 128);
            pendingCcTicks = 5;   // CC base dopo ~170 ms (caricamento patch)
        }
        else
        {
            for (int i = 0; i < NUM_PEDALS; ++i)
                engine.sendControlChange (channel, working.amperoSwitches[(size_t) i].cc,
                                          working.amperoSwitches[(size_t) i].on ? 127 : 0);
        }
    }

    void sendPedal (int i)
    {
        sendPedalState (i, working.amperoSwitches[(size_t) i].on);
        refreshHint();
    }

    void sendPedalState (int pedal, bool on)
    {
        if (engine.isMidiOutputOpen())
            engine.sendControlChange (channel, working.amperoSwitches[(size_t) pedal].cc,
                                      on ? 127 : 0);
    }

    void notifyEdited()
    {
        if (onSongEdited) onSongEdited (working);
    }

    void editPedal (int i)
    {
        auto& sw = working.amperoSwitches[(size_t) i];

        juce::AlertWindow w ("Pedale " + juce::String (i + 1),
                             "Nome e numero CC. Mappa fissa Ampero II Stage: FS1-7 = 79-85, slot A1-A6 = 48-53, B1-B6 = 54-59, tuner = 60:",
                             juce::AlertWindow::NoIcon);
        w.addTextEditor ("name", sw.name);
        w.addTextEditor ("cc", juce::String (sw.cc));
        w.addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w.addButton ("Annulla", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        if (w.runModalLoop() == 1)
        {
            auto n = w.getTextEditorContents ("name").trim();
            if (n.isNotEmpty()) sw.name = n;
            sw.cc = juce::jlimit (0, 127, w.getTextEditorContents ("cc").getIntValue());
            pedals[(size_t) i]->repaint();
            notifyEdited();
        }
    }

    void showEventsEditor()
    {
        auto* content = new EventsEditorContent (working);
        content->onChanged = [this]
        {
            firedEvents.assign (working.pedalEvents.size(), false);
            applyEventsUpTo (currentPosSec());
            notifyEdited();
        };

        juce::DialogWindow::LaunchOptions o;
        o.dialogTitle = "Eventi pedaliera - " + working.title;
        o.content.setOwned (content);
        o.dialogBackgroundColour = theme::background;
        o.escapeKeyTriggersCloseButton = true;
        o.useNativeTitleBar = true;
        if (auto* dw = o.launchAsync())
            dw->centreWithSize (560, 520);
    }

    void refreshHint()
    {
        juce::String t;
        if (! engine.isMidiOutputOpen())
            t = "Nessuna uscita MIDI attiva: MENU -> Impostazioni -> scheda MIDI";
        else if (juce::Time::getMillisecondCounter() < flashMs + 2000)
            t = "Inviato: patch " + juce::String (working.amperoPreset) + " + "
                + juce::String (NUM_PEDALS) + " CC (canale " + juce::String (channel) + ")";
        else
            t = "Canale " + juce::String (channel)
                + "  |  REC + click pedale = evento alla testina (anche da fermo)  |  doppio click = rinomina";
        hintLabel.setText (t, juce::dontSendNotification);
        hintLabel.setColour (juce::Label::textColourId,
                             engine.isMidiOutputOpen() ? theme::screenTextDim
                                                       : theme::warning);
    }

    void flashHint()
    {
        flashMs = juce::Time::getMillisecondCounter();
        refreshHint();
    }

    Engine& engine;
    Song working;                 // copia: contiene amperoPreset, amperoSwitches, pedalEvents
    int channel = 1;
    bool hasSong = false;
    int pendingCcTicks = 0;       // countdown tick (30 Hz) per l'invio CC post-PC
    bool recording = false;
    double lastPosSec = 0.0;
    juce::uint32 flashMs = 0;

    juce::Label titleLabel { "PEDALIERA MIDI" }, pcLabel { "PRESET", "PRESET" }, hintLabel,
                diagLabel;
    juce::String lastDiag;
    juce::Slider pcSlider;
    juce::TextButton sendButton { "INVIA ORA" }, recButton { "REC EVENTI" },
                     eventsButton { "EVENTI..." }, testButton { "TEST" };
    std::vector<std::unique_ptr<PedalButton>> pedals;
    std::vector<bool> firedEvents;

    JUCE_DECLARE_NON_COPYABLE (AmperoStrip)
};
}
