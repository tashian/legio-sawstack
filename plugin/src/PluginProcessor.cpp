// plugin/src/PluginProcessor.cpp
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "pitch.h"  // kVoctZero

SawstackAudioProcessor::SawstackAudioProcessor()
    : juce::AudioProcessor(BusesProperties().withOutput(
          "Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMS", createLayout()) {}

juce::AudioProcessorValueTreeState::ParameterLayout
SawstackAudioProcessor::createLayout() {
    using P = juce::AudioParameterFloat;
    using R = juce::NormalisableRange<float>;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<P>("detune", "Detune", R{0.0f, 1.0f}, 0.3f));
    params.push_back(std::make_unique<P>("morph",  "Morph",  R{0.0f, 1.0f}, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "mode", "Mode", juce::StringArray{"Stack", "Rich", "Sub"}, 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "width", "Width", juce::StringArray{"Mono", "Stereo", "Wide"}, 1));
    params.push_back(std::make_unique<P>("attack",  "Attack",  R{0.001f, 10.0f, 0.0f, 0.3f}, 0.005f));
    params.push_back(std::make_unique<P>("decay",   "Decay",   R{0.001f, 10.0f, 0.0f, 0.3f}, 0.2f));
    params.push_back(std::make_unique<P>("sustain", "Sustain", R{0.0f, 1.0f}, 0.8f));
    params.push_back(std::make_unique<P>("release", "Release", R{0.001f, 10.0f, 0.0f, 0.3f}, 0.3f));
    params.push_back(std::make_unique<P>("level", "Level", R{-60.0f, 6.0f}, 0.0f));

    return { params.begin(), params.end() };
}

void SawstackAudioProcessor::prepareToPlay(double sampleRate, int) {
    engine_.Init(static_cast<float>(sampleRate));
    adsr_.setSampleRate(sampleRate);
}

void SawstackAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                          juce::MidiBuffer& midi) {
    const int n = buffer.getNumSamples();

    bool gateThisBlock = false;
    for (const auto meta : midi) {
        const auto m = meta.getMessage();
        if (m.isNoteOn()) {
            notes_.NoteOn(m.getNoteNumber());
        } else if (m.isNoteOff()) {
            notes_.NoteOff(m.getNoteNumber());
        } else if (m.isAllNotesOff() || m.isAllSoundOff()) {
            notes_ = sawstack::NoteStack{};
        } else if (m.isPitchWheel()) {
            // 14-bit value 0..16383, center 8192 → ±2 semitones.
            pitchBendSemis_ = (m.getPitchWheelValue() - 8192) / 8192.0f * 2.0f;
        }
    }

    // Envelope gating from the note stack (last-note priority).
    const int active = notes_.ActiveNote();
    if (active != lastActiveNote_) {
        if (active >= 0) {
            gateThisBlock = true;     // retrigger saw phases on any new active note
            adsr_.noteOn();
        } else {
            adsr_.noteOff();
        }
        lastActiveNote_ = active;
    }

    // Keep ADSR params live from the knobs.
    adsrParams_.attack  = apvts.getRawParameterValue("attack")->load();
    adsrParams_.decay   = apvts.getRawParameterValue("decay")->load();
    adsrParams_.sustain = apvts.getRawParameterValue("sustain")->load();
    adsrParams_.release = apvts.getRawParameterValue("release")->load();
    adsr_.setParameters(adsrParams_);

    // Pitch: hold last frequency if no note is active so release tails stay in tune.
    // Pitch comes from MIDI (+ pitch bend); no per-instance tune offsets — transpose
    // in the host if needed. NoteToHz keeps its coarse/fine args (passed 0 here).
    float hz;
    if (active >= 0) {
        hz = sawstack::NoteToHz(active, pitchBendSemis_, 0, 0.0f);
        heldHz_ = hz;          // remember it for the release tail
    } else {
        hz = heldHz_;          // hold last note's pitch while the envelope rings out
    }

    sawstack::Params p{};
    p.top_adc    = apvts.getRawParameterValue("detune")->load();
    p.bottom_adc = apvts.getRawParameterValue("morph")->load();
    p.voct_adc   = sawstack::kVoctZero;
    p.mode  = static_cast<sawstack::Mode>(
        static_cast<int>(apvts.getRawParameterValue("mode")->load()));
    p.width = static_cast<sawstack::Width>(
        static_cast<int>(apvts.getRawParameterValue("width")->load()));
    p.external_hz = hz;
    p.gate_edge   = gateThisBlock;
    engine_.ApplyParams(p);

    float* l = buffer.getWritePointer(0);
    float* r = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : l;
    engine_.ProcessBlock(l, r, n);

    adsr_.applyEnvelopeToBuffer(buffer, 0, n);

    const float gainDb = apvts.getRawParameterValue("level")->load();
    buffer.applyGain(juce::Decibels::decibelsToGain(gainDb));
}

juce::AudioProcessorEditor* SawstackAudioProcessor::createEditor() {
    return new SawstackAudioProcessorEditor(*this);
}

void SawstackAudioProcessor::getStateInformation(juce::MemoryBlock& dest) {
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, dest);
}

void SawstackAudioProcessor::setStateInformation(const void* data, int size) {
    if (auto xml = getXmlFromBinary(data, size))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new SawstackAudioProcessor();
}
