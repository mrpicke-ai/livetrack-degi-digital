// Test diretto PC -> Ampero II Stage via USB MIDI (senza Livetrack in mezzo).
// Invia: bank0+PC0 (A01-1), poi PC5 (A02-1), poi FS1 on/off (CC 79).
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <iostream>

int main()
{
    const juce::String target = "Ampero II Stage MIDI";
    juce::String chosen;
    for (auto& d : juce::MidiOutput::getAvailableDevices())
        if (d.name == target) chosen = d.identifier;
    if (chosen.isEmpty())
    {
        std::cout << "Ampero II Stage MIDI non trovato tra le uscite.\n";
        return 1;
    }
    auto out = juce::MidiOutput::openDevice (chosen);
    if (out == nullptr)
    {
        std::cout << "Apertura uscita fallita (dispositivo occupato da un altro programma?).\n";
        return 2;
    }
    std::cout << "Uscita aperta: " << target.toStdString() << "\n";

    out->sendMessageNow (juce::MidiMessage::controllerEvent (1, 0, 0));
    out->sendMessageNow (juce::MidiMessage::programChange (1, 0));
    std::cout << "1) Inviato bank0 + PC0  -> lo Stage dovrebbe caricare A01-1\n";
    juce::Thread::sleep (3000);

    out->sendMessageNow (juce::MidiMessage::programChange (1, 5));
    std::cout << "2) Inviato PC5          -> lo Stage dovrebbe caricare A02-1\n";
    juce::Thread::sleep (3000);

    out->sendMessageNow (juce::MidiMessage::controllerEvent (1, 79, 127));
    std::cout << "3) Inviato CC79=127     -> FS1 ON\n";
    juce::Thread::sleep (1500);
    out->sendMessageNow (juce::MidiMessage::controllerEvent (1, 79, 0));
    std::cout << "4) Inviato CC79=0       -> FS1 OFF\n";
    juce::Thread::sleep (500);
    std::cout << "Fine test.\n";
    return 0;
}
