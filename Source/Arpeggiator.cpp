#include "Arpeggiator.h"

#include <array>
#include <cmath>

#include "NoteRouter.h"
#include "StepSequencer.h"
#include "DSP/SynthVoice.h"

namespace
{
    using HeldSpan = std::span<const NoteStack::HeldNote>;

    // All five helpers below assume a NON-EMPTY set, which chooseNextIndex
    // guarantees before it calls any of them. They compare note NUMBERS, never
    // pitchLog2Hz - see the comment on chooseNextIndex.

    int indexOfLowest (HeldSpan active) noexcept
    {
        int best = 0;

        for (size_t i = 1; i < active.size(); ++i)
            if (active[i].noteNumber < active[(size_t) best].noteNumber)
                best = (int) i;

        return best;
    }

    int indexOfHighest (HeldSpan active) noexcept
    {
        int best = 0;

        for (size_t i = 1; i < active.size(); ++i)
            if (active[i].noteNumber > active[(size_t) best].noteNumber)
                best = (int) i;

        return best;
    }

    // STRICTLY above: the note just played must not be chosen again, which is
    // what makes Up advance rather than stick.
    int indexOfSmallestAbove (HeldSpan active, int key) noexcept
    {
        int best = -1;

        for (size_t i = 0; i < active.size(); ++i)
        {
            const int candidate = active[i].noteNumber;

            if (candidate > key && (best < 0 || candidate < (int) active[(size_t) best].noteNumber))
                best = (int) i;
        }

        return best;
    }

    int indexOfLargestBelow (HeldSpan active, int key) noexcept
    {
        int best = -1;

        for (size_t i = 0; i < active.size(); ++i)
        {
            const int candidate = active[i].noteNumber;

            if (candidate < key && (best < 0 || candidate > (int) active[(size_t) best].noteNumber))
                best = (int) i;
        }

        return best;
    }

    int indexOfNoteNumber (HeldSpan active, int noteNumber) noexcept
    {
        for (size_t i = 0; i < active.size(); ++i)
            if ((int) active[i].noteNumber == noteNumber)
                return (int) i;

        return -1;
    }
}

//==============================================================================
void Arpeggiator::prepare (double newSampleRate) noexcept
{
    clock.prepare (newSampleRate);

    // T8: samplesUntilGateOff is a count of samples at the OLD rate, so it is
    // meaningless after a device change. Clearing it is not tidying.
    reset();
}

void Arpeggiator::reset() noexcept
{
    gateIsOpen = false;
    samplesUntilGateOff = 0;

    // T7: a restart must not resurrect a chord latched before the device
    // stopped.
    numLatched = 0;
    latchAwaitingFreshChord = true;
    publishLatchSnapshot();

    clock.reset();
    resetPattern();
}

void Arpeggiator::releaseVoice (SynthVoice& voice) noexcept
{
    // THE HAND-OVER INVARIANT: whichever side stops driving the voice leaves
    // it silent, and whichever side takes over starts from silence.
    //
    // Guarded, which is what makes this idempotent - calling it twice must not
    // send a second note-off, or a note-off could land on a note the ROUTER
    // has since started.
    if (gateIsOpen)
    {
        voice.noteOff();
        gateIsOpen = false;
    }

    samplesUntilGateOff = 0;

    // THE LATCH IS PHRASE STATE, exactly like lastNoteNumber below, and has to
    // go with it. Found by walking section 7's table in build step 6: hold is
    // maintained by resolveActiveNotes, which only runs while the arp is ON -
    // so the "numLatched is zeroed on every hold-off call" mechanism that makes
    // T4b safe is FROZEN for as long as the arp is off. Without this, latch a
    // chord, switch the arp off, lift every key, switch the arp back on, and a
    // chord from a phrase that ended minutes ago starts playing with nothing
    // held. That is T4b's hazard arriving through a different door.
    numLatched = 0;
    latchAwaitingFreshChord = true;
    publishLatchSnapshot();

    // Park the clock on a boundary and abandon the phrase, so switching the
    // arp on starts the next chord immediately and from a defined end rather
    // than mid-cycle from a key that could be minutes old.
    clock.reset();
    resetPattern();
}

int Arpeggiator::gateSamplesForStep (double samplesPerStep, float gateFraction) noexcept
{
    const auto stepSamples = (int) (samplesPerStep + 0.5);

    // OPPOSITE ROUNDING TREATMENT TO THE CLOCK'S, deliberately. The clock's
    // remainder is fractional and CARRIED FORWARD, so its rounding is a
    // read-only projection that is never fed back. The gate's is integral and
    // RE-DERIVED FROM SCRATCH every step, so its rounding error resets each
    // step instead of accumulating. Swapping the two treatments is exactly the
    // silent bug this design is avoiding - see
    // documents/arpeggiator-design.md section 3.
    const auto gate = (int) ((double) stepSamples * (double) gateFraction + 0.5);

    // The lower clamp is load-bearing for TERMINATION, not just musicality: a
    // zero-length gate would open and close on the same sample forever without
    // ever rendering a sample.
    //
    // The upper clamp keeps the gate strictly inside its step. Gate = 100% is
    // deliberately unreachable: it is not "a longer gate" but a change of
    // SEMANTICS, from a fresh press into a legato retarget - a different
    // feature (Tie mode, section 10), not something to smuggle in at the top
    // of a slider. StepClock's 16-sample floor guarantees stepSamples - 1 >= 1
    // here, so the range is never inverted.
    return juce::jlimit (1, stepSamples - 1, gate);
}

void Arpeggiator::process (SynthVoice& voice, HeldNotes liveNotes, float* output, int numSamples) noexcept
{
    // A pending preset-loaded latch (documents/settings-persistence-design.md
    // section 5), checked at the top of every block - same "check a flag at
    // block boundary" shape VoiceOwner's hand-over already uses. The acquire
    // half of the release in requestLatchLoad(), so pendingLatchLoad's plain
    // (non-atomic) fields are guaranteed visible once this observes true.
    if (hasPendingLatchLoad.exchange (false, std::memory_order_acquire))
    {
        numLatched = pendingLatchLoad.numLatched;

        for (int i = 0; i < numLatched; ++i)
            latched[(size_t) i] = { pendingLatchLoad.noteNumbers[(size_t) i],
                                     pendingLatchLoad.pitchesLog2Hz[(size_t) i],
                                     pendingLatchLoad.velocities[(size_t) i] };

        latchAwaitingFreshChord = pendingLatchLoad.awaitingFreshChord;
        publishLatchSnapshot(); // so a save immediately after this load sees it too
    }

    auto& parameters = voice.getParameters();

    // Tempo and division are a TIME CONSTANT and a discrete switch: read raw
    // once per block, never smoothed. Smoothing the tempo would be actively
    // wrong rather than merely wasteful - it is consumed once per STEP, not
    // per sample, so a smoother would low-pass a value nobody reads
    // continuously and make the knob lag for no benefit.
    //
    // Safe to call every block: an unchanged value does not move the countdown
    // (asserted in runStepClockSelfTest).
    clock.setTempo ((double) parameters.masterTempoBpm.load (std::memory_order_relaxed),
                     (StepDivision) parameters.arpDivision.load (std::memory_order_relaxed));

    const auto pattern = arpPatternFromIndex (parameters.arpPattern.load (std::memory_order_relaxed));

    // The global octave transpose (VoiceParameters::masterOctaveShift),
    // applied to every arp note-on below - see documents/
    // note-handling-design.md section 7's revision.
    const auto octaveShift = (float) parameters.masterOctaveShift.load (std::memory_order_relaxed);

    // A FRACTION of the step rather than a time, so changing tempo does not
    // also change articulation.
    const auto gateFraction = juce::jlimit (minGateFraction, maxGateFraction,
                                             parameters.arpGateLength.load (std::memory_order_relaxed));

    // Discrete switch, raw per block - same treatment as every other atomic
    // here.
    const auto holdEnabled = parameters.arpHold.load (std::memory_order_relaxed) != 0;

    // publishLatchSnapshot() lives INSIDE resolveActiveNotes now (its own
    // single exit point), not called separately here - see that function's
    // comment. This keeps the save-side atomics fresh whether
    // resolveActiveNotes is reached via process() or (documents/
    // settings-persistence-design.md section 10's own test approach) called
    // directly.
    const auto active = resolveActiveNotes (liveNotes, holdEnabled);

    // T10: THE IDLE FAST PATH. With nothing to arpeggiate this block AND no
    // gate pending, park the clock on a boundary and skip the grid entirely
    // rather than let it free-run through samples nothing will ever play.
    // Without this, a chord pressed after a pause could land anywhere up to a
    // full step late - the clock would already be mid-cycle by the time a
    // note exists to play it against.
    //
    // Gated on `! gateIsOpen` for T3: a note that just started still has a
    // pending note-off even if the set that started it has since emptied, and
    // that deadline must be honoured by the normal loop below, not skipped by
    // taking a shortcut here.
    //
    // Called every idle block rather than only on the falling edge - clock.reset()
    // is idempotent, and this is the same "depends only on current state, never
    // on the event stream" posture as resolveActiveNotes above. The result:
    // however many silent blocks pass, the clock is still parked whenever a
    // chord next appears, so the first note lands at the top of the block its
    // key event was drained in.
    if (active.empty() && ! gateIsOpen)
    {
        clock.reset();
        voice.renderNextBlock (output, numSamples);
        return;
    }

    auto offset = 0;

    while (offset < numSamples)
    {
        const auto remaining = numSamples - offset;
        const auto toStep = clock.getSamplesUntilNextStep();

        // TWO INDEPENDENT DEADLINES, take the min - not one alternating
        // countdown. When the gate is shut there is nothing pending, so
        // `remaining` stands in for "no deadline" and the min below ignores it.
        const auto toGateOff = gateIsOpen ? samplesUntilGateOff : remaining;

        // CLOSE BEFORE OPEN. A gate-off and a step landing on the same sample
        // must not note-off the note that just started.
        //
        // Crucially this is NOT conditioned on there being a note to play this
        // step: T3 is exactly the case where every key was released mid-step,
        // so the next step plays nothing while the previous step's gate-off is
        // still pending. An alternating countdown would either skip it - a
        // stuck note forever, the worst failure available here - or fire a
        // phantom.
        if (gateIsOpen && toGateOff <= 0)
        {
            voice.noteOff();
            gateIsOpen = false;
            continue;                   // this sample may ALSO be a step boundary
        }

        if (toStep <= 0)
        {
            const auto index = chooseNextIndex (active, pattern);

            if (index >= 0)
            {
                // A STEP CAN LAND ON TOP OF AN OPEN GATE, and the note-on must
                // still arrive from silence. Section 3 lists the case that gets
                // here - tempo raised mid-gate: gateSamples is fixed at note-on,
                // so setTempo's clamp can pull the next step boundary in FRONT
                // of a pending gate-off. One knob drag reaches it (60BPM 1/4 ->
                // 300BPM 1/32).
                //
                // Without this close, that step's voice.noteOn() arrives GATED
                // and takes SynthVoice's overlap branch instead of its
                // fresh-trigger one - so the pitch glides in rather than
                // snapping, and in Legato mode the envelope does not re-pluck
                // at all. One step articulated differently after a tempo jump:
                // exactly the "sounds slightly off rather than failing" bug
                // this item is written to avoid. Section 10's whole analysis
                // assumes every arp note-on is ungated; this is what makes that
                // true rather than nearly true.
                //
                // Deliberately here rather than unconditionally at the boundary:
                // when nothing is held there is no note to open, and T3 needs
                // that pending gate-off left to its own independent deadline. A
                // future Tie mode (section 10) opts out by skipping these four
                // lines - which is the other reason the loop never assumed the
                // two deadlines alternate.
                if (gateIsOpen)
                {
                    voice.noteOff();
                    gateIsOpen = false;
                }

                // Through voice.noteOn, NOT around it: an arp step genuinely
                // IS a press, and this is the documented reuse boundary.
                // (retargetPitch means "a key came up revealing another still
                // held", which is never true of an arp step.)
                //
                // Consequence, deliberate: since the previous step's noteOff
                // cleared voiceGated, every arp note-on arrives ungated and
                // takes SynthVoice's fresh-trigger branch - so the pitch SNAPS
                // and the envelope always retriggers. Glide Time, Glide Mode
                // and Note Priority therefore do nothing at all while the arp
                // runs. That is the right default (a 200ms glide at 1/16 is a
                // siren, not an arpeggio) but it is worth knowing, since three
                // live-looking controls doing nothing reads as a bug. See
                // documents/arpeggiator-design.md section 10.
                //
                // Velocity travels with the note even though the voice does
                // not route it anywhere yet - item 7's accent and
                // character-and-vim.md B5 are the eventual consumers.
                voice.noteOn (active[(size_t) index].pitchLog2Hz + octaveShift,
                               active[(size_t) index].velocity);

                // Re-derived from the CURRENT step length, every step.
                samplesUntilGateOff = gateSamplesForStep (clock.getSamplesPerStep(), gateFraction);
                gateIsOpen = true;
            }

            // Advances even when nothing played, so the grid keeps phase while
            // the held set is empty.
            clock.advanceStep();
            continue;
        }

        // TERMINATION IS PROVABLE, not hoped for. Every branch above strictly
        // consumes its event - closing the gate clears gateIsOpen so it cannot
        // re-fire until a step reopens it, and advanceStep() adds at least
        // minSamplesPerStep - and the gate is clamped to [1, stepSamples - 1].
        // So every path leaves both deadlines >= 1, chunk >= 1, and offset
        // strictly increases. A step landing exactly on a block boundary gives
        // toStep == 0 and takes the continue above: an EVENT is consumed, not
        // a zero-length render issued.
        const auto chunk = juce::jmin (remaining, toStep, toGateOff);
        jassert (chunk > 0);

        voice.renderNextBlock (output + offset, chunk);
        clock.advance (chunk);

        if (gateIsOpen)
            samplesUntilGateOff -= chunk;

        offset += chunk;
    }
}

//==============================================================================
void Arpeggiator::resetPattern() noexcept
{
    lastNoteNumber = -1;
    goingUp = true;
    patternIsRunning = false;
    randomSource.reset();
}

Arpeggiator::HeldNotes Arpeggiator::resolveActiveNotes (HeldNotes liveNotes, bool holdEnabled) noexcept
{
    // ONE exit point, purely so publishLatchSnapshot() (documents/
    // settings-persistence-design.md section 5) has one call site covering
    // every branch below, rather than one inserted before each of what used
    // to be four independent returns. Behaviour is otherwise unchanged from
    // before section 5 existed.
    HeldNotes result;

    if (! holdEnabled)
    {
        // T4b/T4d: zeroed on every hold-off call, not just the falling edge -
        // see the doc comment on the declaration for why that is what makes
        // both correct with no edge to catch.
        numLatched = 0;
        latchAwaitingFreshChord = true;
        result = liveNotes;
    }
    else if (liveNotes.empty())
    {
        // T5: the phrase just ended. Leave the latch exactly as it was, and
        // arm a REPLACE for whatever the next phrase turns out to be, so it
        // does not get unioned onto this one.
        latchAwaitingFreshChord = true;
        result = { latched.data(), (size_t) numLatched };
    }
    else
    {
        if (latchAwaitingFreshChord)
        {
            // T6: a genuinely fresh phrase. REPLACE wholesale - discard
            // whatever the previous, already-finished phrase left latched.
            numLatched = (int) liveNotes.size();

            for (int i = 0; i < numLatched; ++i)
                latched[(size_t) i] = liveNotes[(size_t) i];

            latchAwaitingFreshChord = false;
        }
        else
        {
            // T4a, and the fix for the reported bug: UNION, never remove. A
            // real hand does not release every finger of a chord on the same
            // sample, so the live set shrinks step by step as it comes up -
            // {C,E,G} then {C,E} then {C} - and each of those is already a
            // SUBSET of what is latched, so this adds nothing and
            // (critically) removes nothing. The old "replace wholesale every
            // non-empty block" rule shrank the latch on every one of those
            // intermediate blocks and froze on whichever single note
            // happened to come up last - audibly wrong, and this is the fix.
            for (size_t i = 0; i < liveNotes.size(); ++i)
            {
                const auto noteNumber = liveNotes[i].noteNumber;
                auto alreadyLatched = false;

                for (int j = 0; j < numLatched; ++j)
                {
                    if (latched[(size_t) j].noteNumber == noteNumber)
                    {
                        alreadyLatched = true;
                        break;
                    }
                }

                if (! alreadyLatched && numLatched < (int) latched.size())
                    latched[(size_t) numLatched++] = liveNotes[i];
            }
        }

        result = { latched.data(), (size_t) numLatched };
    }

    publishLatchSnapshot();
    return result;
}

//==============================================================================
// documents/settings-persistence-design.md section 5.
void Arpeggiator::publishLatchSnapshot() noexcept
{
    numLatchedAtomic.store (numLatched, std::memory_order_relaxed);
    latchAwaitingFreshChordAtomic.store (latchAwaitingFreshChord, std::memory_order_relaxed);

    for (int i = 0; i < numLatched; ++i)
    {
        latchedNoteNumberAtomics[(size_t) i].store (latched[(size_t) i].noteNumber, std::memory_order_relaxed);
        latchedPitchAtomics[(size_t) i].store (latched[(size_t) i].pitchLog2Hz, std::memory_order_relaxed);
        latchedVelocityAtomics[(size_t) i].store (latched[(size_t) i].velocity, std::memory_order_relaxed);
    }
}

Arpeggiator::LatchSnapshot Arpeggiator::getLatchSnapshot() const noexcept
{
    LatchSnapshot snapshot;
    snapshot.numLatched = numLatchedAtomic.load (std::memory_order_relaxed);
    snapshot.awaitingFreshChord = latchAwaitingFreshChordAtomic.load (std::memory_order_relaxed);

    // Reads every slot, not just up to numLatched - simpler than a
    // partial-range read, and slots beyond numLatched are never consulted by
    // anything that reads the returned struct (see LatchSnapshot's own
    // comment).
    for (int i = 0; i < NoteStack::maxHeldNotes; ++i)
    {
        snapshot.noteNumbers[(size_t) i] = (std::uint8_t) latchedNoteNumberAtomics[(size_t) i].load (std::memory_order_relaxed);
        snapshot.pitchesLog2Hz[(size_t) i] = latchedPitchAtomics[(size_t) i].load (std::memory_order_relaxed);
        snapshot.velocities[(size_t) i] = latchedVelocityAtomics[(size_t) i].load (std::memory_order_relaxed);
    }

    return snapshot;
}

void Arpeggiator::requestLatchLoad (const LatchSnapshot& snapshot) noexcept
{
    pendingLatchLoad = snapshot;

    // Release: pairs with process()'s acquire exchange, so the plain-data
    // writes just above are guaranteed visible once that observes true.
    hasPendingLatchLoad.store (true, std::memory_order_release);
}

int Arpeggiator::chooseNextIndex (HeldNotes active, ArpPattern pattern) noexcept
{
    if (active.empty())
    {
        // The phrase is over. Clearing this is what makes the NEXT chord start
        // from a defined end rather than from a lastNoteNumber that could be
        // minutes old. The caller keeps its clock running regardless, so the
        // grid holds phase while nothing is held.
        patternIsRunning = false;
        return -1;
    }

    if (! patternIsRunning)
    {
        patternIsRunning = true;
        goingUp = true;

        const auto first = startIndexFor (active, pattern);
        lastNoteNumber = active[(size_t) first].noteNumber;
        return first;
    }

    int index = -1;

    switch (pattern)
    {
        case ArpPattern::Up:
            index = indexOfSmallestAbove (active, lastNoteNumber);

            if (index < 0)
                index = indexOfLowest (active); // off the top - wrap
            break;

        case ArpPattern::Down:
            index = indexOfLargestBelow (active, lastNoteNumber);

            if (index < 0)
                index = indexOfHighest (active);
            break;

        case ArpPattern::UpDown:
            index = goingUp ? indexOfSmallestAbove (active, lastNoteNumber)
                            : indexOfLargestBelow (active, lastNoteNumber);

            if (index < 0)
            {
                // Turn round. The endpoint is NOT repeated - over C E G this
                // gives C E G E C rather than C E G G E. The two-note case is
                // what decides it: repeating would give C C E E, a doubled
                // trill that reads as a bug. Recorded as taste in
                // documents/arpeggiator-design.md section 12; flipping it is a
                // two-line change here.
                goingUp = ! goingUp;

                index = goingUp ? indexOfSmallestAbove (active, lastNoteNumber)
                                : indexOfLargestBelow (active, lastNoteNumber);
            }

            if (index < 0)
            {
                // BOTH queries failing is a proof, not a guess: it means every
                // held note compares equal to lastNoteNumber, and since
                // NoteStack de-dupes note numbers that can only be "exactly one
                // note is held and it is the one just played". Repeat it. This
                // is why UpDown never recurses and never flips forever.
                index = 0;
            }
            break;

        case ArpPattern::Random:
            index = chooseRandomIndex (active, lastNoteNumber);
            break;

        case ArpPattern::AsPlayed:
        {
            const auto previous = indexOfNoteNumber (active, lastNoteNumber);

            // The one place the comparison key can genuinely go missing: press
            // order has no ordering relation to fall back on, so a released
            // key leaves nothing to compare against. Restarting the phrase at
            // the oldest held note costs at most one step of the cycle and
            // cannot skip or stick. Every other mode answers this from the
            // pitch comparison and needs no fallback at all.
            index = previous < 0 ? 0 : (int) ((size_t) (previous + 1) % active.size());
            break;
        }
    }

    if (index < 0)
    {
        // Unreachable via arpPatternFromIndex, which bounds-checks the raw
        // atomic. Kept because the cost is one branch per STEP - about twenty
        // times a second - and the alternative is indexing a span with -1.
        index = indexOfLowest (active);
    }

    lastNoteNumber = active[(size_t) index].noteNumber;
    return index;
}

int Arpeggiator::startIndexFor (HeldNotes active, ArpPattern pattern) noexcept
{
    if (pattern == ArpPattern::Down)
        return indexOfHighest (active);

    if (pattern == ArpPattern::AsPlayed)
        return 0; // oldest press, since the span is in press order

    if (pattern == ArpPattern::Random)
        return chooseRandomIndex (active, -1); // nothing to exclude yet

    return indexOfLowest (active); // Up, UpDown, and any out-of-range value
}

int Arpeggiator::chooseRandomIndex (HeldNotes active, int excludeNoteNumber) noexcept
{
    // NO IMMEDIATE REPEATS, done by EXCLUDING the last note rather than
    // re-rolling until it differs: an unbounded reject-and-retry loop does not
    // belong on the audio thread. Side effect worth knowing about - with
    // exactly two notes held this degenerates to strict alternation, which
    // makes the rule ear-checkable in three seconds.
    auto numCandidates = 0;

    for (size_t i = 0; i < active.size(); ++i)
        if ((int) active[i].noteNumber != excludeNoteNumber)
            ++numCandidates;

    if (numCandidates == 0)
        return 0; // only the excluded note is held - repeat it

    // Multiply-shift rather than a modulo: it takes the HIGH bits, which are
    // xorshift32's stronger ones, and carries no modulo bias.
    const auto roll = (std::uint64_t) randomSource.nextUInt32() * (std::uint64_t) numCandidates;
    auto chosen = (int) (roll >> 32);

    for (size_t i = 0; i < active.size(); ++i)
    {
        if ((int) active[i].noteNumber == excludeNoteNumber)
            continue;

        if (chosen-- == 0)
            return (int) i;
    }

    return 0; // unreachable: chosen < numCandidates by construction
}

//==============================================================================
void renderVoiceBlock (SynthVoice& voice, NoteRouter& router, Arpeggiator& arp, StepSequencer& seq,
                        VoiceOwner& currentOwner, float* output, int numSamples) noexcept
{
    const auto& parameters = voice.getParameters();

    const auto priorityMode = (NotePriorityMode)
        parameters.notePriorityMode.load (std::memory_order_relaxed);

    const auto arpIsOn = parameters.arpEnabled.load (std::memory_order_relaxed) != 0;
    const auto seqIsOn = parameters.seqEnabled.load (std::memory_order_relaxed) != 0;

    // Arp and seq are a mutually exclusive NOTE source
    // (documents/step-sequencer-design.md section 1) - seq wins the tie if a
    // UI bug or a test ever leaves both atomics on at once. Arbitrary but
    // deterministic, and easy to flip; the real toggle UI (build step 6) is
    // not meant to be able to produce this state in the first place.
    const auto desiredOwner = seqIsOn ? VoiceOwner::Seq
                             : arpIsOn ? VoiceOwner::Arp
                                       : VoiceOwner::Keys;

    // THE HAND-OVER. Switching owners moves ownership of the one voice between
    // the router, the arpeggiator and the sequencer, and the side that stops
    // driving has to leave it silent - otherwise a sustained note keeps
    // sounding with no note-off pending (stuck on), or the router's belief goes
    // stale and a later hand-back leaves silence with a key still held (stuck
    // off, T1/S3). All are as bad as each other.
    //
    // ALL THREE releases are called on EVERY transition, regardless of
    // direction. Each is idempotent, so exactly one of them actually does
    // anything - which is what makes a stuck note unreachable by any toggle
    // order, rather than merely unlikely. See documents/arpeggiator-design.md
    // sections 6 and 7, and documents/step-sequencer-design.md sections 6
    // and 7 (S1-S4).
    if (desiredOwner != currentOwner)
    {
        router.releaseVoice (voice);
        arp.releaseVoice (voice);
        seq.releaseVoice (voice);

        // T2/S4: only the router needs an explicit "sound it again now" call.
        // The arp and the sequencer both park their own clock on a step
        // boundary inside releaseVoice, so their very next process() call
        // below opens the first step at once (T1/S3) - the router is not
        // clocked, so without this a chord already held would stay silent
        // until the next key event.
        if (desiredOwner == VoiceOwner::Keys)
            router.retakeVoice (voice, priorityMode);

        currentOwner = desiredOwner;
    }

    // Drain queued note events before rendering, so the envelope and glide
    // target are settled for the whole block. Block-granular rather than
    // sample-accurate is deliberate for item 4 - human timing jitter dwarfs a
    // block boundary, and CLAUDE.md ties sample-accuracy to item 5's arp clock
    // (and item 7's step clock) specifically. That line still holds with the
    // arp or the seq on: STEPS are sample-accurate, the human's first key
    // press still quantises to a block.
    //
    // In TrackOnly the held-note stack is still updated - only the voice calls
    // are withheld - which is exactly what lets the arp read a live held set,
    // and what makes the transitions above cheap regardless of which non-Keys
    // owner is active.
    router.dispatchPendingEvents (voice, priorityMode,
                                   desiredOwner == VoiceOwner::Keys ? NoteRouter::VoiceDrive::Direct
                                                                     : NoteRouter::VoiceDrive::TrackOnly);

    switch (desiredOwner)
    {
        case VoiceOwner::Arp:
            // The arp splits this one call into per-step sub-blocks, driving
            // the voice at exact sample positions inside the block - the
            // reason renderNextBlock takes a raw pointer and a count.
            arp.process (voice, router.getNoteStack().getHeldNotes(), output, numSamples);
            break;

        case VoiceOwner::Seq:
            // The sequencer has no keyboard input of its own for PLAYBACK -
            // it reads its pattern straight out of VoiceParameters by index,
            // so it takes no HeldNotes span, unlike the arp. It DOES take the
            // router's current priority-resolved pick for RECORDING (build
            // step 7, documents/step-sequencer-design.md section 8) - sampled
            // once per block here, same as liveNotes just above, and ignored
            // internally unless seqRecordArmed is on.
            seq.process (voice, router.getNoteStack().getCurrentResolution (priorityMode), output, numSamples);
            break;

        case VoiceOwner::Keys:
        default:
            voice.renderNextBlock (output, numSamples);
            break;
    }
}

//==============================================================================
#if JUCE_DEBUG

namespace
{
    // Same convention as runNoteStackSelfTest's helper: any deterministic,
    // distinguishable value. This checks that the note travels through the
    // walker intact, not that the conversion itself is right.
    NoteStack::HeldNote makeHeld (int noteNumber) noexcept
    {
        return { (std::uint8_t) noteNumber, (float) noteNumber * 0.01f, 1.0f };
    }
}

void runArpPatternSelfTest()
{
    // MIDI note numbers: C4 = 60, E4 = 64, G4 = 67, B4 = 71.
    constexpr int c4 = 60, e4 = 64, g4 = 67, b4 = 71;

    // Play `count` steps over a FIXED set and collect the note numbers, so a
    // whole cycle can be asserted in one line. -1 means "nothing to play".
    const auto play = [] (Arpeggiator& arp, HeldSpan set, ArpPattern pattern,
                          std::span<int> out)
    {
        for (size_t i = 0; i < out.size(); ++i)
        {
            const auto index = arp.chooseNextIndex (set, pattern);
            jassert (index < (int) set.size());

            out[i] = index < 0 ? -1 : (int) set[(size_t) index].noteNumber;
        }
    };

    //==========================================================================
    // The three ordered modes over a set whose PRESS order and PITCH order
    // deliberately disagree: pressed G, C, E.
    const std::array<NoteStack::HeldNote, 3> gce { makeHeld (g4), makeHeld (c4), makeHeld (e4) };
    {
        Arpeggiator arp;
        std::array<int, 7> played {};
        play (arp, gce, ArpPattern::Up, played);

        // Starts at the LOWEST, not at the first pressed.
        const std::array<int, 7> expected { c4, e4, g4, c4, e4, g4, c4 };
        jassert (played == expected);
    }
    {
        Arpeggiator arp;
        std::array<int, 7> played {};
        play (arp, gce, ArpPattern::Down, played);

        const std::array<int, 7> expected { g4, e4, c4, g4, e4, c4, g4 };
        jassert (played == expected);
    }
    {
        // AS-PLAYED is the only mode that differs from Up on this input - which
        // is exactly why the set above is pressed out of pitch order.
        Arpeggiator arp;
        std::array<int, 7> played {};
        play (arp, gce, ArpPattern::AsPlayed, played);

        const std::array<int, 7> expected { g4, c4, e4, g4, c4, e4, g4 };
        jassert (played == expected);
    }

    //==========================================================================
    // UP-DOWN. Endpoints are NOT repeated.
    {
        Arpeggiator arp;
        std::array<int, 9> played {};
        play (arp, gce, ArpPattern::UpDown, played);

        const std::array<int, 9> expected { c4, e4, g4, e4, c4, e4, g4, e4, c4 };
        jassert (played == expected);
    }
    {
        // THE CASE THAT DECIDES THE ENDPOINT RULE. Repeating endpoints would
        // give C C E E here - a doubled trill that reads as a bug.
        Arpeggiator arp;
        const std::array<NoteStack::HeldNote, 2> ce { makeHeld (c4), makeHeld (e4) };
        std::array<int, 6> played {};
        play (arp, ce, ArpPattern::UpDown, played);

        const std::array<int, 6> expected { c4, e4, c4, e4, c4, e4 };
        jassert (played == expected);
    }
    {
        // ONE note: both queries fail every step. Must repeat the note and
        // terminate, not recurse or flip forever.
        Arpeggiator arp;
        const std::array<NoteStack::HeldNote, 1> c { makeHeld (c4) };
        std::array<int, 4> played {};
        play (arp, c, ArpPattern::UpDown, played);

        const std::array<int, 4> expected { c4, c4, c4, c4 };
        jassert (played == expected);
    }

    //==========================================================================
    // RANDOM: never an immediate repeat, and every held note is reachable.
    {
        Arpeggiator arp;
        const std::array<NoteStack::HeldNote, 3> ceg { makeHeld (c4), makeHeld (e4), makeHeld (g4) };
        std::array<int, 60> played {};
        play (arp, ceg, ArpPattern::Random, played);

        auto sawC = false, sawE = false, sawG = false;

        for (size_t i = 0; i < played.size(); ++i)
        {
            jassert (played[i] == c4 || played[i] == e4 || played[i] == g4);

            if (i > 0)
                jassert (played[i] != played[i - 1]); // the no-repeat rule

            sawC = sawC || played[i] == c4;
            sawE = sawE || played[i] == e4;
            sawG = sawG || played[i] == g4;
        }

        // Deterministic, since the seed is fixed: catches a walker that only
        // ever reaches one or two of the three.
        jassert (sawC && sawE && sawG);
    }
    {
        // With exactly two notes, "exclude the last" degenerates to STRICT
        // ALTERNATION - the property that makes the rule ear-checkable.
        Arpeggiator arp;
        const std::array<NoteStack::HeldNote, 2> ce { makeHeld (c4), makeHeld (e4) };
        std::array<int, 10> played {};
        play (arp, ce, ArpPattern::Random, played);

        for (size_t i = 1; i < played.size(); ++i)
            jassert (played[i] != played[i - 1]);
    }

    //==========================================================================
    // THE CHANGING-SET RECIPES - the ones an index-based walker fails.
    //
    // Removal WHILE SITTING ON THE REMOVED NOTE. Up over C E G: play C, play
    // E, then release E. The next step must be G (no skip) and then wrap to C
    // (no repeat, no stutter).
    {
        Arpeggiator arp;
        const std::array<NoteStack::HeldNote, 3> ceg { makeHeld (c4), makeHeld (e4), makeHeld (g4) };
        std::array<int, 2> before {};
        play (arp, ceg, ArpPattern::Up, before);
        jassert ((before == std::array<int, 2> { c4, e4 }));

        const std::array<NoteStack::HeldNote, 2> cg { makeHeld (c4), makeHeld (g4) };
        std::array<int, 4> after {};
        play (arp, cg, ArpPattern::Up, after);

        const std::array<int, 4> expected { g4, c4, g4, c4 };
        jassert (after == expected);
    }

    // ADDITION LANDS IN PITCH POSITION on the very next cycle, rather than
    // being appended at the end. Up over C G: play C, play G, then press E -
    // which is pressed LAST but must be played BETWEEN C and G.
    {
        Arpeggiator arp;
        const std::array<NoteStack::HeldNote, 2> cg { makeHeld (c4), makeHeld (g4) };
        std::array<int, 2> before {};
        play (arp, cg, ArpPattern::Up, before);
        jassert ((before == std::array<int, 2> { c4, g4 }));

        const std::array<NoteStack::HeldNote, 3> cge { makeHeld (c4), makeHeld (g4), makeHeld (e4) };
        std::array<int, 4> after {};
        play (arp, cge, ArpPattern::Up, after);

        const std::array<int, 4> expected { c4, e4, g4, c4 };
        jassert (after == expected);
    }

    // A note added ABOVE the one just played joins immediately, without
    // waiting for a wrap.
    {
        Arpeggiator arp;
        const std::array<NoteStack::HeldNote, 2> ce { makeHeld (c4), makeHeld (e4) };
        std::array<int, 2> before {};
        play (arp, ce, ArpPattern::Up, before);
        jassert ((before == std::array<int, 2> { c4, e4 }));

        const std::array<NoteStack::HeldNote, 3> ceb { makeHeld (c4), makeHeld (e4), makeHeld (b4) };
        std::array<int, 2> after {};
        play (arp, ceb, ArpPattern::Up, after);
        jassert ((after == std::array<int, 2> { b4, c4 }));
    }

    // AS-PLAYED, removal while sitting on the removed note: press order has no
    // ordering to fall back on, so this is the documented restart-at-oldest
    // case. It must not skip, stick or index out of the shrunken set.
    {
        Arpeggiator arp;
        std::array<int, 2> before {};
        play (arp, gce, ArpPattern::AsPlayed, before);
        jassert ((before == std::array<int, 2> { g4, c4 })); // sitting on C

        const std::array<NoteStack::HeldNote, 2> ge { makeHeld (g4), makeHeld (e4) };
        std::array<int, 4> after {};
        play (arp, ge, ArpPattern::AsPlayed, after);

        const std::array<int, 4> expected { g4, e4, g4, e4 };
        jassert (after == expected);
    }

    //==========================================================================
    // CHANGING PATTERN MID-RUN needs no re-initialisation. Up to C, E, then
    // switch to Down: the next note is the largest BELOW E, not a restart.
    {
        Arpeggiator arp;
        const std::array<NoteStack::HeldNote, 3> ceg { makeHeld (c4), makeHeld (e4), makeHeld (g4) };
        std::array<int, 2> up {};
        play (arp, ceg, ArpPattern::Up, up);
        jassert ((up == std::array<int, 2> { c4, e4 }));

        std::array<int, 3> down {};
        play (arp, ceg, ArpPattern::Down, down);
        jassert ((down == std::array<int, 3> { c4, g4, e4 }));
    }

    //==========================================================================
    // AN EMPTY SET plays nothing and ENDS THE PHRASE, so the next chord starts
    // at a defined end rather than continuing from a stale key. Without this,
    // pressing C E G after a pause would start wherever the last phrase left
    // off.
    {
        Arpeggiator arp;
        const std::array<NoteStack::HeldNote, 3> ceg { makeHeld (c4), makeHeld (e4), makeHeld (g4) };
        std::array<int, 2> before {};
        play (arp, ceg, ArpPattern::Up, before);
        jassert ((before == std::array<int, 2> { c4, e4 }));

        std::array<int, 2> silent {};
        play (arp, {}, ArpPattern::Up, silent);
        jassert ((silent == std::array<int, 2> { -1, -1 }));

        std::array<int, 2> after {};
        play (arp, ceg, ArpPattern::Up, after);
        jassert ((after == std::array<int, 2> { c4, e4 })); // restarted, not g4
    }

    // resetPattern() does the same thing on demand - what step 3 calls when
    // the arp is switched on.
    {
        Arpeggiator arp;
        const std::array<NoteStack::HeldNote, 3> ceg { makeHeld (c4), makeHeld (e4), makeHeld (g4) };
        std::array<int, 2> before {};
        play (arp, ceg, ArpPattern::Up, before);

        arp.resetPattern();

        std::array<int, 1> after {};
        play (arp, ceg, ArpPattern::Up, after);
        jassert (after[0] == c4);
    }

    //==========================================================================
    // The chosen index addresses the note the caller will actually sound, so
    // pitch and velocity have to travel with it - the arp passes both straight
    // to voice.noteOn.
    {
        Arpeggiator arp;
        const std::array<NoteStack::HeldNote, 2> cg { makeHeld (c4), makeHeld (g4) };

        const auto index = arp.chooseNextIndex (cg, ArpPattern::Up);
        jassert (index >= 0 && index < (int) cg.size());
        jassert (cg[(size_t) index].noteNumber == c4);
        jassert (cg[(size_t) index].pitchLog2Hz == makeHeld (c4).pitchLog2Hz);
        jassert (cg[(size_t) index].velocity == 1.0f);
    }

    //==========================================================================
    // A stale or out-of-range pattern index must not index a span with -1.
    // arpPatternFromIndex is the real guard; this checks both halves of it.
    {
        jassert (arpPatternFromIndex (-1) == ArpPattern::Up);
        jassert (arpPatternFromIndex (999) == ArpPattern::Up);
        jassert (arpPatternFromIndex ((int) ArpPattern::AsPlayed) == ArpPattern::AsPlayed);

        Arpeggiator arp;
        const std::array<NoteStack::HeldNote, 2> cg { makeHeld (c4), makeHeld (g4) };
        arp.chooseNextIndex (cg, ArpPattern::Up); // get past the start branch

        const auto index = arp.chooseNextIndex (cg, (ArpPattern) 99);
        jassert (index >= 0 && index < (int) cg.size());
    }

    //==========================================================================
    // A FULL set: sixteen notes, the cap NoteStack enforces. Walks every one
    // in pitch order and wraps exactly once.
    {
        Arpeggiator arp;
        std::array<NoteStack::HeldNote, NoteStack::maxHeldNotes> full {};

        // Pressed HIGHEST first, so press order is the reverse of pitch order.
        for (int i = 0; i < NoteStack::maxHeldNotes; ++i)
            full[(size_t) i] = makeHeld (40 + NoteStack::maxHeldNotes - 1 - i);

        std::array<int, NoteStack::maxHeldNotes + 1> played {};
        play (arp, full, ArpPattern::Up, played);

        for (int i = 0; i < NoteStack::maxHeldNotes; ++i)
            jassert (played[(size_t) i] == 40 + i);

        jassert (played[(size_t) NoteStack::maxHeldNotes] == 40); // wrapped
    }

    //==========================================================================
    // HOLD: resolveActiveNotes implements section 8's rule directly - no
    // SynthVoice needed, which is exactly why it is public like the walker
    // helpers above. The T-numbers match documents/arpeggiator-design.md
    // section 7; formally walking the whole table against the running app is
    // step 6, but the rule itself is cheap to assert here.

    // Hold OFF is the identity function.
    {
        Arpeggiator arp;
        const std::array<NoteStack::HeldNote, 2> ce { makeHeld (c4), makeHeld (e4) };
        const auto active = arp.resolveActiveNotes (ce, false);

        jassert (active.size() == 2 && active[0].noteNumber == c4 && active[1].noteNumber == e4);
    }

    // T4a (latch := live set with keys down) -> T5 (live set empties, latch
    // survives) -> T4c (hold released with keys still down, seamless) -> T4d
    // (hold released with nothing held, active empties - the classic
    // latch-stuck-note case this rule exists to avoid).
    {
        Arpeggiator arp;
        const std::array<NoteStack::HeldNote, 3> ceg { makeHeld (c4), makeHeld (e4), makeHeld (g4) };

        auto active = arp.resolveActiveNotes (ceg, true); // T4a
        jassert (active.size() == 3 && active[0].noteNumber == c4
                 && active[1].noteNumber == e4 && active[2].noteNumber == g4);

        active = arp.resolveActiveNotes ({}, true); // T5
        jassert (active.size() == 3 && active[2].noteNumber == g4);

        active = arp.resolveActiveNotes (ceg, false); // T4c
        jassert (active.size() == 3 && active[1].noteNumber == e4);

        active = arp.resolveActiveNotes ({}, false); // T4d
        jassert (active.empty());
    }

    // T4b: engaging hold with nothing held must not latch a chord from a
    // PREVIOUS hold-on session - numLatched has to be zeroed on every
    // hold-off call, not just the falling edge.
    {
        Arpeggiator arp;
        const std::array<NoteStack::HeldNote, 2> ce { makeHeld (c4), makeHeld (e4) };
        arp.resolveActiveNotes (ce, true);
        arp.resolveActiveNotes ({}, false); // hold released with nothing held (T4d)

        const auto active = arp.resolveActiveNotes ({}, true); // hold re-engaged, still nothing held
        jassert (active.empty());
    }

    // T6: REPLACE, not add, for a genuinely FRESH phrase - one that follows an
    // empty live set, exactly as T5 leaves it armed. Without the empty call in
    // between this would be indistinguishable from mid-phrase union, below.
    {
        Arpeggiator arp;
        const std::array<NoteStack::HeldNote, 2> ce { makeHeld (c4), makeHeld (e4) };
        arp.resolveActiveNotes (ce, true);
        arp.resolveActiveNotes ({}, true); // T5: phrase ends, arms the next REPLACE

        const std::array<NoteStack::HeldNote, 1> b { makeHeld (b4) };
        const auto active = arp.resolveActiveNotes (b, true);
        jassert (active.size() == 1 && active[0].noteNumber == b4);
    }

    // THE REPORTED BUG, fixed: a chord released one finger at a time - which
    // is how every real hand does it, never all on the same sample - must stay
    // fully latched until the LAST finger comes up, not shrink on every
    // intermediate release and freeze on whichever note happened to come up
    // last. Each shrinking live set below is a SUBSET of what got latched by
    // the first (fullest) call, which is exactly what the union in the
    // mid-phrase branch relies on.
    {
        Arpeggiator arp;
        const std::array<NoteStack::HeldNote, 3> ceg { makeHeld (c4), makeHeld (e4), makeHeld (g4) };
        auto active = arp.resolveActiveNotes (ceg, true); // all three pressed together
        jassert (active.size() == 3);

        const std::array<NoteStack::HeldNote, 2> ce { makeHeld (c4), makeHeld (e4) };
        active = arp.resolveActiveNotes (ce, true); // G released first
        jassert (active.size() == 3 && active[2].noteNumber == g4); // G must survive

        const std::array<NoteStack::HeldNote, 1> c { makeHeld (c4) };
        active = arp.resolveActiveNotes (c, true); // E released next
        jassert (active.size() == 3 && active[1].noteNumber == e4); // E must survive too

        active = arp.resolveActiveNotes ({}, true); // C released last - phrase ends
        jassert (active.size() == 3); // the WHOLE chord, not just C
    }

    // A note ADDED mid-phrase (while others from the original press are still
    // down) joins the latch, same as T4a's replace used to give for free -
    // union must not stop new notes from getting in.
    {
        Arpeggiator arp;
        const std::array<NoteStack::HeldNote, 2> ce { makeHeld (c4), makeHeld (e4) };
        arp.resolveActiveNotes (ce, true);

        const std::array<NoteStack::HeldNote, 3> ceg { makeHeld (c4), makeHeld (e4), makeHeld (g4) };
        const auto active = arp.resolveActiveNotes (ceg, true); // G added
        jassert (active.size() == 3 && active[2].noteNumber == g4);
    }
}

//==============================================================================
namespace
{
    /*
        A whole instrument on a bench: the real voice, the real router, the real
        arpeggiator, driven a block at a time through the real renderVoiceBlock.

        Nothing here re-implements the logic under test. The only thing this
        adds is a clock to turn and a meter to read - blocks go in, peak
        amplitude comes out - because "no stuck note" is an OUTPUT property, and
        checking a flag instead would just be asking the code whether it thinks
        it is right.
    */
    class TransitionRig
    {
    public:
        // Five keys, deliberately not in pitch order, so press order and pitch
        // order disagree and the walker's two orderings both get exercised.
        static constexpr int numTestKeys = 5;

        TransitionRig()
        {
            auto& p = voice.getParameters();

            // Amp destination is what makes a stuck note OBSERVABLE at all.
            // With Filter the VCA sits at unity and the voice drones whether or
            // not it is gated (T9), so silence would prove nothing.
            p.envelopeDestination.store ((int) EnvelopeDestination::Amp);

            // Short stages, so settling to silence is a handful of blocks
            // rather than a second of rendering at every Debug startup.
            p.attackSeconds.store (0.001f);
            p.decaySeconds.store (0.001f);
            p.sustainLevel.store (0.8f);
            p.releaseSeconds.store (0.002f);

            // Well clear of Nyquist at the deliberately low test rate below,
            // and low resonance so nothing can ring on past the envelope.
            p.cutoffLog2Hz.store (10.0f);       // ~1kHz
            p.resonance.store (0.1f);
            p.outputLevel.store (0.5f);
            p.sawLevel.store (0.7f);

            // The fastest musical step, so a step is ~6 blocks rather than ~30.
            // This is a transition test, not a timing test - runStepClockSelfTest
            // owns the maths - so short steps buy density for nothing.
            p.masterTempoBpm.store (300.0f);
            p.arpDivision.store ((int) StepDivision::ThirtySecond);

            startDevice();
        }

        VoiceParameters& parameters() noexcept { return voice.getParameters(); }

        bool arpIsOn()  { return voice.getParameters().arpEnabled.load() != 0; }
        bool holdIsOn() { return voice.getParameters().arpHold.load() != 0; }

        void setArp (bool on)  { voice.getParameters().arpEnabled.store (on ? 1 : 0); }
        void setHold (bool on) { voice.getParameters().arpHold.store (on ? 1 : 0); }

        bool noteStackIsEmpty() const noexcept { return router.getNoteStack().isEmpty(); }

        void pressKey (int key)
        {
            if (keyIsDown[(size_t) key])
                return;

            const auto noteNumber = testKeys[(size_t) key];
            router.pushUiEvent ({ NoteEvent::Type::NoteOn, (std::uint8_t) noteNumber,
                                  pitchLog2HzForMidiNote (noteNumber), 0.8f });
            keyIsDown[(size_t) key] = true;
        }

        void releaseKey (int key)
        {
            if (! keyIsDown[(size_t) key])
                return;

            router.pushUiEvent ({ NoteEvent::Type::NoteOff, (std::uint8_t) testKeys[(size_t) key],
                                  0.0f, 0.0f });
            keyIsDown[(size_t) key] = false;
        }

        void releaseAllKeys()
        {
            for (int key = 0; key < numTestKeys; ++key)
                releaseKey (key);
        }

        // Renders numBlocks and returns the LARGEST sample magnitude seen over
        // all of them. Peak-over-a-window rather than the last block's peak,
        // because with the arp running the gate is shut for part of every step,
        // so a single block proves nothing in either direction. Windows used
        // below are always at least one step long.
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

        // T7: exactly MainComponent::releaseResources followed by
        // prepareToPlay, in that order, which is the ordering the design fixes.
        void stopAndRestartDevice()
        {
            voice.reset();
            router.reset();
            arp.reset();
            seq.reset();

            // The device dropped the held notes with the stack, so the bench's
            // idea of which keys are down has to drop them too.
            keyIsDown.fill (false);

            startDevice();
        }

        // T8: a device change at a new rate. Goes through the same stop-then-
        // start the app does, because that is what JUCE actually calls - a bare
        // prepare would leave SynthVoice's own gate set (prepare deliberately
        // does not clear it; reset does) and would be testing a sequence the
        // app never performs.
        void changeSampleRate (double newSampleRate)
        {
            sampleRate = newSampleRate;
            stopAndRestartDevice();
        }

    private:
        void startDevice()
        {
            voice.prepare (sampleRate);
            arp.prepare (sampleRate);
            seq.prepare (sampleRate);

            // Forces the first block to re-run the hand-over whichever side is
            // switched on - MainComponent::prepareToPlay does the same.
            owner = VoiceOwner::Keys;
        }

        // 16kHz, not 44.1: this test renders tens of thousands of samples at
        // every Debug launch, and none of what it checks is rate-dependent. It
        // also keeps a step down to a few blocks.
        double sampleRate = 16000.0;
        static constexpr int blockSize = 64;

        static constexpr std::array<int, (size_t) numTestKeys> testKeys { 60, 67, 55, 64, 72 };

        SynthVoice voice;
        NoteRouter router;
        Arpeggiator arp;
        StepSequencer seq;
        VoiceOwner owner = VoiceOwner::Keys;

        std::array<float, (size_t) blockSize> output {};
        std::array<bool, (size_t) numTestKeys> keyIsDown {};
    };
}

void runArpTransitionSelfTest()
{
    // Exact, not approximate. The ADSR is linear, so Release lands on 0 and the
    // VCA multiplies the whole signal by it - anything above this floor is a
    // note somebody is still holding open.
    constexpr float silence = 1.0e-6f;

    //==========================================================================
    // T1 and T2 - the arp taking the voice and handing it back, with a key down
    // throughout. T1's failure is a STUCK OFF note: the router's sustained note
    // never gets a note-off, its belief goes stale, and the arp-OFF below finds
    // nothing to re-assert. T2's is the mirror image.
    {
        TransitionRig rig;
        rig.pressKey (0);
        jassert (rig.run (2) > silence);        // the router is driving it

        rig.setArp (true);                      // T1
        jassert (rig.run (8) > silence);        // the arp took over and is stepping

        // T2: the key is still down, so it must sound again AT ONCE rather than
        // waiting for the next key press. A missing retakeVoice fails here and
        // only here.
        rig.setArp (false);
        jassert (rig.run (1) > silence);

        // ...and nothing was left stuck on the way through.
        rig.releaseAllKeys();
        rig.run (4);
        jassert (rig.run (4) <= silence);
    }

    //==========================================================================
    // T3 - every key released mid-step, hold off, ARP STILL RUNNING. The next
    // step has no note to play, so an alternating countdown would either skip
    // the previous step's pending gate-off (a stuck note forever) or fire a
    // phantom. The two independent deadlines are what make this work.
    {
        TransitionRig rig;
        rig.setArp (true);
        rig.pressKey (0);
        jassert (rig.run (8) > silence);

        rig.releaseAllKeys();
        rig.run (8);                            // gate-off deadline, then release
        jassert (rig.run (8) <= silence);
    }

    //==========================================================================
    // T5 then T4d. The latched set is the arp's OWN copy, so the stack going
    // empty does not disturb it (T5) - and disengaging hold with nothing held
    // must then empty the active set rather than playing the latched chord
    // forever, which is the classic latch stuck note (T4d).
    {
        TransitionRig rig;
        rig.setHold (true);
        rig.setArp (true);
        rig.pressKey (0);
        rig.pressKey (1);
        jassert (rig.run (8) > silence);

        rig.releaseAllKeys();
        jassert (rig.run (16) > silence);       // T5: still running, no key down

        rig.setHold (false);                    // T4d
        rig.run (8);
        jassert (rig.run (8) <= silence);
    }

    //==========================================================================
    // THE DEFECT THIS STEP TURNED UP, and the reason it is a step rather than a
    // Polish afterthought.
    //
    // Hold is maintained by resolveActiveNotes, which only runs while the arp
    // is ON. So the "numLatched is zeroed on every hold-off call" mechanism
    // that makes T4b safe is FROZEN for as long as the arp is off, and this
    // sequence - latch a chord, arp off, lift every key, arp back on - used to
    // resurrect a finished phrase with nothing held. T4b's hazard arriving
    // through a different door; Arpeggiator::releaseVoice now clears the latch.
    {
        TransitionRig rig;
        rig.setHold (true);
        rig.setArp (true);
        rig.pressKey (0);
        rig.pressKey (1);
        jassert (rig.run (8) > silence);

        rig.setArp (false);
        rig.releaseAllKeys();
        rig.run (8);
        jassert (rig.run (4) <= silence);

        rig.setArp (true);                      // hold still on, nothing held
        jassert (rig.run (16) <= silence);
    }

    //==========================================================================
    // T7 - releaseResources / device stop mid-step, with a chord latched and a
    // gate open. The restart must not resurrect either.
    {
        TransitionRig rig;
        rig.setHold (true);
        rig.setArp (true);
        rig.pressKey (0);
        rig.pressKey (1);
        jassert (rig.run (8) > silence);

        rig.stopAndRestartDevice();
        jassert (rig.run (16) <= silence);
        jassert (rig.noteStackIsEmpty());
    }

    //==========================================================================
    // T8 - sample-rate change mid-gate. samplesUntilGateOff is a COUNT OF
    // SAMPLES, so a stale one is meaningless at the new rate: a gate that
    // closes at the wrong time, or three times too late.
    {
        TransitionRig rig;
        rig.setArp (true);
        rig.pressKey (0);
        jassert (rig.run (8) > silence);

        rig.changeSampleRate (48000.0);
        jassert (rig.run (16) <= silence);      // the restart resurrects nothing

        // ...and it still arpeggiates at the new rate, where a step is 1200
        // samples rather than 400.
        rig.pressKey (0);
        jassert (rig.run (24) > silence);

        rig.releaseAllKeys();
        rig.run (32);
        jassert (rig.run (16) <= silence);
    }

    //==========================================================================
    // T10 - the idle fast path. With nothing held the clock stays parked on a
    // boundary, so the first note of a new chord lands in the very block its
    // key event was drained in, rather than up to a whole step later.
    {
        TransitionRig rig;
        rig.setArp (true);
        jassert (rig.run (32) <= silence);      // arp on, nothing to play

        rig.pressKey (0);
        jassert (rig.run (1) > silence);        // ...and it starts immediately
    }

    //==========================================================================
    // THE STUCK-NOTE FUZZ. Section 12 describes this as thirty seconds of
    // mashing Arp and Hold while pressing and releasing keys in random order,
    // ending with every key up and the arp off - and it must be silent. That is
    // a fine test and a terrible one to rely on: it is unrepeatable, it depends
    // on the hands doing something unusual enough to matter, and a failure
    // gives no sequence to look at. Deterministic seed, same recipe, checked
    // hundreds of times per launch.
    //
    // The settle-and-assert happens EVERY round rather than once at the end, so
    // a failure names the handful of toggles that caused it instead of leaving
    // the whole run to bisect by hand.
    {
        TransitionRig rig;
        NoiseGenerator rng { 0x51a2c7d3u };

        // Multiply-shift, same as the walker's - no modulo bias, high bits.
        const auto roll = [&rng] (int n) noexcept
        {
            return (int) (((std::uint64_t) rng.nextUInt32() * (std::uint64_t) n) >> 32);
        };

        constexpr int numRounds = 250;

        // Guards the fuzz against silently degenerating into 250 rounds of
        // nothing - a settle-to-silence assertion is trivially satisfied by a
        // run that never made a sound in the first place.
        auto soundingRounds = 0;

        for (int round = 0; round < numRounds; ++round)
        {
            const auto chaosBlocks = 1 + roll (10);
            auto roundPeak = 0.0f;

            for (int block = 0; block < chaosBlocks; ++block)
            {
                switch (roll (11))
                {
                    case 0:  rig.setArp (! rig.arpIsOn()); break;
                    case 1:  rig.setHold (! rig.holdIsOn()); break;

                    // Keys weighted double: the interesting failures need a
                    // toggle to land while something is actually held.
                    case 2:
                    case 3:  rig.pressKey (roll (TransitionRig::numTestKeys)); break;
                    case 4:
                    case 5:  rig.releaseKey (roll (TransitionRig::numTestKeys)); break;

                    case 6:  rig.releaseAllKeys(); break;
                    case 7:  rig.parameters().arpPattern.store (roll (numArpPatterns)); break;
                    case 8:  rig.parameters().arpDivision.store (roll (numStepDivisions)); break;

                    // The EXTREMES of tempo, not a gentle wander, and they are
                    // the point of including tempo at all: raising it mid-gate
                    // is what pulls the next step boundary in front of a
                    // pending gate-off - the second of section 3's three
                    // reasons the loop tracks two deadlines instead of
                    // assuming they alternate.
                    case 9:  rig.parameters().masterTempoBpm.store (roll (2) == 0 ? 20.0f : 300.0f); break;

                    // Likewise both ends of the gate clamp, where a rounding
                    // slip would give a zero-length or a full-step gate.
                    default: rig.parameters().arpGateLength.store (roll (2) == 0 ? 0.05f : 0.95f); break;
                }

                roundPeak = juce::jmax (roundPeak, rig.run (1));
            }

            if (roundPeak > silence)
                ++soundingRounds;

            // End as section 12 says to end: every key up, arp off. Hold too -
            // a latched chord is supposed to keep playing, so leaving it on
            // would make silence the wrong expectation rather than a useful one.
            rig.setArp (false);
            rig.setHold (false);
            rig.releaseAllKeys();

            rig.run (4);
            jassert (rig.run (4) <= silence);
            jassert (rig.noteStackIsEmpty());
        }

        // Deliberately a loose fraction rather than a tight number: the point is
        // "the fuzz is exercising a sounding instrument", not a golden value
        // that has to be re-tuned every time the action mix changes.
        jassert (soundingRounds > numRounds / 4);
    }
}

#endif
