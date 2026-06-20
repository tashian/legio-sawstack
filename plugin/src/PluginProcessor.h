// plugin/src/PluginProcessor.h
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "supersaw_engine.h"
#include "note_stack.h"

class SawstackAudioProcessor : public juce::AudioProcessor {
  public:
    SawstackAudioProcessor();
    ~SawstackAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sawstack"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& dest) override;
    void setStateInformation(const void* data, int size) override;

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

  private:
    sawstack::SupersawEngine engine_;
    sawstack::NoteStack notes_;
    juce::ADSR adsr_;
    juce::ADSR::Parameters adsrParams_;
    float pitchBendSemis_ = 0.0f;   // current bend in semitones (±2)
    int   lastActiveNote_ = -1;     // for retrigger detection
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SawstackAudioProcessor)
};
