#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../AppData.h"

namespace ldd
{
// Schermata fullscreen per il palco. Pulsanti molto grandi, titolo brano leggibile
// a distanza, brano successivo, tempo trascorso/rimanente, stato armato.
// SPAZIO play/pause, frecce navigazione, ESC per uscire.
class PerformanceMode : public juce::Component,
                        private juce::Timer
{
public:
    PerformanceMode()
    {
        setWantsKeyboardFocus (true);
        auto setupBig = [this] (juce::TextButton& b, const juce::String& text, juce::Colour c)
        {
            b.setButtonText (text);
            b.setColour (juce::TextButton::buttonColourId, c);
            b.setColour (juce::TextButton::textColourOffId, juce::Colours::black);
            addAndMakeVisible (b);
        };
        setupBig (playButton, "PLAY", theme::primary);
        setupBig (nextButton, "SUCCESSIVO", theme::surfaceLight);
        setupBig (prevButton, "PRECEDENTE", theme::surfaceLight);
        setupBig (panicButton, "PANIC", theme::danger);
        panicButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
        setupBig (stopButton, "STOP", theme::surfaceLight);
        stopButton.setColour (juce::TextButton::textColourOffId, theme::warning);
        exitButton.setButtonText ("Esci (ESC)");
        exitButton.setColour (juce::TextButton::buttonColourId, theme::surfaceDark);
        exitButton.setColour (juce::TextButton::textColourOffId, theme::textOnPanel);
        addAndMakeVisible (exitButton);

        addAndMakeVisible (songTitle);
        songTitle.setJustificationType (juce::Justification::centred);
        songTitle.setColour (juce::Label::textColourId, theme::text);
        addAndMakeVisible (nextSong);
        nextSong.setJustificationType (juce::Justification::centred);
        nextSong.setColour (juce::Label::textColourId, theme::textDim);
        addAndMakeVisible (timeLabel);
        timeLabel.setJustificationType (juce::Justification::centred);
        timeLabel.setColour (juce::Label::textColourId, theme::primary);
        addAndMakeVisible (stateLabel);
        stateLabel.setJustificationType (juce::Justification::centred);
        stateLabel.setColour (juce::Label::textColourId, theme::warning);

        startTimerHz (15);
    }

    std::function<void (TransportCommand)> onCommand;
    std::function<void()> onExit;

    void update (const juce::String& currentSong, const juce::String& nextSongName,
                 bool playing, bool armed, double elapsedSec, double remainingSec,
                 int64_t positionSamples, int64_t lengthSamples)
    {
        songTitle.setText (currentSong.isEmpty() ? "NESSUN BRANO" : currentSong,
                           juce::dontSendNotification);
        nextSong.setText (nextSongName.isEmpty() ? juce::String()
                                                 : "Successivo: " + nextSongName,
                          juce::dontSendNotification);
        auto fmt = [] (double s)
        {
            auto m = (int) (s / 60.0); auto sec = (int) std::abs (s) % 60;
            return juce::String::formatted ("%02d:%02d", m, sec);
        };
        timeLabel.setText (fmt (elapsedSec) + "  /  -" + fmt (remainingSec),
                           juce::dontSendNotification);
        stateLabel.setText (playing ? "IN RIPRODUZIONE"
                                   : (armed ? "ARMATO - premi SPAZIO" : "Fermo"),
                            juce::dontSendNotification);
        playButton.setButtonText (playing ? "PAUSA" : "PLAY");

        playhead = lengthSamples > 0 ? (double) positionSamples / lengthSamples : 0.0;
        repaint (progressArea);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::background);
        // barra di avanzamento incassata, stile schermo
        g.setColour (theme::surfaceDark);
        g.fillRect (progressArea.expanded (2));
        g.setColour (theme::screen);
        g.fillRect (progressArea);
        g.setColour (theme::primary);
        g.fillRect (progressArea.withWidth ((float) progressArea.getWidth() * (float) playhead));
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (24);

        auto top = area.removeFromTop (50);
        exitButton.setBounds (top.removeFromRight (140));

        songTitle.setFont (juce::Font (juce::jmin (area.getWidth() / 14.0f, 84.0f), juce::Font::bold));
        songTitle.setBounds (area.removeFromTop (110));
        nextSong.setFont (juce::Font (26.0f));
        nextSong.setBounds (area.removeFromTop (40));
        timeLabel.setFont (juce::Font (juce::FontOptions().withHeight (44.0f).withStyle ("Bold")));
        timeLabel.setBounds (area.removeFromTop (60));
        stateLabel.setFont (juce::Font (30.0f));
        stateLabel.setBounds (area.removeFromTop (44));

        progressArea = area.removeFromTop (16).reduced (40, 0);
        area.removeFromTop (20);

        auto row = area.removeFromBottom (juce::jmax (120, area.getHeight() / 3));
        auto panicW = row.removeFromRight (row.getWidth() / 4);
        panicButton.setBounds (panicW.reduced (6));
        auto stopW = row.removeFromRight (row.getWidth() / 3);
        stopButton.setBounds (stopW.reduced (6));
        prevButton.setBounds (row.removeFromLeft (row.getWidth() / 3).reduced (6));
        nextButton.setBounds (row.removeFromLeft (row.getWidth() / 2).reduced (6));
        playButton.setBounds (row.reduced (6));
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey)
        {
            if (onExit) onExit();
            return true;
        }
        if (key == juce::KeyPress::spaceKey)
        {
            trigger (TransportCommand::playPause);
            return true;
        }
        if (key == juce::KeyPress::rightKey)
        {
            trigger (TransportCommand::next);
            return true;
        }
        if (key == juce::KeyPress::leftKey)
        {
            trigger (TransportCommand::prev);
            return true;
        }
        return false;
    }

private:
    void trigger (TransportCommand c)
    {
        if (onCommand) onCommand (c);
    }

    void timerCallback() override {}

    juce::TextButton playButton, stopButton, nextButton, prevButton, panicButton, exitButton;
    juce::Label songTitle, nextSong, timeLabel, stateLabel;
    juce::Rectangle<int> progressArea;
    double playhead = 0.0;
};
}
