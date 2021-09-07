if(DEFINED ENV{AAX_SDK_PATH})
    set(AAX_SDK_PATH $ENV{AAX_SDK_PATH})
endif()
if(DEFINED AAX_SDK_PATH)
    set(AAX_BUILD_FLAG AAX)
    file(TO_CMAKE_PATH "${AAX_SDK_PATH}" AAX_SDK_CMAKE_PATH)
    message(STATUS "AAX Build is enabled. AAX SDK = " ${AAX_SDK_CMAKE_PATH})
    juce_set_aax_sdk_path(${AAX_SDK_CMAKE_PATH})
else()
    message(STATUS "AAX Build is disabled. To enable, set AAX_SDK_PATH to your environment variable or specify -DAAX_SDK_PATH to CMake configure option.")
endif()

juce_add_plugin(ayumi-juce
    # VERSION ...                                     # Set this if the plugin version is different to the project version
    # ICON_BIG ""   # ICON_* arguments specify a path to an image file to use as an icon for the Standalone
    #   ICON_SMALL "${CMAKE_SOURCE_DIR}/Assets/icon16.png " 
    COMPANY_NAME "atsushieno"                    # Specify the name of the plugin's author
    COMPANY_COPYRIGHT "atsushieno"
    COMPANY_WEBSITE "https://github.com/atsushieno"
    COMPANY_EMAIL ""
    IS_SYNTH TRUE                       # Is this a synth or an effect?
    NEEDS_MIDI_INPUT TRUE               # Does the plugin need midi input?
    NEEDS_MIDI_OUTPUT FALSE              # Does the plugin need midi output?
    IS_MIDI_EFFECT FALSE                 # Is this plugin a MIDI effect?
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE    # Does the editor need keyboard focus?
    COPY_PLUGIN_AFTER_BUILD TRUE        # Should the plugin be installed to a default location after building?
    #PLUGIN_MANUFACTURER_CODE CCCC               # A four-character manufacturer id with at least one upper-case character
    #PLUGIN_CODE XXXX                            # A unique four-character plugin id with at least one upper-case character
    FORMATS 
    # The formats to build. Other valid formats are: AAX Unity VST AU AUv3
            Standalone 
            AU
            # AUv3 
            VST3 
            # Unity 
            ${AAX_BUILD_FLAG}               
    VST3_CATEGORIES "Synth" 
    AU_MAIN_TYPE "kAudioUnitType_Generator"
    # AU_SANDBOX_SAFE TRUE
    # AAX_CATEGORY ""
    # HARDENED_RUNTIME_ENABLED # macOS app settings
    # HARDENED_RUNTIME_OPTIONS
    # APP_SANDBOX_ENABLED
    # APP_SANDBOX_INHERIT
    # DESCRIPTION ""
    # MICROPHONE_PERMISSION_ENABLED TRUE
    # MICROPHONE_PERMISSION_TEXT "This applicaion requires a permission to use an audio input device of your computer. By Default, Built-In microphone will be used."

    PRODUCT_NAME "ayumi-juce")        # The name of the final 

target_compile_features(ayumi-juce PUBLIC cxx_std_17)


target_compile_definitions(ayumi-juce
    PUBLIC
    # JUCE_WEB_BROWSER and JUCE_USE_CURL would be on by default, but you might not need them.
    JUCE_WEB_BROWSER=0  # If you remove this, add `NEEDS_WEB_BROWSER TRUE` to the `juce_add_plugin` call
    JUCE_USE_CURL=0     # If you remove this, add `NEEDS_CURL TRUE` to the `juce_add_plugin` call
    JUCE_VST3_CAN_REPLACE_VST2=0
    # JUCE_DISPLAY_SPLASH_SCREEN=0 #if your plugin is distributed with GPL license or paid
)

target_sources(ayumi-juce PRIVATE
    PluginEditor.cpp
    PluginProcessor.cpp
    ayumi.cpp # renamed from ayumi.c
)

target_link_libraries(ayumi-juce PUBLIC
juce::juce_audio_basics
juce::juce_audio_devices
juce::juce_audio_formats
juce::juce_audio_plugin_client
juce::juce_audio_processors
juce::juce_audio_utils
juce::juce_core
juce::juce_data_structures
juce::juce_dsp
juce::juce_events
juce::juce_graphics
juce::juce_gui_basics
juce::juce_gui_extra
)

juce_generate_juce_header(ayumi-juce)
