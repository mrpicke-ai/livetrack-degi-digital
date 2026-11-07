#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../AppData.h"
#include "SpaceLookAndFeel.h"

namespace ldd
{
// Logo vettoriale DEGI DIGITAL disegnato in codice (nessuna immagine bitmap).
// Una "D" stilizzata con onda sinusoidale al centro. Proporzione fissa.
class LogoComponent : public juce::Component
{
public:
    LogoComponent() = default;

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        auto side = juce::jmin (bounds.getWidth(), bounds.getHeight());
        auto r = bounds.withSizeKeepingCentre (side, side).reduced (side * 0.02f);

        // Tile pannello console Moonbase: tortora, squadrato, bevel + rivetti
        SpaceLookAndFeel::drawPanel (g, r.toNearestInt());

        // Finestra schermo incassata al centro del pannello
        auto scr = r.reduced (side * 0.13f);
        g.setColour (theme::screen);
        g.fillRect (scr);
        g.setColour (theme::background.brighter (0.2f));
        g.drawRect (scr, 1.0f);

        // Barra verticale della D (scura sullo schermo fosforo)
        auto barW = side * 0.07f;
        auto bar = juce::Rectangle<float> (scr.getX() + scr.getWidth() * 0.22f,
                                         scr.getY() + scr.getHeight() * 0.16f,
                                         barW, scr.getHeight() * 0.68f);
        g.setColour (theme::screenTextDim);
        g.fillRect (bar);

        // Arco della D
        auto arcBounds = juce::Rectangle<float> (bar.getRight() - barW * 0.6f,
                                               scr.getY() + scr.getHeight() * 0.10f,
                                               scr.getWidth() * 0.56f, scr.getHeight() * 0.80f);
        juce::Path arc;
        arc.addArc (arcBounds.getX(), arcBounds.getY(), arcBounds.getWidth(), arcBounds.getHeight(),
                    juce::degreesToRadians (-70.0f), juce::degreesToRadians (70.0f), true);
        g.setColour (theme::screenTextDim);
        g.strokePath (arc, juce::PathStrokeType (barW * 0.95f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));

        // Onda sinusoidale fosforescente al centro
        juce::Path wave;
        const float cx = scr.getCentreX() + scr.getWidth() * 0.06f;
        const float cy = scr.getCentreY();
        const float waveW = scr.getWidth() * 0.46f, amp = scr.getHeight() * 0.20f;
        const int segs = 48;
        for (int i = 0; i <= segs; ++i)
        {
            const float t = (float) i / (float) segs;
            const float x = cx - waveW * 0.5f + waveW * t;
            const float envelope = std::sin (t * juce::MathConstants<float>::pi);
            const float y = cy + std::sin (t * juce::MathConstants<float>::twoPi * 3.5f) * amp * envelope;
            if (i == 0) wave.startNewSubPath (x, y);
            else wave.lineTo (x, y);
        }
        g.setColour (theme::screenText);
        g.strokePath (wave, juce::PathStrokeType (juce::jmax (1.2f, side * 0.014f),
                                                  juce::PathStrokeType::curved));

        // Testo sotto il tile, se c'è spazio verticale
        if (bounds.getHeight() > side * 1.35f)
        {
            auto textArea = bounds.withTrimmedTop (side * 1.05f);
            g.setColour (theme::textOnPanel);
            g.setFont (juce::FontOptions().withHeight (side * 0.15f).withStyle ("Bold"));
            g.drawFittedText ("DEGI DIGITAL", textArea.toNearestInt(),
                              juce::Justification::centredTop, 1);
        }
    }

    JUCE_DECLARE_NON_COPYABLE (LogoComponent)
};
}
