cmake_minimum_required(VERSION 3.22)
project(StemSplitAI VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "")

# ---- Options ---------------------------------------------------------------
option(STEMSPLIT_ONNX "Enable ONNX Runtime backend (real AI stem model)" OFF)
set(ONNXRUNTIME_ROOT "" CACHE PATH "Folder of a prebuilt onnxruntime release (contains include/ and lib/)")

# ---- JUCE ------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.6
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(StemSplit
    PRODUCT_NAME                "StemSplit AI"
    COMPANY_NAME                "StemSplit"
    PLUGIN_MANUFACTURER_CODE    Ssmf
    PLUGIN_CODE                 Ssp1
    FORMATS                     VST3 Standalone
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    VST3_CATEGORIES             Fx Analyzer
    NEEDS_CURL                  FALSE
    NEEDS_WEB_BROWSER           FALSE
    MICROPHONE_PERMISSION_ENABLED TRUE
    COPY_PLUGIN_AFTER_BUILD     FALSE)

target_sources(StemSplit PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/StemSeparator.cpp
    Source/DspSeparator.cpp
    Source/OnnxSeparator.cpp)

target_compile_definitions(StemSplit PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

target_link_libraries(StemSplit
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)

# Thai text in string literals
target_compile_options(StemSplit PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

# ---- ONNX Runtime (optional) ----------------------------------------------
if(STEMSPLIT_ONNX)
    if(NOT ONNXRUNTIME_ROOT)
        message(FATAL_ERROR "Set -DONNXRUNTIME_ROOT=<path to onnxruntime release>")
    endif()
    find_library(ORT_LIB NAMES onnxruntime PATHS "${ONNXRUNTIME_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
    target_include_directories(StemSplit PRIVATE "${ONNXRUNTIME_ROOT}/include")
    target_link_libraries(StemSplit PRIVATE "${ORT_LIB}")
    target_compile_definitions(StemSplit PUBLIC STEMSPLIT_ONNX=1)

    file(GLOB ORT_RUNTIME_FILES
        "${ONNXRUNTIME_ROOT}/lib/*onnxruntime*.dll"
        "${ONNXRUNTIME_ROOT}/lib/*onnxruntime*.dylib"
        "${ONNXRUNTIME_ROOT}/lib/*onnxruntime*.so*")
    foreach(t StemSplit_Standalone StemSplit_VST3)
        if(TARGET ${t})
            foreach(f ${ORT_RUNTIME_FILES})
                add_custom_command(TARGET ${t} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${f}" "$<TARGET_FILE_DIR:${t}>")
            endforeach()
            if(APPLE)
                set_target_properties(${t} PROPERTIES BUILD_RPATH "@loader_path" INSTALL_RPATH "@loader_path")
            elseif(UNIX)
                set_target_properties(${t} PROPERTIES BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN")
            endif()
        endif()
    endforeach()
else()
    target_compile_definitions(StemSplit PUBLIC STEMSPLIT_ONNX=0)
endif()
