#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

// default DSP params
const float DELAY_DEFAULT_VAL = 25.0f;
const float MIX_DEFAULT_VAL   = 0.20f; 

//==============================================================================
AudioPluginAudioProcessor::AudioPluginAudioProcessor()
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
            ), parameters (*this, nullptr, "Parameters", createParameters())
{
    parameters.addParameterListener ("DELAY", this);
    parameters.addParameterListener ("MIX", this);
}

AudioPluginAudioProcessor::~AudioPluginAudioProcessor()
{
    parameters.removeParameterListener ("DELAY", this);
    parameters.removeParameterListener ("MIX", this);
}

//==============================================================================
const juce::String AudioPluginAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool AudioPluginAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool AudioPluginAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool AudioPluginAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double AudioPluginAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int AudioPluginAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int AudioPluginAudioProcessor::getCurrentProgram()
{
    return 0;
}

void AudioPluginAudioProcessor::setCurrentProgram (int index)
{
    juce::ignoreUnused (index);
}

const juce::String AudioPluginAudioProcessor::getProgramName (int index)
{
    juce::ignoreUnused (index);
    return {};
}

void AudioPluginAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
    juce::ignoreUnused (index, newName);
}

//==============================================================================
void AudioPluginAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Use this method as the place to do any pre-playback
    // initialisation that you need..
    juce::dsp::ProcessSpec spec;
    spec.maximumBlockSize = samplesPerBlock;
    spec.sampleRate = sampleRate;
    spec.numChannels = 2;

    delayLine.prepare (spec);
    mixer.prepare (spec);

    delayLine.reset();

    // init DSP params on first plugin load
    // we need sample rate for this, so can't do in constructor (sample rate not known in constr.)
    if (!processorParamsInitialized)
    {
        mixer.setWetMixProportion (MIX_DEFAULT_VAL);
        parameterChanged ("DELAY", DELAY_DEFAULT_VAL);

        processorParamsInitialized = true;
    }    
}

void AudioPluginAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

bool AudioPluginAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // https://forum.juce.com/t/how-to-enable-mono-stereo-option-for-plug-in-within-logic/41545/2
    if (layouts.getMainOutputChannelSet() == AudioChannelSet::stereo())
    {
        // Mono-to-stereo OR stereo-to-stereo
        if ((layouts.getMainInputChannelSet() == AudioChannelSet::mono()) || (layouts.getMainInputChannelSet() == AudioChannelSet::stereo()))
            return true;
    }
    return false;
  #endif
}

void AudioPluginAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                              juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused (midiMessages);

    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // In case we have more outputs than inputs, this code clears any output
    // channels that didn't contain input data, (because these aren't
    // guaranteed to be empty - they may contain garbage).
    // This is here to avoid people getting screaming feedback
    // when they first compile a plugin, but obviously you don't need to keep
    // this code if your algorithm always overwrites all the output channels.
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    const auto numChannels = jmax (totalNumInputChannels, totalNumOutputChannels);

    auto audioBlock = juce::dsp::AudioBlock<float> (buffer).getSubsetChannelBlock (0, (size_t) numChannels);
    auto context = juce::dsp::ProcessContextReplacing<float> (audioBlock);
    const auto& inputBlock = context.getInputBlock();
    const auto& output = context.getOutputBlock();

    mixer.pushDrySamples (inputBlock);

    // This is the place where you'd normally do the guts of your plugin's
    // audio processing...
    // Make sure to reset the state if your inner loop is processing
    // the samples and the outer loop is handling the channels.
    // Alternatively, you can process the samples with the channels
    // interleaved by keeping the same state.
    for (size_t sample = 0; sample < inputBlock.getNumSamples(); ++sample)
    {
        for (int channel = 0; channel < numChannels; ++channel)
        {
            auto* samplesIn  = inputBlock.getChannelPointer (channel);
            auto* samplesOut = output.getChannelPointer (channel);
            auto input = samplesIn[sample];

            // swap l/r
            if (channel == 0)
                delayLine.pushSample (1, input);
            else
                delayLine.pushSample (0, input * -1.0f); // invert polarity

            delayLine.setDelay (delayValue);

            samplesOut[sample] = delayLine.popSample ((int) channel);
        }
    }

    mixer.mixWetSamples (output);
}

//==============================================================================
bool AudioPluginAudioProcessor::hasEditor() const
{
    // return true; // (change this to false if you choose to not supply an editor)
    return false;
}

juce::AudioProcessorEditor* AudioPluginAudioProcessor::createEditor()
{
    // return new AudioPluginAudioProcessorEditor (*this);
    return nullptr; // GUI-less
}

//==============================================================================
void AudioPluginAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void AudioPluginAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
    std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));

    if (xmlState.get() != nullptr)
        if (xmlState->hasTagName (parameters.state.getType()))
            parameters.replaceState (juce::ValueTree::fromXml (*xmlState));
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AudioPluginAudioProcessor();
}

void AudioPluginAudioProcessor::parameterChanged (const String& parameterID, float newValue)
{
    if (parameterID == "DELAY")
        delayValue = newValue / 1000.0 * (float)getSampleRate();

    if (parameterID == "MIX")
        mixer.setWetMixProportion (newValue);
}

AudioProcessorValueTreeState::ParameterLayout AudioPluginAudioProcessor::createParameters()
{
    AudioProcessorValueTreeState::ParameterLayout params;

    using Range = NormalisableRange<float>;

    params.add (std::make_unique<AudioParameterFloat> ("DELAY", "Delay (MS)", 5.0f, 100.0f, DELAY_DEFAULT_VAL));
    params.add (std::make_unique<AudioParameterFloat> ("MIX", "Mix", Range { 0.0f, 1.0f, 0.01f }, MIX_DEFAULT_VAL));

    return params;
}
