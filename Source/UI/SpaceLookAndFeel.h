#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <BinaryData.h>
#include "../AppData.h"

namespace ldd
{
/*
 * Look and feel "Space: 1999" — console Moonbase Alpha.
 * Font CRT monospace (VT323 incorporato), pannelli tortora con bevel e rivetti,
 * schermi incassati a fosforo, tasti squadrati stile slat anni '70.
 */
class SpaceLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SpaceLookAndFeel()
    {
        auto typeface = juce::Typeface::createSystemTypefaceFor (
            BinaryData::VT323Regular_ttf, (size_t) BinaryData::VT323Regular_ttfSize);
        setDefaultSansSerifTypeface (typeface);

        setColour (juce::PopupMenu::backgroundColourId, theme::screen);
        setColour (juce::PopupMenu::textColourId, theme::screenText);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, theme::primary);
        setColour (juce::PopupMenu::highlightedTextColourId, theme::screen);

        setColour (juce::ComboBox::backgroundColourId, theme::surfaceDark);
        setColour (juce::ComboBox::textColourId, theme::text);
        setColour (juce::ComboBox::arrowColourId, theme::textOnPanel);
        setColour (juce::ComboBox::outlineColourId, theme::surfaceDark);

        setColour (juce::ListBox::backgroundColourId, theme::screen);
        setColour (juce::ListBox::textColourId, theme::screenText);
        setColour (juce::ListBox::outlineColourId, theme::surfaceDark.brighter (0.1f));

        setColour (juce::Label::textColourId, theme::text);

        setColour (juce::ScrollBar::thumbColourId, theme::surface);

        setColour (juce::AlertWindow::backgroundColourId, theme::surface);
        setColour (juce::AlertWindow::textColourId, theme::textOnPanel);
        setColour (juce::AlertWindow::outlineColourId, theme::surfaceDark);

        setColour (juce::ProgressBar::backgroundColourId, theme::screen);
        setColour (juce::ProgressBar::foregroundColourId, theme::primary);

        setColour (juce::TextEditor::backgroundColourId, theme::screen);
        setColour (juce::TextEditor::textColourId, theme::screenText);
        setColour (juce::TextEditor::highlightColourId, theme::primary.withAlpha (0.4f));
        setColour (juce::TextEditor::focusedOutlineColourId, theme::primary);
        setColour (juce::TextEditor::outlineColourId, theme::surfaceDark);
        setColour (juce::CaretComponent::caretColourId, theme::primary);

        setColour (juce::ToggleButton::textColourId, theme::text);
        setColour (juce::ToggleButton::tickColourId, theme::screen);
        setColour (juce::ToggleButton::tickDisabledColourId, theme::textDim);
    }

    //==============================================================================
    // Pannello console con bevel e 4 rivetti. Se recessed=true, incassato (schermo).
    static void drawPanel (juce::Graphics& g, juce::Rectangle<int> bounds,
                           bool recessed = false)
    {
        auto b = bounds.toFloat().reduced (0.5f);
        if (recessed)
        {
            g.setColour (theme::screen);
            g.fillRect (b);
            g.setColour (theme::background.brighter (0.15f));
            g.drawRect (b, 1.0f);
            g.setColour (theme::surfaceLight.withAlpha (0.25f));
            g.drawLine (b.getX() + 1.0f, b.getBottom() - 1.0f, b.getRight() - 1.0f,
                        b.getBottom() - 1.0f, 1.0f);
            return;
        }

        g.setColour (theme::surface);
        g.fillRect (b);
        // bevel: luce sopra/sinistra, ombra sotto/destra
        g.setColour (theme::surfaceLight.withAlpha (0.85f));
        g.drawLine (b.getX(), b.getY(), b.getRight(), b.getY(), 1.0f);
        g.drawLine (b.getX(), b.getY(), b.getX(), b.getBottom(), 1.0f);
        g.setColour (theme::background.withAlpha (0.85f));
        g.drawLine (b.getX(), b.getBottom(), b.getRight(), b.getBottom(), 1.0f);
        g.drawLine (b.getRight(), b.getY(), b.getRight(), b.getBottom(), 1.0f);

        // rivetti agli angoli
        g.setColour (theme::surfaceDark);
        g.setColour (theme::surfaceLight.brighter (0.25f));
        for (auto corner : { b.getTopLeft(), b.getTopRight(), b.getBottomLeft(), b.getBottomRight() })
        {
            juce::Point<float> c = corner;
            c += (corner == b.getTopLeft()) ? juce::Point<float> (4.0f, 4.0f)
                  : (corner == b.getTopRight()) ? juce::Point<float> (-4.0f, 4.0f)
                  : (corner == b.getBottomLeft()) ? juce::Point<float> (4.0f, -4.0f)
                                                  : juce::Point<float> (-4.0f, -4.0f);
            g.fillEllipse (c.x - 1.6f, c.y - 1.6f, 3.2f, 3.2f);
            g.setColour (theme::surfaceDark.brighter (0.2f));
            g.drawEllipse (c.x - 1.6f, c.y - 1.6f, 3.2f, 3.2f, 0.7f);
            g.setColour (theme::surfaceLight.brighter (0.25f));
        }
    }

    //==============================================================================
    // Tasti squadrati con bevel meccanico.
    void drawButtonBackground (juce::Graphics& g, juce::Button& button,
                               const juce::Colour& backgroundColour,
                               bool highlighted, bool down) override
    {
        auto b = button.getLocalBounds().toFloat().reduced (0.5f);
        auto base = backgroundColour;
        if (down)             base = base.darker (0.25f);
        else if (highlighted) base = base.brighter (0.12f);

        g.setColour (base);
        g.fillRect (b);

        // bevel meccanico
        juce::Colour light = base.brighter (0.45f), dark = base.darker (0.45f);
        if (down) std::swap (light, dark);
        g.setColour (light);
        g.drawLine (b.getX(), b.getY(), b.getRight(), b.getY(), 1.5f);
        g.drawLine (b.getX(), b.getY(), b.getX(), b.getBottom(), 1.5f);
        g.setColour (dark);
        g.drawLine (b.getX(), b.getBottom(), b.getRight(), b.getBottom(), 1.5f);
        g.drawLine (b.getRight(), b.getY(), b.getRight(), b.getBottom(), 1.5f);

        if (button.getToggleState() && button.getClickingTogglesState())
        {
            g.setColour (theme::primary);
            g.drawRect (b.reduced (2.5f), 1.2f);
        }
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& button,
                         bool highlighted, bool down) override
    {
        auto colour = button.findColour (button.getToggleState()
                                            ? juce::TextButton::textColourOnId
                                            : juce::TextButton::textColourOffId)
                          .withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.45f);
        g.setColour (colour);
        g.setFont (getTextButtonFont (button, button.getHeight()));
        g.drawText (button.getButtonText(), button.getLocalBounds(),
                    juce::Justification::centred);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override
    {
        return juce::FontOptions().withHeight (juce::jlimit (13.0f, 20.0f, buttonHeight * 0.42f));
    }

    // Checkbox esplicita: box scuro incassato + spunta a fosforo ben visibile.
    // (Il rendering standard con questi colori rendeva la spunta illeggibile.)
    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                           bool highlighted, bool down) override
    {
        auto b = button.getLocalBounds();
        const int box = 18;
        auto boxArea = b.removeFromLeft (box + 8)
                           .withSizeKeepingCentre (box, box).toFloat();

        g.setColour (theme::screen);
        g.fillRect (boxArea);
        g.setColour (highlighted || down ? theme::primary : theme::textDim);
        g.drawRect (boxArea, 1.2f);

        if (button.getToggleState())
        {
            g.setColour (theme::primary);
            juce::Path tick;
            tick.startNewSubPath (boxArea.getX() + 4.0f, boxArea.getCentreY());
            tick.lineTo (boxArea.getCentreX() - 1.0f, boxArea.getBottom() - 4.0f);
            tick.lineTo (boxArea.getRight() - 3.0f, boxArea.getY() + 3.0f);
            g.strokePath (tick, juce::PathStrokeType (2.6f));
        }

        g.setColour (button.findColour (juce::ToggleButton::textColourId));
        g.setFont (juce::FontOptions().withHeight (15.0f));
        g.drawText (button.getButtonText(), b.reduced (2, 0),
                    juce::Justification::centredLeft);
    }

    // Slider a slat: traccia incassata + cursore squadrato colorato.
    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float minPos, float maxPos,
                           juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        if (style == juce::Slider::LinearVertical || style == juce::Slider::LinearHorizontal)
        {
            auto trackW = juce::jmin (8.0f, (float) width * 0.28f);
            juce::Point<float> start ((float) x + (float) width * 0.5f, (float) (y + height) - 3.0f);
            juce::Point<float> end (start.x, (float) y + 3.0f);

            juce::Path track;
            track.startNewSubPath (start);
            track.lineTo (end);
            g.setColour (theme::screen);
            g.strokePath (track, juce::PathStrokeType (trackW));
            g.setColour (theme::background.brighter (0.2f));
            g.strokePath (track, juce::PathStrokeType (1.0f));

            juce::Path filled;
            filled.startNewSubPath (start);
            filled.lineTo (start.x, sliderPos);
            g.setColour (slider.findColour (juce::Slider::trackColourId));
            g.strokePath (filled, juce::PathStrokeType (trackW - 2.0f));

            auto thumbW = juce::jmin (30.0f, (float) width * 0.66f);
            auto thumb = juce::Rectangle<float> (thumbW, 16.0f)
                             .withCentre ({ start.x, sliderPos});
            g.setColour (slider.findColour (juce::Slider::thumbColourId));
            g.fillRect (thumb);
            g.setColour (theme::textOnPanel);
            g.drawRect (thumb, 1.0f);
            return;
        }
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minPos, maxPos,
                                          style, slider);
    }

    JUCE_DECLARE_NON_COPYABLE (SpaceLookAndFeel)
};
}
