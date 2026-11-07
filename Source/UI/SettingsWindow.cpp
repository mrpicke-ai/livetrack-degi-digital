#include "SettingsWindow.h"
#include "../AppData.h"

namespace ldd
{
//==============================================================================
AudioSettingsTab::AudioSettingsTab (Engine& e) : engine (e)
{
    auto setupLabel = [] (juce::Label& l)
    {
        l.setColour (juce::Label::textColourId, theme::text);
        l.setFont (juce::Font (13.0f));
    };
    for (auto* l : { &driverLabel, &deviceLabel, &rateLabel, &bufferLabel, &routingLabel, &infoLabel })
        setupLabel (*l);

    driverBox.addListener (this);
    deviceBox.addListener (this);
    for (auto* type : engine.getDeviceManager().getAvailableDeviceTypes())
        driverBox.addItem (type->getTypeName(), driverBox.getNumItems() + 1);
    driverBox.setSelectedItemIndex (0, juce::dontSendNotification);

    rateBox.addItem ("44100", 1);
    rateBox.addItem ("48000", 2);
    rateBox.addItem ("96000", 3);
    rateBox.setSelectedId (2, juce::dontSendNotification);

    const int buffers[] = { 64, 128, 256, 512, 1024 };
    for (int i = 0; i < 5; ++i)
        bufferBox.addItem (juce::String (buffers[i]), i + 1);
    bufferBox.setSelectedId (3, juce::dontSendNotification);

    for (auto* c : { &driverBox, &deviceBox, &rateBox, &bufferBox })
        addAndMakeVisible (*c);
    for (auto* l : { &driverLabel, &deviceLabel, &rateLabel, &bufferLabel, &routingLabel, &infoLabel })
        addAndMakeVisible (*l);
    for (auto* b : { &stereoLink, &autoStart, &preferAsio })
    {
        b->setColour (juce::ToggleButton::textColourId, theme::text);
        addAndMakeVisible (*b);
    }
    preferAsio.setToggleState (true, juce::dontSendNotification);

    applyButton.onClick = [this]
    {
        if (onApplySettings) onApplySettings (collectSettings());
        refreshDeviceLists();
    };
    closeButton.onClick = [this]
    {
        if (auto* dw = dynamic_cast<juce::DocumentWindow*> (getTopLevelComponent())) dw->closeButtonPressed();
    };
    addAndMakeVisible (applyButton);
    addAndMakeVisible (closeButton);

    refreshDeviceLists();
    syncFromEngine();
}

void AudioSettingsTab::setStateFromSettings (const ProjectSettings& s)
{
    stereoLink.setToggleState (s.stereoLinked, juce::dontSendNotification);
    autoStart.setToggleState (s.autoStartNext, juce::dontSendNotification);
    preferAsio.setToggleState (s.preferAsio, juce::dontSendNotification);
}

juce::StringArray AudioSettingsTab::getDeviceNamesForCurrentType() const
{
    auto typeName = driverBox.getItemText (juce::jmax (0, driverBox.getSelectedItemIndex()));
    for (auto* type : engine.getDeviceManager().getAvailableDeviceTypes())
        if (type->getTypeName() == typeName)
            return type->getDeviceNames (false);
    return {};
}

void AudioSettingsTab::syncFromEngine()
{
    auto log = [] (const juce::String& line)
    {
        juce::File (juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                        .getChildFile ("DEGI Digital")
                        .getChildFile ("device_log.txt"))
            .appendText (line + "\n");
    };
    updating = true;
    // Seleziona il tipo di driver effettivamente attivo (es. ASIO dopo
    // l'aggiornamento automatico), poi la lista dispositivi, poi il
    // dispositivo attivo.
    auto currentType = engine.getDeviceManager().getCurrentAudioDeviceType();
    juce::String items;
    for (int i = 0; i < driverBox.getNumItems(); ++i)
    {
        items << "[" << driverBox.getItemText (i) << "]";
        if (driverBox.getItemText (i) == currentType)
            driverBox.setSelectedItemIndex (i, juce::dontSendNotification);
    }
    log ("syncFromEngine: attivo=" + currentType + "  combo=" + items);
    updating = false;
    refreshDeviceLists();
    updating = true;
    if (auto* dev = engine.getDeviceManager().getCurrentAudioDevice())
        for (int i = 0; i < deviceNamesActual.size(); ++i)
            if (deviceNamesActual[i] == dev->getName())
                deviceBox.setSelectedItemIndex (i, juce::dontSendNotification);
    // Sample rate e buffer: mostra i valori EFFETTIVAMENTE attivi.
    rateBox.setText (juce::String ((int) engine.getDeviceSampleRate()),
                     juce::dontSendNotification);
    bool bufferFound = false;
    for (int i = 0; i < bufferBox.getNumItems(); ++i)
        if (bufferBox.getItemText (i).getIntValue() == engine.getBufferSize())
        {
            bufferBox.setSelectedItemIndex (i, juce::dontSendNotification);
            bufferFound = true;
            break;
        }
    if (! bufferFound)
        bufferBox.setText (juce::String (engine.getBufferSize()), juce::dontSendNotification);
    updating = false;
}

void AudioSettingsTab::refreshDeviceLists()
{
    updating = true;
    // Conserva la selezione attuale: senza questo, dopo "Applica" la combo
    // torna al primo elemento della lista (es. un altro driver ASIO).
    juce::String previousActual;
    if (deviceBox.getSelectedItemIndex() >= 0
        && deviceBox.getSelectedItemIndex() < deviceNamesActual.size())
        previousActual = deviceNamesActual[deviceBox.getSelectedItemIndex()];
    deviceBox.clear (juce::dontSendNotification);
    deviceNamesActual.clear();
    auto names = getDeviceNamesForCurrentType();
    int previousIndex = -1;
    for (auto& n : names)
    {
        // Nome mostrato: aggiungi l'hardware quando il driver ASIO non lo
        // dice esplicitamente (es. "UMC ASIO Driver" = Behringer UMC404HD).
        juce::String display = n;
        if (n.containsIgnoreCase ("umc") && ! n.containsIgnoreCase ("behringer"))
            display << "  (= Behringer UMC404HD)";
        if (n == previousActual) previousIndex = deviceNamesActual.size();
        deviceNamesActual.add (n);
        deviceBox.addItem (display, deviceBox.getNumItems() + 1);
    }
    if (names.size() > 0)
        deviceBox.setSelectedItemIndex (previousIndex >= 0 ? previousIndex : 0,
                                        juce::dontSendNotification);

    infoLabel.setText ("Attivo: " + engine.getDeviceSummary() + "   |   Uscite: "
                           + juce::String (engine.getNumOutputChannels()),
                       juce::dontSendNotification);

    // Routing: una combo per traccia con singoli, coppie e gruppi 4 canali.
    auto outNames = engine.getOutputChannelNames();
    outputBoxes.clear();
    outputOptions.clear();
    outputSelections.clear();
    const int n = outNames.size();
    for (int i = 0; i < engine.getNumActiveTracks(); ++i)
    {
        auto box = std::make_unique<juce::ComboBox>();
        box->addItem ("-", 1);
        std::vector<std::pair<int, int>> opts;
        int itemId = 2;
        for (int c = 0; c < n; ++c)
        {
            box->addItem (juce::String (c + 1) + " - " + outNames[c], itemId++);
            opts.push_back ({ c, 1 });
        }
        for (int c = 0; c + 1 < n; c += 2)
        {
            box->addItem (juce::String (c + 1) + "-" + juce::String (c + 2), itemId++);
            opts.push_back ({ c, 2 });
        }
        for (int c = 0; c + 3 < n; c += 4)
        {
            box->addItem (juce::String (c + 1) + "-" + juce::String (c + 4), itemId++);
            opts.push_back ({ c, 4 });
        }

        int sel = 0, wantStart = engine.getTrack (i).outputStart,
                 wantCount = engine.getTrack (i).outputCount;
        for (int o = 0; o < (int) opts.size(); ++o)
            if (opts[(size_t) o].first == wantStart && opts[(size_t) o].second == wantCount) sel = o + 2;
        box->setSelectedId (sel, juce::dontSendNotification);
        box->addListener (this);

        addAndMakeVisible (*box);
        outputBoxes.push_back (std::move (box));
        outputOptions.push_back (std::move (opts));
        outputSelections.emplace_back (wantStart, wantCount);
    }
    updating = false;
    resized();
    repaint();
}

ProjectSettings AudioSettingsTab::collectSettings() const
{
    ProjectSettings s;
    s.audioDeviceType = driverBox.getItemText (juce::jmax (0, driverBox.getSelectedItemIndex()));
    s.outputDeviceName = deviceBox.getSelectedItemIndex() >= 0
                             && deviceBox.getSelectedItemIndex() < deviceNamesActual.size()
                                 ? deviceNamesActual[deviceBox.getSelectedItemIndex()]
                                 : juce::String();
    s.sampleRate = rateBox.getText().getDoubleValue();
    s.bufferSize = bufferBox.getText().getIntValue();
    s.stereoLinked = stereoLink.getToggleState();
    s.autoStartNext = autoStart.getToggleState();
    s.preferAsio = preferAsio.getToggleState();
    for (int i = 0; i < MAX_TRACKS; ++i)
    {
        if (i < (int) outputSelections.size())
        {
            s.trackOutStart[(size_t) i] = outputSelections[(size_t) i].first;
            s.trackOutCount[(size_t) i] = outputSelections[(size_t) i].second;
        }
    }
    return s;
}

void AudioSettingsTab::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
}

void AudioSettingsTab::resized()
{
    auto area = getLocalBounds().reduced (12);
    infoLabel.setBounds (area.removeFromTop (20));

    auto row = area.removeFromTop (28);
    driverLabel.setBounds (row.removeFromLeft (110));
    driverBox.setBounds (row.removeFromLeft (220));

    row = area.removeFromTop (28);
    deviceLabel.setBounds (row.removeFromLeft (110));
    deviceBox.setBounds (row.removeFromLeft (320));

    row = area.removeFromTop (28);
    rateLabel.setBounds (row.removeFromLeft (110));
    rateBox.setBounds (row.removeFromLeft (120));
    row.removeFromLeft (20);
    bufferLabel.setBounds (row.removeFromLeft (70));
    bufferBox.setBounds (row.removeFromLeft (110));

    area.removeFromTop (10);
    routingLabel.setBounds (area.removeFromTop (20));
    for (auto& box : outputBoxes)
    {
        row = area.removeFromTop (24);
        box->setBounds (row.removeFromLeft (320));
    }

    area.removeFromTop (8);
    stereoLink.setBounds (area.removeFromTop (24));
    autoStart.setBounds (area.removeFromTop (24));
    preferAsio.setBounds (area.removeFromTop (24));

    auto buttons = area.removeFromBottom (34);
    closeButton.setBounds (buttons.removeFromRight (100));
    buttons.removeFromRight (8);
    applyButton.setBounds (buttons.removeFromRight (100));
}

void AudioSettingsTab::comboBoxChanged (juce::ComboBox* box)
{
    if (updating) return;
    if (box == &driverBox)
    {
        // Driver diverso da ASIO: togli la spunta "Preferisci ASIO", altrimenti
        // il programma tornerebbe da solo su ASIO e Windows Audio non si
        // sentirebbe mai (il test audio incluso).
        if (driverBox.getItemText (juce::jmax (0, driverBox.getSelectedItemIndex())) != "ASIO")
            preferAsio.setToggleState (false, juce::dontSendNotification);
        refreshDeviceLists();
        return;
    }
    if (box == &deviceBox) return; // il nome viene letto all'Applica

    for (int i = 0; i < (int) outputBoxes.size(); ++i)
    {
        if (box == outputBoxes[(size_t) i].get())
        {
            auto id = box->getSelectedItemIndex() - 1;
            if (id < 0 || id >= (int) outputOptions[(size_t) i].size())
                outputSelections[(size_t) i] = { -1, 1 };
            else
                outputSelections[(size_t) i] = outputOptions[(size_t) i][(size_t) id];
            return;
        }
    }
}

void AudioSettingsTab::buttonClicked (juce::Button*)
{
}

//==============================================================================
MidiSettingsTab::MidiSettingsTab (Engine& e, ProjectSettings& s) : engine (e), settings (s)
{
    outLabel.setColour (juce::Label::textColourId, theme::text);
    outLabel.setFont (juce::Font (13.0f, juce::Font::bold));
    addAndMakeVisible (outLabel);
    channelLabel.setColour (juce::Label::textColourId, theme::text);
    channelLabel.setFont (juce::Font (13.0f, juce::Font::bold));
    addAndMakeVisible (channelLabel);

    outBox.addListener (this);
    addAndMakeVisible (outBox);
    refreshOutputList();

    for (int ch = 1; ch <= 16; ++ch)
        channelBox.addItem (juce::String (ch), ch);
    channelBox.setSelectedId (juce::jlimit (1, 16, settings.midiChannel),
                              juce::dontSendNotification);
    channelBox.addListener (this);
    addAndMakeVisible (channelBox);

    inputsLabel.setColour (juce::Label::textColourId, theme::text);
    inputsLabel.setFont (juce::Font (13.0f, juce::Font::bold));
    addAndMakeVisible (inputsLabel);

    for (auto& d : juce::MidiInput::getAvailableDevices())
    {
        auto t = std::make_unique<juce::ToggleButton> (d.name);
        t->setColour (juce::ToggleButton::textColourId, theme::text);
        t->setToggleState (engine.getDeviceManager().isMidiInputDeviceEnabled (d.identifier),
                           juce::dontSendNotification);
        t->onClick = [this, id = d.identifier, ptr = t.get()]
        {
            engine.getDeviceManager().setMidiInputDeviceEnabled (id, ptr->getToggleState());
        };
        addAndMakeVisible (*t);
        inputToggles.push_back (std::move (t));
    }

    for (int i = 0; i < (int) TransportCommand::count; ++i)
    {
        auto cmdLabel = std::make_unique<juce::Label> (getCommandName ((TransportCommand) i),
                                                       getCommandName ((TransportCommand) i));
        cmdLabel->setColour (juce::Label::textColourId, theme::text);
        addAndMakeVisible (*cmdLabel);
        commandLabels.push_back (std::move (cmdLabel));

        auto mapLabel = std::make_unique<juce::Label> ("non mappato");
        mapLabel->setColour (juce::Label::textColourId, theme::textDim);
        addAndMakeVisible (*mapLabel);
        mappingLabels.push_back (std::move (mapLabel));

        auto learn = std::make_unique<juce::TextButton> ("Impara");
        learn->addListener (this);
        addAndMakeVisible (*learn);
        learnButtons.push_back (std::move (learn));
    }

    clearButton.onClick = [this]
    {
        engine.setMidiMappings ({});
        learningCommand = -1;
        engine.setMidiLearn (false);
        refreshRows();
    };
    addAndMakeVisible (clearButton);

    refreshRows();
    startTimerHz (10);
}

void MidiSettingsTab::refreshRows()
{
    auto& maps = engine.getMidiMappings();
    for (int i = 0; i < (int) TransportCommand::count; ++i)
    {
        juce::String txt = "non mappato";
        for (auto& m : maps)
            if (m.command == i) txt = describeMapping (m);
        mappingLabels[(size_t) i]->setText (txt, juce::dontSendNotification);
    }
}

void MidiSettingsTab::refreshOutputList()
{
    updating = true;
    outBox.clear (juce::dontSendNotification);
    outIds.clear();
    outBox.addItem ("-- nessuna --", 1);
    outIds.add (juce::String());
    int selected = 1;
    auto ids = Engine::getMidiOutputDeviceIds();
    auto names = Engine::getMidiOutputDeviceNames();
    for (int i = 0; i < names.size(); ++i)
    {
        outBox.addItem (names[i], i + 2);
        outIds.add (ids[i]);
        if (ids[i] == settings.midiOutputId)
            selected = i + 2;
    }
    outBox.setSelectedId (selected, juce::dontSendNotification);
    updating = false;
}

void MidiSettingsTab::comboBoxChanged (juce::ComboBox* box)
{
    if (updating) return;
    if (box == &outBox)
    {
        const int id = outBox.getSelectedItemIndex();   // 0 = nessuna
        const auto ident = id >= 0 && id < outIds.size() ? outIds[id] : juce::String();
        settings.midiOutputId = ident;
        engine.setMidiOutputDevice (ident);
    }
    else if (box == &channelBox)
    {
        settings.midiChannel = juce::jlimit (1, 16, channelBox.getSelectedId());
    }
}

juce::String MidiSettingsTab::describeMapping (const MidiMapping& m) const
{
    juce::String type = m.type == 0 ? "CC" : (m.type == 1 ? "Nota" : "Program");
    juce::String ch = m.channel > 0 ? ("ch " + juce::String (m.channel)) : "qualsiasi canale";
    return type + " " + juce::String (m.number) + " (" + ch + ")";
}

void MidiSettingsTab::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
}

void MidiSettingsTab::resized()
{
    auto area = getLocalBounds().reduced (12);

    auto row = area.removeFromTop (28);
    outLabel.setBounds (row.removeFromLeft (170));
    outBox.setBounds (row.removeFromLeft (280));
    row.removeFromLeft (20);
    channelLabel.setBounds (row.removeFromLeft (60));
    channelBox.setBounds (row.removeFromLeft (70));

    area.removeFromTop (12);
    inputsLabel.setBounds (area.removeFromTop (20));
    for (auto& t : inputToggles)
        t->setBounds (area.removeFromTop (24));

    area.removeFromTop (10);
    for (int i = 0; i < (int) TransportCommand::count; ++i)
    {
        auto row = area.removeFromTop (30);
        learnButtons[(size_t) i]->setBounds (row.removeFromRight (90));
        mappingLabels[(size_t) i]->setBounds (row.removeFromRight (260));
        commandLabels[(size_t) i]->setBounds (row);
    }

    clearButton.setBounds (area.removeFromBottom (34).removeFromRight (170));
}

void MidiSettingsTab::buttonClicked (juce::Button* b)
{
    if (b == &clearButton) return;
    for (int i = 0; i < (int) TransportCommand::count; ++i)
    {
        if (b == learnButtons[(size_t) i].get())
        {
            learningCommand = i;
            engine.setMidiLearn (true, i);
        }
    }
}

void MidiSettingsTab::timerCallback()
{
    if (learningCommand >= 0 && engine.consumeLearnedMessage() != 0)
    {
        learningCommand = -1;
        engine.setMidiLearn (false);
        refreshRows();
    }
}

//==============================================================================
SettingsWindow::SettingsWindow (Engine& e, const ProjectSettings& current,
                                std::function<void (const ProjectSettings&)> onApply)
    : DocumentWindow ("Impostazioni", theme::background, DocumentWindow::closeButton)
{
    working = current;
    auto tabs = new juce::TabbedComponent (juce::TabbedButtonBar::TabsAtTop);
    auto* audio = new AudioSettingsTab (e);
    audio->setStateFromSettings (current);
    // Il tab Audio raccoglie un ProjectSettings nuovo: ripristina i campi
    // MIDI out scritti dal tab MIDI, altrimenti verrebbero azzerati ad Applica.
    audio->onApplySettings = [this, cb = std::move (onApply)] (const ProjectSettings& s)
    {
        ProjectSettings merged = s;
        merged.midiOutputId = working.midiOutputId;
        merged.midiChannel = working.midiChannel;
        if (cb) cb (merged);
    };
    auto* midi = new MidiSettingsTab (e, working);
    tabs->setTabBarDepth (30);
    tabs->setColour (juce::TabbedComponent::backgroundColourId, theme::background);
    tabs->setColour (juce::TabbedButtonBar::tabTextColourId, theme::text);
    tabs->setColour (juce::TabbedButtonBar::frontTextColourId, theme::primary);
    tabs->addTab ("Audio", theme::background, audio, true);
    tabs->addTab ("MIDI", theme::background, midi, true);
    tabs->setCurrentTabIndex (0);
    setContentOwned (tabs, true);
    setUsingNativeTitleBar (true);
    setResizable (true, true);
    setResizeLimits (700, 480, 1600, 1200);
    centreWithSize (760, 560);
}

void SettingsWindow::closeButtonPressed()
{
    if (onClose) onClose();
    else setVisible (false);
}
}
