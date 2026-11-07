#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../Audio/Engine.h"

namespace ldd
{
class AudioSettingsTab : public juce::Component,
                         private juce::ComboBox::Listener,
                         private juce::Button::Listener
{
public:
    explicit AudioSettingsTab (Engine& e);

    std::function<void (const ProjectSettings&)> onApplySettings;

    void refreshDeviceLists();
    void syncFromEngine();
    void setStateFromSettings (const ProjectSettings&);
    ProjectSettings collectSettings() const;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void comboBoxChanged (juce::ComboBox*) override;
    void buttonClicked (juce::Button*) override;
    juce::StringArray getDeviceNamesForCurrentType() const;

    Engine& engine;
    juce::Label driverLabel { "driver", "Driver" }, deviceLabel { "device", "Dispositivo" },
                rateLabel { "rate", "Sample rate" }, bufferLabel { "buffer", "Buffer" },
                infoLabel, routingLabel { "routing", "Routing tracce -> uscite fisiche" };
    juce::ComboBox driverBox, deviceBox, rateBox, bufferBox;
    juce::StringArray deviceNamesActual;   // nomi reali, paralleli alle voci di deviceBox
    std::vector<std::unique_ptr<juce::ComboBox>> outputBoxes;
    std::vector<std::vector<std::pair<int, int>>> outputOptions;   // per traccia: (start,count)
    std::vector<std::pair<int, int>> outputSelections;
    juce::ToggleButton stereoLink { "Collega Backing L/R (stesso volume)" },
                       autoStart { "Live: avvia automaticamente il brano successivo a fine brano" },
                       preferAsio { "Preferisci ASIO: usa il driver dedicato della scheda (consigliato)" };
    juce::TextButton applyButton { "Applica" }, closeButton { "Chiudi" };
    bool updating = false;

    JUCE_DECLARE_NON_COPYABLE (AudioSettingsTab)
};

class MidiSettingsTab : public juce::Component,
                        private juce::Button::Listener,
                        private juce::ComboBox::Listener,
                        private juce::Timer
{
public:
    MidiSettingsTab (Engine& e, ProjectSettings& s);
    ~MidiSettingsTab() override { engine.setMidiLearn (false); }

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void buttonClicked (juce::Button*) override;
    void comboBoxChanged (juce::ComboBox*) override;
    void timerCallback() override;
    void refreshRows();
    void refreshOutputList();
    juce::String describeMapping (const MidiMapping&) const;

    Engine& engine;
    ProjectSettings& settings;   // copia di lavoro della finestra: campi MIDI out
    juce::Label inputsLabel { "inputs", "Ingressi MIDI" },
                outLabel { "out", "Uscita MIDI (pedaliera)" },
                channelLabel { "channel", "Canale" };
    juce::ComboBox outBox, channelBox;
    juce::StringArray outIds;   // parallelo alle voci di outBox (voce 1 = nessuna)
    std::vector<std::unique_ptr<juce::ToggleButton>> inputToggles;
    std::vector<std::unique_ptr<juce::Label>> commandLabels;
    std::vector<std::unique_ptr<juce::Label>> mappingLabels;
    std::vector<std::unique_ptr<juce::TextButton>> learnButtons;
    juce::TextButton clearButton { "Cancella mappature" };
    int learningCommand = -1;
    bool updating = false;

    JUCE_DECLARE_NON_COPYABLE (MidiSettingsTab)
};

class SettingsWindow : public juce::DocumentWindow
{
public:
    SettingsWindow (Engine& e, const ProjectSettings& current,
                    std::function<void (const ProjectSettings&)> onApply);
    void closeButtonPressed() override;
    std::function<void()> onClose;

private:
    // Copia di lavoro: il tab MIDI ci scrive dispositivo/canale in uscita,
    // che vengono fusi nelle impostazioni raccolte dal tab Audio ad ogni Applica.
    ProjectSettings working;

    JUCE_DECLARE_NON_COPYABLE (SettingsWindow)
};
}
