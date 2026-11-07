cmake_minimum_required(VERSION 3.22)

project(LivetrackDegiDigital VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if (MSVC)
    # Sorgenti UTF-8: senza questo flag MSVC li interpreta nella codepage
    # locale e gli accenti/apostrofi dei testi diventano caratteri errati.
    add_compile_options(/utf-8)
endif()

add_subdirectory(external/JUCE)

juce_add_gui_app(LivetrackDegiDigital
    VERSION 1.0.0
    COMPANY_NAME "DEGISOFT"
    COMPANY_COPYRIGHT "(c) 2026 DEGISOFT"
    PRODUCT_NAME "Livetrack Degi Digital"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon.png"
    NEEDS_WEB_BROWSER FALSE
)

juce_generate_juce_header(LivetrackDegiDigital)

# Nome file senza spazi: piu' robusto in script e servizi di indicizzazione.
set_target_properties(LivetrackDegiDigital PROPERTIES OUTPUT_NAME "LivetrackDegiDigital")

# Font CRT retro (VT323, licenza SIL OFL) incorporato nel binario.
juce_add_binary_data(LivetrackFonts SOURCES Resources/VT323-Regular.ttf)
target_link_libraries(LivetrackDegiDigital PRIVATE LivetrackFonts)

target_sources(LivetrackDegiDigital PRIVATE
    Source/Main.cpp
    Source/AppData.h
    Source/Audio/Engine.cpp
    Source/Audio/Engine.h
    Source/Audio/ClickGenerator.cpp
    Source/Audio/ClickGenerator.h
    Source/Data/Project.cpp
    Source/Data/Project.h
    Source/UI/LogoComponent.h
    Source/UI/SongOverview.cpp
    Source/UI/SongOverview.h
    Source/UI/ConsoleChannel.cpp
    Source/UI/ConsoleChannel.h
    Source/UI/AmperoStrip.h
    Source/UI/SongEditor.cpp
    Source/UI/SongEditor.h
    Source/UI/SettingsWindow.cpp
    Source/UI/SettingsWindow.h
    Source/UI/PerformanceMode.cpp
    Source/UI/PerformanceMode.h
    Source/UI/HelpComponent.cpp
    Source/UI/HelpComponent.h
    Source/UI/MainComponent.cpp
    Source/UI/MainComponent.h
)

target_compile_definitions(LivetrackDegiDigital PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
)

target_link_libraries(LivetrackDegiDigital PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_gui_extra
    juce::juce_gui_basics
    juce::juce_graphics
    juce::juce_data_structures
    juce::juce_events
    juce::juce_core
    juce::juce_recommended_config_flags
)

if (MSVC)
    target_compile_options(LivetrackDegiDigital PRIVATE /W3)
endif()

#==============================================================================
# Supporto ASIO nativo (Steinberg ASIO SDK).
# Lo SDK non e' ridistribuibile: scaricalo da https://www.steinberg.net/developers/
# (ASIO SDK, richiede registrazione gratuita) e decomprimilo in external/asiosdk
# oppure imposta la variabile d'ambiente JUCE_ASIO_SDK_PATH.
set(ASIO_SDK_PATH "" CACHE PATH "Percorso dello Steinberg ASIO SDK")
if (NOT ASIO_SDK_PATH AND DEFINED ENV{JUCE_ASIO_SDK_PATH})
    file(TO_CMAKE_PATH "$ENV{JUCE_ASIO_SDK_PATH}" ASIO_SDK_PATH)
endif()
if (NOT ASIO_SDK_PATH AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/asiosdk/common/asiosys.h")
    set(ASIO_SDK_PATH "${CMAKE_CURRENT_SOURCE_DIR}/external/asiosdk")
endif()

if (ASIO_SDK_PATH AND EXISTS "${ASIO_SDK_PATH}/common/asiosys.h")
    message(STATUS "ASIO SDK trovato in: ${ASIO_SDK_PATH} -> supporto ASIO nativo ATTIVO")
    target_compile_definitions(LivetrackDegiDigital PRIVATE
        JUCE_ASIO=1
        "JUCE_ASIO_SDK_PATH=\"${ASIO_SDK_PATH}\"")
    target_include_directories(LivetrackDegiDigital PRIVATE
        "${ASIO_SDK_PATH}/common"
        "${ASIO_SDK_PATH}/host")
else()
    message(STATUS "ASIO SDK non trovato -> compilazione SENZA supporto ASIO nativo.")
    message(STATUS "Per l'ASIO: scarica lo SDK Steinberg e decomprimilo in external/asiosdk")
endif()

# Strumento di diagnostica dispositivi (console)
juce_add_console_app(DeviceProbe PRODUCT_NAME "DeviceProbe")
juce_generate_juce_header(DeviceProbe)
target_sources(DeviceProbe PRIVATE Source/DeviceProbe.cpp)
target_compile_definitions(DeviceProbe PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
target_link_libraries(DeviceProbe PRIVATE juce::juce_audio_devices juce::juce_events juce::juce_core)

# Smoke test del motore audio: apre il dispositivo, suona un file e misura l'uscita
juce_add_console_app(AudioSmokeTest PRODUCT_NAME "AudioSmokeTest")
juce_generate_juce_header(AudioSmokeTest)
target_sources(AudioSmokeTest PRIVATE
    Source/AudioSmokeTest.cpp
    Source/Audio/Engine.cpp
    Source/Data/Project.cpp)
target_compile_definitions(AudioSmokeTest PRIVATE
    JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0
    JUCE_ASIO=1
    "JUCE_ASIO_SDK_PATH=\"${ASIO_SDK_PATH}\"")
target_include_directories(AudioSmokeTest PRIVATE
    "${ASIO_SDK_PATH}/common"
    "${ASIO_SDK_PATH}/host")
target_link_libraries(AudioSmokeTest PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_devices
    juce::juce_events
    juce::juce_core)

# Test diretto MIDI out verso l'Ampero (bypassa il programma principale)
juce_add_console_app(MidiSendTest PRODUCT_NAME "MidiSendTest")
juce_generate_juce_header(MidiSendTest)
target_sources(MidiSendTest PRIVATE Source/MidiSendTest.cpp)
target_compile_definitions(MidiSendTest PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
target_link_libraries(MidiSendTest PRIVATE
    juce::juce_audio_devices
    juce::juce_events
    juce::juce_core)
