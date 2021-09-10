/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "ayumi.h"

#define AYUMI_JUCE_STATE_MAGIC_NUMBER 37564

//==============================================================================
/**
*/
class AyumiAudioProcessor  : public juce::AudioProcessor, private juce::AudioProcessorListener
{
public:
    //==============================================================================
    AyumiAudioProcessor();
    ~AyumiAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

private:
    typedef struct {
        int magic_number{0}; // if it is loaded from state or initialized, then it is set.

        // ayumi parameters (saved in state)
        int32_t clock_rate{2000000};
        int32_t envelope{0x40}; // somewhat slow
        // see http://fmpdoc.fmp.jp/%E3%82%A8%E3%83%B3%E3%83%99%E3%83%AD%E3%83%BC%E3%83%97%E3%83%8F%E3%83%BC%E3%83%89%E3%82%A6%E3%82%A7%E3%82%A2/
        int32_t envelope_shape{14};
        int32_t noise_freq{0};
        // per-slot parameters.
        int volume[3]{14, 14, 14};
        float pan[3]{0.5, 0.5, 0.5};
        // 0-5bits: unused (noise freq), 6:tone_off 7:noise_off 8:env_on
        // (note the `_on` and `_off` difference, off=1 means "disabled")
        int mixer[3]{2, 2, 2};

        inline void reset() {
            magic_number = AYUMI_JUCE_STATE_MAGIC_NUMBER;
            mixer[0] = mixer[1] = mixer[2] = 2;
            volume[0] = volume[1] = volume[2] = 14;
            pan[0] = pan[1] = pan[2] = 0.5;
            envelope = 0x40;
            envelope_shape = 14;
            noise_freq = 0;
            clock_rate = 2000000;
        }
    } AyumiState;

    typedef struct {
        struct ayumi impl{};
        AyumiState  state{};
        // non-persistent states
        int32_t sample_rate{44100}; // stored for reconfiguration
        bool active{false};
        int32_t pitchbend{0};
        bool note_on_state[3]{false, false, false};

        inline void reset() {
            state.reset();
            sample_rate = 44100;
            active = false;
            pitchbend = 0;
            note_on_state[0] = note_on_state[1] = note_on_state[2] = false;
        }
    } AyumiContext;

    AyumiContext ayumi;
    juce::NormalisableRange<float> mixerRange{0.0f, 8.0f, 1.0f};
    juce::NormalisableRange<float> volumeRange{0.0f, 14.0f, 1.0f}; // FIXME: max = 14?? 15 doesn't work
    juce::NormalisableRange<float> panRange{0.0f, 1.0f};
    juce::NormalisableRange<float> envelopeRange{0.0f, 65535.0f, 1.0f, 0.2f};
    juce::NormalisableRange<float> envelopeShapeRange{0.0f, 15.0f, 1.0f};
    juce::NormalisableRange<float> noiseFreqRange{0.0f, 31.0f, 1.0f};
    juce::NormalisableRange<float> clockRange{0.0f, 16777215.0f, 1.0f, 0.2f}; // does this range make sense?

    void ayumi_process_midi_event(juce::MidiMessage &msg);
    void audioProcessorParameterChanged(AudioProcessor *processor, int parameterIndex, float newValue) override;
    void audioProcessorChanged(AudioProcessor *processor, const AudioProcessor::ChangeDetails &details) override;

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AyumiAudioProcessor)
};
