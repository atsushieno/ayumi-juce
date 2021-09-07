/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include <math.h>
#include "cmidi2.h"
#include "PluginProcessor.h"
#include "PluginEditor.h"

#define AYUMI_LV2_MIDI_CC_ENVELOPE_H 0x10
#define AYUMI_LV2_MIDI_CC_ENVELOPE_M 0x11
#define AYUMI_LV2_MIDI_CC_ENVELOPE_L 0x12
#define AYUMI_LV2_MIDI_CC_ENVELOPE_SHAPE 0x13
#define AYUMI_LV2_MIDI_CC_DC 0x50

//==============================================================================
AyumiAudioProcessor::AyumiAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       )
#endif
{
}

AyumiAudioProcessor::~AyumiAudioProcessor()
{
}

//==============================================================================
const juce::String AyumiAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool AyumiAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool AyumiAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool AyumiAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double AyumiAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int AyumiAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int AyumiAudioProcessor::getCurrentProgram()
{
    return 0;
}

void AyumiAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String AyumiAudioProcessor::getProgramName (int index)
{
    return {};
}

void AyumiAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

//==============================================================================
void AyumiAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Use this method as the place to do any pre-playback
    // initialisation that you need..

    ayumi.active = false;
    ayumi.sample_rate = sampleRate;
    ayumi_configure(&ayumi.impl, 1, 2000000, (int) sampleRate);
    ayumi_set_noise(&ayumi.impl, 4); // pink noise by default
	for (int i = 0; i < 3; i++) {
		ayumi.mixer[i] = 1 << 6; // tone, without envelope
		ayumi_set_pan(&ayumi.impl, i, 0.5, 0); // 0(L)...1(R)
		ayumi_set_mixer(&ayumi.impl, i, 1, 1, 0); // should be quiet by default
		ayumi_set_envelope_shape(&ayumi.impl, 14); // see http://fmpdoc.fmp.jp/%E3%82%A8%E3%83%B3%E3%83%99%E3%83%AD%E3%83%BC%E3%83%97%E3%83%8F%E3%83%BC%E3%83%89%E3%82%A6%E3%82%A7%E3%82%A2/
		ayumi_set_envelope(&ayumi.impl, 0x40); // somewhat slow
		ayumi_set_volume(&ayumi.impl, i, 14); // FIXME: max = 14?? 15 doesn't work
	}
    ayumi.active = true;
}

void AyumiAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.

    ayumi.active = false;
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool AyumiAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

double key_to_freq(double key) {
    // We use equal temperament
    // https://pages.mtu.edu/~suits/NoteFreqCalcs.html
    double ret = 220.0 * pow(1.059463, key - 45.0);
    return ret;
}

void AyumiAudioProcessor::ayumi_process_midi_event(juce::MidiMessage &msg) {
    AyumiContext *a = &ayumi;
	int noise, tone_switch, noise_switch, env_switch;
	uint8_t * bytes = (uint8_t*) msg.getRawData();
	int channel = bytes[0] & 0xF;
	if (channel > 2)
		return;
	int mixer;
	switch (bytes[0] & 0xF0) {
    note_off:
	case CMIDI2_STATUS_NOTE_OFF:
		if (!a->note_on_state[channel])
			break; // not at note on state
		ayumi_set_mixer(&a->impl, channel, 1, 1, 0);
		a->note_on_state[channel] = false;
		break;
	case CMIDI2_STATUS_NOTE_ON:
		if (bytes[2] == 0)
			goto note_off; // it is illegal though.
		if (a->note_on_state[channel])
			break; // busy
		mixer = a->mixer[channel];
		tone_switch = (mixer >> 5) & 1;
		noise_switch = (mixer >> 6) & 1;
		env_switch = (mixer >> 7) & 1;
		ayumi_set_mixer(&a->impl, channel, tone_switch, noise_switch, env_switch);
		ayumi_set_tone(&a->impl, channel, 2000000.0 / (16.0 * key_to_freq(bytes[1])));
		a->note_on_state[channel] = true;
		break;
	case CMIDI2_STATUS_PROGRAM:
		noise = bytes[1] & 0x1F;
		ayumi_set_noise(&a->impl, noise);
		mixer = bytes[1];
		tone_switch = (mixer >> 5) & 1;
		noise_switch = (mixer >> 6) & 1;
		// We cannot pass 8 bit message, so we remove env_switch here. Use BankMSB for it.
		env_switch = (a->mixer[channel] >> 7) & 1;
		a->mixer[channel] = bytes[1];
		ayumi_set_mixer(&a->impl, channel, tone_switch, noise_switch, env_switch);
		break;
	case CMIDI2_STATUS_CC:
		switch (bytes[1]) {
		case CMIDI2_CC_BANK_SELECT:
			mixer = bytes[1];
			tone_switch = mixer & 1;
			noise_switch = (mixer >> 1) & 1;
			env_switch = (mixer >> 2) & 1;
			a->mixer[channel] = bytes[1];
			ayumi_set_mixer(&a->impl, channel, tone_switch, noise_switch, env_switch);
			break;
		case CMIDI2_CC_PAN:
			ayumi_set_pan(&a->impl, channel, bytes[2] / 128.0, 0);
			break;
		case CMIDI2_CC_VOLUME:
			ayumi_set_volume(&a->impl, channel, (bytes[2] > 119 ? 119 : bytes[2]) / 8); // FIXME: max is 14?? 15 doesn't work
			break;
		case AYUMI_LV2_MIDI_CC_ENVELOPE_H:
			a->envelope = (a->envelope & 0x3FFF) + (bytes[2] << 14);
			ayumi_set_envelope(&a->impl, a->envelope);
			break;
		case AYUMI_LV2_MIDI_CC_ENVELOPE_M:
			a->envelope = (a->envelope & 0xC07F) + (bytes[2] << 7);
			ayumi_set_envelope(&a->impl, a->envelope);
			break;
		case AYUMI_LV2_MIDI_CC_ENVELOPE_L:
			a->envelope = (a->envelope & 0xFF80) + bytes[2];
			ayumi_set_envelope(&a->impl, a->envelope);
			break;
		case AYUMI_LV2_MIDI_CC_ENVELOPE_SHAPE:
			ayumi_set_envelope_shape(&a->impl, bytes[2] & 0xF);
			break;
		case AYUMI_LV2_MIDI_CC_DC:
			ayumi_remove_dc(&a->impl);
			break;
		}
		break;
	case CMIDI2_STATUS_PITCH_BEND:
		a->pitchbend = (bytes[1] << 7) + bytes[2];
		break;
	default:
		break;
	}
}

void AyumiAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    auto *a = &ayumi;
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();
    auto sample_count = buffer.getNumSamples();

    // In case we have more outputs than inputs, this code clears any output
    // channels that didn't contain input data, (because these aren't
    // guaranteed to be empty - they may contain garbage).
    // This is here to avoid people getting screaming feedback
    // when they first compile a plugin, but obviously you don't need to keep
    // this code if your algorithm always overwrites all the output channels.
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    // This is the place where you'd normally do the guts of your plugin's
    // audio processing...
    // Make sure to reset the state if your inner loop is processing
    // the samples and the outer loop is handling the channels.
    // Alternatively, you can process the samples with the channels
    // interleaved by keeping the same state.
    for (int channel = 0; channel < totalNumInputChannels; ++channel)
    {
        auto* channelData = buffer.getWritePointer (channel);

        // ..do something to the data...
    }

	int currentFrame = 0;

	for(auto msgRef : midiMessages) {
        auto msg = msgRef.getMessage();
		if (msg.getTimeStamp() != 0) {
			int max = currentFrame + (int) msg.getTimeStamp();
			max = max < sample_count ? max : sample_count;
			for (int i = currentFrame; i < max; i++) {
				ayumi_process(&a->impl);
				ayumi_remove_dc(&a->impl);
                int nCh = totalNumOutputChannels - totalNumInputChannels;
                if (nCh > 0)
                    auto &ptr = buffer.getWritePointer(totalNumInputChannels)[i] = (float) ayumi.impl.left;
                if (nCh > 1)
                    auto &ptr = buffer.getWritePointer(totalNumInputChannels + 1)[i] = (float) ayumi.impl.right;
			}
			currentFrame = max;
		}
        ayumi_process_midi_event(msg);
	}

	for (int i = currentFrame; i < sample_count; i++) {
		ayumi_process(&a->impl);
		ayumi_remove_dc(&a->impl);
        int nCh = totalNumOutputChannels - totalNumInputChannels;
        if (nCh > 0)
            auto &ptr = buffer.getWritePointer(totalNumInputChannels)[i] = (float) ayumi.impl.left;
        if (nCh > 1)
            auto &ptr = buffer.getWritePointer(totalNumInputChannels + 1)[i] = (float) ayumi.impl.right;
	}
}

//==============================================================================
bool AyumiAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* AyumiAudioProcessor::createEditor()
{
    return new AyumiAudioProcessorEditor (*this);
}

//==============================================================================
void AyumiAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.
}

void AyumiAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AyumiAudioProcessor();
}
