// plugin/src/PluginEditor.cpp
#include "PluginEditor.h"

SawstackAudioProcessorEditor::SawstackAudioProcessorEditor(SawstackAudioProcessor& p)
    : juce::AudioProcessorEditor(&p), generic_(p) {
    addAndMakeVisible(generic_);
    setSize(420, 360);
}

void SawstackAudioProcessorEditor::resized() {
    generic_.setBounds(getLocalBounds());
}
