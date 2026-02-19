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
    src/third_party/kissfft/kiss_fft.c
    src/third_party/kissfft/kiss_fftr.c
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
    src/third_party/kissfft/kiss_fft.h
    src/third_party/kissfft/kiss_fftr.h
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

add_executable(GrooveBoxUI ${SOURCES} ${HEADERS})

target_link_libraries(GrooveBoxUI PRIVATE Qt6::Widgets Qt6::Multimedia dx7_core)

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
target_include_directories(GrooveBoxUI PRIVATE src src/third_party/kissfft)

# ALSA handled above for Linux
