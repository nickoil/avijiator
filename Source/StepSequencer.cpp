#include "StepSequencer.h"

#include <array>
#include <cmath>

#include "DSP/SynthVoice.h"

#if JUCE_DEBUG
 #include "Arpeggiator.h"
 #include "NoteRouter.h"
 #include "DSP/NoteEvent.h"
#endif

void StepSequencer::prepare (double newSampleRate) noexcept
{
    clock.prepare (newSampleRate);
    reset();
}

void StepSequencer::reset() noexcept
{
    // T7/T9-shaped, ahead of step 4 actually wiring releaseResources/prepare
    // into this: a stale samplesUntilGateOff is a count of samples at the
    // OLD sample rate, meaningless after a device change, same reasoning as
    // Arpeggiator::reset.
    gateIsOpen = false;
    samplesUntilGateOff = 0;

    clock.reset();
}

int StepSequencer::gateSamplesForStep (double samplesPerStep, float gateFraction) noexcept
{
    const auto stepSamples = (int) (samplesPerStep + 0.5);
    const auto gate = (int) ((double) stepSamples * (double) gateFraction + 0.5);

    // Identical clamp to Arpeggiator::gateSamplesForStep (Arpeggiator.cpp:139-163)
    // and the same two reasons: the lower bound is load-bearing for loop
    // termination (a zero-length gate would never render a sample), and the
    // upper bound keeps the gate strictly inside its step - StepClock's
    // 16-sample floor guarantees stepSamples - 1 >= 1 here.
    return juce::jlimit (1, stepSamples - 1, gate);
}

void StepSequencer::process (SynthVoice& voice, const NoteStack::Resolution& liveResolution,
                              float* output, int numSamples) noexcept
{
    auto& parameters = voice.getParameters();

    // Time constant and discrete switch, both read raw once per block, never
    // smoothed - identical reasoning to Arpeggiator::process's own tempo/
    // division read. Safe to call every block: an unchanged value does not
    // move the countdown (runStepClockSelfTest).
    clock.setTempo ((double) parameters.seqTempoBpm.load (std::memory_order_relaxed),
                     (StepDivision) parameters.seqDivision.load (std::memory_order_relaxed));

    const auto patternLength = parameters.seqPatternLength.load (std::memory_order_relaxed);

    // A FRACTION of the step, not a time, same reasoning as arpGateLength.
    const auto gateFraction = juce::jlimit (minGateFraction, maxGateFraction,
                                             parameters.seqGateLength.load (std::memory_order_relaxed));

    // Discrete switch, raw once per block - same treatment as every other
    // on/off atomic read above. Build step 7: documents/step-sequencer-design.md
    // section 8.
    const auto recordArmed = parameters.seqRecordArmed.load (std::memory_order_relaxed) != 0;

    auto offset = 0;

    while (offset < numSamples)
    {
        const auto remaining = numSamples - offset;
        const auto toStep = clock.getSamplesUntilNextStep();

        // TWO INDEPENDENT DEADLINES, take the min - identical shape to
        // Arpeggiator::process. `remaining` stands in for "no deadline"
        // while the gate is shut.
        const auto toGateOff = gateIsOpen ? samplesUntilGateOff : remaining;

        // CLOSE BEFORE OPEN, unconditional on there being a note to open -
        // T3's reasoning applies here identically: a rest can fall while a
        // previous note's gate-off is still pending, and that deadline has
        // to fire regardless.
        if (gateIsOpen && toGateOff <= 0)
        {
            voice.noteOff();
            gateIsOpen = false;
            continue;                   // this sample may ALSO be a step boundary
        }

        if (toStep <= 0)
        {
            const auto index = (size_t) patternIndexFor (clock.getStepIndex(), patternLength);

            // Build step 6: the UI-facing playhead. Written every step
            // boundary regardless of gate state, same reasoning as the
            // filter lanes below - a rest is still a step the grid should
            // visibly move through. Relaxed: SynthPanel's Timer only ever
            // reads it for a highlight repaint, never anything
            // audio-affecting.
            parameters.currentStepForUi.store ((int) index, std::memory_order_relaxed);

            // Read every step boundary, independent of gate state - a rest
            // can still sweep the filter (documents/step-sequencer-design.md
            // section 3). Build step 5: now actually pushed into SynthVoice's
            // own smoothed summing points via setStepFilterModulation,
            // rather than read-and-discarded.
            voice.setStepFilterModulation (parameters.stepCutoffNorm[index].load (std::memory_order_relaxed),
                                            parameters.stepResonanceNorm[index].load (std::memory_order_relaxed));

            // Build step 7 (pitch entry): overwrite THIS step's pitch/gate
            // with whatever is currently held, before reading them back for
            // playback two lines down - so a freshly recorded step sounds
            // its own new value at once rather than one lap late. Deliberately
            // only these two fields - stepAccent/stepSlide are left exactly
            // as they were, still hand-edited from the grid only. No key
            // held writes a REST (gateOn = false), overwriting whatever gate
            // state the step had before - documents/step-sequencer-design.md
            // section 8's settled answer to "no key pressed at a boundary".
            // Nothing here ever clears seqRecordArmed, so a pattern shorter
            // than the phrase being played keeps being re-captured lap after
            // lap for as long as it stays on - the settled "wraps, does not
            // auto-stop" answer, for free.
            if (recordArmed)
            {
                parameters.stepPitchLog2Hz[index].store (liveResolution.pitchLog2Hz, std::memory_order_relaxed);
                parameters.stepGateOn[index].store (liveResolution.isSounding ? 1 : 0, std::memory_order_relaxed);
            }

            const auto gateOn = parameters.stepGateOn[index].load (std::memory_order_relaxed) != 0;

            if (gateOn)
            {
                const auto slide = parameters.stepSlide[index].load (std::memory_order_relaxed) != 0;
                const auto accented = parameters.stepAccent[index].load (std::memory_order_relaxed) != 0;
                const auto pitch = parameters.stepPitchLog2Hz[index].load (std::memory_order_relaxed);

                // THE TIE IDEA, reused verbatim from the arp
                // (arpeggiator-design.md section 10): skipping the
                // force-close here is what lets SynthVoice see voiceGated
                // still true below and take its legato branch instead of a
                // fresh trigger. Under constant tempo this is a no-op the
                // same way Arpeggiator's own force-close mostly is - the
                // gate-fraction clamp above guarantees a note's own auto-close
                // always lands strictly before the next boundary, so "still
                // gated here" is reached the same way the arp's is: a
                // mid-gate tempo drop pulling the next boundary in front of a
                // pending gate-off (Arpeggiator.cpp section 3's third
                // reason). runStepSequencerRenderSelfTest exercises exactly
                // that case.
                if (! slide && gateIsOpen)
                {
                    voice.noteOff();
                    gateIsOpen = false;
                }

                // Velocity hardcoded per build step 2 - build step 3 replaces
                // this with depth-knob routing into currentVelocity's DSP
                // consumers (SynthVoice.cpp:183-188).
                voice.noteOn (pitch, accented ? accentedStepVelocity : normalStepVelocity);

                // Re-derived from the CURRENT step length, every step - same
                // reasoning as the arp's identical line.
                samplesUntilGateOff = gateSamplesForStep (clock.getSamplesPerStep(), gateFraction);
                gateIsOpen = true;
            }

            // Advances even on a rest, to keep grid phase - identical
            // reasoning to Arpeggiator::process.
            clock.advanceStep();
            continue;
        }

        // TERMINATION IS PROVABLE - identical proof to Arpeggiator::process:
        // closing the gate clears gateIsOpen so it cannot re-fire until a
        // step reopens it, advanceStep() adds at least minSamplesPerStep, and
        // the gate is clamped to [1, stepSamples - 1]. Every path leaves both
        // deadlines >= 1, chunk >= 1, and offset strictly increases.
        const auto chunk = juce::jmin (remaining, toStep, toGateOff);
        jassert (chunk > 0);

        voice.renderNextBlock (output + offset, chunk);
        clock.advance (chunk);

        if (gateIsOpen)
            samplesUntilGateOff -= chunk;

        offset += chunk;
    }
}

void StepSequencer::releaseVoice (SynthVoice& voice) noexcept
{
    // THE HAND-OVER INVARIANT, identical to Arpeggiator::releaseVoice: whichever
    // side stops driving the voice leaves it silent, whichever side takes over
    // starts from silence. Guarded, which is what makes this idempotent -
    // calling it after the router or the arp has already started something new
    // must not send a stray note-off onto a note that is no longer this
    // sequencer's to close.
    if (gateIsOpen)
    {
        voice.noteOff();
        gateIsOpen = false;
    }

    samplesUntilGateOff = 0;

    // Build step 6: the UI playhead is only meaningful while THIS sequencer
    // owns the voice - leaving it pointing at a stale index after a hand-over
    // away would highlight a step that is not actually playing. Unconditional,
    // not guarded like the note-off above: idempotently re-storing -1 is
    // harmless even when it was already -1.
    voice.getParameters().currentStepForUi.store (-1, std::memory_order_relaxed);

    // Park the clock on a boundary and abandon the current pass through the
    // pattern, so switching the sequencer back on starts the next phrase at
    // step 0 with the first step firing at once (S3), rather than resuming
    // mid-cycle from a clock that may have kept running silently for minutes.
    clock.reset();
}

int StepSequencer::patternIndexFor (std::uint64_t stepIndex, int patternLength) noexcept
{
    const auto clamped = (patternLength >= 1 && patternLength <= seqMaxSteps) ? patternLength
                                                                               : seqMaxSteps;
    return (int) (stepIndex % (std::uint64_t) clamped);
}

//==============================================================================
#if JUCE_DEBUG

void runStepSequencerPatternSelfTest()
{
    //==========================================================================
    // REST HANDLING: a freshly-constructed pattern is silent everywhere.
    // Matches every other "off on first load" atomic in VoiceParameters
    // (arpEnabled, seqEnabled itself) - the instrument must not start
    // sequencing notes nobody programmed.
    {
        VoiceParameters params;

        // Pitch is the one exception to "zero-init is silent": stepGateOn off
        // is what actually keeps a fresh pattern silent, so the housekeeping
        // fix that gave stepPitchLog2Hz a real default (C2, VoiceParameters'
        // own constructor) doesn't change what this block is proving.
        const auto defaultPitch = pitchLog2HzForMidiNote (VoiceParameters::defaultStepMidiNote);

        for (int i = 0; i < seqMaxSteps; ++i)
        {
            jassert (params.stepGateOn[(size_t) i].load() == 0);
            jassert (params.stepAccent[(size_t) i].load() == 0);
            jassert (params.stepSlide[(size_t) i].load() == 0);
            jassert (params.stepPitchLog2Hz[(size_t) i].load() == defaultPitch);
            jassert (params.stepCutoffNorm[(size_t) i].load() == 0.0f);
            jassert (params.stepResonanceNorm[(size_t) i].load() == 0.0f);
        }

        jassert (params.seqEnabled.load() == 0);
    }

    //==========================================================================
    // STORAGE READ/WRITE BY INDEX, no aliasing between the six parallel
    // arrays. Each array is written a DIFFERENT pattern so a copy-paste bug
    // that wrote one array's value into another's slot would be caught here
    // rather than only showing up as a wrong note or a wrong sweep by ear.
    {
        VoiceParameters params;

        for (int i = 0; i < seqMaxSteps; ++i)
        {
            const auto idx = (size_t) i;

            params.stepPitchLog2Hz[idx].store (8.0f + 0.1f * (float) i);
            params.stepGateOn[idx].store (i % 2);
            params.stepAccent[idx].store ((i % 3) == 0 ? 1 : 0);
            params.stepSlide[idx].store ((i % 4) == 0 ? 1 : 0);
            params.stepCutoffNorm[idx].store (0.01f * (float) i);
            params.stepResonanceNorm[idx].store (1.0f - 0.01f * (float) i);
        }

        for (int i = 0; i < seqMaxSteps; ++i)
        {
            const auto idx = (size_t) i;

            jassert (std::abs (params.stepPitchLog2Hz[idx].load() - (8.0f + 0.1f * (float) i)) < 1.0e-6f);
            jassert (params.stepGateOn[idx].load() == i % 2);
            jassert (params.stepAccent[idx].load() == ((i % 3) == 0 ? 1 : 0));
            jassert (params.stepSlide[idx].load() == ((i % 4) == 0 ? 1 : 0));

            // FILTER-LANE READ-BACK, specifically checked against each other
            // and against the note fields at the SAME index - the two lanes
            // are deliberately given values that would collide (0.15 vs 0.85
            // at i == 15 is fine, but the point is they are read from
            // genuinely separate storage, not derived from one another).
            jassert (std::abs (params.stepCutoffNorm[idx].load() - 0.01f * (float) i) < 1.0e-6f);
            jassert (std::abs (params.stepResonanceNorm[idx].load() - (1.0f - 0.01f * (float) i)) < 1.0e-6f);
        }
    }

    //==========================================================================
    // stepIndex % patternLength WRAP - the exact arithmetic
    // StepClock.h:159-163 hands the second owner.
    {
        // Full 16-step pattern: wraps every 16 steps.
        jassert (StepSequencer::patternIndexFor (0, 16) == 0);
        jassert (StepSequencer::patternIndexFor (15, 16) == 15);
        jassert (StepSequencer::patternIndexFor (16, 16) == 0);
        jassert (StepSequencer::patternIndexFor (31, 16) == 15);
        jassert (StepSequencer::patternIndexFor (32, 16) == 0);

        // A shorter pattern length wraps sooner - this is what lets a
        // pattern-length change (S6) take effect at the next wrap rather
        // than needing any special-casing in the index arithmetic itself.
        jassert (StepSequencer::patternIndexFor (0, 4) == 0);
        jassert (StepSequencer::patternIndexFor (3, 4) == 3);
        jassert (StepSequencer::patternIndexFor (4, 4) == 0);
        jassert (StepSequencer::patternIndexFor (10, 4) == 2);

        // A one-step "pattern" always reads index 0 - the degenerate case a
        // mod-by-zero bug would instead crash or UB on.
        jassert (StepSequencer::patternIndexFor (0, 1) == 0);
        jassert (StepSequencer::patternIndexFor (123, 1) == 0);
    }

    //==========================================================================
    // DEFENSIVE CLAMP: an out-of-range patternLength (as could arrive from a
    // stale or corrupt atomic) must not divide by zero or index out of
    // bounds - it falls back to the full seqMaxSteps, same posture as
    // beatsPerStepForDivision's table lookup.
    {
        jassert (StepSequencer::patternIndexFor (16, 0) == 0);
        jassert (StepSequencer::patternIndexFor (16, -1) == 0);
        jassert (StepSequencer::patternIndexFor (16, 999) == 0);
        jassert (StepSequencer::patternIndexFor (17, 0) == 1);
    }

    //==========================================================================
    // OWN CLOCK: prepare/reset behave like Arpeggiator's - parked on a
    // boundary so the first step of a phrase fires at once.
    {
        StepSequencer seq;
        seq.prepare (44100.0);

        jassert (seq.getClock().getStepIndex() == 0);
        jassert (seq.getClock().getSamplesUntilNextStep() == 0);
    }
}

//==============================================================================
namespace
{
    // A bench for the render loop only - no NoteRouter, no Arpeggiator, no
    // hand-over, since that seam is build step 4's
    // (documents/step-sequencer-design.md section 11). Same idea as
    // Arpeggiator.cpp's TransitionRig: drive the REAL voice and the REAL
    // StepSequencer a block at a time and read the rendered output, because
    // "did it actually play" (or "did it stay silent") is an OUTPUT property,
    // not something a flag can prove.
    class SequencerRenderRig
    {
    public:
        SequencerRenderRig()
        {
            voice.prepare (sampleRate);
            seq.prepare (sampleRate);

            auto& p = voice.getParameters();

            // Amp destination, same reasoning as the arp's rig: with Filter
            // the VCA sits at unity regardless of gate state, so silence
            // would prove nothing either way.
            p.envelopeDestination.store ((int) EnvelopeDestination::Amp);

            p.attackSeconds.store (0.001f);
            p.decaySeconds.store (0.001f);
            p.sustainLevel.store (0.8f);
            p.releaseSeconds.store (0.002f);

            // Well clear of Nyquist at this test rate, low resonance so
            // nothing can ring on past the envelope - identical reasoning to
            // the arp's rig.
            p.cutoffLog2Hz.store (10.0f);
            p.resonance.store (0.1f);
            p.outputLevel.store (0.5f);
            p.sawLevel.store (0.7f);

            // 300 BPM / 1/32 -> 400 samples/step at this sample rate, exactly
            // (16000 * 0.2 * 0.125) - chosen so test windows below are exact
            // sample counts, not rounded ones.
            p.seqTempoBpm.store (300.0f);
            p.seqDivision.store ((int) StepDivision::ThirtySecond);
            p.seqGateLength.store (0.5f);
            p.seqPatternLength.store (seqMaxSteps);

            clearPattern();
        }

        VoiceParameters& parameters() noexcept { return voice.getParameters(); }

        void clearPattern() noexcept
        {
            auto& p = parameters();

            for (int i = 0; i < seqMaxSteps; ++i)
            {
                const auto idx = (size_t) i;
                p.stepGateOn[idx].store (0);
                p.stepAccent[idx].store (0);
                p.stepSlide[idx].store (0);
                p.stepPitchLog2Hz[idx].store (8.0f);
                p.stepCutoffNorm[idx].store (0.0f);
                p.stepResonanceNorm[idx].store (0.0f);
            }
        }

        void setStep (int index, float pitchLog2Hz, bool gateOn, bool accent, bool slide,
                      float cutoffNorm = 0.0f, float resonanceNorm = 0.0f) noexcept
        {
            auto& p = parameters();
            const auto idx = (size_t) index;

            p.stepPitchLog2Hz[idx].store (pitchLog2Hz);
            p.stepGateOn[idx].store (gateOn ? 1 : 0);
            p.stepAccent[idx].store (accent ? 1 : 0);
            p.stepSlide[idx].store (slide ? 1 : 0);
            p.stepCutoffNorm[idx].store (cutoffNorm);
            p.stepResonanceNorm[idx].store (resonanceNorm);
        }

        std::uint64_t stepIndex() const noexcept { return seq.getClock().getStepIndex(); }

        // process() splits numSamples at every step boundary internally -
        // this just forwards to it against the bench's own voice.
        //
        // `liveResolution` defaults to not-sounding (NoteStack::Resolution's
        // own default construction) so every call site written before build
        // step 7 - none of which passes one - still exercises exactly the
        // "recording off, or recording on with nothing held" path, unchanged.
        void render (float* output, int numSamples,
                     const NoteStack::Resolution& liveResolution = {}) noexcept
        {
            seq.process (voice, liveResolution, output, numSamples);
        }

    private:
        // 16kHz, same reasoning as the arp's rig: this runs at every Debug
        // launch and nothing checked here is rate-dependent.
        double sampleRate = 16000.0;

        SynthVoice voice;
        StepSequencer seq;
    };

    float peakAbs (const float* buffer, int numSamples) noexcept
    {
        auto peak = 0.0f;

        for (int i = 0; i < numSamples; ++i)
            peak = juce::jmax (peak, std::abs (buffer[i]));

        return peak;
    }

}

void runStepSequencerRenderSelfTest()
{
    // Exact, not approximate - the ADSR is linear (Adsr.h:15), so an unfired
    // envelope or a completed Release both land on exactly 0. Same constant
    // and same reasoning as runArpTransitionSelfTest's.
    constexpr float silence = 1.0e-6f;

    // One step at 300BPM/1-32 at 16kHz - see the rig's own comment.
    constexpr int samplesPerStep = 400;

    //==========================================================================
    // REST HANDLING: an all-rest pattern (the rig's default) never gates,
    // over many steps - proven as actual silence in the rendered output, not
    // as an unset flag.
    {
        SequencerRenderRig rig;

        std::array<float, samplesPerStep * 10> buffer {};
        rig.render (buffer.data(), (int) buffer.size());

        jassert (peakAbs (buffer.data(), (int) buffer.size()) <= silence);
    }

    //==========================================================================
    // GRID PHASE HELD THROUGH RESTS: the clock must keep advancing one step
    // at a time even while nothing gates - the exact property
    // "clock.advanceStep() runs even on a rest" exists for. Checked as an
    // EXACT step count, not just "some steps happened".
    {
        SequencerRenderRig rig;

        constexpr int numSteps = 7;
        std::array<float, samplesPerStep * numSteps> buffer {};
        rig.render (buffer.data(), (int) buffer.size());

        jassert (rig.stepIndex() == (std::uint64_t) numSteps);
    }

    //==========================================================================
    // A SINGLE GATED STEP AMONG RESTS: proves gate-on actually triggers a
    // note (peak > silence somewhere in the cycle) and that the sequencer's
    // own gate-close deadline actually fires and the envelope actually
    // settles back to silence well before the pattern wraps - no stuck note
    // in the sequencer's own bookkeeping, checked the same way
    // runArpTransitionSelfTest checks the arp's hand-over.
    {
        SequencerRenderRig rig;
        rig.setStep (5, 8.0f, true, false, false);

        // One full 16-step cycle.
        std::array<float, samplesPerStep * seqMaxSteps> buffer {};
        rig.render (buffer.data(), (int) buffer.size());

        jassert (peakAbs (buffer.data(), (int) buffer.size()) > silence);

        // The last step (15) is a rest, comfortably more than one release
        // (0.002s = 32 samples at 16kHz) after step 5's gate closed - the
        // envelope must have settled back to true silence by then.
        const auto* tail = buffer.data() + samplesPerStep * 15;
        jassert (peakAbs (tail, samplesPerStep) <= silence);
    }

    //==========================================================================
    // CUTOFF/RESONANCE LANES NOW REACH THE DSP (build step 5) - this is the
    // exact test that was byte-identical when written at build step 2, and
    // its own comment at the time predicted step 5 would flip it. That has
    // now happened: SynthVoice::setStepFilterModulation makes both lane
    // values reach the filter, so an extreme setting on step 3 must render
    // DIFFERENTLY from the default rather than identically. This is the
    // INTEGRATION-level proof - that process() actually calls the setter
    // with the pattern's real per-step values - complementing
    // runFilterAutomationSelfTest (SynthVoice.h/.cpp), which proves the
    // summing formula itself in isolation.
    {
        SequencerRenderRig defaultLanes;
        defaultLanes.setStep (3, 8.5f, true, false, false, 0.0f, 0.0f);

        SequencerRenderRig extremeLanes;
        extremeLanes.setStep (3, 8.5f, true, false, false, 1.0f, 1.0f);

        std::array<float, samplesPerStep * seqMaxSteps> bufferA {};
        std::array<float, samplesPerStep * seqMaxSteps> bufferB {};
        defaultLanes.render (bufferA.data(), (int) bufferA.size());
        extremeLanes.render (bufferB.data(), (int) bufferB.size());

        jassert (! (bufferA == bufferB));
    }

    //==========================================================================
    // ACCENT IS LIKEWISE READ BUT INERT THIS BUILD STEP: currentVelocity is
    // captured by SynthVoice::noteOn but not routed anywhere yet
    // (SynthVoice.h:82-86) - build step 3 wires it in, and this is the test
    // that starts failing, on purpose, the day it does.
    {
        SequencerRenderRig plain;
        plain.setStep (3, 8.5f, true, false, false);

        SequencerRenderRig accented;
        accented.setStep (3, 8.5f, true, true, false);

        std::array<float, samplesPerStep * seqMaxSteps> bufferA {};
        std::array<float, samplesPerStep * seqMaxSteps> bufferB {};
        plain.render (bufferA.data(), (int) bufferA.size());
        accented.render (bufferB.data(), (int) bufferB.size());

        jassert (bufferA == bufferB);
    }

    //==========================================================================
    // SLIDE TAKES THE LEGATO BRANCH INSTEAD OF FORCE-CLOSING.
    //
    // Under CONSTANT tempo the gate-fraction clamp guarantees a note's own
    // auto-close always lands strictly before the next step boundary (same
    // property as the arp's identical clamp), so "still gated at the next
    // boundary" - the only condition under which skipping the force-close is
    // observable - is reached the same way arpeggiator-design.md section 3
    // reaches it for the arp: a tempo drop mid-gate, pulling the next
    // boundary in front of a pending gate-off. That is deliberately
    // engineered here rather than hoped for.
    //
    // Legato mode is set explicitly so the distinguishing signal is the
    // envelope, not glide time: SynthVoice::noteOn's overlap branch only
    // skips envelope.noteOn() in Legato (SynthVoice.cpp:70-71) - in the
    // default Retrigger mode a slide would still re-pluck the envelope and
    // the two runs could end up indistinguishable if glide time also
    // happened to be zero.
    {
        const auto runScenario = [] (bool step1Slides, float* output, int numSamples)
        {
            SequencerRenderRig rig;
            rig.parameters().legatoRetriggerMode.store ((int) LegatoRetriggerMode::Legato);

            // A slow step so its gate-off deadline (95% of a HUGE step) is
            // still far from elapsed when tempo drops mid-gate below.
            rig.parameters().seqTempoBpm.store (20.0f);
            rig.parameters().seqDivision.store ((int) StepDivision::Quarter);
            rig.parameters().seqGateLength.store (0.95f);

            rig.setStep (0, 8.0f, true, false, false);
            rig.setStep (1, 9.0f, true, false, step1Slides);

            // Render just past step 0's note-on, well short of its own step
            // boundary (48000 samples away) or its gate-off (45600 away).
            std::array<float, 100> warmup {};
            rig.render (warmup.data(), (int) warmup.size());

            // TEMPO DROPS MID-GATE: step 0's gate-off deadline (a raw sample
            // count) is untouched by this, but the CLOCK's own countdown gets
            // clamped down to the new, much shorter step length
            // (StepClock::setTempo's documented usability valve) - so step
            // 1's boundary now arrives while step 0's gate is still open.
            rig.parameters().seqTempoBpm.store (300.0f);
            rig.parameters().seqDivision.store ((int) StepDivision::ThirtySecond);

            // 400 samples to reach step 1's boundary, plus 50 more to capture
            // the actual transition.
            rig.render (output, numSamples);
        };

        constexpr int windowSamples = 450;
        std::array<float, windowSamples> retriggered {};
        std::array<float, windowSamples> slid {};

        runScenario (false, retriggered.data(), windowSamples);
        runScenario (true, slid.data(), windowSamples);

        // The first 400 samples are identical by construction (both runs are
        // still playing step 0's note, untouched by step 1's slide flag) -
        // the point is that they diverge at all, which only happens if the
        // force-close was genuinely skipped.
        jassert (retriggered != slid);
    }
}

//==============================================================================
namespace
{
    // Bench for build step 4's 3-way hand-over - voice, router, arp and seq
    // exactly as MainComponent wires them, driven through the REAL
    // renderVoiceBlock rather than a copy of it. Same reasoning as
    // Arpeggiator.cpp's own TransitionRig: "no stuck note" is an OUTPUT
    // property, so blocks go in and peak amplitude comes out.
    class SeqTransitionRig
    {
    public:
        SeqTransitionRig()
        {
            auto& p = voice.getParameters();

            // Amp destination, same reasoning as every other transition rig in
            // this codebase: with Filter the VCA sits at unity regardless of
            // gate state, so silence would prove nothing either way.
            p.envelopeDestination.store ((int) EnvelopeDestination::Amp);

            p.attackSeconds.store (0.001f);
            p.decaySeconds.store (0.001f);
            p.sustainLevel.store (0.8f);
            p.releaseSeconds.store (0.002f);

            p.cutoffLog2Hz.store (10.0f);
            p.resonance.store (0.1f);
            p.outputLevel.store (0.5f);
            p.sawLevel.store (0.7f);

            // Fast clocks for both the arp and the seq, same 300BPM/1-32
            // rate as SequencerRenderRig and Arpeggiator.cpp's own rig - a
            // step is a few blocks rather than a fraction of a second of
            // rendering at every Debug launch.
            p.arpTempoBpm.store (300.0f);
            p.arpDivision.store ((int) StepDivision::ThirtySecond);
            p.seqTempoBpm.store (300.0f);
            p.seqDivision.store ((int) StepDivision::ThirtySecond);
            p.seqGateLength.store (0.5f);
            p.seqPatternLength.store (seqMaxSteps);

            // Only step 0 is gated - enough to prove "the sequencer's first
            // step fires at once" (S3) without needing a full pattern. Every
            // other step is a rest.
            for (int i = 0; i < seqMaxSteps; ++i)
            {
                const auto idx = (size_t) i;
                p.stepGateOn[idx].store (i == 0 ? 1 : 0);
                p.stepAccent[idx].store (0);
                p.stepSlide[idx].store (0);
                p.stepPitchLog2Hz[idx].store (8.0f);
            }

            startDevice();
        }

        VoiceParameters& parameters() noexcept { return voice.getParameters(); }

        // Paired stores, exactly what a real mutually-exclusive toggle UI
        // (build step 6) would do on one click - both atoms land together
        // rather than leaving a stale "both on" state for the audio thread to
        // referee. The static seq-wins tie-break inside renderVoiceBlock is a
        // defensive fallback for a state this rig deliberately never produces.
        void switchToArp()  { parameters().seqEnabled.store (0); parameters().arpEnabled.store (1); }
        void switchToSeq()  { parameters().arpEnabled.store (0); parameters().seqEnabled.store (1); }
        void switchToKeys() { parameters().arpEnabled.store (0); parameters().seqEnabled.store (0); }

        void pressKey()
        {
            router.pushUiEvent ({ NoteEvent::Type::NoteOn, (std::uint8_t) testKey,
                                   pitchLog2HzForMidiNote (testKey), 0.8f });
        }

        void releaseKey()
        {
            router.pushUiEvent ({ NoteEvent::Type::NoteOff, (std::uint8_t) testKey, 0.0f, 0.0f });
        }

        // Renders numBlocks and returns the LARGEST sample magnitude seen -
        // same peak-over-a-window reasoning as Arpeggiator.cpp's rig: with
        // the arp or the seq running the gate is shut for part of every step,
        // so a single block proves nothing in either direction.
        float run (int numBlocks)
        {
            auto peak = 0.0f;

            for (int block = 0; block < numBlocks; ++block)
            {
                renderVoiceBlock (voice, router, arp, seq, owner, output.data(), blockSize);

                for (auto sample : output)
                    peak = juce::jmax (peak, std::abs (sample));
            }

            return peak;
        }

    private:
        void startDevice()
        {
            voice.prepare (sampleRate);
            arp.prepare (sampleRate);
            seq.prepare (sampleRate);

            // Forces the first block to re-run the hand-over whichever side
            // is switched on - MainComponent::prepareToPlay does the same.
            owner = VoiceOwner::Keys;
        }

        // 16kHz, not 44.1: this test renders at every Debug launch and
        // nothing checked here is rate-dependent - same reasoning as every
        // other rig in this project.
        double sampleRate = 16000.0;
        static constexpr int blockSize = 64;
        static constexpr int testKey = 60;

        SynthVoice voice;
        NoteRouter router;
        Arpeggiator arp;
        StepSequencer seq;
        VoiceOwner owner = VoiceOwner::Keys;

        std::array<float, (size_t) blockSize> output {};
    };
}

void runSeqTransitionSelfTest()
{
    // Exact, not approximate - the ADSR is linear, so Release lands on 0 and
    // the VCA multiplies the whole signal by it. Same constant and reasoning
    // as runArpTransitionSelfTest's.
    constexpr float silence = 1.0e-6f;

    //==========================================================================
    // S1 - Seq ON while Arp already ON. Mutual exclusion: turning seq on
    // releases the arp first. Failure mode is a stuck note: if the arp's own
    // gate were not force-closed, or the router's retake later found stale
    // state, a hand-back to keys would either drop the still-held note or
    // never re-sound it.
    {
        SeqTransitionRig rig;
        rig.pressKey();
        rig.switchToArp();
        jassert (rig.run (8) > silence);        // arp is driving the held key

        rig.switchToSeq();                      // S1
        jassert (rig.run (4) > silence);        // seq's own step 0 fires at once

        rig.switchToKeys();
        jassert (rig.run (1) > silence);        // T2-shaped: still-held key retaken AT ONCE

        rig.releaseKey();
        rig.run (4);
        jassert (rig.run (4) <= silence);       // nothing left stuck
    }

    //==========================================================================
    // S2 - Arp ON while Seq already ON. Symmetric to S1.
    {
        SeqTransitionRig rig;
        rig.switchToSeq();
        jassert (rig.run (4) > silence);        // seq's step 0, no key needed

        rig.pressKey();
        rig.switchToArp();                      // S2
        jassert (rig.run (8) > silence);        // arp now driving the held key

        rig.switchToKeys();
        jassert (rig.run (1) > silence);

        rig.releaseKey();
        rig.run (4);
        jassert (rig.run (4) <= silence);
    }

    //==========================================================================
    // S3 - Seq ON while a key is held/sounding. router.releaseVoice silences
    // the router's note; seq.releaseVoice parks the clock at step 0, so the
    // first step fires at once rather than waiting for the free-running grid
    // to come round (mirrors arp T1/T10).
    {
        SeqTransitionRig rig;
        rig.pressKey();
        jassert (rig.run (2) > silence);        // router driving the held key

        rig.switchToSeq();                      // S3
        jassert (rig.run (1) > silence);        // step 0 fires in this very block

        rig.switchToKeys();
        rig.releaseKey();
        rig.run (4);
        jassert (rig.run (4) <= silence);
    }

    //==========================================================================
    // S4 - Seq OFF mid-step, gate open. seq.releaseVoice force-closes the
    // gate unconditionally (the guard is on gateIsOpen alone - a pending
    // slide flag plays no part, since slide only changes what happens at the
    // sequencer's OWN next step boundary, never what an external release
    // does); router.retakeVoice re-asserts the still-held key at once
    // (mirrors arp T2).
    {
        SeqTransitionRig rig;
        rig.pressKey();
        rig.parameters().seqGateLength.store (0.95f);   // gate stays open long
        rig.switchToSeq();
        jassert (rig.run (1) > silence);        // step 0 open, well short of its own gate-off

        rig.switchToKeys();                     // S4: interrupts mid-gate
        jassert (rig.run (1) > silence);        // held key retaken at once

        rig.releaseKey();
        rig.run (4);
        jassert (rig.run (4) <= silence);       // nothing stuck from the interrupted step
    }
}

//==============================================================================
namespace
{
    // Build step 7's own helper - a "someone is playing this note" stand-in,
    // matching NoteStack::Resolution's field order (isSounding, noteNumber,
    // pitchLog2Hz, velocity). The note number and velocity are never read by
    // recording (only pitch and gate are captured - see process()'s own
    // comment), so both are fixed, deliberately-arbitrary values here.
    NoteStack::Resolution soundingAt (float pitchLog2Hz) noexcept
    {
        return { true, 60, pitchLog2Hz, 0.8f };
    }
}

void runStepRecordSelfTest()
{
    // One step at 300BPM/1-32 at 16kHz, same rig and same exact-sample-count
    // reasoning as runStepSequencerRenderSelfTest.
    constexpr int samplesPerStep = 400;

    //==========================================================================
    // DISARMED: a sounding resolution reaches process() every block, exactly
    // as it would while actually recording, but seqRecordArmed is off - the
    // gate this whole feature hangs off. Pattern storage must be untouched,
    // proven byte-identical across the WHOLE pattern rather than just the
    // step visited, so a bug that wrote to the wrong index would still be
    // caught.
    {
        SequencerRenderRig rig;

        auto& p = rig.parameters();
        std::array<float, seqMaxSteps> pitchBefore {};
        std::array<int, seqMaxSteps> gateBefore {};
        for (int i = 0; i < seqMaxSteps; ++i)
        {
            pitchBefore[(size_t) i] = p.stepPitchLog2Hz[(size_t) i].load();
            gateBefore[(size_t) i] = p.stepGateOn[(size_t) i].load();
        }

        std::array<float, samplesPerStep * 3> buffer {};
        rig.render (buffer.data(), (int) buffer.size(), soundingAt (9.5f));

        for (int i = 0; i < seqMaxSteps; ++i)
        {
            jassert (p.stepPitchLog2Hz[(size_t) i].load() == pitchBefore[(size_t) i]);
            jassert (p.stepGateOn[(size_t) i].load() == gateBefore[(size_t) i]);
        }
    }

    //==========================================================================
    // ARMED + SOUNDING: the current step's pitch and gate are overwritten
    // with the live resolution's values.
    {
        SequencerRenderRig rig;
        rig.parameters().seqRecordArmed.store (1);

        std::array<float, samplesPerStep> buffer {};
        rig.render (buffer.data(), (int) buffer.size(), soundingAt (9.5f));

        jassert (std::abs (rig.parameters().stepPitchLog2Hz[0].load() - 9.5f) < 1.0e-6f);
        jassert (rig.parameters().stepGateOn[0].load() == 1);
    }

    //==========================================================================
    // ARMED + NOTHING HELD: records a REST, overwriting a step that was
    // previously gated - documents/step-sequencer-design.md section 8's
    // settled answer to "no key pressed at a boundary" (leave gateOn false),
    // proven here as an actual OVERWRITE, not just "already false".
    {
        SequencerRenderRig rig;
        rig.setStep (0, 8.0f, true, false, false); // pre-seed step 0 gated
        rig.parameters().seqRecordArmed.store (1);

        std::array<float, samplesPerStep> buffer {};
        rig.render (buffer.data(), (int) buffer.size()); // default resolution: not sounding

        jassert (rig.parameters().stepGateOn[0].load() == 0);
    }

    //==========================================================================
    // ACCENT AND SLIDE SURVIVE: recording only ever touches pitch and gate -
    // a step's own accent/slide flags, set by hand from the grid, must not be
    // disturbed by a live-record pass over the same step.
    {
        SequencerRenderRig rig;
        rig.setStep (0, 8.0f, false, true, true); // accent + slide pre-set, gate off
        rig.parameters().seqRecordArmed.store (1);

        std::array<float, samplesPerStep> buffer {};
        rig.render (buffer.data(), (int) buffer.size(), soundingAt (9.0f));

        jassert (rig.parameters().stepAccent[0].load() == 1);
        jassert (rig.parameters().stepSlide[0].load() == 1);
        // Gate and pitch DID change, same proof as the ARMED + SOUNDING case
        // above - confirms this run actually recorded rather than trivially
        // leaving everything alone.
        jassert (rig.parameters().stepGateOn[0].load() == 1);
        jassert (std::abs (rig.parameters().stepPitchLog2Hz[0].load() - 9.0f) < 1.0e-6f);
    }

    //==========================================================================
    // WRAPS RATHER THAN AUTO-STOPPING: documents/step-sequencer-design.md
    // section 8's other open question. A short, 2-step pattern so a full lap
    // is cheap to render; step 0 is visited once per lap (global step indices
    // 0 and 2). First call records resolution A into it; second call - AFTER
    // the wrap - must have overwritten it with resolution B, and
    // seqRecordArmed must still read as on, since nothing in process() ever
    // clears it.
    {
        SequencerRenderRig rig;
        rig.parameters().seqPatternLength.store (2);
        rig.parameters().seqRecordArmed.store (1);

        std::array<float, samplesPerStep> firstStep {};
        rig.render (firstStep.data(), (int) firstStep.size(), soundingAt (7.0f));
        jassert (std::abs (rig.parameters().stepPitchLog2Hz[0].load() - 7.0f) < 1.0e-6f);

        // Two more step boundaries: global step 1 (pattern index 1), then
        // global step 2 - pattern index 0 again, the wrap.
        std::array<float, samplesPerStep * 2> nextTwoSteps {};
        rig.render (nextTwoSteps.data(), (int) nextTwoSteps.size(), soundingAt (11.0f));

        jassert (std::abs (rig.parameters().stepPitchLog2Hz[0].load() - 11.0f) < 1.0e-6f);
        jassert (rig.parameters().seqRecordArmed.load() == 1);
    }
}

#endif
