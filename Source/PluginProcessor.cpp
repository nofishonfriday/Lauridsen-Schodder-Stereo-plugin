#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

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
    parameters.addParameterListener ("FEEDBACK", this);
    parameters.addParameterListener ("MIX", this);
}

AudioPluginAudioProcessor::~AudioPluginAudioProcessor()
{
    parameters.removeParameterListener ("DELAY", this);
    parameters.removeParameterListener ("FEEDBACK", this);
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

    linear.prepare (spec);
    mixer.prepare (spec);

    linear.reset();
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
    const auto& input = context.getInputBlock();
    const auto& output = context.getOutputBlock();

    mixer.pushDrySamples (input);

    // This is the place where you'd normally do the guts of your plugin's
    // audio processing...
    // Make sure to reset the state if your inner loop is processing
    // the samples and the outer loop is handling the channels.
    // Alternatively, you can process the samples with the channels
    // interleaved by keeping the same state.
    for (int channel = 0; channel < numChannels; ++channel)
    {
        auto* samplesIn = input.getChannelPointer (channel);
        auto* samplesOut = output.getChannelPointer (channel);

        for (size_t sample = 0; sample < input.getNumSamples(); ++sample)
        {
            auto input = samplesIn[sample];
            auto delayAmount = delayValue[channel];

            linear.pushSample (int (channel), input);
            linear.setDelay ((float) delayAmount);
            samplesOut[sample] = linear.popSample ((int) channel);
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
        std::fill (delayValue.begin(), delayValue.end(), newValue / 1000.0 * getSampleRate());

    if (parameterID == "MIX")
        mixer.setWetMixProportion (newValue);
}

AudioProcessorValueTreeState::ParameterLayout AudioPluginAudioProcessor::createParameters()
{
    AudioProcessorValueTreeState::ParameterLayout params;

    using Range = NormalisableRange<float>;

    params.add (std::make_unique<AudioParameterFloat> ("DELAY", "Delay (MS)", 0.01f, 1000.0f, 10.0)); // TODO: set range for prod.
    params.add (std::make_unique<AudioParameterFloat> ("FEEDBACK", "Feedback", -100.0f, 0.0f, -100.0f));
    params.add (std::make_unique<AudioParameterFloat> ("MIX", "Mix", Range { 0.0f, 1.0f, 0.01f }, 0.0f));

    return params;
}
