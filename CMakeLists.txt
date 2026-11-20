cmake_minimum_required(VERSION 3.22)

project(FluxFilter VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)

add_subdirectory(/Users/tilo_hk/Documents/Development/AudioDevResources/Libraries/JUCE JUCE_build)

juce_add_plugin(FluxFilter
        PRODUCT_NAME "flux_filter"     # shown in the DAW and used for the bundle file names
        COMPANY_NAME "Tilo Hauk"
        BUNDLE_ID com.tilohauk.fluxfilter
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        COPY_PLUGIN_AFTER_BUILD TRUE

        PLUGIN_MANUFACTURER_CODE Tlhk
        PLUGIN_CODE Fltf

        FORMATS VST3 AU Standalone
)

target_sources(FluxFilter PRIVATE
        src/PluginProcessor.cpp
        src/PluginProcessor.h
        src/PluginEditor.cpp
        src/PluginEditor.h
        src/dsp/FluxCell.h
        src/dsp/FluxField.h
        src/dsp/FluxLfo.h
        src/dsp/FluxPropagator.h
        src/params/Parameters.h
        src/params/ParamSpec.h
        src/mod/CurveShape.h
        src/mod/ModSources.h
        src/mod/ModMatrix.h
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
# Offline DSP tests (plain C++, no JUCE)
add_executable(FluxFieldTest tests/FieldTest.cpp)
target_include_directories(FluxFieldTest PRIVATE src)
target_compile_features(FluxFieldTest PRIVATE cxx_std_20)

add_executable(FluxModTest tests/ModTest.cpp)
target_include_directories(FluxModTest PRIVATE src)
target_compile_features(FluxModTest PRIVATE cxx_std_20)
