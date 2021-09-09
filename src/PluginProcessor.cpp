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

#define AYUMI_PARAMETER_MIXER_0_INDEX 0
#define AYUMI_PARAMETER_MIXER_1_INDEX 1
#define AYUMI_PARAMETER_MIXER_2_INDEX 2
#define AYUMI_PARAMETER_VOLUME_0_INDEX 3
#define AYUMI_PARAMETER_VOLUME_1_INDEX 4
#define AYUMI_PARAMETER_VOLUME_2_INDEX 5
#define AYUMI_PARAMETER_PAN_0_INDEX 6
#define AYUMI_PARAMETER_PAN_1_INDEX 7
#define AYUMI_PARAMETER_PAN_2_INDEX 8
#define AYUMI_PARAMETER_ENVELOPE_INDEX 9
#define AYUMI_PARAMETER_ENVELOPE_SHAPE_INDEX 10
#define AYUMI_PARAMETER_NOISE_INDEX 11
#define AYUMI_PARAMETER_CLOCK_RATE_INDEX 12

//==============================================================================

juce::AudioParameterFloat* createParameter(const char* nameBase, int i, juce::NormalisableRange<float>& range, float def)
{
    auto name = juce::String::formatted("%s %d", nameBase, i);
    return new juce::AudioParameterFloat(name, name, range, def);
}

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
    for (int i = 0; i < 3; i++)
        addParameter(createParameter("Mixer", i, mixerRange, 2.0f));
    for (int i = 0; i < 3; i++)
        addParameter(createParameter("Volume", i, volumeRange, 14.0f));
    for (int i = 0; i < 3; i++)
        addParameter(createParameter("Pan", i, panRange, 0.5f));
    addParameter(new juce::AudioParameterFloat("Envelope", "Envelope", envelopeRange, 64.0f));
    addParameter(new juce::AudioParameterFloat("EnvelopeShape", "EnvelopeShape", envelopeShapeRange, 14.0f));
    addParameter(new juce::AudioParameterFloat("NoiseFrequency", "NoiseFrequency", noiseFreqRange, 0.0f));
    // FIXME: enable once this got to make more sense.
    //addParameter(new juce::AudioParameterFloat("ClockRate", "ClockRate", clockRange, 2000000.0f));

    addListener(this);
}

AyumiAudioProcessor::~AyumiAudioProcessor() = default;

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
    if (ayumi.state.magic_number != AYUMI_JUCE_STATE_MAGIC_NUMBER)
        ayumi.reset();

    ayumi.active = false;
    ayumi.sample_rate = (int) sampleRate;
    ayumi_configure(&ayumi.impl, 1, ayumi.state.clock_rate, ayumi.sample_rate);
    ayumi_set_noise(&ayumi.impl, ayumi.state.noise_freq); // pink noise by default

	for (int i = 0; i < 3; i++) {
		ayumi_set_pan(&ayumi.impl, i, ayumi.state.pan[i], 0); // 0(L)...1(R)
		ayumi_set_mixer(&ayumi.impl, i, 1, 1, 0); // should be quiet by default
		ayumi_set_volume(&ayumi.impl, i, ayumi.state.volume[i]);
	}
    ayumi_set_envelope_shape(&ayumi.impl, ayumi.state.envelope_shape);
    ayumi_set_envelope(&ayumi.impl, ayumi.state.envelope);
    ayumi.active = true;
}

void AyumiAudioProcessor::releaseResources()
{
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
		mixer = a->state.mixer[channel];
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
		env_switch = (a->state.mixer[channel] >> 7) & 1;
		a->state.mixer[channel] = bytes[1];
		ayumi_set_mixer(&a->impl, channel, tone_switch, noise_switch, env_switch);
		break;
	case CMIDI2_STATUS_CC:
		switch (bytes[1]) {
		case CMIDI2_CC_BANK_SELECT:
			mixer = bytes[1];
			tone_switch = mixer & 1;
			noise_switch = (mixer >> 1) & 1;
			env_switch = (mixer >> 2) & 1;
			a->state.mixer[channel] = bytes[1];
			ayumi_set_mixer(&a->impl, channel, tone_switch, noise_switch, env_switch);
			break;
		case CMIDI2_CC_PAN:
            a->state.pan[channel] = (float) bytes[2] / 128.0f;
			ayumi_set_pan(&a->impl, channel, a->state.pan[channel], 0);
			break;
		case CMIDI2_CC_VOLUME:
            a->state.volume[channel] = (bytes[2] > 119 ? 119 : bytes[2]) / 8;
			ayumi_set_volume(&a->impl, channel, a->state.volume[channel]); // FIXME: max is 14?? 15 doesn't work
			break;
		case AYUMI_LV2_MIDI_CC_ENVELOPE_H:
			a->state.envelope = (a->state.envelope & 0x3FFF) + (bytes[2] << 14);
			ayumi_set_envelope(&a->impl, a->state.envelope);
			break;
		case AYUMI_LV2_MIDI_CC_ENVELOPE_M:
			a->state.envelope = (a->state.envelope & 0xC07F) + (bytes[2] << 7);
			ayumi_set_envelope(&a->impl, a->state.envelope);
			break;
		case AYUMI_LV2_MIDI_CC_ENVELOPE_L:
			a->state.envelope = (a->state.envelope & 0xFF80) + bytes[2];
			ayumi_set_envelope(&a->impl, a->state.envelope);
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

    // If we support release envelope this optimization will have to change
    if (ayumi.active && (ayumi.note_on_state[0] || ayumi.note_on_state[1] || ayumi.note_on_state[2])) {
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
}

//==============================================================================
bool AyumiAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* AyumiAudioProcessor::createEditor()
{
    return new juce::GenericAudioProcessorEditor (*this);
}

//==============================================================================
void AyumiAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::MemoryOutputStream stream{destData, true};

    for (int i = 0; i < 3; i++)
        stream.writeInt(ayumi.state.mixer[i]);
    for (int i = 0; i < 3; i++)
        stream.writeInt(ayumi.state.volume[i]);
    for (int i = 0; i < 3; i++)
        stream.writeFloat(ayumi.state.pan[i]);
    stream.writeInt(ayumi.state.envelope);
    stream.writeInt(ayumi.state.envelope_shape);
    stream.writeInt(ayumi.state.noise_freq);
    stream.writeInt(ayumi.state.clock_rate);
    stream.flush();
}

void AyumiAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (sizeInBytes < 52)
        return; // insufficient space
    juce::MemoryInputStream stream{data, (size_t) sizeInBytes, true};

    for (int i = 0; i < 3; i++)
        ayumi.state.mixer[i] = stream.readInt();
    for (int i = 0; i < 3; i++)
        ayumi.state.volume[i] = stream.readInt();
    for (int i = 0; i < 3; i++)
        ayumi.state.pan[i] = stream.readFloat();
    ayumi.state.envelope = stream.readInt();
    ayumi.state.envelope_shape = stream.readInt();
    ayumi.state.noise_freq = stream.readInt();
    ayumi.state.clock_rate = stream.readInt();

    ayumi.state.magic_number = AYUMI_JUCE_STATE_MAGIC_NUMBER;
}

void AyumiAudioProcessor::audioProcessorParameterChanged(juce::AudioProcessor *processor, int parameterIndex,
                                                         float newValue) {
    if (parameterIndex <= AYUMI_PARAMETER_MIXER_2_INDEX) {
        ayumi.state.mixer[parameterIndex % 3] = ((int) mixerRange.convertFrom0to1(newValue)) << 5;
    } else if (parameterIndex <= AYUMI_PARAMETER_VOLUME_2_INDEX) {
        auto vol = (int) volumeRange.convertFrom0to1(newValue);
        ayumi.state.volume[parameterIndex % 3] = vol;
        ayumi_set_volume(&ayumi.impl, parameterIndex % 3, vol);
    } else if (parameterIndex <= AYUMI_PARAMETER_PAN_2_INDEX) {
        ayumi.state.pan[parameterIndex % 3] = newValue;
        ayumi_set_pan(&ayumi.impl, parameterIndex % 3, newValue, false);
    } else {
        int env, shape, noise, clock;
        switch(parameterIndex) {
            case AYUMI_PARAMETER_ENVELOPE_INDEX:
                env = (int) envelopeRange.convertFrom0to1(newValue);
                ayumi.state.envelope = env;
                ayumi_set_envelope(&ayumi.impl, env);
                break;
            case AYUMI_PARAMETER_ENVELOPE_SHAPE_INDEX:
                shape = (int) envelopeShapeRange.convertFrom0to1(newValue);
                ayumi.state.envelope_shape = shape;
                ayumi_set_envelope_shape(&ayumi.impl, shape);
                break;
            case AYUMI_PARAMETER_NOISE_INDEX:
                noise = (int) noiseFreqRange.convertFrom0to1(newValue);
                ayumi.state.noise_freq = noise;
                ayumi_set_noise(&ayumi.impl, noise);
                break;
            case AYUMI_PARAMETER_CLOCK_RATE_INDEX:
                clock = (int) clockRange.convertFrom0to1(newValue);
                ayumi.state.clock_rate = clock;
                ayumi_configure(&ayumi.impl, 1, clock, ayumi.sample_rate);
            default: break;
        }
    }
}

void AyumiAudioProcessor::audioProcessorChanged (juce::AudioProcessor *processor, const juce::AudioProcessor::ChangeDetails &details)
{
    // what can be done here...?
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AyumiAudioProcessor();
}
