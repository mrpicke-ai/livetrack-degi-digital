// Strumento di diagnostica: elenca tipi di driver, dispositivi ASIO e uscite.
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <iostream>

int main()
{
    juce::AudioDeviceManager dm;
    dm.initialise (0, 8, nullptr, true);

    std::cout << "Tipi di driver disponibili:\n";
    for (auto* type : dm.getAvailableDeviceTypes())
    {
        std::cout << "  - " << type->getTypeName().toStdString() << "\n";
        auto names = type->getDeviceNames (false);
        for (auto& n : names)
        {
            std::cout << "      dispositivo: " << n.toStdString() << "\n";
            if (auto* dev = type->createDevice (n, {}))
            {
                std::cout << "        uscite (" << dev->getOutputChannelNames().size() << "): ";
                for (auto& ch : dev->getOutputChannelNames())
                    std::cout << "[" << ch.toStdString() << "] ";
                std::cout << "\n        sample rate: " << dev->getCurrentSampleRate()
                          << "  buffer min/max: " << dev->getDefaultBufferSize() << "\n";
                delete dev;
            }
            else
                std::cout << "        (non apribile ora: forse non collegato)\n";
        }
    }
    std::cout << "\nDispositivi MIDI in uscita:\n";
    for (auto& d : juce::MidiOutput::getAvailableDevices())
        std::cout << "  - [" << d.name.toStdString() << "]  id=" << d.identifier.toStdString() << "\n";
    std::cout << "Dispositivi MIDI in ingresso:\n";
    for (auto& d : juce::MidiInput::getAvailableDevices())
        std::cout << "  - [" << d.name.toStdString() << "]  id=" << d.identifier.toStdString() << "\n";
    return 0;
}
