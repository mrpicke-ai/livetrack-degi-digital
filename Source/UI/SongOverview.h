#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "../AppData.h"

namespace ldd
{
/*
 * Timeline generale del brano: waveform del mix (tutte le tracce sommate),
 * righello con i secondi, marker/cue, regione di loop A/B e testina.
 * Clic o trascinamento = sposta la testina (seek). Schermo fosforo incassato.
 */
class SongOverview : public juce::Component
{
public:
    SongOverview() = default;

    // Picchi (min,max) per colonna, precomputati dalla GUI direttamente dai
    // buffer delle tracce (nessun mixdown intermedio). lengthSec = durata.
    void setPeaks (std::vector<std::pair<float, float>> p, double lengthSec)
    {
        peaks = std::move (p);
        mixdownLengthSec = lengthSec;
        repaint();
    }

    void clear()
    {
        peaks.clear();
        mixdownLengthSec = 0.0;
        markers.clear();
        playhead = 0.0;
        repaint();
    }

    void setPlayheadFraction (double f)
    {
        playhead = juce::jlimit (0.0, 1.0, f);
        repaint();
    }

    void setLoopRegion (double startF, double endF, bool enabled)
    {
        loopStartF = startF; loopEndF = endF; loopEnabled = enabled;
        repaint();
    }

    void setMarkers (const std::vector<std::pair<double, juce::String>>& m, double lengthSec)
    {
        markers = m; markersLengthSec = lengthSec;
        repaint();
    }

    std::function<void (double fraction)> onSeek;

    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds();
        auto ruler = area.removeFromTop (rulerHeight);
        drawRuler (g, ruler);

        // schermo incassato
        auto w = area.reduced (2).toFloat();
        g.setColour (theme::background.brighter (0.2f));
        g.fillRect (w.expanded (1.5f));
        g.setColour (theme::screen);
        g.fillRect (w);
        auto wi = w.toNearestInt();

        // loop
        if (loopEnabled && loopEndF > loopStartF)
        {
            int x0 = wi.getX() + (int) (loopStartF * wi.getWidth());
            int x1 = wi.getX() + (int) (loopEndF * wi.getWidth());
            g.setColour (theme::primary.withAlpha (0.14f));
            g.fillRect (x0, wi.getY(), x1 - x0, wi.getHeight());
            g.setColour (theme::primary.withAlpha (0.8f));
            g.drawVerticalLine (x0, (float) wi.getY(), (float) wi.getBottom());
            g.drawVerticalLine (x1, (float) wi.getY(), (float) wi.getBottom());
        }

        // waveform mix (picchi precomputati, mappati sulla larghezza)
        if (! peaks.empty())
        {
            float midY = w.getCentreY();
            float amp = w.getHeight() * 0.5f * 0.92f;
            const float xScale = (float) wi.getWidth() / (float) peaks.size();
            g.setColour (theme::screenText.withAlpha (0.9f));
            for (int i = 0; i < (int) peaks.size(); ++i)
            {
                float x = (float) wi.getX() + (float) i * xScale;
                float y0 = midY - peaks[(size_t) i].second * amp;
                float y1 = midY - peaks[(size_t) i].first * amp;
                g.drawVerticalLine ((int) x, y0, juce::jmax (y0 + 1.0f, y1));
            }
        }
        else
        {
            g.setColour (theme::screenTextDim);
            g.setFont (juce::FontOptions().withHeight (16.0f));
            g.drawText ("- NESSUN BRANO CARICATO -", wi, juce::Justification::centred);
        }

        // marker / cue
        if (markersLengthSec > 0.0)
        {
            g.setFont (juce::FontOptions().withHeight (14.0f));
            for (auto& mk : markers)
            {
                int x = wi.getX() + (int) (mk.first / markersLengthSec * wi.getWidth());
                g.setColour (theme::warning);
                g.drawVerticalLine (x, (float) wi.getY(), (float) wi.getBottom());
                g.setColour (theme::screenText);
                g.drawText (mk.second, x + 4, wi.getY() + 2, 130, 16, juce::Justification::left);
            }
        }

        // testina
        int x = wi.getX() + (int) (playhead * wi.getWidth());
        g.setColour (juce::Colours::white);
        g.drawVerticalLine (x, (float) wi.getY(), (float) wi.getBottom());
        juce::Path arrow;
        arrow.addTriangle ((float) x - 6.0f, (float) wi.getY() - 2.0f,
                           (float) x + 6.0f, (float) wi.getY() - 2.0f,
                           (float) x, (float) wi.getY() + 8.0f);
        g.fillPath (arrow);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        seekAt (e);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        seekAt (e);
    }

    void resized() override
    {
        // I picchi sono a risoluzione fissa: il paint li mappa sulla larghezza.
    }

private:
    void seekAt (const juce::MouseEvent& e)
    {
        if (onSeek && getWidth() > 0)
            onSeek (juce::jlimit (0.0, 1.0, (double) e.x / (double) getWidth()));
    }

    void drawRuler (juce::Graphics& g, juce::Rectangle<int> area)
    {
        g.setColour (theme::screen);
        g.fillRect (area);
        g.setColour (theme::screenTextDim);
        g.drawLine ((float) area.getX(), (float) area.getBottom() - 0.5f,
                    (float) area.getRight(), (float) area.getBottom() - 0.5f, 1.0f);

        if (markersLengthSec <= 0.0 || peaks.empty()) return;

        // passo "pulito" in secondi
        static const double steps[] = { 1, 2, 5, 10, 15, 30, 60, 120, 300 };
        double step = 60;
        for (double s : steps)
            if (markersLengthSec / s <= 12.0) { step = s; break; }

        g.setFont (juce::FontOptions().withHeight (15.0f));
        for (double t = 0.0; t <= markersLengthSec + 0.001; t += step)
        {
            int x = area.getX() + (int) (t / markersLengthSec * area.getWidth());
            g.setColour (theme::screenTextDim);
            g.drawVerticalLine (x, (float) area.getBottom() - 7.0f, (float) area.getBottom() - 1.0f);
            g.setColour (theme::screenText);
            int mm = (int) (t / 60.0), ss = ((int) t) % 60;
            g.drawText (juce::String::formatted ("%02d:%02d", mm, ss),
                        x + 3, area.getY(), 60, area.getHeight() - 6,
                        juce::Justification::left);
        }
    }

    static constexpr int rulerHeight = 22;
    double mixdownLengthSec = 0.0;
    std::vector<std::pair<float, float>> peaks;   // (min, max) per colonna
    std::vector<std::pair<double, juce::String>> markers;
    double markersLengthSec = 0.0;
    double playhead = 0.0, loopStartF = 0.0, loopEndF = 0.0;
    bool loopEnabled = false;

    JUCE_DECLARE_NON_COPYABLE (SongOverview)
};
}
