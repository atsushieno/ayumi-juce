/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

//==============================================================================
/**
*/
class AyumiAudioProcessorEditor  : public juce::AudioProcessorEditor
{
public:
    AyumiAudioProcessorEditor (AyumiAudioProcessor&);
    ~AyumiAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    AyumiAudioProcessor& audioProcessor;
    std::unique_ptr<juce::Drawable> svgimg;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AyumiAudioProcessorEditor)
};
