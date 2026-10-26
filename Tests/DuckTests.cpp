#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "PluginEditor.h"
#include "Shapes.h"

using Catch::Matchers::WithinAbs;

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 480;

    struct TestPlayHead : juce::AudioPlayHead
    {
        juce::Optional<PositionInfo> getPosition() const override { return info; }
        PositionInfo info;
    };

    struct Fixture
    {
        Fixture()
        {
            processor.setRateAndBufferSizeDetails (sampleRate, blockSize);
            processor.prepareToPlay (sampleRate, blockSize);
        }

        void set (const juce::String& id, float value)
        {
            auto* param = processor.apvts.getParameter (id);
            param->setValueNotifyingHost (param->convertTo0to1 (value));
        }

        void enableSidechain()
        {
            auto layout = processor.getBusesLayout();
            layout.inputBuses.getReference (1) = juce::AudioChannelSet::stereo();
            REQUIRE (processor.setBusesLayout (layout));
            processor.prepareToPlay (sampleRate, blockSize);
        }

        // One block with constant 1.0 on the main input; `hit` puts a kick on the sidechain.
        juce::AudioBuffer<float> process (bool hit = false)
        {
            juce::AudioBuffer<float> buffer (processor.getTotalNumInputChannels(), blockSize);
            juce::MidiBuffer midi;
            buffer.clear();

            for (int i = 0; i < blockSize; ++i)
            {
                buffer.setSample (0, i, 1.0f);
                buffer.setSample (1, i, 1.0f);

                if (hit && buffer.getNumChannels() > 2 && i < 50)
                {
                    buffer.setSample (2, i, 0.9f);
                    buffer.setSample (3, i, 0.9f);
                }
            }

            processor.processBlock (buffer, midi);
            return buffer;
        }

        juce::ScopedJuceInitialiser_GUI juce;
        DuckProcessor processor;
    };
}

TEST_CASE ("Every shape starts ducked, recovers fully and loops cleanly", "[shapes]")
{
    for (int s = 0; s < Shapes::names().size(); ++s)
    {
        Curve curve;
        curve.setPoints (Shapes::points (s));

        CHECK (curve.evaluate (0.0f) <= 0.15f);
        CHECK_THAT (curve.evaluate (0.9f), WithinAbs (1.0f, 1.0e-4));
        CHECK_THAT (curve.evaluate (0.0f), WithinAbs (curve.evaluate (1.0f), 1.0e-6));
    }
}

TEST_CASE_METHOD (Fixture, "Zero amount leaves audio untouched", "[duck]")
{
    set (Params::amount, 0.0f);
    const auto out = process();

    for (int i = 0; i < blockSize; ++i)
        REQUIRE_THAT (out.getSample (0, i), WithinAbs (1.0f, 1.0e-6));
}

TEST_CASE_METHOD (Fixture, "Full amount ducks to silence on the beat", "[duck]")
{
    TestPlayHead host;
    host.info.setBpm (120.0);
    host.info.setIsPlaying (true);
    host.info.setPpqPosition (0.0);
    processor.setPlayHead (&host);
    set (Params::amount, 1.0f);

    // Let the 1 ms smoothing settle on the first block, then land exactly on a beat.
    process();
    host.info.setPpqPosition (1.0);
    const auto out = process();

    CHECK (out.getSample (0, 60) < 0.05f);
}

TEST_CASE_METHOD (Fixture, "Tempo mode freezes when the host stops", "[duck]")
{
    TestPlayHead host;
    host.info.setBpm (120.0);
    host.info.setIsPlaying (true);
    host.info.setPpqPosition (0.3);
    processor.setPlayHead (&host);
    process();

    host.info.setIsPlaying (false);
    process();
    const auto stopped = processor.displayPhase.load();
    process();
    process();

    CHECK (processor.displayPhase.load() == stopped);
}

TEST_CASE_METHOD (Fixture, "The 1 bar rate starts on bar lines", "[duck]")
{
    TestPlayHead host;
    host.info.setBpm (120.0);
    host.info.setIsPlaying (true);
    host.info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
    host.info.setPpqPosition (6.0); // bar start after two bars of 3/4
    host.info.setPpqPositionOfLastBarStart (6.0);
    processor.setPlayHead (&host);
    set (Params::rate, 3.0f);

    juce::AudioBuffer<float> one (2, 1);
    juce::MidiBuffer midi;
    one.clear();
    processor.processBlock (one, midi);

    CHECK_THAT (processor.displayPhase.load(), WithinAbs (0.0f, 1.0e-4));
}

TEST_CASE_METHOD (Fixture, "Sidechain hits duck once, then the track comes back to full volume", "[duck]")
{
    enableSidechain();
    set (Params::trigger, (float) Params::sidechainTrigger);
    set (Params::amount, 1.0f);
    set (Params::threshold, -20.0f);

    // Before any hit: full volume.
    CHECK_THAT (process().getSample (0, blockSize - 1), WithinAbs (1.0f, 1.0e-4));

    // A kick: ducked right after.
    const auto hitBlock = process (true);
    CHECK (processor.triggerCount.load() == 1);
    CHECK (hitBlock.getSample (0, 300) < 0.01f); // 6 ms in: past the 1 ms anti-click fade, still in the hold

    // A 1/4 at 120 bpm is 24000 samples (50 blocks); after that it stays at full volume.
    for (int b = 0; b < 60; ++b)
        process();

    CHECK_THAT (process().getSample (0, blockSize - 1), WithinAbs (1.0f, 1.0e-4));
}

TEST_CASE_METHOD (Fixture, "Main input never triggers in sidechain mode", "[duck]")
{
    enableSidechain();
    set (Params::trigger, (float) Params::sidechainTrigger);
    set (Params::threshold, -20.0f);

    for (int b = 0; b < 20; ++b)
        process();

    CHECK (processor.triggerCount.load() == 0);
}

TEST_CASE_METHOD (Fixture, "Settings survive a save and reload", "[duck]")
{
    set (Params::amount, 0.42f);
    set (Params::shape, 3.0f);
    set (Params::trigger, 1.0f);

    juce::MemoryBlock state;
    processor.getStateInformation (state);

    DuckProcessor restored;
    restored.setStateInformation (state.getData(), (int) state.getSize());

    CHECK_THAT (restored.apvts.getRawParameterValue (Params::amount)->load(), WithinAbs (0.42f, 1.0e-4));
    CHECK (restored.apvts.getRawParameterValue (Params::shape)->load() == 3.0f);
    CHECK (restored.apvts.getRawParameterValue (Params::trigger)->load() == 1.0f);
}

// Not part of the normal run: renders the editor to a PNG.  ./build/DuckTests "[.snapshot]"
TEST_CASE_METHOD (Fixture, "Editor snapshot", "[.snapshot]")
{
    TestPlayHead host;
    host.info.setBpm (120.0);
    host.info.setIsPlaying (true);
    host.info.setPpqPosition (0.2);
    processor.setPlayHead (&host);
    process();

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
    const auto path = juce::SystemStats::getEnvironmentVariable ("DUCK_SNAPSHOT", "/tmp/duck.png");
    juce::File (path).deleteFile();
    juce::FileOutputStream stream { juce::File (path) };
    REQUIRE (juce::PNGImageFormat().writeImageToStream (image, stream));
}

TEST_CASE ("Shapes hold down briefly instead of springing straight back", "[shapes]")
{
    // 1 ms after the kick at 120 bpm 1/4 is phase 0.002: the ducking shapes should still be near the bottom.
    for (int s : { 0, 1, 3, 5 })
    {
        Curve curve;
        curve.setPoints (Shapes::points (s));
        CHECK (curve.evaluate (0.002f) < 0.05f);
    }
}
