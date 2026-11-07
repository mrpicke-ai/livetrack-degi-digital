#include "SongEditor.h"

namespace ldd
{
SongEditorWindow::SongEditorWindow (Song& songToEdit, int numTracks, double deviceSampleRate,
                                    juce::StringArray trackNames)
    : DocumentWindow ("Modifica brano", theme::background, DocumentWindow::closeButton)
{
    auto* content = new SongEditorContent (songToEdit, numTracks, deviceSampleRate,
                                         std::move (trackNames));
    content->onSave = [this] { exitModalState (1); };
    setContentOwned (content, true);
    setUsingNativeTitleBar (true);
    setResizable (false, false);
    centreWithSize (720, 520);
}

SongEditorContent::SongEditorContent (Song& s, int n, double sr, juce::StringArray names)
    : song (s), numTracks (n), deviceSampleRate (sr), trackNames (std::move (names))
{
    auto setupLabel = [] (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setColour (juce::Label::textColourId, theme::text);
        l.setFont (juce::Font (14.0f));
    };

    setupLabel (titleLabel, "Titolo");
    setupLabel (artistLabel, "Artista");
    setupLabel (bpmLabel, "BPM");
    setupLabel (noteLabel, "Note");

    titleEditor.setText (song.title);
    artistEditor.setText (song.artist);
    bpmEditor.setText (juce::String (song.bpm, 1));
    noteEditor.setMultiLine (true);
    noteEditor.setText (song.notes);

    for (auto* e : { &titleEditor, &artistEditor, &bpmEditor, &noteEditor })
        addAndMakeVisible (*e);   // colori editor: schermo fosforo da SpaceLookAndFeel

    for (int i = 0; i < numTracks; ++i)
    {
        auto label = std::make_unique<juce::Label>();
        label->setText (trackNames[i], juce::dontSendNotification);
        label->setColour (juce::Label::textColourId, getTrackColour (i));
        label->setFont (juce::Font (13.0f, juce::Font::bold));
        addAndMakeVisible (*label);
        trackLabels.push_back (std::move (label));

        auto fileLabel = std::make_unique<juce::Label>();
        fileLabel->setText (song.filePaths[(size_t) i].isEmpty() ? "nessun file"
                                                                 : juce::File (song.filePaths[(size_t) i]).getFileName(),
                            juce::dontSendNotification);
        fileLabel->setColour (juce::Label::textColourId, theme::textDim);
        addAndMakeVisible (*fileLabel);
        fileLabels.push_back (std::move (fileLabel));

        auto browse = std::make_unique<juce::TextButton> ("Sfoglia...");
        browse->onClick = [this, i] { browseForTrack (i); };
        addAndMakeVisible (*browse);
        browseButtons.push_back (std::move (browse));

        if (i == 2) // traccia Click
        {
            auto gen = std::make_unique<juce::TextButton> ("Genera click...");
            gen->onClick = [this, i] { generateClickForTrack (i); };
            addAndMakeVisible (*gen);
            clickButtons.push_back (std::move (gen));
        }
    }

    saveButton.onClick = [this] { save(); };
    cancelButton.onClick = [this]
    {
        if (auto* dw = dynamic_cast<juce::DocumentWindow*> (getTopLevelComponent()))
            dw->exitModalState (0);
    };
    addAndMakeVisible (saveButton);
    addAndMakeVisible (cancelButton);
}

void SongEditorContent::resized()
{
    auto area = getLocalBounds().reduced (12);
    area.removeFromTop (34);

    auto row = area.removeFromTop (26);
    titleLabel.setBounds (row.removeFromLeft (70));
    titleEditor.setBounds (row);

    row = area.removeFromTop (26);
    artistLabel.setBounds (row.removeFromLeft (70));
    artistEditor.setBounds (row.reduced (0, 1));
    auto bpmArea = row.removeFromRight (160);
    bpmLabel.setBounds (bpmArea.removeFromLeft (40));
    bpmEditor.setBounds (bpmArea);

    row = area.removeFromTop (60);
    noteLabel.setBounds (row.removeFromLeft (70));
    noteEditor.setBounds (row);

    area.removeFromTop (8);
    for (int i = 0; i < numTracks; ++i)
    {
        row = area.removeFromTop (30);
        trackLabels[(size_t) i]->setBounds (row.removeFromLeft (90));
        if (i == 2 && ! clickButtons.empty())
            clickButtons[0]->setBounds (row.removeFromRight (110));
        browseButtons[(size_t) i]->setBounds (row.removeFromRight (90));
        fileLabels[(size_t) i]->setBounds (row.reduced (4, 0));
    }

    area.removeFromTop (10);
    auto buttons = area.removeFromTop (34);
    cancelButton.setBounds (buttons.removeFromRight (100));
    buttons.removeFromRight (8);
    saveButton.setBounds (buttons.removeFromRight (100));
}

void SongEditorContent::browseForTrack (int trackIndex)
{
    juce::FileChooser fc ("Seleziona file per " + trackNames[trackIndex], {},
                          "*.wav;*.aif;*.aiff;*.flac;*.mp3");
    if (fc.browseForFileToOpen())
    {
        song.filePaths[(size_t) trackIndex] = fc.getResult().getFullPathName();
        fileLabels[(size_t) trackIndex]->setText (fc.getResult().getFileName(), juce::dontSendNotification);
    }
}

void SongEditorContent::generateClickForTrack (int trackIndex)
{
    ClickGenerator::Config cfg;
    cfg.bpm = juce::jlimit (20.0, 300.0, bpmEditor.getText().getDoubleValue());
    cfg.sampleRate = deviceSampleRate > 0 ? deviceSampleRate : 48000.0;
    cfg.numBars = 400;

    auto target = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                      .getChildFile ("DEGI Digital Click")
                      .getChildFile ("click_" + juce::String ((int) cfg.bpm) + "bpm.wav");
    target.getParentDirectory().createDirectory();

    juce::String error;
    auto written = ClickGenerator::generateToFile (target, cfg, error);
    if (written.existsAsFile())
    {
        song.filePaths[(size_t) trackIndex] = written.getFullPathName();
        fileLabels[(size_t) trackIndex]->setText (written.getFileName(), juce::dontSendNotification);
    }
    else
    {
        juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::WarningIcon,
                                                "Generazione click", error);
    }
}

void SongEditorContent::save()
{
    song.title = titleEditor.getText().trim().isEmpty() ? "Senza titolo" : titleEditor.getText().trim();
    song.artist = artistEditor.getText().trim();
    song.bpm = juce::jlimit (20.0, 300.0, bpmEditor.getText().getDoubleValue());
    song.notes = noteEditor.getText();
    if (onSave) onSave();
}
}
