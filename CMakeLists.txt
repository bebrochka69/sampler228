cmake_minimum_required(VERSION 3.16)
project(GrooveBoxUI LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Multimedia)

set(SOURCES
    src/main.cpp
    src/MainWindow.cpp
    src/FramebufferCleaner.cpp
    src/SampleSession.cpp
    src/ui/TopToolbarWidget.cpp
    src/ui/BpmArcWidget.cpp
    src/ui/SamplePageWidget.cpp
    src/ui/EditPageWidget.cpp
    src/ui/SeqPageWidget.cpp
    src/ui/SimplePageWidget.cpp
    src/ui/SystemStats.cpp
    src/ui/SampleBrowserModel.cpp
    src/ui/WaveformRenderer.cpp
)

set(HEADERS
    src/MainWindow.h
    src/FramebufferCleaner.h
    src/SampleSession.h
    src/Theme.h
    src/ui/TopToolbarWidget.h
    src/ui/BpmArcWidget.h
    src/ui/SamplePageWidget.h
    src/ui/EditPageWidget.h
    src/ui/SeqPageWidget.h
    src/ui/SimplePageWidget.h
    src/ui/SystemStats.h
    src/ui/SampleBrowserModel.h
    src/ui/WaveformRenderer.h
)

add_executable(GrooveBoxUI ${SOURCES} ${HEADERS})

target_link_libraries(GrooveBoxUI PRIVATE Qt6::Widgets Qt6::Multimedia)
target_include_directories(GrooveBoxUI PRIVATE src)
