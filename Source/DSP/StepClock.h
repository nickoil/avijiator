#pragma once

#include <cstdint>

#include <juce_core/juce_core.h>

//==============================================================================
/*
    How long one step lasts, as a musical division of the beat.

    An enum plus a table of BEATS PER STEP, rather than a multiplier: triplets
    are 1/3, which no power-of-two multiplier reaches, and the UI needs display
    names anyway - so the table earns its place twice.

    Ordered longest-step-first, so a combo box built from it reads slow -> fast.
    Whole/Half added on top of the original Quarter..ThirtySecond range at the
    user's request (more, slower Division options) - no triplet variant for
    either, matching ThirtySecond's own precedent of not every division
    having one. ThirtyTwoBars..TwoBars added on top of THAT, at the user's
    further request for the LFO specifically ("slow evolutions of sound",
    long sweeps down to 32 bars) - arp/seq's own Division combo deliberately
    does NOT expose these five (SynthPanel.cpp's arpChoiceSpecs/
    seqChoiceSpecs slice this same table starting at Whole, via ChoiceSpec's
    firstChoiceValue - see that field's own comment in ParameterControls.h),
    since a single arp/seq step lasting more than a bar stops being a
    "slow groove" and becomes tedious - the LFO's own Sync Division combo is
    the only one that sees the full range.
*/
enum class StepDivision : int
{
    ThirtyTwoBars = 0,  // 32/1 - 128 beats
    SixteenBars,        // 16/1 - 64 beats
    EightBars,          // 8/1  - 32 beats
    FourBars,           // 4/1  - 16 beats
    TwoBars,            // 2/1  - 8 beats
    Whole,               // 1/1 - 4 beats
    Half,                // 1/2 - 2 beats
    Quarter,
    QuarterTriplet,
    Eighth,
    EighthTriplet,
    Sixteenth,
    SixteenthTriplet,
    ThirtySecond
};

static constexpr int numStepDivisions = 14;

inline constexpr double beatsPerStepForDivision (StepDivision division) noexcept
{
    constexpr double table[numStepDivisions] =
    {
        128.0,      // 32/1
        64.0,       // 16/1
        32.0,       // 8/1
        16.0,       // 4/1
        8.0,        // 2/1
        4.0,        // 1/1
        2.0,        // 1/2
        1.0,        // 1/4
        2.0 / 3.0,  // 1/4T  - three of these fill two beats
        0.5,        // 1/8
        1.0 / 3.0,  // 1/8T  - three of these fill one beat
        0.25,       // 1/16
        1.0 / 6.0,  // 1/16T - three of these fill one 1/8
        0.125       // 1/32
    };

    const auto i = (int) division;

    // Not paranoia: this index arrives from a std::atomic<int> the message
    // thread writes, so a stale or out-of-range value must not index out of
    // the table on the audio thread. Same defensive posture as
    // noteEventFromMidiMessage not trusting isNoteOn's default arguments.
    return table[(i >= 0 && i < numStepDivisions) ? i : (int) StepDivision::Sixteenth];
}

//==============================================================================
/*
    A drift-free musical step clock: internal tempo only, no host sync (which
    CLAUDE.md forbids outright - this is a standalone instrument).

    AUDIO-THREAD-PRIVATE. Owned by whoever is sequencing; the arpeggiator owns
    one, and item 7's step sequencer will own another. This class is the piece
    those two SHARE - the sub-block render loop around it is deliberately not
    abstracted, since item 7's loop does different things at a step (pattern
    position, accent, slide) and factoring before there are two real users
    would cost more than the duplication.

    See documents/arpeggiator-design.md section 2.
*/
class StepClock
{
public:
    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate;
        reset();

        // So samplesPerStep is never stale for this sample rate even if the
        // caller somehow renders before its first setTempo.
        setTempo (defaultBpm, StepDivision::Sixteenth);
    }

    // Parks the clock so the NEXT sample is a step boundary. That is the right
    // start-of-phrase behaviour: the first note of a chord lands at once,
    // rather than up to a whole step later while a free-running grid comes
    // round.
    void reset() noexcept
    {
        samplesUntilNextStep = 0.0;
        stepIndex = 0;
    }

    // AUDIO THREAD, once per block. Both inputs are read RAW from their
    // atomics by the caller - a rate and a discrete switch, neither smoothed.
    void setTempo (double bpm, StepDivision division) noexcept
    {
        const auto clampedBpm = juce::jlimit (minBpm, maxBpm, bpm);

        samplesPerStep = juce::jmax (minSamplesPerStep,
                                      sampleRate * (60.0 / clampedBpm)
                                                 * beatsPerStepForDivision (division));

        // A tempo or division change is a TIME CONSTANT change - the same
        // category as the ADSR times, lfoRateHz and glideTimeSeconds. It
        // alters the length of FUTURE steps and never the position of the one
        // in flight, so the countdown is deliberately left alone: no rescale,
        // no restart, no special-casing.
        //
        // That falls out of storing "samples remaining" rather than a 0..1
        // phase - a phase would rescale the in-flight step implicitly, which
        // is the wrong behaviour arrived at by accident.
        //
        // ONE exception, and it is a usability valve rather than a maths one:
        // if the new step is SHORTER than what is left of the current one, a
        // big tempo jump would otherwise stall for up to a whole OLD step -
        // four seconds at 20 BPM - before anything responded. In the steady
        // state the countdown is always <= samplesPerStep already, so this is
        // a no-op and calling setTempo every block costs nothing.
        samplesUntilNextStep = juce::jmin (samplesUntilNextStep, samplesPerStep);
    }

    // Whole-sample distance to the next step boundary. Never negative.
    int getSamplesUntilNextStep() const noexcept
    {
        if (samplesUntilNextStep <= 0.0)
            return 0;

        // Rounded to NEAREST, not truncated: a boundary at 5512.5 samples has
        // to fire on 5512 or 5513, and nearest halves the worst-case error to
        // half a sample (11us at 44.1kHz).
        //
        // This rounding is a READ-ONLY PROJECTION and is never fed back - only
        // the double accumulator advances - so the errors cancel instead of
        // accumulating. See advanceStep().
        return (int) (samplesUntilNextStep + 0.5);
    }

    // Move time forward WITHOUT crossing a boundary. The caller chunks its
    // block so that is always true.
    void advance (int numSamples) noexcept
    {
        samplesUntilNextStep -= (double) numSamples;
    }

    // Consume one step boundary.
    //
    // `+=`, NEVER `=`. The fractional remainder of the previous step has to
    // carry into the next one or the clock drifts. With samplesPerStep =
    // 5512.5 the fire offsets alternate 5513, 5512, 5513, ... and average
    // exactly 5512.5. Assigning instead would truncate half a sample every
    // step - about 8 samples per second at 1/16 120BPM, 36ms over a
    // three-minute jam - and would be COMPLETELY INAUDIBLE while being wrong.
    // That is why this has an assertion (runStepClockSelfTest) rather than a
    // listening test.
    void advanceStep() noexcept
    {
        samplesUntilNextStep += samplesPerStep;
        ++stepIndex;
    }

    double getSamplesPerStep() const noexcept { return samplesPerStep; }

    // Free-running, never wrapped. Item 7 takes stepIndex % patternLength for
    // its pattern position, and character-and-vim.md B2's swing takes its
    // parity from here - which is why the counter lives in the clock rather
    // than in whatever is sequencing.
    std::uint64_t getStepIndex() const noexcept { return stepIndex; }

private:
    static constexpr double defaultBpm = 120.0;
    static constexpr double minBpm = 20.0;
    static constexpr double maxBpm = 300.0;

    // Bounds the caller's sub-block loop. Nothing musical comes near it - the
    // fastest reachable step is 300BPM 1/32 at 44.1kHz, which is 1102 samples
    // - so it exists purely so a future parameter-range mistake cannot degrade
    // that loop into per-sample render calls.
    static constexpr double minSamplesPerStep = 16.0;

    double sampleRate = 44100.0;
    double samplesPerStep = 5512.5;

    // Fractional, and allowed to go slightly negative (to about -0.5) between
    // the rounded fire point and advanceStep(). getSamplesUntilNextStep()
    // clamps that to 0.
    double samplesUntilNextStep = 0.0;

    std::uint64_t stepIndex = 0;
};

//==============================================================================
#if JUCE_DEBUG

/*
    Debug-only self-test, run once at startup.

    This one matters more than the others in the project, because clock drift
    is the one failure mode that CANNOT be caught by ear at all - see
    documents/arpeggiator-design.md section 12. A `=` where a `+=` belongs
    loses about 0.1ms per second, which is below the threshold at which it is
    distinguishable from a human's own timing. The assertion below catches it
    in 100 steps.
*/
inline void runStepClockSelfTest()
{
    constexpr double sr = 44100.0;
    constexpr double expectedSamplesPerStep = 5512.5; // 44100 @ 120BPM, 1/16

    //==========================================================================
    // The half-sample case that makes all of this necessary.
    {
        StepClock clock;
        clock.prepare (sr);
        clock.setTempo (120.0, StepDivision::Sixteenth);

        jassert (std::abs (clock.getSamplesPerStep() - expectedSamplesPerStep) < 1.0e-9);

        // reset() parks on a boundary, so the first step fires immediately.
        jassert (clock.getSamplesUntilNextStep() == 0);
    }

    //==========================================================================
    // THE ANTI-DRIFT ASSERTION - the sharpest test in this item.
    //
    // Fire 100 steps, summing the whole-sample offsets actually handed out.
    // They alternate 5513, 5512, ... and must total exactly 100 * 5512.5.
    // With `=` instead of `+=` in advanceStep(), every offset would be 5513
    // and the total would be 551300 rather than 551250 - out by 50 samples,
    // an error the ear would need an hour of playing to notice.
    {
        StepClock clock;
        clock.prepare (sr);
        clock.setTempo (120.0, StepDivision::Sixteenth);

        auto total = 0.0;

        for (int i = 0; i < 100; ++i)
        {
            clock.advanceStep();

            const auto offset = clock.getSamplesUntilNextStep();
            total += (double) offset;
            clock.advance (offset);
        }

        jassert (std::abs (total - 100.0 * expectedSamplesPerStep) < 1.0);
        jassert (clock.getStepIndex() == 100);
    }

    //==========================================================================
    // TRIPLET IDENTITY. Three triplet steps must fill exactly the straight
    // division above them. Catches a wrong table entry, which is otherwise
    // only findable by ear against a metronome.
    {
        jassert (std::abs (3.0 * beatsPerStepForDivision (StepDivision::EighthTriplet)
                            - beatsPerStepForDivision (StepDivision::Quarter)) < 1.0e-12);

        jassert (std::abs (3.0 * beatsPerStepForDivision (StepDivision::SixteenthTriplet)
                            - beatsPerStepForDivision (StepDivision::Eighth)) < 1.0e-12);

        // Three 1/4T fill TWO beats, not one - the odd one out.
        jassert (std::abs (3.0 * beatsPerStepForDivision (StepDivision::QuarterTriplet)
                            - 2.0 * beatsPerStepForDivision (StepDivision::Quarter)) < 1.0e-12);
    }

    //==========================================================================
    // Straight divisions halve cleanly.
    {
        jassert (std::abs (beatsPerStepForDivision (StepDivision::ThirtyTwoBars)
                            - 2.0 * beatsPerStepForDivision (StepDivision::SixteenBars)) < 1.0e-12);

        jassert (std::abs (beatsPerStepForDivision (StepDivision::SixteenBars)
                            - 2.0 * beatsPerStepForDivision (StepDivision::EightBars)) < 1.0e-12);

        jassert (std::abs (beatsPerStepForDivision (StepDivision::EightBars)
                            - 2.0 * beatsPerStepForDivision (StepDivision::FourBars)) < 1.0e-12);

        jassert (std::abs (beatsPerStepForDivision (StepDivision::FourBars)
                            - 2.0 * beatsPerStepForDivision (StepDivision::TwoBars)) < 1.0e-12);

        jassert (std::abs (beatsPerStepForDivision (StepDivision::TwoBars)
                            - 2.0 * beatsPerStepForDivision (StepDivision::Whole)) < 1.0e-12);

        jassert (std::abs (beatsPerStepForDivision (StepDivision::Whole)
                            - 2.0 * beatsPerStepForDivision (StepDivision::Half)) < 1.0e-12);

        jassert (std::abs (beatsPerStepForDivision (StepDivision::Half)
                            - 2.0 * beatsPerStepForDivision (StepDivision::Quarter)) < 1.0e-12);

        jassert (std::abs (beatsPerStepForDivision (StepDivision::Quarter)
                            - 2.0 * beatsPerStepForDivision (StepDivision::Eighth)) < 1.0e-12);

        jassert (std::abs (beatsPerStepForDivision (StepDivision::Eighth)
                            - 2.0 * beatsPerStepForDivision (StepDivision::Sixteenth)) < 1.0e-12);
    }

    //==========================================================================
    // An out-of-range division index must not read past the table.
    {
        const auto fallback = beatsPerStepForDivision (StepDivision::Sixteenth);
        jassert (beatsPerStepForDivision ((StepDivision) -1) == fallback);
        jassert (beatsPerStepForDivision ((StepDivision) 999) == fallback);
    }

    //==========================================================================
    // END-TO-END through a realistic (and deliberately awkward) block size.
    // 10 seconds at 120BPM 1/16 is 8 steps/sec = 80 steps.
    {
        StepClock clock;
        clock.prepare (sr);
        clock.setTempo (120.0, StepDivision::Sixteenth);

        constexpr int blockSize = 511; // not a power of two, on purpose
        auto samplesRemaining = (int) (10.0 * sr);
        auto stepCount = 0;

        while (samplesRemaining > 0)
        {
            const auto thisBlock = juce::jmin (blockSize, samplesRemaining);
            auto offset = 0;

            while (offset < thisBlock)
            {
                const auto toStep = clock.getSamplesUntilNextStep();

                if (toStep <= 0)
                {
                    ++stepCount;
                    clock.advanceStep();
                    continue;
                }

                const auto chunk = juce::jmin (thisBlock - offset, toStep);
                clock.advance (chunk);
                offset += chunk;
            }

            samplesRemaining -= thisBlock;
        }

        jassert (stepCount >= 79 && stepCount <= 81);
    }

    //==========================================================================
    // setTempo is safe to call every block: an unchanged value must not move
    // the countdown, or the clock would never advance.
    {
        StepClock clock;
        clock.prepare (sr);
        clock.setTempo (120.0, StepDivision::Sixteenth);
        clock.advanceStep();

        const auto before = clock.getSamplesUntilNextStep();

        for (int i = 0; i < 10; ++i)
            clock.setTempo (120.0, StepDivision::Sixteenth);

        jassert (clock.getSamplesUntilNextStep() == before);
    }

    //==========================================================================
    // A LONGER step leaves the in-flight countdown alone - the time-constant
    // behaviour. A SHORTER one clamps it, so a big tempo jump cannot stall.
    {
        StepClock clock;
        clock.prepare (sr);
        clock.setTempo (240.0, StepDivision::ThirtySecond);
        clock.advanceStep();

        const auto before = clock.getSamplesUntilNextStep();
        clock.setTempo (60.0, StepDivision::Quarter); // much longer
        jassert (clock.getSamplesUntilNextStep() == before);
    }
    {
        StepClock clock;
        clock.prepare (sr);
        clock.setTempo (60.0, StepDivision::Quarter);
        clock.advanceStep();

        clock.setTempo (240.0, StepDivision::ThirtySecond); // much shorter
        jassert ((double) clock.getSamplesUntilNextStep() <= clock.getSamplesPerStep() + 1.0);
    }

    //==========================================================================
    // Tempo is clamped, and the step floor holds even at an absurd request.
    {
        StepClock clock;
        clock.prepare (sr);

        clock.setTempo (1.0e6, StepDivision::ThirtySecond);
        jassert (clock.getSamplesPerStep() >= 16.0);

        clock.setTempo (0.0001, StepDivision::Quarter);
        jassert (clock.getSamplesPerStep() > 0.0);
    }
}

#endif
