# DisDorktion GitHub ticket drafts

Status: all 20 approved issues published and verified. This document archives their approved scope and publication links. Repository: `LordZordack/disDorktion`.

These drafts implement [the overarching project plan](PROJECT_PLAN.md). Planning IDs are mapped to published issue links below. `docs/PROJECT_PLAN.md` references in issue bodies name the repository document, which must be committed/pushed before a remote file link is advertised.

These complete drafts were approved before publication using ticket-create. Publishing issues does not imply starting DSP implementation. Roadmap issue #1 links the complete backlog.

## P0: Establish the project roadmap and research conventions

**Title:** Establish the project roadmap and research conventions

**Labels:** `documentation`

**Published issue:** [#1](https://github.com/LordZordack/disDorktion/issues/1)

### Motivation

Keep module development, section integration, and release qualification aligned with one reviewable project roadmap.

### Proposed Approach

Adopt docs/PROJECT_PLAN.md as the overarching roadmap and establish the project record and research conventions in the existing OKF bundle.

Project reference: `docs/PROJECT_PLAN.md`, sections 1–5.

### Dependencies

None.

### Acceptance Criteria

- [ ] The roadmap covers every core and experimental module, dependencies, acceptance gates, testing, reference material, and release strategy.
- [ ] The README links the project plan and the roadmap links the published issue backlog.
- [ ] An OKF project record links the roadmap and defines where method designs, papers, and experiment results are kept.
- [ ] Research records distinguish proposed behavior from measured behavior and preserve verified source details.
- [ ] OKF metadata, indexes, internal links, and the change log pass the existing curator validation.

---

## F1: Establish the JUCE build, module contract, and automated test foundation

**Title:** Establish the JUCE build, module contract, and automated test foundation

**Labels:** `enhancement`

**Published issue:** [#2](https://github.com/LordZordack/disDorktion/issues/2)

### Motivation

Independent modules need a common processing contract and reproducible build and validation environment.

### Proposed Approach

Create a CMake-based JUCE foundation with shared DSP, VST3, standalone, and automated-test targets. Prove the contract with a minimal reference gain processor.

Project reference: `docs/PROJECT_PLAN.md`, sections 2, 4.

### Dependencies

- [P0 / #1](https://github.com/LordZordack/disDorktion/issues/1) - Establish the project roadmap and research conventions

### Acceptance Criteria

- [ ] Windows build commands, supported toolchain versions, a pinned JUCE revision, and the test framework are documented and reproducible.
- [ ] The module contract specifies prepare, reset, validated parameter updates, process, and latency reporting for mono/stereo.
- [ ] Variable, zero-length, and oversized block handling and channel-state isolation have defined and tested behavior.
- [ ] A reference gain processor can be exercised without a plugin editor or host-state dependency.
- [ ] Windows CI builds targets and runs meaningful DSP tests.
- [ ] Reference hardware, CPU measurement procedure, and measurable performance budgets are recorded before module implementation.
- [ ] Processing conventions prohibit callback allocation, blocking, file/logging access, and UI calls.

---

## F2: Build the reusable audition and measurement harness

**Title:** Build the reusable audition and measurement harness

**Labels:** `enhancement`

**Published issue:** [#3](https://github.com/LordZordack/disDorktion/issues/3)

### Motivation

Each module must be audibly and numerically evaluated before section integration.

### Proposed Approach

Build one standalone harness that selects a module and later accepted sections/full chains, using the same DSP implementation as the VST3.

Project reference: `docs/PROJECT_PLAN.md`, sections 2–4.

### Dependencies

- [F1 / #2](https://github.com/LordZordack/disDorktion/issues/2) - Establish the JUCE build, module contract, and automated test foundation

### Acceptance Criteria

- [ ] Looped audio playback and keyboard/MIDI sine input can audition a selected module.
- [ ] Controls, bypass, input/output meters, and separate monitoring volume are available.
- [ ] A reference module can be auditioned without depending on completed production modules.
- [ ] Sources and settings can be reproduced and recorded with sample rate and observations.
- [ ] Offline utilities produce impulses, sweeps, sine/two-tone signals, noise, and rendered results.
- [ ] Monitoring gain does not alter underlying DSP measurements; audio loading and display work do not block processing.

---

## M1: Design, implement, and validate the reusable gain module

**Title:** Design, implement, and validate the reusable gain module

**Labels:** `enhancement`

**Published issue:** [#4](https://github.com/LordZordack/disDorktion/issues/4)

### Motivation

Input drive and final output level need predictable gain behavior that can be reused in different positions.

### Proposed Approach

Design and implement one DSP gain module with separately instantiated input/output state, documented gain range and defaults, and smoothed control changes.

Project reference: `docs/PROJECT_PLAN.md`, sections 2–4.

### Dependencies

- [F1 / #2](https://github.com/LordZordack/disDorktion/issues/2) - Establish the JUCE build, module contract, and automated test foundation
- [F2 / #3](https://github.com/LordZordack/disDorktion/issues/3) - Build the reusable audition and measurement harness

### Acceptance Criteria

- [ ] The reviewed design defines units, range, default, unity/zero behavior where supported, smoothing, bypass, and latency.
- [ ] Automated tests verify amplitude scaling, unity, control ramps, and mono/stereo independence.
- [ ] The same implementation is exercised by the harness and available to the plugin.
- [ ] Listening checks demonstrate controllable drive/level changes without unintended transition clicks.
- [ ] Input and output instances do not share mutable processing state.
- [ ] Shared module acceptance: use the F1 contract; verify mono/stereo, silence, deterministic reset, finite output, bypass transitions, control changes, and the plan's sample-rate/block-size matrix.
- [ ] Record verified sources, design acceptance limits before implementation, test results, listening settings, and CPU/latency measurements in the OKF bundle. Audio processing performs no allocation, blocking, file/logging access, or UI calls.

---

## M2: Design, implement, and validate bit crushing

**Title:** Design, implement, and validate bit crushing

**Labels:** `enhancement`

**Published issue:** [#5](https://github.com/LordZordack/disDorktion/issues/5)

### Motivation

The input section needs intentional digital degradation through bit-depth and sample-rate reduction.

### Proposed Approach

Specify quantization and sample-hold behavior, then implement the reusable crusher with separate bit-depth and rate controls.

Project reference: `docs/PROJECT_PLAN.md`, sections 1–4.

### Dependencies

- [F1 / #2](https://github.com/LordZordack/disDorktion/issues/2) - Establish the JUCE build, module contract, and automated test foundation
- [F2 / #3](https://github.com/LordZordack/disDorktion/issues/3) - Build the reusable audition and measurement harness

### Acceptance Criteria

- [ ] The reviewed design defines quantization, bit-depth and reduction-rate units/ranges/defaults, overload handling, and discrete-control transitions.
- [ ] Tests verify quantization levels, endpoint behavior, sample-hold timing, and block-boundary continuity.
- [ ] Sample-rate reduction timing behaves consistently across supported host rates and variable blocks.
- [ ] Measurements and listening document intentional aliasing and crushing rather than treating it as an unwanted artifact.
- [ ] The module operates independently in the harness with bypass and stable channel histories.
- [ ] Shared module acceptance: use the F1 contract; verify mono/stereo, silence, deterministic reset, finite output, bypass transitions, control changes, and the plan's sample-rate/block-size matrix.
- [ ] Record verified sources, design acceptance limits before implementation, test results, listening settings, and CPU/latency measurements in the OKF bundle. Audio processing performs no allocation, blocking, file/logging access, or UI calls.

---

## M3: Design, implement, and validate tanh saturation

**Title:** Design, implement, and validate tanh saturation

**Labels:** `enhancement`

**Published issue:** [#6](https://github.com/LordZordack/disDorktion/issues/6)

### Motivation

The middle section needs a controllable soft-saturation module spanning subtle coloration and stronger distortion.

### Proposed Approach

Specify drive and gain behavior around a tanh transfer function, implement it as reusable DSP, and collect native-rate baseline measurements for Q1.

Project reference: `docs/PROJECT_PLAN.md`, sections 1–4.

### Dependencies

- [F1 / #2](https://github.com/LordZordack/disDorktion/issues/2) - Establish the JUCE build, module contract, and automated test foundation
- [F2 / #3](https://github.com/LordZordack/disDorktion/issues/3) - Build the reusable audition and measurement harness

### Acceptance Criteria

- [ ] The reviewed design specifies the transfer function, drive/range/defaults, normalization policy, bypass, and control smoothing.
- [ ] Tests verify the expected transfer curve, symmetry, bounded response, and parameter endpoints.
- [ ] Sine, sweep, and two-tone measurements characterize harmonic and alias behavior.
- [ ] Recorded listening covers mild and extreme settings at matched monitoring levels.
- [ ] Q1 can evaluate the same implementation at multiple processing rates; quality-driven changes trigger renewed validation.
- [ ] Shared module acceptance: use the F1 contract; verify mono/stereo, silence, deterministic reset, finite output, bypass transitions, control changes, and the plan's sample-rate/block-size matrix.
- [ ] Record verified sources, design acceptance limits before implementation, test results, listening settings, and CPU/latency measurements in the OKF bundle. Audio processing performs no allocation, blocking, file/logging access, or UI calls.

---

## M4: Design, implement, and validate wave folding

**Title:** Design, implement, and validate wave folding

**Labels:** `enhancement`

**Published issue:** [#7](https://github.com/LordZordack/disDorktion/issues/7)

### Motivation

The middle section needs a folding module that creates a controllable range of harmonically rich sounds.

### Proposed Approach

Choose and document a folding function, controls, and scaling; independently implement and evaluate it before middle-section assembly.

Project reference: `docs/PROJECT_PLAN.md`, sections 1–4.

### Dependencies

- [F1 / #2](https://github.com/LordZordack/disDorktion/issues/2) - Establish the JUCE build, module contract, and automated test foundation
- [F2 / #3](https://github.com/LordZordack/disDorktion/issues/3) - Build the reusable audition and measurement harness

### Acceptance Criteria

- [ ] The reviewed design specifies the folding function, threshold/drive controls, range/defaults, and identity/bypass behavior.
- [ ] Tests verify fold boundaries, symmetry where designed, continuity, and extreme drive.
- [ ] Spectral measurements characterize harmonics and aliasing across representative inputs.
- [ ] Listening evaluations record useful mild and aggressive settings and any audible limitations.
- [ ] Q1 can evaluate the same implementation at multiple processing rates; quality-driven changes trigger renewed validation.
- [ ] Shared module acceptance: use the F1 contract; verify mono/stereo, silence, deterministic reset, finite output, bypass transitions, control changes, and the plan's sample-rate/block-size matrix.
- [ ] Record verified sources, design acceptance limits before implementation, test results, listening settings, and CPU/latency measurements in the OKF bundle. Audio processing performs no allocation, blocking, file/logging access, or UI calls.

---

## M5: Design, implement, and validate high-pass and low-pass modules

**Title:** Design, implement, and validate high-pass and low-pass modules

**Labels:** `enhancement`

**Published issue:** [#8](https://github.com/LordZordack/disDorktion/issues/8)

### Motivation

The output section needs independently validated low- and high-frequency shaping.

### Proposed Approach

Design and implement separate high-pass and low-pass DSP modules with documented topology, slope, cutoff controls, and stable parameter transitions.

Project reference: `docs/PROJECT_PLAN.md`, sections 1–4.

### Dependencies

- [F1 / #2](https://github.com/LordZordack/disDorktion/issues/2) - Establish the JUCE build, module contract, and automated test foundation
- [F2 / #3](https://github.com/LordZordack/disDorktion/issues/3) - Build the reusable audition and measurement harness

### Acceptance Criteria

- [ ] Each filter has a reviewed design covering topology, slope, parameter limits/defaults, bypass, and smoothing.
- [ ] Impulse/sweep tests verify cutoff response, attenuation, and stability against independent expectations.
- [ ] Cutoff limits remain valid below Nyquist at every supported sample rate.
- [ ] Rapid cutoff changes and silence/reset conditions remain finite and stable.
- [ ] Both filters can be selected and evaluated independently in the harness; each has its own acceptance evidence.
- [ ] Shared module acceptance: use the F1 contract; verify mono/stereo, silence, deterministic reset, finite output, bypass transitions, control changes, and the plan's sample-rate/block-size matrix.
- [ ] Record verified sources, design acceptance limits before implementation, test results, listening settings, and CPU/latency measurements in the OKF bundle. Audio processing performs no allocation, blocking, file/logging access, or UI calls.

---

## Q1: Evaluate and approve middle-section anti-aliasing and latency design

**Title:** Evaluate and approve middle-section anti-aliasing and latency design

**Labels:** `enhancement`

**Published issue:** [#9](https://github.com/LordZordack/disDorktion/issues/9)

### Motivation

Nonlinear waveshaping quality must be balanced against CPU cost, latency, and audible filtering.

### Proposed Approach

Evaluate native-rate and oversampled tanh/folding processing, starting from fixed 4× FIR oversampling around the middle section. Approve or replace that baseline using measurements and listening.

Project reference: `docs/PROJECT_PLAN.md`, sections 3, 4.

Reference material:

- [Reference 1](https://docs.juce.com/develop/classjuce_1_1dsp_1_1Oversampling.html)
- [Reference 2](https://aaltodoc.aalto.fi/items/3d3a2f3d-022a-4b48-98a5-a172c79dfb7a)

### Dependencies

- [M3 / #6](https://github.com/LordZordack/disDorktion/issues/6) - Design, implement, and validate tanh saturation
- [M4 / #7](https://github.com/LordZordack/disDorktion/issues/7) - Design, implement, and validate wave folding

### Acceptance Criteria

- [ ] Comparisons record alias spectra, frequency/phase behavior, CPU cost, latency, and matched-level listening results for representative drive settings.
- [ ] The approved design specifies processing rate, filters, latency reporting, dry alignment, and stable latency across middle-module bypass states.
- [ ] Intentional bit-crusher processing remains outside the middle oversampling boundary.
- [ ] Acceptance limits and the selected quality tradeoff are documented with source-backed reasoning.
- [ ] Required waveshaper revisions repeat affected M3/M4 validation before S2 is accepted.

---

## S1: Assemble and qualify the input section

**Title:** Assemble and qualify the input section

**Labels:** `enhancement`

**Published issue:** [#10](https://github.com/LordZordack/disDorktion/issues/10)

### Motivation

Accepted input modules must work predictably together before full-chain integration.

### Proposed Approach

Assemble input gain followed by bit crushing in a reusable section and qualify their interactions.

Project reference: `docs/PROJECT_PLAN.md`, sections 3, 4.

### Dependencies

- [M1 / #4](https://github.com/LordZordack/disDorktion/issues/4) - Design, implement, and validate the reusable gain module
- [M2 / #5](https://github.com/LordZordack/disDorktion/issues/5) - Design, implement, and validate bit crushing

### Acceptance Criteria

- [ ] The section processes accepted gain/crusher implementations in the fixed order.
- [ ] Section results match sequential module processing within documented tolerance.
- [ ] Input gain, quantization overload, and sample-rate reduction interactions are measured and auditioned.
- [ ] Bypass combinations, rapid controls, mono/stereo, resets, and variable blocks pass.
- [ ] Any module changes receive renewed module acceptance; section CPU and latency meet the recorded budget.

---

## S2: Assemble and qualify the middle section

**Title:** Assemble and qualify the middle section

**Labels:** `enhancement`

**Published issue:** [#11](https://github.com/LordZordack/disDorktion/issues/11)

### Motivation

Tanh and folding interactions and their anti-aliasing boundary define the central distortion behavior.

### Proposed Approach

Assemble tanh followed by wave folding inside Q1's approved processing boundary and tune their interaction with recorded evidence.

Project reference: `docs/PROJECT_PLAN.md`, sections 3, 4.

### Dependencies

- [M3 / #6](https://github.com/LordZordack/disDorktion/issues/6) - Design, implement, and validate tanh saturation
- [M4 / #7](https://github.com/LordZordack/disDorktion/issues/7) - Design, implement, and validate wave folding
- [Q1 / #9](https://github.com/LordZordack/disDorktion/issues/9) - Evaluate and approve middle-section anti-aliasing and latency design

### Acceptance Criteria

- [ ] The section uses accepted waveshapers and Q1's approved anti-aliasing design.
- [ ] Results match the equivalent ordered modules and sample-rate wrappers within documented tolerance.
- [ ] Mild/extreme combined drive, module bypass combinations, and parameter transitions pass measurements and listening.
- [ ] Reported latency remains consistent across effect bypass; impulse tests verify processing delay.
- [ ] Changes from tuning are reflected in module designs and repeat affected tests.
- [ ] CPU, aliasing, and listening evidence satisfy the approved section criteria.

---

## S3: Assemble and qualify the output section

**Title:** Assemble and qualify the output section

**Labels:** `enhancement`

**Published issue:** [#12](https://github.com/LordZordack/disDorktion/issues/12)

### Motivation

The final filtering must remain stable and predictable when the two accepted filters are combined.

### Proposed Approach

Assemble high-pass followed by low-pass and qualify combined response and control interactions.

Project reference: `docs/PROJECT_PLAN.md`, sections 3, 4.

### Dependencies

- [M5 / #8](https://github.com/LordZordack/disDorktion/issues/8) - Design, implement, and validate high-pass and low-pass modules

### Acceptance Criteria

- [ ] The section uses independently accepted filter modules in the agreed order.
- [ ] Measured response matches the ordered module reference within documented tolerance.
- [ ] Overlapping cutoffs, endpoint settings, automation, bypass combinations, and reconfiguration remain stable.
- [ ] Listening documents useful tonal ranges and any interaction limitations.
- [ ] Processing cost/latency are recorded; any module changes repeat affected acceptance checks.

---

## C1: Integrate the full chain, dry/wet mixing, and output gain

**Title:** Integrate the full chain, dry/wet mixing, and output gain

**Labels:** `enhancement`

**Published issue:** [#13](https://github.com/LordZordack/disDorktion/issues/13)

### Motivation

Accepted sections need full-chain validation before the production interface and release work.

### Proposed Approach

Connect input, middle, and output sections; capture dry audio before input gain, align it to processing latency, mix globally, then apply final output gain.

Project reference: `docs/PROJECT_PLAN.md`, sections 1–4.

### Dependencies

- [S1 / #10](https://github.com/LordZordack/disDorktion/issues/10) - Assemble and qualify the input section
- [S2 / #11](https://github.com/LordZordack/disDorktion/issues/11) - Assemble and qualify the middle section
- [S3 / #12](https://github.com/LordZordack/disDorktion/issues/12) - Assemble and qualify the output section
- [M1 / #4](https://github.com/LordZordack/disDorktion/issues/4) - Design, implement, and validate the reusable gain module

### Acceptance Criteria

- [ ] The fixed chain uses accepted sections and the reusable output-gain implementation.
- [ ] Dry/wet endpoints and final gain behave as specified; input drive does not alter the dry branch.
- [ ] Measured dry alignment and reported host latency are correct across supported configurations.
- [ ] Full-chain bypass combinations, automation, variable blocks, mono/stereo, and sample-rate changes pass.
- [ ] Cumulative coloration, overload/headroom behavior, and phase-sensitive intermediate mix settings are measured and auditioned.
- [ ] Full-chain CPU cost meets the foundation budget and integration tuning repeats affected module/section acceptance.

---

## U1: Complete the plugin interface, automation, state, and presets

**Title:** Complete the plugin interface, automation, state, and presets

**Labels:** `enhancement`

**Published issue:** [#14](https://github.com/LordZordack/disDorktion/issues/14)

### Motivation

The validated processing chain needs a usable interface and reliable host behavior.

### Proposed Approach

Build the production VST3 interface with three visible processing sections, effect bypass, global mix/output controls, metering, host parameters, versioned state, and curated presets.

Project reference: `docs/PROJECT_PLAN.md`, sections 2–4.

Reference material:

- [Reference 1](https://docs.juce.com/master/classjuce_1_1AudioProcessorValueTreeState.html)

### Dependencies

- [C1 / #13](https://github.com/LordZordack/disDorktion/issues/13) - Integrate the full chain, dry/wet mixing, and output gain

### Acceptance Criteria

- [ ] Controls expose the approved DSP parameters with readable units, defaults, bypass states, and meters.
- [ ] Stable parameter IDs and versioned state support host automation and save/restore round trips.
- [ ] Missing/malformed state is handled safely according to a documented policy.
- [ ] Presets cover subtle through aggressive sounds and recall without unintended gain jumps or unstable processing.
- [ ] Editor reopen, multiple instances, reset, and offline render retain correct state and behavior.
- [ ] The UI uses the same accepted DSP implementation and does not block the audio thread.

---

## R1: Qualify and package the first Windows VST3 release

**Title:** Qualify and package the first Windows VST3 release

**Labels:** `enhancement`

**Published issue:** [#15](https://github.com/LordZordack/disDorktion/issues/15)

### Motivation

The first finished plugin needs reproducible evidence that it is ready to install and use.

### Proposed Approach

Run the release validation matrix, pluginval, REAPER host checks, performance measurements, license review, and a clean-machine installation check; package the results with user documentation.

Project reference: `docs/PROJECT_PLAN.md`, sections 4.

### Dependencies

- [U1 / #14](https://github.com/LordZordack/disDorktion/issues/14) - Complete the plugin interface, automation, state, and presets

### Acceptance Criteria

- [ ] Windows CI/build and the required module/section/full-chain suites pass with recorded commands and versions.
- [ ] pluginval passes with tool version and configuration recorded.
- [ ] REAPER checks cover load, automation, bypass, recall, multiple instances, and offline rendering.
- [ ] Worst-case performance on documented hardware meets the budget, including small buffers and extreme settings.
- [ ] A clean-machine installation loads the packaged VST3 successfully.
- [ ] Versioned artifacts include presets, installation/use instructions, supported configurations, licenses, and known limitations.
- [ ] The release evidence is linked from the project record and the issue.

---

## X1: Design, implement, and validate carrier modulation and demodulation

**Title:** Design, implement, and validate carrier modulation and demodulation

**Labels:** `enhancement`

**Published issue:** [#16](https://github.com/LordZordack/disDorktion/issues/16)

### Motivation

Explore and deliver the README's radio-like carrier modulation/demodulation character as an independently accepted module.

### Proposed Approach

Review modulation references, define the audible distortion mechanism and phase behavior, approve the design, then implement and independently qualify the module in the harness.

Project reference: `docs/PROJECT_PLAN.md`, sections 3, 4.

Reference material:

- [Reference 1](https://www.dafx.de/paper-archive/details/U0QyREn-tXZ76ItfJA-tDQ)

### Dependencies

- [F1 / #2](https://github.com/LordZordack/disDorktion/issues/2) - Establish the JUCE build, module contract, and automated test foundation
- [F2 / #3](https://github.com/LordZordack/disDorktion/issues/3) - Build the reusable audition and measurement harness

### Acceptance Criteria

- [ ] The reviewed design defines carrier/modulator/demodulator relationships, filtering, controls/ranges/defaults, phase behavior, and internal sample-rate requirements.
- [ ] Measurements demonstrate the intended audible mechanism rather than assuming ideal coherent demodulation produces distortion.
- [ ] Tests verify oscillator continuity, block-boundary behavior, expected spectral products, parameter endpoints, and reset behavior.
- [ ] Listening documents phase adjustment and useful radio-like settings on multiple source types.
- [ ] Aliasing, CPU, latency, and stability meet limits chosen during design.
- [ ] The accepted reusable module is available to X5; production-chain integration is deferred to that issue.
- [ ] Shared module acceptance: use the F1 contract; verify mono/stereo, silence, deterministic reset, finite output, bypass transitions, control changes, and the plan's sample-rate/block-size matrix.
- [ ] Record verified sources, design acceptance limits before implementation, test results, listening settings, and CPU/latency measurements in the OKF bundle. Audio processing performs no allocation, blocking, file/logging access, or UI calls.

---

## X2: Design, implement, and validate resonant comb filtering

**Title:** Design, implement, and validate resonant comb filtering

**Labels:** `enhancement`

**Published issue:** [#17](https://github.com/LordZordack/disDorktion/issues/17)

### Motivation

Deliver independently validated metallic/resonant coloration for the expanded output section.

### Proposed Approach

Research comb structures, approve topology and controls, then implement and qualify a reusable comb module in the harness.

Project reference: `docs/PROJECT_PLAN.md`, sections 3, 4.

Reference material:

- [Reference 1](https://www.dsprelated.com/freebooks/pasp/)

### Dependencies

- [F1 / #2](https://github.com/LordZordack/disDorktion/issues/2) - Establish the JUCE build, module contract, and automated test foundation
- [F2 / #3](https://github.com/LordZordack/disDorktion/issues/3) - Build the reusable audition and measurement harness

### Acceptance Criteria

- [ ] The reviewed design defines feedforward/feedback topology, delay range/units, interpolation, damping, feedback bounds, defaults, and tail behavior.
- [ ] Impulse/frequency-response tests verify expected resonances, notches, delay, and decay.
- [ ] Feedback endpoints and silence/extreme-input tests demonstrate stability within the documented range.
- [ ] Delay/feedback control transitions avoid undocumented discontinuities and remain stable across block sizes.
- [ ] Listening and CPU/latency/tail measurements support the accepted design.
- [ ] The accepted reusable module is available to X5; production-chain integration is deferred to that issue.
- [ ] Shared module acceptance: use the F1 contract; verify mono/stereo, silence, deterministic reset, finite output, bypass transitions, control changes, and the plan's sample-rate/block-size matrix.
- [ ] Record verified sources, design acceptance limits before implementation, test results, listening settings, and CPU/latency measurements in the OKF bundle. Audio processing performs no allocation, blocking, file/logging access, or UI calls.

---

## X3: Design, implement, and validate frequency warping

**Title:** Design, implement, and validate frequency warping

**Labels:** `enhancement`

**Published issue:** [#18](https://github.com/LordZordack/disDorktion/issues/18)

### Motivation

Turn the README's warping concept into a defined, measurable, independently accepted audio effect.

### Proposed Approach

Compare spectral delay, all-pass/feedback structures, and frequency-remapping interpretations against the desired sound; approve a specific design before implementation and harness qualification.

Project reference: `docs/PROJECT_PLAN.md`, sections 3, 4.

Reference material:

- [Reference 1](https://www.dafx.de/paper-archive/2009/papers/paper_36.pdf)
- [Reference 2](https://www.dsprelated.com/freebooks/filters/)

### Dependencies

- [F1 / #2](https://github.com/LordZordack/disDorktion/issues/2) - Establish the JUCE build, module contract, and automated test foundation
- [F2 / #3](https://github.com/LordZordack/disDorktion/issues/3) - Build the reusable audition and measurement harness

### Acceptance Criteria

- [ ] The reviewed design states the audible objective, topology, controls, stability limits, defaults, and intended pitch behavior.
- [ ] The design distinguishes static phase/group-delay changes from harmonic-frequency remapping, using verified sources and measurements.
- [ ] Tests verify the selected structure's magnitude/phase or spectral behavior against independent expectations.
- [ ] Feedback/time-varying behavior, where used, remains stable through endpoint and transition tests.
- [ ] Recorded listening and CPU/latency/tail measurements satisfy limits chosen during design.
- [ ] The accepted reusable module is available to X5; production-chain integration is deferred to that issue.
- [ ] Shared module acceptance: use the F1 contract; verify mono/stereo, silence, deterministic reset, finite output, bypass transitions, control changes, and the plan's sample-rate/block-size matrix.
- [ ] Record verified sources, design acceptance limits before implementation, test results, listening settings, and CPU/latency measurements in the OKF bundle. Audio processing performs no allocation, blocking, file/logging access, or UI calls.

---

## X4: Design, implement, and validate the tone module

**Title:** Design, implement, and validate the tone module

**Labels:** `enhancement`

**Published issue:** [#19](https://github.com/LordZordack/disDorktion/issues/19)

### Motivation

Add a musically useful tone control with a defined purpose beyond the existing high-pass and low-pass filters.

### Proposed Approach

Evaluate the accepted output section, approve a distinct tone response and control mapping, then implement and independently qualify that module.

Project reference: `docs/PROJECT_PLAN.md`, sections 3, 4.

Reference material:

- [Reference 1](https://www.dsprelated.com/freebooks/filters/)

### Dependencies

- [F1 / #2](https://github.com/LordZordack/disDorktion/issues/2) - Establish the JUCE build, module contract, and automated test foundation
- [F2 / #3](https://github.com/LordZordack/disDorktion/issues/3) - Build the reusable audition and measurement harness
- [S3 / #12](https://github.com/LordZordack/disDorktion/issues/12) - Assemble and qualify the output section

### Acceptance Criteria

- [ ] The reviewed design explains the control's purpose, selected topology, ranges/defaults, mapping, and relation to existing output filters.
- [ ] Impulse/sweep tests verify the intended response, neutral setting where designed, and control endpoints.
- [ ] Automation, supported sample rates, bypass, and reset remain stable.
- [ ] Matched-level listening demonstrates the intended tonal shaping on representative material.
- [ ] CPU/latency evidence and an independently accepted reusable module are available to X5.
- [ ] Production-chain integration is deferred to X5.
- [ ] Shared module acceptance: use the F1 contract; verify mono/stereo, silence, deterministic reset, finite output, bypass transitions, control changes, and the plan's sample-rate/block-size matrix.
- [ ] Record verified sources, design acceptance limits before implementation, test results, listening settings, and CPU/latency measurements in the OKF bundle. Audio processing performs no allocation, blocking, file/logging access, or UI calls.

---

## X5: Integrate and qualify the experimental modules

**Title:** Integrate and qualify the experimental modules

**Labels:** `enhancement`

**Published issue:** [#20](https://github.com/LordZordack/disDorktion/issues/20)

### Motivation

Expand the released plugin using accepted experimental modules while preserving existing sessions and predictable section behavior.

### Proposed Approach

Approve the expanded fixed order using the README as a starting proposal, integrate each accepted module into its section, and repeat section, chain, and release qualification.

Project reference: `docs/PROJECT_PLAN.md`, sections 1, 3, 4.

### Dependencies

- [X1 / #16](https://github.com/LordZordack/disDorktion/issues/16) - Design, implement, and validate carrier modulation and demodulation
- [X2 / #17](https://github.com/LordZordack/disDorktion/issues/17) - Design, implement, and validate resonant comb filtering
- [X3 / #18](https://github.com/LordZordack/disDorktion/issues/18) - Design, implement, and validate frequency warping
- [X4 / #19](https://github.com/LordZordack/disDorktion/issues/19) - Design, implement, and validate the tone module
- [R1 / #15](https://github.com/LordZordack/disDorktion/issues/15) - Qualify and package the first Windows VST3 release

### Acceptance Criteria

- [ ] The approved expanded order and per-module bypass/default behavior are recorded before integration.
- [ ] Only accepted X1–X4 implementations are integrated; affected section reference comparisons pass.
- [ ] Existing parameter IDs, automation, and saved presets retain their behavior; new modules default to bypass in old sessions.
- [ ] Dry/wet alignment, host latency, delay tails, feedback limits, and transitions pass combined-chain testing.
- [ ] Performance, automation, state, host validation, and listening qualification repeat with all new effects enabled and bypassed.
- [ ] Any module changes prompted by integration repeat their independent acceptance checks.
- [ ] Updated presets, documentation, and versioned release artifacts describe the expanded plugin.

---
