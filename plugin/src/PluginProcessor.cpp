// plugin/src/PluginProcessor.cpp
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "pitch.h"  // kVoctZero

SawstackAudioProcessor::SawstackAudioProcessor()
    : juce::AudioProcessor(BusesProperties().withOutput(
          "Output", juce::AudioChannelSet::stereo(), true)) {}

void SawstackAudioProcessor::prepareToPlay(double sampleRate, int) {
    engine_.Init(static_cast<float>(sampleRate));
}

void SawstackAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                          juce::MidiBuffer&) {
    const int n = buffer.getNumSamples();

    sawstack::Params p{};
    p.top_adc    = 0.3f;                  // fixed drone defaults for now
    p.bottom_adc = 0.0f;
    p.voct_adc   = sawstack::kVoctZero;   // neutral
    p.mode       = sawstack::Mode::STACK;
    p.width      = sawstack::Width::STEREO;
    p.external_hz = 261.63f;              // fixed C4 drone until Task 6 wires MIDI
    engine_.ApplyParams(p);

    float* l = buffer.getWritePointer(0);
    float* r = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : l;
    engine_.ProcessBlock(l, r, n);
}

juce::AudioProcessorEditor* SawstackAudioProcessor::createEditor() {
    return new SawstackAudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new SawstackAudioProcessor();
}
