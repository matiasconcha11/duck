#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Curve.h"

namespace Params
{
    inline const juce::String amount { "amount" };
    inline const juce::String shape { "shape" };
    inline const juce::String rate { "rate" };
    inline const juce::String trigger { "trigger" };
    inline const juce::String threshold { "threshold" };
    inline const juce::String bypass { "bypass" };

    enum Trigger
    {
        tempoTrigger,
        sidechainTrigger
    };

    juce::StringArray rateNames();
    juce::StringArray triggerNames();
}

class DuckProcessor : public juce::AudioProcessor
{
public:
    DuckProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorParameter* getBypassParameter() const override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Duck"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // One curve per shape, built once; the audio thread only reads their lookup tables.
    const Curve& getShape (int index) const;

    juce::AudioProcessorValueTreeState apvts;

    // Written by the audio thread, read by the editor.
    std::atomic<float> displayPhase { 0.0f };
    std::atomic<float> displayGain { 1.0f };
    std::atomic<int> triggerCount { 0 };
    std::atomic<bool> sidechainConnected { false };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    std::vector<std::unique_ptr<Curve>> shapes;

    std::atomic<float>* amountParam = nullptr;
    std::atomic<float>* shapeParam = nullptr;
    std::atomic<float>* rateParam = nullptr;
    std::atomic<float>* triggerParam = nullptr;
    std::atomic<float>* thresholdParam = nullptr;
    std::atomic<float>* bypassParam = nullptr;

    double currentSampleRate = 44100.0;
    double tempoPpq = 0.0;

    // Sidechain mode: one pass through the shape per hit, then full volume until the next one.
    double hitPhase = 1.0;
    float triggerEnvelope = 0.0f;
    bool triggerArmed = true;
    int samplesSinceTrigger = 0;

    float smoothedGain = 1.0f;
    juce::SmoothedValue<float> bypassMix;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DuckProcessor)
};
