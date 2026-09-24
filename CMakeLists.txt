cmake_minimum_required(VERSION 3.22)
project(PrismRack VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# JUCE: use libs/JUCE if present, otherwise download JUCE 8.0.4 automatically.
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libs/JUCE/CMakeLists.txt")
    add_subdirectory(libs/JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

option(PRISM_BUILD_TESTS "Build the offline render test app" ON)

juce_add_plugin(PrismRack
    COMPANY_NAME "Chris Gilling"
    PLUGIN_MANUFACTURER_CODE Cgil
    PLUGIN_CODE Prsm
    FORMATS VST3 Standalone
    PRODUCT_NAME "Prism Rack"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx
)

set(PRISM_SOURCES
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
)

target_sources(PrismRack PRIVATE ${PRISM_SOURCES})
target_include_directories(PrismRack PRIVATE src)

set(PRISM_DEFS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_compile_definitions(PrismRack PUBLIC ${PRISM_DEFS})

set(PRISM_LIBS
    juce::juce_audio_utils
    juce::juce_dsp
)

target_link_libraries(PrismRack
    PRIVATE ${PRISM_LIBS}
    PUBLIC juce::juce_recommended_config_flags
           juce::juce_recommended_lto_flags
           juce::juce_recommended_warning_flags)

if (PRISM_BUILD_TESTS)
    juce_add_console_app(PrismRenderTest PRODUCT_NAME "PrismRenderTest")
    target_sources(PrismRenderTest PRIVATE tests/RenderTest.cpp ${PRISM_SOURCES})
    target_include_directories(PrismRenderTest PRIVATE src)
    target_compile_definitions(PrismRenderTest PRIVATE ${PRISM_DEFS} JUCE_STANDALONE_APPLICATION=0 PRISM_TEST_BUILD=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(PrismRenderTest PRIVATE ${PRISM_LIBS}
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()
