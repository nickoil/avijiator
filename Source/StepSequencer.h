#pragma once

#include <cstdint>

#include <juce_core/juce_core.h>

#include "DSP/StepClock.h"

class SynthVoice;

//==============================================================================
// Pattern length, v1: exactly 16 steps, single page - paging to longer
// patterns is explicitly out of scope (documents/step-sequencer-design.md
// section 13). Lives here, not inside the class, for the same reason
// numArpPatterns sits outside Arpeggiator: VoiceParameters.h needs it to size
// the pattern-storage arrays, and it reuses this constant rather than
// re-declaring it - see the comment on that include there.
static constexpr int seqMaxSteps = 16;

//==============================================================================
/*
    Plays a fixed-length pattern back on its own musical clock - the second
    StepClock owner that class already names for itself
    (Source/DSP/StepClock.h:54-65). A peer class owned by MainComponent,
    exactly like Arpeggiator: dsp-voice-design.md section 6 and
    Arpeggiator.h's own header comment both say items 5 and 7 become this
    kind of class, not part of the voice.

    Step 1 (documents/step-sequencer-design.md section 11) built clock
    ownership and the pattern-index arithmetic. Step 2 added process(): its
    own sub-block render loop, copying Arpeggiator.cpp's loop SHAPE
    (StepClock.h:54-65 says not to share it until there are two real users,
    and now there are). Step 4 added releaseVoice below and wired this class
    into the real 3-way renderVoiceBlock hand-over
    (Arpeggiator.h/.cpp, documents/step-sequencer-design.md section 6) - this
    is reachable from MainComponent's real audio callback, not just from the
    self-tests. Step 5 makes process()'s two filter-lane reads actually reach
    the DSP, via SynthVoice::setStepFilterModulation - see that method's own
    doc comment for the summing points it feeds. Pattern data itself lives in
    VoiceParameters (struct-of-arrays, see
    documents/step-sequencer-design.md section 4), not here - this class owns
    only the clock, the gate, and the render loop over that storage.

    AUDIO-THREAD-PRIVATE, like Arpeggiator and NoteStack.
*/
class StepSequencer
{
public:
    StepSequencer() = default;

    //==============================================================================
    void prepare (double newSampleRate) noexcept;

    // Clears the clock. Mirrors Arpeggiator::reset - called from
    // releaseResources (S8) and from prepare (S9, where a stale clock state
    // would be in the OLD sample rate's samples). Later steps add clearing
    // the gate and the walker here, same shape as Arpeggiator::reset.
    void reset() noexcept;

    /*
        Render one block, splitting it at every step boundary and every gate
        close - identical loop SHAPE to Arpeggiator::process
        (Arpeggiator.cpp:165-331), copied rather than shared per StepClock.h's
        own comment on why. Reads the pattern straight out of VoiceParameters
        by index rather than taking a HeldNotes span - the step sequencer has
        no keyboard input of its own, unlike the arp.

        Build step 2 (documents/step-sequencer-design.md section 3): rest and
        gate handling, slide via the arp's Tie idea ("skip the force-close
        while still gated" - Arpeggiator.cpp:274-278's four lines, the exact
        ones arpeggiator-design.md section 10 names as Tie mode's), accent
        hardcoded to a fixed elevated velocity (depth routing was build step
        3). Cutoff and resonance are read every step boundary, independent of
        gate state - a rest can still sweep the filter - and build step 5
        pushes both into SynthVoice::setStepFilterModulation right here,
        rather than reading and discarding them. Build step 6 adds one more
        write at the same point: VoiceParameters::currentStepForUi, the
        pattern grid's playhead - the only field in this class that exists
        for the UI thread rather than the DSP.

        Writes into output, does not add to it - same contract as
        SynthVoice::renderNextBlock and Arpeggiator::process.

        AUDIO THREAD - called from renderVoiceBlock (build step 4) whenever
        the sequencer owns the voice, and directly by
        runStepSequencerRenderSelfTest. Allocation-free; the number of
        iterations is bounded by numSamples / minSamplesPerStep + 2, same
        termination proof as the arp's loop.
    */
    void process (SynthVoice& voice, float* output, int numSamples) noexcept;

    /*
        Stop driving the voice, leaving it SILENT.

        Build step 4 (documents/step-sequencer-design.md sections 6 and 7):
        the second half of the 3-way hand-over renderVoiceBlock now performs.
        IDEMPOTENT, mirroring Arpeggiator::releaseVoice - MainComponent calls
        this, router.releaseVoice and arp.releaseVoice on EVERY change of
        voice owner, and exactly one of the three actually does anything.
        That is what makes a stuck note unreachable by any toggle order
        rather than merely unlikely - see S1-S4.

        Also parks the clock on a step boundary, so switching the sequencer
        back on starts the pattern from step 0 with the first step firing at
        once (S3) rather than resuming mid-cycle from a clock that may have
        been running silently for minutes. No latch/phrase state to clear
        here unlike the arp's own releaseVoice - the pattern itself lives in
        VoiceParameters, not in per-run walker state, so there is nothing
        else to reset - except build step 6's currentStepForUi, which IS
        reset here: the grid's playhead must not keep pointing at a step
        that stopped playing the moment ownership moved elsewhere.

        Unlike NoteRouter, there is no matching retakeVoice: the sequencer's
        own clock parks on a boundary right here, so the very next process()
        call opens the first step at once - the same reason Arpeggiator never
        needed one either. A retakeVoice would have nothing to do.

        AUDIO THREAD.
    */
    void releaseVoice (SynthVoice& voice) noexcept;

    //==============================================================================
    /*
        Pattern position for a given clock step: stepIndex % patternLength,
        per StepClock.h:159-163's own doc comment on how a second owner
        should read step position.

        patternLength arrives from a std::atomic<int> the message thread
        writes, so it is defensively clamped to [1, seqMaxSteps] rather than
        trusted - same posture as beatsPerStepForDivision's table lookup and
        arpPatternFromIndex's enum conversion. An out-of-range length falls
        back to the full seqMaxSteps rather than risking a divide-by-zero or
        an out-of-bounds pattern read.

        Static and free of instance state so the self-test can drive it
        directly, and because it is pure arithmetic - it does not need the
        clock itself, only a step index already read from one.
    */
    static int patternIndexFor (std::uint64_t stepIndex, int patternLength) noexcept;

    // Read-only. Not yet used outside the self-test - step 2's render loop
    // is what actually reads and advances this every block.
    const StepClock& getClock() const noexcept { return clock; }

private:
    // Same clamp range and reasoning as Arpeggiator::minGateFraction/
    // maxGateFraction - the raw atomic is written by the message thread, and
    // 0 or 1 would both break the render loop. 100% is deliberately
    // unreachable, same reasoning as the arp's: it is not "a longer gate" but
    // a different feature (a note that never closes on its own).
    static constexpr float minGateFraction = 0.05f;
    static constexpr float maxGateFraction = 0.95f;

    // Hardcoded starting point (documents/step-sequencer-design.md section 3,
    // "taste - see section 12"). Build step 3 replaces these with depth
    // knobs wired into SynthVoice's currentVelocity consumers; until then
    // every accented step plays at exactly 1.0 and every plain step at
    // exactly 0.75, with no in-between.
    static constexpr float normalStepVelocity = 0.75f;
    static constexpr float accentedStepVelocity = 1.0f;

    static int gateSamplesForStep (double samplesPerStep, float gateFraction) noexcept;

    StepClock clock;

    // THE SECOND DEADLINE, same reasoning as Arpeggiator's own gateIsOpen/
    // samplesUntilGateOff (Arpeggiator.h:243-251): folding this into the
    // clock as an alternating "next event" flag breaks the moment a rest
    // falls while a previous note's gate is still counting down.
    bool gateIsOpen = false;
    int samplesUntilGateOff = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StepSequencer)
};

//==============================================================================
#if JUCE_DEBUG

/*
    Debug-only self-test, run once at startup.

    Covers what step 1 actually adds: pattern storage read/write by index
    (each of the six per-step arrays is independently addressable, with no
    aliasing between them), the stepIndex % patternLength wrap - including a
    defensively clamped out-of-range length - and rest handling, i.e. that a
    freshly-constructed pattern defaults to silence (every stepGateOn entry
    off) rather than an armed pattern nobody asked for. Nothing here plays
    audio yet - see documents/step-sequencer-design.md sections 4 and 10.
*/
void runStepSequencerPatternSelfTest();

/*
    Debug-only self-test, run once at startup.

    Covers what step 2 actually adds: process()'s render loop, driven through
    the real SynthVoice with a fixed test pattern and a peak-amplitude meter -
    same "output is the only thing that proves it" approach as
    runArpTransitionSelfTest, not a re-implementation of the loop it checks.
    Rest handling (an all-rest pattern never gates, silence proven exactly
    rather than approximately - the ADSR is linear, same reasoning as the
    arp's silence checks), grid phase held through rests (stepIndex advances
    by exactly the block count even with nothing gated), a single gated step
    among rests actually sounds and then settles, accent is likewise proven
    inert (velocity routing was step 3), and slide is proven to take
    SynthVoice's legato branch rather than force-closing - exercised via the
    one scenario that can actually make "skip the force-close" observable
    under constant gate-fraction arithmetic: a mid-gate tempo drop, the same
    edge case arpeggiator-design.md section 3 documents for the arp's own
    force-close.

    Cutoff/resonance were proven byte-identically INERT when this test was
    first written (build step 2, not yet summed into any DSP) - build step 5
    flipped that same block to the opposite assertion, on purpose, exactly as
    its own comment predicted: SynthVoice::setStepFilterModulation now makes
    those two lane values reach the filter, so an extreme setting no longer
    renders identically to the default. See runFilterAutomationSelfTest
    (SynthVoice.h) for the summing-point-level proof; this test is the
    integration-level one - that StepSequencer::process actually calls the
    setter with the right per-step values, not just that the formula itself
    is correct in isolation.
*/
void runStepSequencerRenderSelfTest();

/*
    Debug-only self-test, run once at startup.

    Covers what step 4 actually adds: releaseVoice and the sequencer's two
    new legs of the 3-way renderVoiceBlock hand-over (Arpeggiator.h/.cpp) -
    S1-S4 of documents/step-sequencer-design.md section 7, the ones that
    actually exercise new code. (S5-S12 either need no new code path - S6/S7
    fall out of process() already reading pattern data fresh at every step
    boundary, S10 is documented-not-coded, S11/S12 are seq-internal behaviour
    already covered by runStepSequencerRenderSelfTest and by Hold simply never
    being read while the seq drives - or mirror an arp transition already
    exhaustively covered by runArpTransitionSelfTest, S8/S9 being T7/T8 with
    an extra seq.reset()/seq.prepare() in the ordering.)

    Same "output is the only thing that proves it" approach as
    runArpTransitionSelfTest, driving whole blocks through the REAL
    renderVoiceBlock with a real SynthVoice, NoteRouter and Arpeggiator -
    because a stuck note is a property of the seam between all four, not of
    any one of them, same reasoning as that test's own header comment.
*/
void runSeqTransitionSelfTest();

#endif
