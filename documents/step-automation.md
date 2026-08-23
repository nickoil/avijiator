# Per-Step Parameter Automation

Extends the step sequencer defined in `architecture.md` (Phase 2) so that *any*
parameter — not just pitch/gate/accent/slide — can be varied per step, with
optional interpolation between values.

Prior art worth studying: Elektron parameter locks, Polyend Tracker, Synthstrom
Deluge, and modular sequencers with CV-per-step lanes. Each solves this
differently; the model below is closest to Elektron's, with interpolation added.

---

## 1. Data model

**Sparse locks, not a dense grid.** Storing a value for every parameter on every
step (16 steps × ~20 params = 320 values) is mostly meaningless data and forces
you to define everything. Instead, a step touches a parameter only if a lock
exists:

```cpp
struct ParamLock {
    ParamId  paramId;       // which slider/knob
    int      stepIndex;     // which step
    float    targetValue;   // normalised 0..1
    CurveType curve;        // Hold | Linear | Exp | Log | SCurve | Random
    float    tension;       // -1..+1, shapes the curve (see §2)
};
```

A step with no lock for a parameter leaves that parameter alone. Default
behaviour is "nothing changes"; movement is opt-in.

Storage: per-pattern, a list (or map keyed by `paramId`) of locks. Patterns
serialise to disk with the rest of the pattern data.

---

## 2. Glide / interpolation semantics

### The key rule
Glide runs **between consecutive locks of the same parameter**, not between
adjacent steps.

If cutoff is locked at step 1 (value 0.2) and again at step 9 (value 0.8), a
glide ramps smoothly across all eight intervening steps. It does *not* hold flat
and snap at step 9. Each lock therefore needs to resolve its *next* lock (with
wraparound to the pattern start) to compute the ramp length.

### Curve types
- **Hold** — no glide, snap at the step. Classic p-lock behaviour. Should be the
  default, since it's the least surprising.
- **Linear** — straight ramp.
- **Exponential / Logarithmic** — matches perception for cutoff and time-based
  parameters, where linear ramps sound wrong.
- **S-curve** — ease-in/ease-out.
- **Random / stepped** — generative flavour; new random value per step within
  the span.

### Tension, not a curve menu
Rather than a discrete curve-type dropdown, prefer **one tension control**
(-1..+1) spanning the curve family: negative = log-ish/ease-out, 0 = linear,
positive = exp-ish/ease-in, extremes approaching near-instant. Faster to dial,
fewer taps, and continuously variable. Keep `Hold` and `Random` as explicit
modes outside the tension range.

### Audio-rate smoothing — non-negotiable
Interpolated values must be computed **per sample (or per small block)**, not
applied once at each step boundary. Setting a parameter once per step produces
audible zipper noise on cutoff sweeps. The ramp is evaluated continuously
against the sample-accurate step clock already specced for the arp/sequencer.

---

## 3. UI modes

Three surfaces, of which two are worth building first.

### 3a. Focus-lane editor — primary edit surface
- Select one parameter from a list; the 16-step grid becomes a lane for *that*
  parameter
- Locks shown as bar heights (or draggable points)
- **Overlay the actual interpolated curve on top of the step grid** so you see
  the real resulting shape, not just the lock points — this matters a lot with
  glide, where the audible result spans steps
- Scales to any number of parameters without needing N lanes on a phone screen

### 3b. Hold-step-and-turn — primary live surface
- Hold a step button, move a knob/slider → that step gets a lock for that
  parameter
- No mode switch, no menu diving
- This is what makes p-locks feel like playing rather than data entry; it's the
  single most important interaction to get right for stage use

### 3c. Multi-lane overview — optional, later
- All lanes stacked, small
- Reasonable on desktop, cramped on a Pixel — treat as a *view* on mobile, not
  an edit surface

**Build order: 3a and 3b first. 3c only if it earns its space.**

### Touch considerations
Per `architecture.md`, this is a touch-first design — no right-click. Lock
editing needs long-press or an explicit mode toggle for secondary actions
(clear lock, set curve), consistent with how accent/slide are handled.

---

## 4. Expression layer (advanced mode)

### Per-step code: rejected as the primary interface
Considered and not recommended as the main path:
- Slow to edit live
- Miserable on a touch keyboard
- 16 tiny programs are hard to reason about as a coherent pattern

### Per-lane expressions: the useful middle ground
One formula generating a whole lane, rather than code per step:

```
value = 0.5 + 0.3 * sin(step * PI / 8)
value = prev + random(-0.05, 0.05)
```

This is where scripting genuinely earns its place — generative and evolving
patterns that aren't practical to draw by hand.

**Available variables (keep the set small):**
`step`, `bar`, `time`, `prev`, `noteVelocity`, `random()`

**Bake-to-locks:** let an expression generate the lane, then convert the result
to ordinary editable locks. Algorithmic starting point, direct manipulation
afterwards — best of both, and it keeps the runtime simple (no expression
evaluation in the audio thread).

**Keep it an advanced mode.** The instrument must be fully usable without ever
opening it.

---

## 5. Scope warning

This feature is large — parameter locks are most of what makes an Elektron box
an Elektron box, and this is plausibly a bigger build than the synth voice
itself.

It also pushes the instrument well past SH-101 territory toward a
generative/modular machine. Same tension flagged in `character-and-vim.md`:
**don't lose the SH-101's immediacy chasing complexity.**

### Recommended first slice
Build locks for **three or four parameters only** — cutoff, resonance, envelope
amount, accent — with:
- Hold-step-and-turn entry (3b)
- Linear glide only
- No expression layer

That is small, shippable, and will already transform how the instrument sounds.
Generalise to all parameters, full curve types, and expressions *after* playing
it enough to know which controls you actually reach for.

---

## 6. Open questions

- **Lock resolution**: one lock per parameter per step, or sub-step (e.g. two
  locks within a step for fast movement)? Start with one; sub-step is a
  significant complexity jump.
- **Interaction with the VIM switch and Tier 2 controls**
  (`character-and-vim.md`): does a lock override a live knob position, or offset
  it? Elektron-style override is more predictable; offset is more playable.
  Worth trying both.
- **Pattern length vs. lane length**: should lanes be able to run at a different
  length to the note sequence (e.g. 16-step notes, 7-step cutoff lane) for
  polymetric movement? Cheap to implement, very high musical payoff — but adds
  UI to explain.
- **CPU cost** of per-sample interpolation across many simultaneously-glided
  parameters on the Daisy target, if the port happens. Less of a concern on the
  Pixel.
