# Livetrack Degi Digital

Player multitraccia professionale per concerti e prove — C++20 / JUCE 8.0.9 / CMake.

## Struttura

- `Source/Audio/Engine.*` — motore audio: callback ASIO/WASAPI, rendering sample-accurate delle tracce, routing fisico, loop A/B, comandi thread-safe, rilevamento xrun e dispositivo perso.
- `Source/Audio/ClickGenerator.*` — generatore di click metronomico (WAV).
- `Source/Data/Project.*` — brani, scalette, impostazioni, persistenza JSON `.liveset`.
- `Source/UI/` — interfaccia dark, mixer, waveform, trasporto, performance fullscreen, MIDI learn, guida.
- `external/JUCE` — JUCE 8.0.9 (clone).

## Build (Windows x64)

Richiede Visual Studio 2022 BuildTools (MSVC v143) con componente "C++ CMake tools".

1. Clona JUCE 8.0.9 in `external/JUCE`:

   ```bat
   git clone --branch 8.0.9 --depth 1 https://github.com/juce-framework/JUCE.git external/JUCE
   ```

2. Per il supporto ASIO nativo, scarica lo Steinberg ASIO SDK e decomprimilo in `external/asiosdk`. Lo SDK non è incluso nel repository e non va ridistribuito. In alternativa imposta `JUCE_ASIO_SDK_PATH` o passa `-DASIO_SDK_PATH=...` a CMake. Senza SDK resta disponibile WASAPI.
3. Compila:

   ```bat
   configure.bat
   build.bat 16
   ```

L'eseguibile è in `build/LivetrackDegiDigital_artefacts/Release/LivetrackDegiDigital.exe`.

## Note tecniche

- 9 tracce indipendenti (costante `MAX_TRACKS`), nomi personalizzabili, routing per traccia su singolo canale, coppia (1-2, 3-4…) o gruppo 4 canali (1-2-3-4).
- Tutti i brani della scaletta vengono precaricati prima dell'uso; i file audio sono decodificati interamente in RAM e passati al callback con swap atomico (`shared_ptr`): nessuna allocazione o I/O su disco nel real-time path.
- Il cambio brano gapless avviene con doppio buffering (stage → commit atomico).
- Il routing oltre 4 uscite richiede un dispositivo con i corrispondenti canali fisici; si configura per traccia nelle impostazioni.
- Per creare un installer serve NSIS o WiX Toolset; la versione corrente è distribuita come applicazione portable.
