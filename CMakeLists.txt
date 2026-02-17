cmake_minimum_required(VERSION 3.16)
project(GrooveBoxUI LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Multimedia)

# ALSA handled after target creation.

find_package(PkgConfig)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(JACK QUIET jack)
endif()

set(SOURCES
    src/main.cpp
    src/MainWindow.cpp
    src/ConsoleModeGuard.cpp
    src/FramebufferCleaner.cpp
    src/AudioEngine.cpp
    src/simple_fm.cpp
    src/PadBank.cpp
    src/SampleSession.cpp
    src/ui/TopToolbarWidget.cpp
    src/ui/BpmArcWidget.cpp
    src/ui/SamplePageWidget.cpp
    src/ui/EditPageWidget.cpp
    src/ui/SeqPageWidget.cpp
    src/ui/FxPageWidget.cpp
    src/ui/SynthPageWidget.cpp
    src/ui/PadAssignOverlay.cpp
    src/ui/PadHoldMenuOverlay.cpp
    src/ui/PianoRollOverlay.cpp
    src/ui/ProjectMenuOverlay.cpp
    src/ui/SimplePageWidget.cpp
    src/ui/SystemStats.cpp
    src/ui/SampleBrowserModel.cpp
    src/ui/WaveformRenderer.cpp
)

set(HEADERS
    src/MainWindow.h
    src/ConsoleModeGuard.h
    src/FramebufferCleaner.h
    src/AudioEngine.h
    src/simple_fm.h
    src/PadBank.h
    src/SampleSession.h
    src/Theme.h
    src/ui/TopToolbarWidget.h
    src/ui/BpmArcWidget.h
    src/ui/SamplePageWidget.h
    src/ui/EditPageWidget.h
    src/ui/SeqPageWidget.h
    src/ui/FxPageWidget.h
    src/ui/SynthPageWidget.h
    src/ui/PadAssignOverlay.h
    src/ui/PadHoldMenuOverlay.h
    src/ui/PianoRollOverlay.h
    src/ui/ProjectMenuOverlay.h
    src/ui/SimplePageWidget.h
    src/ui/SystemStats.h
    src/ui/SampleBrowserModel.h
    src/ui/WaveformRenderer.h
)

set(DX7_CORE_SOURCES
    src/dx7_core/dx7_core.cpp
    src/dx7_core/dx7note.cc
    src/dx7_core/env.cc
    src/dx7_core/exp2.cc
    src/dx7_core/fm_core.cc
    src/dx7_core/fm_op_kernel.cc
    src/dx7_core/freqlut.cc
    src/dx7_core/lfo.cc
    src/dx7_core/pitchenv.cc
    src/dx7_core/porta.cpp
    src/dx7_core/sin.cc
    src/dx7_core/tuning.cpp
)

set(DX7_CORE_HEADERS
    src/dx7_core/aligned_buf.h
    src/dx7_core/controllers.h
    src/dx7_core/dx7_core.h
    src/dx7_core/dx7note.h
    src/dx7_core/env.h
    src/dx7_core/exp2.h
    src/dx7_core/fm_core.h
    src/dx7_core/fm_op_kernel.h
    src/dx7_core/freqlut.h
    src/dx7_core/lfo.h
    src/dx7_core/libMTSClient.h
    src/dx7_core/module.h
    src/dx7_core/pitchenv.h
    src/dx7_core/porta.h
    src/dx7_core/sin.h
    src/dx7_core/synth.h
    src/dx7_core/tuning.h
)

add_library(dx7_core STATIC ${DX7_CORE_SOURCES} ${DX7_CORE_HEADERS})
target_include_directories(dx7_core PUBLIC src/dx7_core)

set(VITAL_ROOT ${CMAKE_SOURCE_DIR}/vital-main)
if(UNIX AND EXISTS ${VITAL_ROOT})
    set(VITAL_SOURCES
        ${VITAL_ROOT}/src/unity_build/common.cpp
        ${VITAL_ROOT}/src/unity_build/synthesis.cpp
        ${VITAL_ROOT}/headless/JuceLibraryCode/include_juce_audio_basics.cpp
        ${VITAL_ROOT}/headless/JuceLibraryCode/include_juce_audio_formats.cpp
        ${VITAL_ROOT}/headless/JuceLibraryCode/include_juce_core.cpp
        ${VITAL_ROOT}/headless/JuceLibraryCode/include_juce_data_structures.cpp
        ${VITAL_ROOT}/headless/JuceLibraryCode/include_juce_dsp.cpp
        ${VITAL_ROOT}/headless/JuceLibraryCode/include_juce_events.cpp
    )

    add_library(vital_core STATIC
        ${VITAL_SOURCES}
        src/vital_core/vital_core.cpp
        src/vital_core/vital_core.h
    )
    set_target_properties(vital_core PROPERTIES
        CXX_STANDARD 14
        CXX_STANDARD_REQUIRED ON
    )
    target_compile_definitions(vital_core PRIVATE
        GROOVEBOX_WITH_VITAL=1
        HEADLESS=1
        NO_AUTH=1
        JUCE_USE_XRANDR=0
        JUCE_USE_CURL=0
        JUCE_DSP_USE_SHARED_FFTW=0
        JUCE_DSP_USE_STATIC_FFTW=0
        JUCE_APP_VERSION="99999.9.9"
        JUCE_APP_VERSION_HEX=0x869f0909
        JUCE_STANDALONE_APPLICATION=0
        JucePlugin_Build_VST=0
        JucePlugin_Build_VST3=0
        JucePlugin_Build_AU=0
        JucePlugin_Build_AUv3=0
        JucePlugin_Build_RTAS=0
        JucePlugin_Build_AAX=0
        JucePlugin_Build_Standalone=0
        JucePlugin_Build_Unity=0
    )
    target_compile_options(vital_core PRIVATE
        -ffast-math
        -ftree-vectorize
        -ftree-slp-vectorize
        -funroll-loops
    )
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
        target_compile_options(vital_core PRIVATE
            -march=armv8-a
            -mtune=cortex-a53
        )
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "arm|armv6|armv7")
        target_compile_options(vital_core PRIVATE
            -march=armv8-a
            -mtune=cortex-a53
            -mfpu=neon-fp-armv8
            -mfloat-abi=hard
        )
        target_compile_definitions(vital_core PRIVATE NEON_ARM32=1)
    endif()
    target_include_directories(vital_core PRIVATE
        ${VITAL_ROOT}/headless/JuceLibraryCode
        ${VITAL_ROOT}/third_party/JUCE/modules
        ${VITAL_ROOT}/src/common
        ${VITAL_ROOT}/src/common/wavetable
        ${VITAL_ROOT}/src/interface/editor_components
        ${VITAL_ROOT}/src/interface/editor_sections
        ${VITAL_ROOT}/src/interface/look_and_feel
        ${VITAL_ROOT}/src/interface/wavetable
        ${VITAL_ROOT}/src/interface/wavetable/editors
        ${VITAL_ROOT}/src/interface/wavetable/overlays
        ${VITAL_ROOT}/src/standalone
        ${VITAL_ROOT}/src/synthesis/synth_engine
        ${VITAL_ROOT}/src/synthesis/effects
        ${VITAL_ROOT}/src/synthesis/filters
        ${VITAL_ROOT}/src/synthesis/framework
        ${VITAL_ROOT}/src/synthesis/lookups
        ${VITAL_ROOT}/src/synthesis/modulators
        ${VITAL_ROOT}/src/synthesis/modules
        ${VITAL_ROOT}/src/synthesis/producers
        ${VITAL_ROOT}/src/synthesis/utilities
        ${VITAL_ROOT}/third_party
    )
    target_link_libraries(vital_core PRIVATE pthread dl rt)
else()
    add_library(vital_core STATIC
        src/vital_core/vital_core.cpp
        src/vital_core/vital_core.h
    )
endif()

add_executable(GrooveBoxUI ${SOURCES} ${HEADERS})

target_link_libraries(GrooveBoxUI PRIVATE Qt6::Widgets Qt6::Multimedia dx7_core vital_core)

if(UNIX AND NOT APPLE)
    # Force ALSA usage on Linux; GrooveBox relies on ALSA for realtime audio + MIDI.
    target_link_libraries(GrooveBoxUI PRIVATE asound)
    target_compile_definitions(GrooveBoxUI PRIVATE GROOVEBOX_WITH_ALSA=1)
    if(JACK_FOUND)
        target_include_directories(GrooveBoxUI PRIVATE ${JACK_INCLUDE_DIRS})
        target_link_libraries(GrooveBoxUI PRIVATE ${JACK_LIBRARIES})
        target_compile_definitions(GrooveBoxUI PRIVATE GROOVEBOX_WITH_JACK=1)
    endif()
endif()
target_include_directories(GrooveBoxUI PRIVATE src)

# ALSA handled above for Linux
