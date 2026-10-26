#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Shapes.h"

#include <cmath>
#include <limits>
#include <optional>

namespace Params
{
    juce::StringArray rateNames() { return { "1/8", "1/4", "1/2", "1 bar" }; }
    juce::StringArray triggerNames() { return { "Tempo", "Sidechain" }; }

    // Quarter notes per cycle; 0 means one bar in the host's time signature.
    static double rateBeats (int index)
    {
        static constexpr double beats[] { 0.5, 1.0, 2.0, 0.0 };
        return beats[juce::jlimit (0, 3, index)];
    }
}

DuckProcessor::DuckProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    for (int i = 0; i < Shapes::names().size(); ++i)
    {
        shapes.push_back (std::make_unique<Curve>());
        shapes.back()->setPoints (Shapes::points (i));
    }

    amountParam = apvts.getRawParameterValue (Params::amount);
    shapeParam = apvts.getRawParameterValue (Params::shape);
    rateParam = apvts.getRawParameterValue (Params::rate);
    triggerParam = apvts.getRawParameterValue (Params::trigger);
    thresholdParam = apvts.getRawParameterValue (Params::threshold);
    bypassParam = apvts.getRawParameterValue (Params::bypass);
}

juce::AudioProcessorValueTreeState::ParameterLayout DuckProcessor::createLayout()
{
    using namespace juce;

    return {
        std::make_unique<AudioParameterFloat> (ParameterID { Params::amount, 1 }, "Amount", NormalisableRange<float> (0.0f, 1.0f), 0.8f,
                                               AudioParameterFloatAttributes().withStringFromValueFunction (
                                                   [] (float v, int) { return String (roundToInt (v * 100.0f)) + "%"; })),
        std::make_unique<AudioParameterChoice> (ParameterID { Params::shape, 1 }, "Shape", Shapes::names(), 0),
        std::make_unique<AudioParameterChoice> (ParameterID { Params::rate, 1 }, "Rate", Params::rateNames(), 1),
        std::make_unique<AudioParameterChoice> (ParameterID { Params::trigger, 1 }, "Trigger", Params::triggerNames(), 0),
        std::make_unique<AudioParameterFloat> (ParameterID { Params::threshold, 1 }, "Threshold", NormalisableRange<float> (-60.0f, 0.0f, 0.1f), -24.0f,
                                               AudioParameterFloatAttributes().withStringFromValueFunction (
                                                   [] (float v, int) { return String (v, 1) + " dB"; })),
        std::make_unique<AudioParameterBool> (ParameterID { Params::bypass, 1 }, "Bypass", false)
    };
}

const Curve& DuckProcessor::getShape (int index) const
{
    return *shapes[(size_t) juce::jlimit (0, (int) shapes.size() - 1, index)];
}

juce::AudioProcessorParameter* DuckProcessor::getBypassParameter() const
{
    return apvts.getParameter (Params::bypass);
}

bool DuckProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();

    if ((out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) || out != layouts.getMainInputChannelSet())
        return false;

    if (layouts.inputBuses.size() > 1)
    {
        const auto sidechain = layouts.getChannelSet (true, 1);
        return sidechain.isDisabled() || sidechain == juce::AudioChannelSet::mono() || sidechain == juce::AudioChannelSet::stereo();
    }

    return true;
}

void DuckProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate;
    smoothedGain = 1.0f;
    hitPhase = 1.0;
    triggerEnvelope = 0.0f;
    triggerArmed = true;
    samplesSinceTrigger = std::numeric_limits<int>::max() / 2; // the first hit after starting must count
    bypassMix.reset (sampleRate, 0.03);
    bypassMix.setCurrentAndTargetValue (bypassParam->load() > 0.5f ? 0.0f : 1.0f);
}

void DuckProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (auto ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    // Transport.
    double bpm = 120.0;
    double beatsPerBar = 4.0;
    bool hasHost = false, hostPlaying = false;
    std::optional<double> lastBarStart;

    if (auto* playHead = getPlayHead())
    {
        if (auto position = playHead->getPosition())
        {
            hasHost = true;

            if (auto hostBpm = position->getBpm(); hostBpm && *hostBpm > 0.0)
                bpm = *hostBpm;

            if (auto sig = position->getTimeSignature(); sig && sig->denominator > 0)
                beatsPerBar = sig->numerator * 4.0 / sig->denominator;

            if (auto ppq = position->getPpqPosition(); ppq && position->getIsPlaying())
            {
                tempoPpq = *ppq;
                hostPlaying = true;

                if (auto barStart = position->getPpqPositionOfLastBarStart())
                    lastBarStart = *barStart;
            }
        }
    }

    auto main = getBusBuffer (buffer, false, 0);
    const auto numSamples = main.getNumSamples();
    const auto numChannels = main.getNumChannels();

    const auto* sidechainBus = getBus (true, 1);
    const auto sidechainActive = sidechainBus != nullptr && sidechainBus->isEnabled() && getChannelCountOfBus (true, 1) > 0;
    const auto sidechain = sidechainActive ? getBusBuffer (buffer, true, 1) : juce::AudioBuffer<float>();
    sidechainConnected.store (sidechainActive, std::memory_order_relaxed);

    const auto rateIndex = (int) rateParam->load();
    const auto barRate = Params::rateBeats (rateIndex) == 0.0;
    const auto cycleBeats = barRate ? beatsPerBar : Params::rateBeats (rateIndex);
    const auto ppqPerSample = bpm / 60.0 / currentSampleRate;
    const auto increment = ppqPerSample / cycleBeats;

    const auto& shape = getShape ((int) shapeParam->load());
    const auto amount = amountParam->load();
    const auto sidechainMode = (int) triggerParam->load() == Params::sidechainTrigger;
    const auto threshold = juce::Decibels::decibelsToGain (thresholdParam->load());
    const auto envelopeRelease = (float) std::exp (-1.0 / (0.03 * currentSampleRate));
    const auto minTriggerGap = (int) (0.05 * currentSampleRate);
    const auto smoothCoeff = (float) std::exp (-1.0 / (0.001 * currentSampleRate)); // 1 ms against clicks

    // Tempo mode follows the host and freezes while it is stopped; without a host it runs at 120 bpm.
    const auto tempoRunning = hostPlaying || ! hasHost;
    auto tempoPhase = tempoPpq / cycleBeats;

    if (barRate && lastBarStart)
        tempoPhase = (tempoPpq - *lastBarStart) / beatsPerBar; // cycles start on bar lines

    bypassMix.setTargetValue (bypassParam->load() > 0.5f ? 0.0f : 1.0f);
    auto phase = 0.0;

    for (int i = 0; i < numSamples; ++i)
    {
        if (sidechainMode)
        {
            const auto level = sidechainActive ? juce::jmax (std::abs (sidechain.getSample (0, i)),
                                                             std::abs (sidechain.getSample (sidechain.getNumChannels() - 1, i)))
                                               : 0.0f;
            triggerEnvelope = juce::jmax (level, triggerEnvelope * envelopeRelease);
            ++samplesSinceTrigger;

            if (triggerArmed && triggerEnvelope >= threshold && samplesSinceTrigger >= minTriggerGap)
            {
                hitPhase = 0.0;
                triggerArmed = false;
                samplesSinceTrigger = 0;
                triggerCount.fetch_add (1, std::memory_order_relaxed);
            }
            else if (triggerEnvelope < threshold * 0.5f)
            {
                triggerArmed = true;
            }

            phase = juce::jmin (hitPhase, 1.0);
            hitPhase += increment;
        }
        else
        {
            phase = tempoPhase - std::floor (tempoPhase);

            if (tempoRunning)
                tempoPhase += increment;
        }

        // After a sidechain hit has played through, stay at full volume until the next one.
        const auto curveValue = phase >= 1.0 ? 1.0f : shape.lookup ((float) phase);
        const auto target = 1.0f - amount * (1.0f - curveValue);
        smoothedGain = target + smoothCoeff * (smoothedGain - target);

        const auto mix = bypassMix.getNextValue();
        const auto gain = 1.0f + (smoothedGain - 1.0f) * mix;

        for (int ch = 0; ch < numChannels; ++ch)
            main.getWritePointer (ch)[i] *= gain;
    }

    if (tempoRunning)
        tempoPpq += numSamples * ppqPerSample;

    displayPhase.store ((float) juce::jmin (phase, 1.0), std::memory_order_relaxed);
    displayGain.store (smoothedGain, std::memory_order_relaxed);
}

void DuckProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void DuckProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes); xml != nullptr && xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* DuckProcessor::createEditor()
{
    return new DuckEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DuckProcessor();
}
