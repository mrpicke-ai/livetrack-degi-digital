#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "../AppData.h"
#include "SpaceLookAndFeel.h"

namespace ldd
{
/*
 * Canale della console live (stile Space 1999):
 * nome (dal file importato, rinominabile col doppio click), nome file piccolo,
 * meter incassato, fader, valore gain, patchbay a jack per il routing fisico
 * (click sul jack = selezione; i jack accesi = canali su cui esce la traccia),
 * M/S e mini-waveform del file in basso.
 */
class ConsoleChannel : public juce::Component,
                       private juce::Slider::Listener,
                       private juce::ChangeListener
{
public:
    ConsoleChannel (int trackIndex, juce::StringArray outputNames,
                    juce::AudioFormatManager& fm, juce::AudioThumbnailCache& cache)
        : index (trackIndex), availableOutputs (std::move (outputNames)),
          formatManager (fm), thumbnail (std::make_unique<juce::AudioThumbnail> (512, fm, cache))
    {
        thumbnail->addChangeListener (this);   // ridisegna quando il caricamento finisce
        auto colour = getTrackColour (trackIndex);

        nameLabel.setText (getDefaultTrackNames()[trackIndex], juce::dontSendNotification);
        nameLabel.setJustificationType (juce::Justification::centred);
        nameLabel.setColour (juce::Label::textColourId, theme::screenText);
        nameLabel.setColour (juce::Label::backgroundColourId, theme::screen);
        nameLabel.setColour (juce::Label::backgroundWhenEditingColourId, theme::screen);
        nameLabel.setColour (juce::Label::textWhenEditingColourId, theme::screenText);
        nameLabel.setColour (juce::Label::outlineColourId, theme::surfaceDark);
        nameLabel.setFont (juce::FontOptions().withHeight (20.0f).withStyle ("Bold"));
        nameLabel.setEditable (true, true, false);
        nameLabel.onTextChange = [this]
        {
            if (onNameChanged) onNameChanged (nameLabel.getText (false));
        };
        addAndMakeVisible (nameLabel);

        fileLabel.setJustificationType (juce::Justification::centred);
        fileLabel.setColour (juce::Label::textColourId, theme::screenTextDim);
        fileLabel.setFont (juce::FontOptions().withHeight (14.0f));
        fileLabel.setText ("- vuota -", juce::dontSendNotification);
        addAndMakeVisible (fileLabel);

        fader.setSliderStyle (juce::Slider::LinearVertical);
        fader.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        fader.setRange (0.0, 2.0, 0.0);
        fader.setValue (1.0, juce::dontSendNotification);
        fader.textFromValueFunction = [] (double v) { return juce::String (v, 2); };
        fader.setColour (juce::Slider::thumbColourId, colour);
        fader.setColour (juce::Slider::trackColourId, colour.withAlpha (0.5f));
        fader.addListener (this);
        addAndMakeVisible (fader);

        gainLabel.setJustificationType (juce::Justification::centred);
        gainLabel.setColour (juce::Label::textColourId, theme::screenText);
        gainLabel.setFont (juce::FontOptions().withHeight (17.0f));
        addAndMakeVisible (gainLabel);

        muteButton.setButtonText ("M");
        soloButton.setButtonText ("S");
        for (auto* b : { &muteButton, &soloButton })
        {
            b->setClickingTogglesState (true);
            b->setColour (juce::TextButton::buttonColourId, theme::surfaceDark);
            b->setColour (juce::TextButton::buttonOnColourId, theme::danger);
            b->setColour (juce::TextButton::textColourOffId, theme::textOnPanel);
            b->setColour (juce::TextButton::textColourOnId, juce::Colours::white);
            addAndMakeVisible (*b);
        }
        muteButton.onClick = [this] { if (onMuteChanged) onMuteChanged (muteButton.getToggleState()); };
        soloButton.onClick = [this] { if (onSoloChanged) onSoloChanged (soloButton.getToggleState()); };

        rebuildOutputOptions();
    }

    ~ConsoleChannel() override
    {
        if (thumbnail) thumbnail->removeChangeListener (this);
    }

    //==============================================================================
    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        repaint (waveArea);   // la thumbnail ha finito di caricare: ridisegna
    }
    void setTrackFile (const juce::String& path)
    {
        if (path.isNotEmpty() && juce::File::isAbsolutePath (path))
        {
            fileLabel.setText (juce::File (path).getFileName(), juce::dontSendNotification);
            thumbnail->setSource (new juce::FileInputSource (juce::File (path)));
        }
        else
        {
            fileLabel.setText ("- vuota -", juce::dontSendNotification);
            thumbnail->setSource (nullptr);
        }
        repaint (waveArea);
    }

    void rebuildOutputOptions()
    {
        // Il patchbay legge direttamente availableOutputs: basta ridisegnare.
        setRouting (currentStart, currentCount);
    }

    void setAvailableOutputs (juce::StringArray names)
    {
        availableOutputs = std::move (names);
        rebuildOutputOptions();
    }

    int getRoutingStart() const { return currentStart; }
    int getRoutingCount() const { return currentCount; }

    void setRouting (int start, int count)
    {
        currentStart = start;
        currentCount = count;
        repaint (patchArea);
    }

    std::function<void (float)> onGainChanged;
    std::function<void (bool)> onMuteChanged, onSoloChanged;
    std::function<void (const juce::String&)> onNameChanged;
    std::function<void (int start, int count)> onRoutingChanged;

    void setLevel (float level, bool clip)
    {
        currentLevel = level;
        clipActive = clip;
        repaint (meterArea);
    }

    void setGainValue (float g, juce::NotificationType ntf = juce::sendNotificationSync)
    {
        fader.setValue (g, ntf);
        // Aggiorna sempre etichetta e waveform: con dontSendNotification lo
        // slider non scatta sliderValueChanged e resterebbero disallineati.
        gainLabel.setText (juce::String (fader.getValue(), 2), juce::dontSendNotification);
        repaint (waveArea);
    }

    void setMuteState (bool m, juce::NotificationType ntf = juce::sendNotificationSync)
    {
        muteButton.setToggleState (m, ntf);
    }

    void setSoloState (bool s, juce::NotificationType ntf = juce::sendNotificationSync)
    {
        soloButton.setToggleState (s, ntf);
    }

    void setName (const juce::String& n)
    {
        if (n.isNotEmpty()) nameLabel.setText (n, juce::dontSendNotification);
    }

    //==============================================================================
    void paint (juce::Graphics& g) override
    {
        SpaceLookAndFeel::drawPanel (g, getLocalBounds());
        g.setColour (getTrackColour (index));
        g.fillRect (getLocalBounds().reduced (2).removeFromTop (4));

        // meter incassato
        auto m = meterArea.toFloat().reduced (0.5f);
        g.setColour (theme::background.brighter (0.2f));
        g.fillRect (m.expanded (1.5f));
        g.setColour (theme::screen);
        g.fillRect (m);
        float h = (float) meterArea.getHeight() * juce::jlimit (0.0f, 1.0f, currentLevel);
        auto mc = clipActive ? theme::danger
                             : (currentLevel > 0.85f ? theme::warning : getTrackColour (index));
        g.setColour (mc.withAlpha (clipActive ? 1.0f : 0.9f));
        g.fillRect (m.getX() + 1.0f, m.getBottom() - h - 1.0f, m.getWidth() - 2.0f, h);

        // scala graduata del fader: tacche numerate 0..2, con il livello 1.0
        // (= 0 dB) evidenziato da una riga continua attraverso il canale.
        if (faderRowArea.getHeight() > 60 && fader.getWidth() > 0)
        {
            auto tr = fader.getBounds().toFloat();
            auto scaleX = (float) meterArea.getRight() + 2.0f;
            auto scaleW = tr.getX() - 2.0f - scaleX;
            const float ticks[] = { 0.0f, 0.5f, 1.0f, 1.5f, 2.0f };
            g.setFont (juce::FontOptions().withHeight (14.0f).withStyle ("Bold"));
            for (float v : ticks)
            {
                const float p = v / 2.0f;
                const float y = tr.getBottom() - p * tr.getHeight();
                const bool unity = (v == 1.0f);
                juce::Colour c = unity ? theme::primary : theme::text;

                if (unity)
                {
                    // riga di riferimento 0 dB attraverso tutto il canale
                    g.setColour (c.withAlpha (0.35f));
                    g.drawLine ((float) meterArea.getX(), y, tr.getRight() + 6.0f, y, 1.2f);
                    // cartellino pieno dietro l'etichetta, per leggerla sempre
                    juce::Rectangle<float> lab (scaleX - 1.0f, y - 8.0f, scaleW + 1.0f, 16.0f);
                    g.setColour (theme::surfaceDark);
                    g.fillRect (lab);
                }
                g.setColour (c.withAlpha (unity ? 1.0f : 0.9f));
                g.drawLine (scaleX + scaleW - 5.0f, y, scaleX + scaleW, y, unity ? 2.0f : 1.2f);
                g.drawText (unity ? "0dB" : juce::String (v, v == 0.0f || v == 2.0f ? 0 : 1),
                            juce::Rectangle<float> (scaleX, y - 7.0f, scaleW - 6.0f, 14.0f),
                            juce::Justification::centredRight, false);
            }
        }

        // patchbay: un jack per ogni canale fisico disponibile
        auto p = patchArea.reduced (2).toFloat();
        const int n = juce::jmax (1, availableOutputs.size());
        jackCentres.clear();
        jackHot.clear();
        const float jackR = juce::jmin (9.0f, p.getWidth() / (2.2f * (float) n));
        const float spacing = p.getWidth() / (float) n;
        for (int c = 0; c < n; ++c)
        {
            juce::Point<float> centre (p.getX() + spacing * ((float) c + 0.5f),
                                       p.getCentreY());
            bool hot = currentStart >= 0 && c >= currentStart
                       && c < currentStart + currentCount;
            jackCentres.push_back (centre);
            jackHot.push_back (hot);

            // morsetto
            g.setColour (theme::surfaceDark);
            g.fillEllipse (centre.x - jackR - 2.0f, centre.y - jackR - 2.0f,
                           (jackR + 2.0f) * 2.0f, (jackR + 2.0f) * 2.0f);
            // foro del jack
            juce::Colour hole = hot ? getTrackColour (index).withAlpha (0.35f)
                                    : theme::background.brighter (0.1f);
            g.setColour (hole);
            g.fillEllipse (centre.x - jackR, centre.y - jackR, jackR * 2.0f, jackR * 2.0f);
            if (hot)
            {
                g.setColour (getTrackColour (index));
                g.fillEllipse (centre.x - jackR * 0.5f, centre.y - jackR * 0.5f,
                               jackR, jackR);
            }
        }

        // etichetta routing sotto i jack
        auto rout = getRoutingDescription();
        g.setColour (currentStart >= 0 ? theme::screenText : theme::screenTextDim);
        g.setFont (juce::FontOptions().withHeight (15.0f));
        g.drawText (rout, patchArea.getX(), patchArea.getBottom() - 14,
                    patchArea.getWidth(), 14, juce::Justification::centred);

        // mini-waveform: segue il fader, con tetto a 0 dB (gain 1.0)
        auto w = waveArea.reduced (2).toFloat();
        g.setColour (theme::background.brighter (0.15f));
        g.fillRect (w.expanded (1.5f));
        g.setColour (theme::screen);
        g.fillRect (w);
        if (thumbnail->getTotalLength() > 0.0)
        {
            g.setColour (getTrackColour (index).withAlpha (0.9f));
            g.reduceClipRegion (w.toNearestInt());
            thumbnail->drawChannels (g, w.toNearestInt(), 0.0, thumbnail->getTotalLength(),
                                     0.9f * juce::jmin (1.0f, (float) fader.getValue()));
        }
        else
        {
            g.setColour (theme::screenTextDim);
            g.setFont (juce::FontOptions().withHeight (14.0f));
            g.drawText ("-", waveArea, juce::Justification::centred);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (6);
        area.removeFromTop (6);   // striscia colore

        nameLabel.setBounds (area.removeFromTop (26));
        fileLabel.setBounds (area.removeFromTop (18));
        area.removeFromTop (4);

        // fader + meter affiancati; ai lati del fader resta il margine per la
        // scala graduata (sinistra: numeri, destra: niente)
        auto fmRow = area.removeFromTop (juce::jmax (90, area.getHeight() - 150));
        faderRowArea = fmRow;
        auto meterW = fmRow.removeFromLeft (16);
        meterArea = meterW.reduced (2, 0).withTrimmedTop (6);
        fader.setBounds (fmRow.reduced (28, 4));

        gainLabel.setBounds (area.removeFromTop (20));
        area.removeFromTop (2);

        // patchbay (jack + etichetta)
        patchArea = area.removeFromTop (34);
        area.removeFromTop (4);

        // M/S
        auto msRow = area.removeFromTop (20);
        muteButton.setBounds (msRow.removeFromLeft (msRow.getWidth() / 2).reduced (1, 0));
        soloButton.setBounds (msRow.reduced (1, 0));
        area.removeFromTop (4);

        waveArea = area;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        // Click su un jack = seleziona quel canale/coppia come routing.
        if (! patchArea.expanded (0, 6).contains (e.getPosition())) return;
        auto pos = e.getPosition().toFloat();
        for (size_t c = 0; c < jackCentres.size(); ++c)
        {
            if (pos.getDistanceFrom (jackCentres[(size_t) c]) < 14.0f)
            {
                int start = (int) c;
                int count = 1;
                // click su un jack già attivo in una coppia/gruppo -> spegni (—)
                if (jackHot[(size_t) c] && currentCount > 1)
                {
                    start = -1; count = 1;
                }
                else
                {
                    // singolo -> coppia se il successivo è libero e disponibile
                    if (start % 2 == 0 && start + 1 < (int) jackCentres.size()
                        && availableOutputs.size() >= start + 2)
                    {
                        // click normale = singolo, doppio giro = coppia:
                        // qui: singolo. La coppia si ottiene cliccando il jack
                        // con il tasto destro o tenendo premuto Shift.
                        if (e.mods.isShiftDown() || e.mods.isRightButtonDown())
                            count = 2;
                    }
                }
                currentStart = start;
                currentCount = count;
                if (onRoutingChanged) onRoutingChanged (currentStart, currentCount);
                repaint (patchArea);
                break;
            }
        }
    }

private:
    void sliderValueChanged (juce::Slider* s) override
    {
        if (s == &fader)
        {
            gainLabel.setText (juce::String (fader.getValue(), 2), juce::dontSendNotification);
            repaint (waveArea);
            if (onGainChanged) onGainChanged ((float) fader.getValue());
        }
    }

    juce::String getRoutingDescription() const
    {
        if (currentStart < 0) return "-";
        if (currentCount == 1) return juce::String (currentStart + 1);
        return juce::String (currentStart + 1) + "-" + juce::String (currentStart + currentCount);
    }

    int index;
    juce::StringArray availableOutputs;
    int currentStart = 0, currentCount = 1;
    juce::AudioFormatManager& formatManager;
    std::unique_ptr<juce::AudioThumbnail> thumbnail;

    juce::Label nameLabel, fileLabel, gainLabel;
    juce::Slider fader;
    juce::TextButton muteButton, soloButton;

    juce::Rectangle<int> meterArea, patchArea, waveArea, faderRowArea;
    std::vector<juce::Point<float>> jackCentres;
    std::vector<bool> jackHot;
    float currentLevel = 0.0f;
    bool clipActive = false;

    JUCE_DECLARE_NON_COPYABLE (ConsoleChannel)
};
}
