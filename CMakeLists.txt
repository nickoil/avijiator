cmake_minimum_required(VERSION 3.22)

project(Avijiator VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)
set(CMAKE_CXX_EXTENSIONS OFF)

# Static MSVC runtime (/MT, /MTd for Debug) — single-exe distribution, no
# Visual C++ Redistributable required on the target machine. Set globally,
# before add_subdirectory(JUCE), so JUCE's own targets link consistently
# with ours rather than mixing /MD and /MT in the same build.
if (MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# JUCE is vendored as a git submodule pinned to a release tag — see
# libs/JUCE and .gitmodules. Do not `find_package(JUCE)` against a system
# install; this project builds against the pinned copy only.
add_subdirectory(libs/JUCE)

# Standalone GUI app target — no VST/AU wrapper, no DAW host required.
juce_add_gui_app(Avijiator
    PRODUCT_NAME "Avijiator"
    COMPANY_NAME "Nick Casey"
    BUNDLE_ID "com.nickcasey.avijiator"
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE)

# IBM Plex Sans, compiled in as binary data rather than relying on the
# typeface being installed on whatever machine runs the .exe — matches the
# static-runtime, single-exe, no-redistributable approach the rest of the
# build already uses. Two weights only: Regular (body/labels) and SemiBold
# (the header mark, matching documents/ui-mockup's font-weight:600) — no
# need for the family's other weights this UI doesn't use.
juce_add_binary_data(AvijiatorFonts
    SOURCES
        Source/UI/Fonts/IBMPlexSans-Regular.ttf
        Source/UI/Fonts/IBMPlexSans-SemiBold.ttf)

target_sources(Avijiator
    PRIVATE
        Source/Main.cpp
        Source/MainComponent.cpp
        Source/Arpeggiator.cpp
        Source/StepSequencer.cpp
        Source/NoteRouter.cpp
        Source/QwertyNoteInput.cpp
        Source/DSP/PolyBlepOscillator.cpp
        Source/DSP/SynthVoice.cpp
        Source/DSP/Vcf.cpp
        Source/DSP/Adsr.cpp
        Source/DSP/Lfo.cpp
        Source/DSP/NoteStack.cpp
        Source/UI/PanelLookAndFeel.cpp
        Source/UI/PanelSection.cpp
        Source/UI/SynthPanel.cpp)

# Quoted includes already resolve relative to the including file, so this is
# convenience, not a requirement. Keep writing includes in relative form
# anyway - the Stage B .jucer exporter sets its own header search paths
# (CLAUDE.md), and relative includes port without touching it.
target_include_directories(Avijiator PRIVATE Source)

target_compile_definitions(Avijiator
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0

        # ASIO gives far lower latency than WASAPI shared mode, which is what
        # JUCE otherwise defaults to on Windows. JUCE 9 bundles the three
        # Steinberg interface headers it needs (modules/juce_audio_devices/
        # native/asio), so no SDK download is required - older JUCE versions
        # did need one.
        #
        # LICENCE NOTE: those headers are dual-licensed, proprietary Steinberg
        # ASIO Licence OR GPLv3. Fine either way for private use. Distributing
        # a build means either accepting GPLv3 for the whole app, or obtaining
        # a signed agreement from Steinberg. See that folder's LICENSE.txt.
        #
        # Windows-only: Stage B's Android target uses AAudio and must not see
        # this.
        $<$<PLATFORM_ID:Windows>:JUCE_ASIO=1>
        JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Avijiator,JUCE_PRODUCT_NAME>"
        JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:Avijiator,JUCE_VERSION>")

# Module set: audio basics/devices/formats/utils for the standalone audio
# app + MIDI input, dsp for filter/oscillator building blocks, gui_basics/
# gui_extra for the control UI (item 6). Their own dependencies (core,
# events, graphics, ...) are pulled in transitively.
target_link_libraries(Avijiator
    PRIVATE
        AvijiatorFonts
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

