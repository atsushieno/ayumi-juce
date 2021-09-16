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
// FIXME: make use of them (but we first need to determine how 0-127 falls to represent "seconds").
#define AYUMI_LV2_MIDI_CC_SOFTENV_NUM_PARAMS_INDEX 0x20
#define AYUMI_LV2_MIDI_CC_SOFTENV_STOP_0_AT_INDEX 0x21
#define AYUMI_LV2_MIDI_CC_SOFTENV_STOP_0_VRATE_INDEX 0x22
#define AYUMI_LV2_MIDI_CC_SOFTENV_STOP_1_AT_INDEX 0x23
#define AYUMI_LV2_MIDI_CC_SOFTENV_STOP_1_VRATE_INDEX 0x24
// ...(contd)...
#define AYUMI_LV2_MIDI_CC_SOFTENV_STOP_5_AT_INDEX 0x2B
#define AYUMI_LV2_MIDI_CC_SOFTENV_STOP_5_VRATE_INDEX 0x2C
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
#define AYUMI_PARAMETER_SOFTENV_0_NUM_POINTS 13 // start
#define AYUMI_PARAMETER_SOFTENV_0_POINT_0_CLOCK 14
#define AYUMI_PARAMETER_SOFTENV_0_POINT_0_RATIO 15
#define AYUMI_PARAMETER_SOFTENV_0_POINT_5_CLOCK 24
#define AYUMI_PARAMETER_SOFTENV_0_POINT_5_RATIO 25
#define AYUMI_PARAMETER_SOFTENV_1_NUM_POINTS 26
#define AYUMI_PARAMETER_SOFTENV_1_POINT_0_CLOCK 27
#define AYUMI_PARAMETER_SOFTENV_1_POINT_0_RATIO 28
#define AYUMI_PARAMETER_SOFTENV_2_NUM_POINTS 39
#define AYUMI_PARAMETER_SOFTENV_2_POINT_0_CLOCK 40
#define AYUMI_PARAMETER_SOFTENV_2_POINT_0_RATIO 41
#define AYUMI_PARAMETER_SOFTENV_2_POINT_5_RATIO 51 // end
#define AYUMI_NUM_PARAMETERS 52

//==============================================================================

juce::AudioParameterFloat* createParameter(juce::String nameBase, int i, juce::NormalisableRange<float>& range, float def)
{
    auto name = juce::String::formatted("%s %d", nameBase.toRawUTF8(), i);
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
    addParameter(new juce::AudioParameterFloat("<reserved>", "<reserved>", 0.0f, 1.0f, 0.0f));

    // software envelope
    for (int i = 0; i < 3; i++) {
        addParameter(createParameter("SoftEnv NumStops ch:", i, softwareEnvelopeNumStopsRange, 2.0f));
        for (int p = 0; p < 6; p++) {
            auto clockName = juce::String::formatted("SoftEnv At: %d ch:", p);
            auto ratioName = juce::String::formatted("SoftEnv vol.rate: %d ch:", p);
            addParameter(createParameter(clockName, i, softwareEnvelopeStopSecondsRange,
                                         softwareEnvelopeStopSecondsRange.convertTo0to1(stopsDefault[p].stopAt)));
            addParameter(createParameter(ratioName, i, softwareEnvelopeStopRatioRange,
                                         softwareEnvelopeStopRatioRange.convertTo0to1(stopsDefault[p].volumeRatio)));
        }
    }

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

void AyumiAudioProcessor::setParametersFromState() {
    auto pl = getParameterTree().getParameters(false);
    for (int i = 0; i < 3; i++) {
        pl[AYUMI_PARAMETER_MIXER_0_INDEX + i]->setValue(mixerRange.convertTo0to1(ayumi.state.mixer[i]));
        pl[AYUMI_PARAMETER_VOLUME_0_INDEX + i]->setValue(volumeRange.convertTo0to1(ayumi.state.volume[i]));
        pl[AYUMI_PARAMETER_PAN_0_INDEX + i]->setValue(panRange.convertTo0to1(ayumi.state.pan[i]));
    }
    pl[AYUMI_PARAMETER_ENVELOPE_INDEX]->setValue(envelopeRange.convertTo0to1(ayumi.state.envelope));
    pl[AYUMI_PARAMETER_ENVELOPE_SHAPE_INDEX]->setValue(envelopeShapeRange.convertTo0to1(ayumi.state.envelope_shape));
    pl[AYUMI_PARAMETER_NOISE_INDEX]->setValue(noiseFreqRange.convertTo0to1(ayumi.state.noise_freq));
    pl[AYUMI_PARAMETER_CLOCK_RATE_INDEX]->setValue(clockRange.convertTo0to1(ayumi.state.clock_rate));
    for (int i = 0; i < 3; i++) {
        pl[AYUMI_PARAMETER_SOFTENV_0_NUM_POINTS + i * 13]->setValue(
                softwareEnvelopeNumStopsRange.convertTo0to1(ayumi.state.softenv_form[i].num_points));
        for (int p = 0; p < 6; p++) {
            pl[AYUMI_PARAMETER_SOFTENV_0_POINT_0_CLOCK + i * 13 + p * 2]->setValue(
                    softwareEnvelopeStopSecondsRange.convertTo0to1(ayumi.state.softenv_form[i].stops[p].stopAt));
            pl[AYUMI_PARAMETER_SOFTENV_0_POINT_0_RATIO + i * 13 + p * 2]->setValue(
                    softwareEnvelopeStopRatioRange.convertTo0to1(ayumi.state.softenv_form[i].stops[p].volumeRatio));
        }
    }
}

//==============================================================================
void AyumiAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    if (ayumi.state.magic_number != AYUMI_JUCE_STATE_MAGIC_NUMBER)
        ayumi.reset();

    setParametersFromState();

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
    double ret = 440.0 * pow(1.059463, key - 57.0);
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
		// It is kinda hacky, but we "reset" envelope shape to "different value" so that every note can start the envelope waveform
		// FIXME: should we add another plugin parameter to control whether or not we reset envelope for each note off?
		ayumi_set_envelope_shape(&a->impl, (ayumi.state.envelope_shape + 1) % 16);
		a->note_on_state[channel] = false;
		break;
	case CMIDI2_STATUS_NOTE_ON:
		if (bytes[2] == 0)
			goto note_off;
		if (a->note_on_state[channel])
			break; // busy
		mixer = a->state.mixer[channel];
		tone_switch = mixer & 1;
		noise_switch = mixer & 2 ? 1 : 0;
		env_switch = mixer & 4 ? 1 : 0;
		ayumi_set_mixer(&a->impl, channel, tone_switch, noise_switch, env_switch);
		ayumi_set_envelope_shape(&a->impl, a->state.envelope_shape);
        a->softenv[channel].started_at = a->totalProcessRunSeconds;
		ayumi_set_tone(&a->impl, channel, 2000000.0 / (16.0 * key_to_freq(bytes[1])));
		a->note_on_state[channel] = true;
		break;
	case CMIDI2_STATUS_PROGRAM:
		noise = bytes[1] & 0x1F;
		ayumi_set_noise(&a->impl, noise);
		mixer = bytes[1] >> 5;
		tone_switch = mixer & 1;
		noise_switch = mixer & 2 ? 1 : 0;
		// We cannot pass 8 bit message, so we remove env_switch here. Use BankMSB for it.
		env_switch = mixer & 4 ? 1 : 0;
		a->state.mixer[channel] = mixer;
        a->state.noise_freq = noise;
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
        default:
            if (AYUMI_LV2_MIDI_CC_SOFTENV_NUM_PARAMS_INDEX <= bytes[1] && bytes[1] <= AYUMI_LV2_MIDI_CC_SOFTENV_STOP_5_VRATE_INDEX) {
                int para = bytes[1] - AYUMI_LV2_MIDI_CC_SOFTENV_NUM_PARAMS_INDEX;
                if (para == 0)
                    a->state.softenv_form[channel].num_points = bytes[2];
                else {
                    if (para % 2)
                        // y=0.001x^2. It can be different curve, but I find this useful.
                        a->state.softenv_form[channel].stops[para / 2].stopAt = bytes[2] * bytes[2] * 0.001f;
                    else
                        // it does not necessarily have to be like this, but we treat 64 as 0.5 here.
                        a->state.softenv_form[channel].stops[para / 2].volumeRatio = bytes[2] <= 64 ? bytes[2] / 128.0f : bytes[2] / 127.0f;
                }
            }
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
            processFrames(buffer, currentFrame, max);
			currentFrame = max;
		}
        ayumi_process_midi_event(msg);
	}

    processFrames(buffer, currentFrame, sample_count);

    a->totalProcessRunSeconds += (float) sample_count / (float) a->sample_rate;
}

void AyumiAudioProcessor::processFrames(juce::AudioBuffer<float>& buffer, int start, int end) {
    // If we support release envelope this optimization will have to change
    if (!ayumi.active)
        return;
    if(!ayumi.note_on_state[0] && !ayumi.note_on_state[1] && !ayumi.note_on_state[2])
        return;

    auto *a = &ayumi;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    float secondsPerFrame = 1.0f / (float) a->sample_rate;
    float positionInSeconds = a->totalProcessRunSeconds;
    int v_cache[3]{-1, -1, -1};
    for (int i = start; i < end; i++) {
        // adjust volume for software envelope
        if (i % 25 == 0) {
            // software envelope does not always have to be processed. Do it only once in 100 frames.
            for (int ch = 0; ch < 3; ch++) {
                if (!a->note_on_state[ch])
                    continue;
                if (a->state.softenv_form[ch].num_points == 0)
                    continue; // software envelope is disabled.
                float at = positionInSeconds - a->softenv[ch].started_at;
                float f = (float) a->state.volume[ch] * a->softenv[ch].getRatio(at, a->state.softenv_form[ch]);
                int v = (int) round(f);
                if (v != v_cache[ch]) {
                    ayumi_set_volume(&a->impl, ch, v);
                    v_cache[ch] = v;
                }
            }
        }

        ayumi_process(&a->impl);
        ayumi_remove_dc(&a->impl);
        int nCh = totalNumOutputChannels - totalNumInputChannels;
        if (nCh > 0)
            auto &ptr = buffer.getWritePointer(totalNumInputChannels)[i] = (float) ayumi.impl.left;
        if (nCh > 1)
            auto &ptr = buffer.getWritePointer(totalNumInputChannels + 1)[i] = (float) ayumi.impl.right;

        positionInSeconds += secondsPerFrame;
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
    juce::MemoryOutputStream stream{destData, false};

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

    for (int i = 0; i < 3; i++) {
        stream.writeInt(ayumi.state.softenv_form[i].num_points);
        for (int p = 0; p < 6; p++) {
            stream.writeFloat(ayumi.state.softenv_form[i].stops[p].stopAt);
            stream.writeFloat(ayumi.state.softenv_form[i].stops[p].volumeRatio);
        }
    }

    stream.flush();
}

void AyumiAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (sizeInBytes < AYUMI_NUM_PARAMETERS * 4)
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

    for (int i = 0; i < 3; i++) {
        ayumi.state.softenv_form[i].num_points = stream.readInt();
        for (int p = 0; p < 6; p++) {
            ayumi.state.softenv_form[i].stops[p].stopAt = stream.readFloat();
            ayumi.state.softenv_form[i].stops[p].volumeRatio = stream.readFloat();
        }
    }

    ayumi.state.magic_number = AYUMI_JUCE_STATE_MAGIC_NUMBER;
}

void AyumiAudioProcessor::audioProcessorParameterChanged(juce::AudioProcessor *processor, int parameterIndex,
                                                         float newValue) {
    if (parameterIndex <= AYUMI_PARAMETER_MIXER_2_INDEX) {
        ayumi.state.mixer[parameterIndex % 3] = (int) mixerRange.convertFrom0to1(newValue);
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
                break;
            default:
                if (AYUMI_PARAMETER_SOFTENV_0_NUM_POINTS <= parameterIndex && parameterIndex <= AYUMI_PARAMETER_SOFTENV_0_POINT_0_CLOCK + 12) {
                    auto targetCh = (parameterIndex - AYUMI_PARAMETER_SOFTENV_0_POINT_0_CLOCK) / 13;
                    auto offset = parameterIndex - targetCh * 13 - AYUMI_PARAMETER_SOFTENV_0_NUM_POINTS;
                    if (offset == 0)
                        ayumi.state.softenv_form[targetCh].num_points = (int) softwareEnvelopeNumStopsRange.convertFrom0to1(newValue);
                    else {
                        auto &stop = ayumi.state.softenv_form[targetCh].stops[(offset - 1) / 2];
                        if (offset % 2)
                            stop.stopAt = softwareEnvelopeStopSecondsRange.convertFrom0to1(newValue);
                        else
                            stop.volumeRatio = softwareEnvelopeStopRatioRange.convertFrom0to1(newValue);
                    }
                }
                break;
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
