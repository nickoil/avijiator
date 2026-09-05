#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <span>

#include <juce_core/juce_core.h>

#include "DSP/Humanise.h"
#include "DSP/NoiseGenerator.h"
#include "DSP/NoteStack.h"
#include "DSP/StepClock.h"

class NoteRouter;
class SynthVoice;
class StepSequencer;

//==============================================================================
/*
    Which order the arpeggiator walks the held notes in.

    Stored as a plain std::atomic<int> in VoiceParameters, no smoother - a
    discrete switch, exactly like envelopeDestination, lfoWaveform and
    notePriorityMode.
*/
enum class ArpPattern : int
{
    Up = 0,
    Down,
    UpDown,
    Random,
    AsPlayed
};

static constexpr int numArpPatterns = 5;

/*
    Bounds-checked conversion from the raw atomic. Same defensive posture as
    beatsPerStepForDivision's table lookup: the index arrives from a
    std::atomic<int> the message thread writes, so a stale or out-of-range
    value must not reach the walker on the audio thread.
*/
inline constexpr ArpPattern arpPatternFromIndex (int index) noexcept
{
    return (index >= 0 && index < numArpPatterns) ? (ArpPattern) index : ArpPattern::Up;
}

//==============================================================================
/*
    Plays the held notes back as a pattern on a musical clock.

    A peer class owned by MainComponent - dsp-voice-design.md section 6 keeps
    the voice free of patterns and clocks, and says items 5 and 7 become
    exactly this kind of class. It owns no DSP: it decides WHICH note happens
    WHEN, and drives SynthVoice through the same noteOn/noteOff boundary a key
    press uses.

    AUDIO-THREAD-PRIVATE, like NoteStack and SynthVoice's own gated flag.

    THE HELD SET IS NOT COPIED HERE. NoteStack stays owned by NoteRouter and
    the arp only ever reads it, through NoteStack::getHeldNotes() - see
    documents/arpeggiator-design.md sections 5 and 6.

    Build step 6 of 7 (documents/arpeggiator-design.md section 11): step 5
    added Hold and the idle fast path; step 6 walks section 7's T1-T10
    transition table against the real code rather than trusting it, and turns
    it into runArpTransitionSelfTest below. Two defects came out of that walk
    and are fixed here - see Arpeggiator::releaseVoice (a latched chord
    surviving the arp being switched off) and the gate close at the top of
    process()'s step branch (a step opening a note on top of an already-open
    gate after a tempo jump).
*/
class Arpeggiator
{
public:
    // Explicit, for the same reason as NoteRouter's and SynthVoice's:
    // JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR below declares a deleted
    // copy constructor, which suppresses the implicit default one.
    Arpeggiator() = default;

    // A READ-ONLY view of the notes to arpeggiate, in PRESS ORDER, oldest
    // first - exactly what NoteStack::getHeldNotes() hands out. Not copied:
    // the stack stays owned by NoteRouter, and this is read at every step on
    // the audio thread.
    using HeldNotes = std::span<const NoteStack::HeldNote>;

    //==============================================================================
    void prepare (double newSampleRate) noexcept;

    // Clears the gate, the clock and the walker. Called from releaseResources
    // (T7) and from prepare (T8, where a stale samplesUntilGateOff would be in
    // the OLD sample rate's samples).
    //
    // Deliberately does NOT touch the voice: MainComponent's teardown order is
    // voice.reset(); router.reset(); arp.reset(); so the voice is already
    // silent by the time this runs.
    void reset() noexcept;

    /*
        Stop driving the voice, leaving it SILENT.

        IDEMPOTENT, which is the whole point: MainComponent calls this and
        NoteRouter::releaseVoice on every arp on/off transition, and exactly
        one of them actually does anything. That is what makes a stuck note
        unreachable by any toggle order rather than merely unlikely - see
        documents/arpeggiator-design.md sections 6 and 7 (T1, T2).

        Also ends the PHRASE, not just the note: the clock is parked on a
        boundary so the first step of the next phrase fires at once instead of
        waiting for a free-running grid to come round, the walker is reset, and
        the latched (hold) set is cleared. That last one is what stops a chord
        latched before the arp was switched off from resurrecting when it is
        switched back on - see the comment in the implementation.

        AUDIO THREAD.
    */
    void releaseVoice (SynthVoice& voice) noexcept;

    /*
        Render one block, splitting it at every step boundary and every gate
        close, and driving the voice through the same noteOn/noteOff boundary a
        key press uses.

        `liveNotes` is NoteRouter's held set this block, sampled once - key
        events are drained at BLOCK granularity (item 4's deliberate scope), so
        steps are sample-accurate while the human's first key press still
        quantises to a block boundary. A few milliseconds, dwarfed by human
        timing jitter, but written down rather than implied.

        Reads arpHold itself, same as every other arp atomic - resolveActiveNotes
        below turns liveNotes into the set actually walked this step, which is
        the live set with hold off and the latched-or-live set with hold on.

        Writes into output, does not add to it - same contract as
        SynthVoice::renderNextBlock, which this calls with advancing pointers.

        AUDIO THREAD. Allocation-free; the number of iterations is bounded by
        numSamples / minSamplesPerStep + 2.
    */
    void process (SynthVoice& voice, HeldNotes liveNotes, float* output, int numSamples) noexcept;

    //==============================================================================
    /*
        THE WALKER. Given the notes active this step, in PRESS ORDER, return
        the index of the one to play - or -1 if nothing is held.

        The central idea (documents/arpeggiator-design.md section 4): the held
        set changes size between steps, so an index into it is ill-defined
        while a COMPARISON KEY is always well-defined. The entire walker state
        is therefore the note number last played, plus a direction. Each step
        asks a QUERY about the current set rather than incrementing a cursor,
        and everything awkward falls out free:

          - releasing the note just played is harmless: lastNoteNumber is only
            a key, it need not still be held;
          - a note added mid-pattern lands in PITCH POSITION on the very next
            step rather than being appended at the end;
          - changing pattern mid-run needs no re-initialisation;
          - no index is ever clamped, so the classic "released a note and the
            arp skipped or stuttered" bug is structurally impossible.

        Ordering compares noteNumber (an int), NEVER pitchLog2Hz (a float).
        They are monotonically related so it is the same order, NoteStack
        de-dupes note numbers so ties are impossible, and there is no reason to
        take a float-comparison risk. Cost is O(n) with n <= 16, about twenty
        times a second - nothing.

        AUDIO THREAD. Allocation-free and branch-bounded; mutates walker state,
        hence non-const.
    */
    int chooseNextIndex (std::span<const NoteStack::HeldNote> active, ArpPattern pattern) noexcept;

    /*
        Abandon the current phrase, so the next step starts at a defined end
        (lowest for Up and UpDown, highest for Down, oldest press for
        AsPlayed) rather than from a lastNoteNumber that may be minutes old.

        Same reasoning as SynthVoice snapping the pitch on a fresh trigger from
        silence. Step 3's Arpeggiator::reset() calls this alongside clearing
        the gate and the clock.
    */
    void resetPattern() noexcept;

    /*
        THE HOLD RULE (documents/arpeggiator-design.md section 8), REVISED after
        the original "replace wholesale on every non-empty live set" rule proved
        audibly wrong: releasing a real chord finger-by-finger (never all on the
        same sample) made the latch shrink on every release and freeze at
        whichever single note came up last, instead of the chord that was
        actually played. GROW, not replace, is the fix:

        - Hold off: identity function. `numLatched` is zeroed on every hold-off
          call rather than only on the falling edge, and `latchAwaitingFreshChord`
          is armed - so engaging hold later cannot latch a stale chord (T4b), and
          the active set empties at once rather than sustaining the old latch
          forever (T4d).
        - Hold on, live empty (T5): the latch is left exactly as it is, and
          `latchAwaitingFreshChord` is armed for next time - the live set going
          empty ends the current phrase without disturbing what it latched.
        - Hold on, live non-empty, a fresh phrase (`latchAwaitingFreshChord`):
          the latch is REPLACED wholesale (T6) - discards whatever the previous,
          already-finished phrase left behind.
        - Hold on, live non-empty, mid-phrase: the latch is a UNION - any live
          note not already in it is added; nothing is ever removed while the
          phrase is in progress. This is what makes T4a correct for a real hand:
          holding C+E+G and lifting fingers one at a time keeps all three
          latched right up to the last one, because each intermediate live set
          (`{C,E}`, then `{C}`) is already a subset of what is latched, so the
          union adds nothing and removes nothing.

        Known, accepted limitation carried over from the original design: two
        chords played back-to-back with fingers overlapping (never a fully
        empty live set between them) will union together rather than one
        replacing the other - the arp samples the stack once per block and
        genuinely cannot see an empty-then-pressed pair inside one block. Taste,
        not a bug; flip-able by ear in step 6/7 if it reads wrong in play.

        Public, like chooseNextIndex and resetPattern, so the self-test can
        exercise the rule directly without a SynthVoice.

        AUDIO THREAD. Allocation-free (bounded by NoteStack::maxHeldNotes);
        mutates the latch, hence non-const.
    */
    HeldNotes resolveActiveNotes (HeldNotes liveNotes, bool holdEnabled) noexcept;

    //==============================================================================
    /*
        Save/load plumbing for the latched (Hold) chord - documents/
        settings-persistence-design.md section 5. New for item 9:
        latched/numLatched/latchAwaitingFreshChord (below) are plain,
        audio-thread-private data with zero public accessors otherwise, and a
        preset save/load happens on the message thread.

        Plain data, no atomics itself - the atomics are the backing storage
        on Arpeggiator (see getLatchSnapshot/requestLatchLoad), this is just
        the value type passed between threads.
    */
    struct LatchSnapshot
    {
        std::array<std::uint8_t, (size_t) NoteStack::maxHeldNotes> noteNumbers {};
        std::array<float, (size_t) NoteStack::maxHeldNotes> pitchesLog2Hz {};
        std::array<float, (size_t) NoteStack::maxHeldNotes> velocities {};
        int numLatched = 0;
        bool awaitingFreshChord = true;
    };

    /*
        ANY THREAD (in practice, the message thread, at the moment of a
        preset save). Reads the save-side atomics the audio thread keeps
        fresh (publishLatchSnapshot, called every block from process() -
        the latch changes rarely, but a save only ever wants the LATEST
        state, not a history of every change, so there is no FIFO here).

        A torn read across slots is harmless, same "torn read is fine"
        convention this codebase already applies to the step-sequencer
        arrays (VoiceParameters.h) - this is a rare, one-shot,
        user-initiated read, not a hot path needing double-buffering.
    */
    LatchSnapshot getLatchSnapshot() const noexcept;

    /*
        MESSAGE THREAD only. Stores the snapshot into a pending-load buffer
        and arms hasPendingLatchLoad; process() picks it up at the top of the
        next block, entirely on the audio thread (the same "check a flag at
        block boundary" shape VoiceOwner's hand-over already uses). This
        preserves the existing invariant that latched/numLatched/
        latchAwaitingFreshChord are touched from the audio thread only - the
        message thread only ever writes the separate pending-snapshot state
        below.

        ORDERING REQUIREMENT ON THE CALLER: a preset load also loads
        arpEnabled/arpHold (ordinary VoiceParameters atomics). This must be
        called no later than those are written, so both land together by the
        next block boundary.
    */
    void requestLatchLoad (const LatchSnapshot& snapshot) noexcept;

private:
    //==============================================================================
    // How much of a step the note actually sounds for. Clamped rather than
    // trusted: the raw atomic is written by the message thread, and 0 or 1
    // would both break the render loop - see gateSamplesForStep().
    static constexpr float minGateFraction = 0.05f;
    static constexpr float maxGateFraction = 0.95f;

    static int gateSamplesForStep (double samplesPerStep, float gateFraction) noexcept;

    int startIndexFor (HeldNotes active, ArpPattern pattern) noexcept;

    // Uniform over the notes that are NOT excludeNoteNumber. Pass -1 to
    // exclude nothing (no note number is negative).
    int chooseRandomIndex (HeldNotes active, int excludeNoteNumber) noexcept;

    //==============================================================================
    StepClock clock;

    // THE SECOND DEADLINE. Written as its own countdown rather than folded
    // into the clock as an alternating "next event" flag, because the
    // alternation between note-on and note-off breaks in three reachable
    // cases - all keys released mid-step (T3), tempo raised mid-gate, and a
    // future Tie mode - one of which is a stuck note forever. See
    // documents/arpeggiator-design.md section 3.
    bool gateIsOpen = false;
    int samplesUntilGateOff = 0;

    //==============================================================================
    // THE THIRD DEADLINE (character-and-vim.md B2, item 10). A step boundary
    // that chose a note to play does not necessarily fire it immediately any
    // more - Humanise::onsetDelaySamples can push it a few samples LATE
    // (swing, timing jitter), and this is the countdown for that, same
    // "independent countdown, take the min" shape as samplesUntilGateOff
    // above. At humaniseAmount == 0 the delay is always exactly 0 (see that
    // function's own comment), so noteOnPending is never set true and this
    // whole mechanism is dead code - the step-boundary branch takes the
    // EXACT same immediate-fire statements process() always has. Pitch and
    // velocity are captured at scheduling time, not re-derived at fire time -
    // `active` is a view into state (NoteStack/the arp's own latch) that
    // could theoretically change in the few samples between scheduling and
    // firing, and the note that was actually CHOSEN is the one that must
    // sound.
    bool noteOnPending = false;
    int samplesUntilNoteOn = 0;
    float pendingPitchLog2Hz = 0.0f;
    float pendingVelocity = 0.0f;

    // Distinct seed, same reasoning as randomSource below and every other
    // seeded generator in this codebase - this must not emit the same
    // sequence as the Random pattern's own walker noise.
    NoiseGenerator humaniseNoise { 0x7c1f9a3du };

    //==============================================================================
    // int, not uint8_t, so "no note played yet" is representable.
    int lastNoteNumber = -1;

    bool goingUp = true;            // UpDown only
    bool patternIsRunning = false;

    //==============================================================================
    // T5: the arp's OWN copy of the held set, sized to NoteStack's cap - it
    // must not live in NoteStack, because the whole point is that it survives
    // the live stack going empty. See resolveActiveNotes and
    // documents/arpeggiator-design.md section 8.
    std::array<NoteStack::HeldNote, (size_t) NoteStack::maxHeldNotes> latched {};
    int numLatched = 0;

    // True whenever the NEXT non-empty live set should REPLACE the latch
    // rather than being unioned into it - set whenever the live set is seen
    // empty (T5's phrase boundary) or hold is off, cleared once a fresh
    // phrase has replaced the latch. See resolveActiveNotes.
    bool latchAwaitingFreshChord = true;

    //==============================================================================
    // Save-side mirror of latched/numLatched/latchAwaitingFreshChord above -
    // documents/settings-persistence-design.md section 5. Kept fresh every
    // block from process() (publishLatchSnapshot(), called right after
    // resolveActiveNotes - cheap, and simpler than only publishing on an
    // actual change). int, not uint8_t, for the note-number atomics - same
    // "guaranteed lock-free" reasoning VoiceParameters.h gives for using int
    // over smaller/bool atomics everywhere else in this codebase.
    std::array<std::atomic<int>, (size_t) NoteStack::maxHeldNotes> latchedNoteNumberAtomics {};
    std::array<std::atomic<float>, (size_t) NoteStack::maxHeldNotes> latchedPitchAtomics {};
    std::array<std::atomic<float>, (size_t) NoteStack::maxHeldNotes> latchedVelocityAtomics {};
    std::atomic<int> numLatchedAtomic { 0 };
    std::atomic<bool> latchAwaitingFreshChordAtomic { true };

    void publishLatchSnapshot() noexcept;

    // Message-thread -> audio-thread handoff for a preset load (section 5).
    // pendingLatchLoad itself is plain data, not atomics - hasPendingLatchLoad
    // is the single flag gating it, so a release store here paired with the
    // acquire exchange in process() is what makes reading it back safe.
    LatchSnapshot pendingLatchLoad {};
    std::atomic<bool> hasPendingLatchLoad { false };

    // The arp's OWN generator, with a seed distinct from the audible noise
    // source, the filter's floor noise and the LFO's sample-and-hold - which
    // is exactly why NoiseGenerator takes a seed. juce::Random is avoided for
    // the same reason it is everywhere else here: a fixed seed makes an A/B
    // between two builds an actual comparison.
    NoiseGenerator randomSource { 0x1b56c4e9u };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Arpeggiator)
};

//==============================================================================
/*
    Which of the three note sources currently owns the voice's pitch/gate.
    Item 7 build step 4 (documents/step-sequencer-design.md sections 6 and 7)
    extends the arp's original two-way hand-over (a plain arpWasOn bool) to
    this three-way one - raw keys, the arp, or the step sequencer, arp and
    seq being a mutually exclusive note source per that document's section 1.
*/
enum class VoiceOwner : int { Keys = 0, Arp, Seq };

/*
    ONE BLOCK of the note-input -> voice path: the keys/arp/seq hand-over, the
    event drain, and the render - everything MainComponent::getNextAudioBlock
    does apart from the AudioBuffer plumbing and the mono fan-out.

    A free function rather than lines inside getNextAudioBlock purely so the
    transition self-tests can drive the REAL hand-over instead of a copy of
    it. A test that re-implements the logic it is checking proves nothing,
    and the hand-over is precisely where a stuck note comes from - see
    documents/arpeggiator-design.md section 7 and
    documents/step-sequencer-design.md section 7. Ownership is unchanged:
    MainComponent still owns the voice, the router, the arp, the sequencer and
    currentOwner, and passes them in.

    `currentOwner` is IN/OUT: it is the edge-detect state that used to be the
    bare arpWasOn bool, audio-thread-private, and belongs to whoever is
    calling blocks in sequence. Seq wins an arbitrary but deterministic
    tie-break if both arpEnabled and seqEnabled are ever seen on at once (not
    a case the real toggle UI, build step 6, is meant to produce) - see the
    definition.

    Writes into output, does not add to it.

    AUDIO THREAD.
*/
void renderVoiceBlock (SynthVoice& voice, NoteRouter& router, Arpeggiator& arp, StepSequencer& seq,
                        VoiceOwner& currentOwner, float* output, int numSamples) noexcept;

//==============================================================================
#if JUCE_DEBUG

/*
    Debug-only self-test, run once at startup.

    Like the clock's, this exists because the failure mode is quiet: a walker
    that skips a note, stutters, or drops a note from the cycle when the chord
    changes sounds like a playing mistake rather than a bug. The changing-set
    recipes below are the ones an index-based walker fails.

    See documents/arpeggiator-design.md sections 4 and 12.
*/
void runArpPatternSelfTest();

/*
    Debug-only self-test, run once at startup: section 7's T1-T10 transition
    table, plus a deterministic stuck-note fuzz.

    The other self-tests in this item check components in isolation. This one
    drives whole blocks through renderVoiceBlock - the real hand-over, the real
    router, the real voice - because a stuck note is not a property of any one
    of those, it is a property of the seam between them, and the failure is
    reached by a TOGGLE ORDER rather than by any single call being wrong.

    "No stuck note" is asserted as actual silence in the rendered output rather
    than as a flag, which is the same criterion section 12 gives the human at
    the speakers. It is exact rather than approximate: the ADSR is linear, so
    Release lands on 0 and the VCA multiplies the whole signal by it.

    See documents/arpeggiator-design.md sections 7 and 12.
*/
void runArpTransitionSelfTest();

/*
    Debug-only self-test, run once at startup.

    Covers character-and-vim.md B2's own instance: the third-deadline onset
    delay in Arpeggiator::process. Proves, in order: humaniseAmount == 0
    renders byte-identical to before this feature existed (a real render
    through renderVoiceBlock, not a re-implementation of the scheduling); a
    turned-up amount delays note onsets without ever losing one (the same
    peak-amplitude-in-a-window technique runArpTransitionSelfTest's own rig
    uses); and no stuck note is introduced by the extra pending-onset state
    across a hand-over mid-delay - the one new way this item could reintroduce
    the exact failure mode runArpTransitionSelfTest exists to rule out.
*/
void runArpHumaniseSelfTest();

#endif
