// plugin/src/PluginEditor.h
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

class SawstackAudioProcessorEditor : public juce::AudioProcessorEditor {
  public:
    explicit SawstackAudioProcessorEditor(SawstackAudioProcessor&);
    void resized() override;
  private:
    juce::GenericAudioProcessorEditor generic_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SawstackAudioProcessorEditor)
};
