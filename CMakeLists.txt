cmake_minimum_required(VERSION 3.22)

# macOS: one universal binary that runs from 10.13 on (set before project()).
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
# Windows: link the C++ runtime statically, so the plugin needs no Visual C++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# The one place the version lives: plugin metadata, bundle versions and the "vX.Y.Z" in the header.
project(FluxFilter VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)

# --- JUCE ---
# Point FLUX_JUCE_DIR (or the JUCE_DIR environment variable) at a local JUCE checkout;
# without one, the pinned JUCE release is downloaded at configure time.
set(FLUX_JUCE_VERSION "8.0.14")
set(FLUX_JUCE_DIR "$ENV{JUCE_DIR}" CACHE PATH "Local JUCE checkout (empty = download JUCE ${FLUX_JUCE_VERSION})")
if (FLUX_JUCE_DIR AND EXISTS "${FLUX_JUCE_DIR}/CMakeLists.txt")
    add_subdirectory("${FLUX_JUCE_DIR}" JUCE_build)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG        ${FLUX_JUCE_VERSION}
            GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# Copying into the system plug-in folders is handy while developing; CI turns it off
# (on Windows it would need admin rights).
option(FLUX_COPY_PLUGIN "Copy the plug-ins into the system plug-in folders after building" ON)

set(FLUX_FORMATS VST3 Standalone)
if (APPLE)
    list(APPEND FLUX_FORMATS AU)
elseif (UNIX)
    list(APPEND FLUX_FORMATS LV2)
endif()

juce_add_plugin(FluxFilter
        PRODUCT_NAME "flux_filter"     # shown in the DAW and used for the bundle file names
        COMPANY_NAME "Tilo Hauk"
        BUNDLE_ID com.tilohauk.fluxfilter
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        COPY_PLUGIN_AFTER_BUILD ${FLUX_COPY_PLUGIN}
        VST3_CATEGORIES Fx Filter
        AU_MAIN_TYPE kAudioUnitType_Effect
        LV2URI https://github.com/tilohk/flux_filter

        PLUGIN_MANUFACTURER_CODE Tlhk
        PLUGIN_CODE Fltf

        FORMATS ${FLUX_FORMATS}
)

target_sources(FluxFilter PRIVATE
        src/PluginProcessor.cpp
        src/PluginProcessor.h
        src/PluginEditor.cpp
        src/PluginEditor.h
        src/dsp/FluxCell.h
        src/dsp/FormantTable.h
        src/dsp/FluxChaos.h
        src/dsp/FluxField.h
        src/dsp/FluxAbsorber.h
        src/params/Parameters.h
        src/params/ParamSpec.h
        src/mod/ModSources.h
        src/mod/ModMatrix.h
        src/ui/FluxLookAndFeel.h
        src/ui/ParamControl.h
        src/ui/FieldView.h
        src/ui/ModContext.h
        src/ui/SourceModules.h
        src/ui/Sections.h
        src/ui/PresetBar.h
        src/presets/PresetManager.h
)

target_include_directories(FluxFilter PRIVATE src)

target_compile_definitions(FluxFilter PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_compile_features(FluxFilter PRIVATE cxx_std_20)

target_link_libraries(FluxFilter
        PRIVATE
            juce::juce_audio_utils
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags
)

# --- Tests ---
enable_testing()

# Offline DSP tests (plain C++, no JUCE)
foreach (t FluxFieldTest:FieldTest FluxModTest:ModTest FluxBenchmark:Benchmark)
    string(REPLACE ":" ";" t ${t})
    list(GET t 0 target)
    list(GET t 1 source)
    add_executable(${target} tests/${source}.cpp)
    target_include_directories(${target} PRIVATE src)
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_compile_definitions(${target} PRIVATE $<$<CXX_COMPILER_ID:MSVC>:_USE_MATH_DEFINES>)   # M_PI
endforeach()
add_test(NAME FieldTest COMMAND FluxFieldTest)
add_test(NAME ModTest COMMAND FluxModTest)

# CPU benchmark: build it optimised for meaningful numbers.
target_compile_options(FluxBenchmark PRIVATE $<IF:$<CXX_COMPILER_ID:MSVC>,/O2,-O2>)

# Every factory preset through the real plugin (needs JUCE): finite, limited, not silent, not too loud.
juce_add_console_app(FluxPresetCheck PRODUCT_NAME "FluxPresetCheck")
target_sources(FluxPresetCheck PRIVATE tests/PresetCheck.cpp)
target_include_directories(FluxPresetCheck PRIVATE src)
target_compile_definitions(FluxPresetCheck PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(FluxPresetCheck PRIVATE FluxFilter juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
target_compile_options(FluxPresetCheck PRIVATE $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wno-macro-redefined>)   # console app vs. plugin defaults for JUCE_STANDALONE_APPLICATION
set_target_properties(FluxPresetCheck PROPERTIES EXCLUDE_FROM_ALL TRUE)   # builds JUCE a second time: only on request
add_test(NAME PresetCheck COMMAND FluxPresetCheck)
