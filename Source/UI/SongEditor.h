#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "../Data/Project.h"
#include "../Audio/ClickGenerator.h"

namespace ldd
{
class SongEditorContent;

// Finestra modale per creare/modificare un brano: metadati + assegnazione file per traccia.
class SongEditorWindow : public juce::DocumentWindow
{
public:
    SongEditorWindow (Song& songToEdit, int numTracks, double deviceSampleRate,
                      juce::StringArray trackNames);

    void closeButtonPressed() override
    {
        exitModalState (0);
    }

    JUCE_DECLARE_NON_COPYABLE (SongEditorWindow)
};

class SongEditorContent : public juce::Component
{
public:
    SongEditorContent (Song& songToEdit, int numTracks, double deviceSampleRate,
                       juce::StringArray trackNames);

    std::function<void()> onSave;

private:
    Song& song;
    int numTracks;
    double deviceSampleRate;
    juce::StringArray trackNames;

    juce::Label titleLabel, artistLabel, bpmLabel, noteLabel;
    juce::TextEditor titleEditor, artistEditor, bpmEditor, noteEditor;
    std::vector<std::unique_ptr<juce::Label>> trackLabels;
    std::vector<std::unique_ptr<juce::Label>> fileLabels;
    std::vector<std::unique_ptr<juce::TextButton>> browseButtons;
    std::vector<std::unique_ptr<juce::TextButton>> clickButtons;
    juce::TextButton saveButton { "Salva" }, cancelButton { "Annulla" };

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::background);
        g.setColour (theme::text);
        g.setFont (juce::Font (16.0f, juce::Font::bold));
        g.drawText ("Brano", getLocalBounds().removeFromTop (30).reduced (12, 0),
                    juce::Justification::centredLeft);
    }

    void resized() override;
    void browseForTrack (int trackIndex);
    void generateClickForTrack (int trackIndex);
    void save();

    JUCE_DECLARE_NON_COPYABLE (SongEditorContent)
};
}
