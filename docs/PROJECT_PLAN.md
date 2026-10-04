# DisDorktion overarching project plan

Status: agreed project direction; development has not started. Ticket drafts await approval before GitHub publication.

## 1. Goal and development methodology

Build a finished Windows VST3 distortion plugin with a broad sound palette, from gentle saturation to aggressive digital destruction. A standalone audition application supports development using the same DSP implementations as the plugin.

Develop every module independently through:

**Research → design → implementation → automated tests → listening evaluation → acceptance.**

Once the modules belonging to a section are accepted, assemble and tune that section. Once all three sections are accepted, assemble and validate the complete plugin. A section may advance when its own dependencies are accepted, without waiting for unrelated modules.

### First release

The fixed serial processing chain is:

**Input gain → bit crusher → tanh saturation → wave folder → high-pass → low-pass → global dry/wet → final output gain.**

- Input section: input gain and bit crusher.
- Middle section: tanh saturation and wave folding, with a shared anti-aliasing boundary.
- Output section: high-pass and low-pass filters.
- Global controls: independent effect bypass, one dry/wet control, and final output gain.
- Supported targets: Windows VST3 and a standalone audition application; mono and stereo.

The dry branch is captured before input gain. Final output gain affects both dry and wet audio. Changing wet-path drive must not change the dry reference.

Carrier modulation/demodulation, resonant comb filtering, frequency warping, and a dedicated tone control follow as experimental expansion modules. Their research can begin before the first release; production integration follows its qualification. Reordering, parallel routing, additional platforms, and additional plugin formats are future extensions.

## 2. Architecture and shared foundation

### Reusable module contract

Keep DSP independent of plugin editors, host state, and audition sources. The standalone harness and finished plugin use the same module implementations.

Every module supports:

- Preparation using sample rate, maximum block size, and channel count; allocate working storage during preparation.
- Reset to clear processing history deterministically.
- Validated parameter updates without allocating or blocking during processing.
- Audio processing that preserves channel layout and sample count.
- Latency reporting for section and plugin integration.

Support mono and stereo with independent channel histories. Define behavior for zero-length and varying blocks, and safely handle blocks exceeding the prepared capacity through bounded chunk processing or an explicitly tested equivalent.

Smooth continuous controls and bypass transitions. Each module design explicitly describes transitions for discrete parameters. Define module parameter ranges, defaults, endpoint behavior, and identity/bypass behavior before implementation.

Audio processing must avoid memory allocation, locks, file access, logging, and UI calls. Use preallocated state, bounded processing, and suitable thread communication. Transfer meter measurements to the interface without blocking the audio callback. Guard against denormals and unstable feedback where relevant.

### Parameters, automation, and saved state

The plugin adapter owns host automation and persistence; modules own DSP behavior. Use stable parameter identifiers and versioned saved state. Use JUCE AudioProcessorValueTreeState for the host-facing layer.

Restore parameters through the same validation used for live controls. Define safe handling of malformed state, missing values, and future versions before the first release. Preserve existing parameter identifiers and preset semantics when adding experimental modules.

Reference: [JUCE AudioProcessorValueTreeState](https://docs.juce.com/master/classjuce_1_1AudioProcessorValueTreeState.html).

### Audition and measurement harness

Build one standalone development application that can select an individual module, an accepted section, or the complete chain. Do not maintain separate applications for each effect.

Provide:

- Looped audio-file playback and keyboard/MIDI sine input.
- Module controls, bypass, input/output meters, and separate monitoring-volume control.
- Repeatable settings and source selection for listening comparisons.
- Offline utilities for impulses, sweeps, sine waves, two-tone signals, noise, and rendered audio.
- A way to record source, settings, sample rate, and observations for each experiment.

Test sources belong to the harness. The VST3 processes host audio. Harness monitoring gain must not affect the underlying module measurements.

### Build and tooling

Use C++ and JUCE with CMake, a pinned JUCE revision, and a Windows toolchain documented in the foundation ticket. Provide shared DSP, VST3, standalone, and automated-test targets. Run Windows builds and DSP tests in CI.

The foundation design records supported tool versions, reproducible commands, test framework, reference hardware, and performance measurement procedure before module implementation begins.

## 3. Development stages and acceptance gates

| Stage | Deliverables | Gate before advancing |
| --- | --- | --- |
| A. Foundation | JUCE/CMake project, module contract, parameter conventions, test utilities, audition harness | A simple gain processor works through the harness and automated tests |
| B. Core modules | Independently accepted gain, bit crusher, tanh, wave folder, high-pass, and low-pass modules | Every module meets its documented acceptance criteria |
| C. Section integration | Accepted input, middle, and output sections | Gain staging, interactions, bypass, automation, and section performance pass |
| D. Complete chain | Connected sections, dry/wet, final output gain, latency handling | Full-chain measurements and listening evaluations pass |
| E. Finished plugin | Production interface, presets, host state, automation, documentation, packaging | Windows VST3 passes release qualification |
| F. Experimental expansion | Independently designed and accepted experimental modules | Every new module and affected section passes regression qualification |

Foundation can use a minimal reference gain processor to prove the contract. M1 subsequently qualifies the production gain module; the harness does not depend on that ticket being complete.

### Definition of an accepted module

Every module must have:

- A design describing the algorithm, controls, ranges, defaults, bypass/identity behavior, and expected sound.
- Verified reference material and documented alternatives considered.
- Automated tests against mathematical expectations or an independent reference implementation.
- Recorded listening results with source material and exact settings.
- Processing-cost, latency, stability, and relevant aliasing measurements.
- Documented behavior across supported sample rates, block sizes, resets, and parameter changes.
- Numeric tolerances and spectral/performance acceptance limits established in its design before implementation.

A promising sound alone does not satisfy this gate. Complete module evidence accompanies its ticket closure.

### Section integration and tuning

- Input: confirm gain and bit crushing interact predictably, including quantization overload and sample-rate reduction.
- Middle: tune how tanh drives the wave folder; compare isolated and combined behavior and finalize anti-aliasing.
- Output: confirm filter interaction, cutoff limits, stable control changes, and predictable response.
- Complete chain: evaluate cumulative coloration, loudness, clipping risk, latency, and dry/wet behavior.

Keep adjustments traceable. If integration tuning changes a module algorithm or parameter semantics, update its design and repeat affected module tests before accepting the section. Use internal headroom and visible overload indication; do not silently add an undocumented limiter or gain-normalization stage.

### Audio-quality strategy

Preserve intentional bit-crusher artifacts. Apply anti-aliasing around the nonlinear middle section rather than automatically oversampling the entire chain.

Use fixed **4× FIR oversampling as the initial design baseline**, validated through spectral measurements and listening. Q1 must approve or replace this baseline before production middle-section integration. Initial module prototypes provide the comparison material; Q1 results may require changes and renewed module acceptance before S2 closes.

Keep the oversampling path active across individual middle-module bypass states so those switches do not change reported latency. Align the global dry path to wet processing latency and report latency to the host. Delay compensation does not remove all filter phase differences; assess the audible blend across the full mix range.

Reference: [JUCE Oversampling](https://docs.juce.com/develop/classjuce_1_1dsp_1_1Oversampling.html).

### Experimental module gates

Each experimental algorithm receives one ticket covering separate research/design, implementation, and validation gates. Do not implement an uncertain mechanism until its design is reviewed.

- Carrier modulation/demodulation: define carrier modulation, demodulation, filtering, and the mechanism producing audible distortion; test the effect of phase adjustment. Ideal coherent modulation/demodulation must not be assumed to create the intended radio character.
- Resonant comb: choose feedforward/feedback topology, delay controls, damping, feedback limits, and tail behavior.
- Frequency warping: define the audible objective and choose a structure; distinguish spectral delay, phase shaping, and frequency remapping. A static all-pass response must not be described as harmonic-frequency remapping without evidence.
- Tone: establish a distinct purpose relative to high-pass and low-pass filtering before choosing its topology and control range.

During X5, approve the expanded fixed order before integration. The README's order is the starting proposal: input gain → demodulation → bit crusher → tanh → wave folder → comb → warping → output filtering/tone → global mix → output gain. Experimental effects default to bypass for existing sessions. Repeat affected section and full-chain qualification.

## 4. Research, testing, and release strategy

### Research documentation

Maintain research and decisions in the existing OKF bundle:

- `docs/okf/projects/disdorktion.md`: project sound, design decisions, milestones, and links to this plan and tickets.
- `docs/okf/methods/`: individual DSP designs, ranges, endpoints, and source-backed explanations.
- `docs/okf/papers/`: verified paper metadata, relevant findings, and method links.
- `docs/okf/notes/`: measurements, listening experiments, and resulting decisions.

These are planned records, not already completed research. Use paper-reader for paper ingestion and okf-curator for metadata, cross-links, index maintenance, and the change log. Verify full sources before adopting DSP claims. Clearly distinguish proposed behavior from measured behavior.

### Initial reference set

| Reference | Intended use |
| --- | --- |
| [JUCE documentation](https://docs.juce.com/master/) | Processing lifecycle, parameters, filters, and plugin integration |
| [Introduction to Digital Filters with Audio Applications — Julius O. Smith](https://www.dsprelated.com/freebooks/filters/) | Output filters, stability, and phase response |
| [Physical Audio Signal Processing — Julius O. Smith](https://www.dsprelated.com/freebooks/pasp/) | Delay lines, comb structures, and all-pass processing |
| [Oversampling for Nonlinear Waveshaping: Choosing the Right Filters](https://aaltodoc.aalto.fi/items/3d3a2f3d-022a-4b48-98a5-a172c79dfb7a) | Middle-section anti-aliasing research |
| [Digital Emulation of Distortion Effects by Wave and Phase Shaping Methods](https://www.dafx.de/paper-archive/details/xa8gqNE6X8u_cgZiVbAIig) | Waveshaping and phase-shaping research |
| [Modulation and Delay Line Based Digital Audio Effects](https://www.dafx.de/paper-archive/details/U0QyREn-tXZ76ItfJA-tDQ) | Radio/modulation research |
| [Spectral delay filters with feedback and time-varying coefficients](https://www.dafx.de/paper-archive/2009/papers/paper_36.pdf) | Spectral-delay structures and stability |

Also verify the editions and relevant chapters of the Udo Zölzer books suggested in the README. This bibliography is a starting point, not evidence that all sources have been fully reviewed.

### Module tests

| Module | Required scenarios |
| --- | --- |
| Gain | Expected amplitude scaling, unity behavior, smoothing, and independent channel behavior |
| Bit crusher | Quantization levels, sample-hold timing, endpoints, overload policy, and continuity across blocks |
| Tanh | Transfer curve, symmetry, bounded output, drive response, and harmonic behavior |
| Wave folder | Fold boundaries, symmetry, continuity, and extreme drive |
| High-pass/low-pass | Cutoff response, attenuation, stability, and sample-rate-dependent limits |
| Experimental modules | Design-specific mathematical expectations, audible mechanism, stability limits, transitions, and latency/tails |

All modules: silence, reset reproducibility, channel independence, finite output within documented ranges, supported layouts, variable blocks, and bypass transitions.

### Integration tests

- Compare each section against its constituent modules processed in the same order.
- Test rapid controls, combined drive settings, parameter endpoints, and bypass combinations.
- Verify global mix endpoints, latency alignment, output gain, host bypass, and saved-state round trips.
- Test sample-rate reconfiguration, reopening the editor, reset, offline rendering, and multiple instances.
- Confirm experimental additions preserve existing presets and automation behavior.

### Validation matrix and listening

Test mono/stereo at **44.1, 48, 88.2, 96, and 192 kHz**. Include block sizes **1, 32, 64, 127, 256, and 1024 samples**, zero-length blocks, and changing block sizes. Frequency-dependent controls must remain valid below the active Nyquist limit.

Use drums, bass, guitar, vocals, synths, and transient-rich material for repeatable listening. Record source licensing, settings, sample rate, and observations. Match monitoring levels for algorithm comparisons. Keep deterministic measurement fixtures separate from subjective listening material.

### Performance and release qualification

- Automated Windows build and DSP tests in CI.
- Plugin validation, including pluginval with tool version and configuration recorded.
- Manual REAPER checks for loading, automation, bypass, state recall, offline rendering, and multiple instances.
- CPU measurements on documented hardware; F1 establishes the budget and procedure, and every integration gate checks them.
- Assess worst-case settings and small blocks, not just average cost at default settings.
- Check all relevant third-party and test-audio licenses before distribution.
- Perform a clean-machine installation check.
- Package the versioned VST3 with presets, installation instructions, supported configurations, and known limitations.

R1 records the resulting evidence and constitutes the first-release gate. Expanded releases repeat relevant checks through X5.

## 5. Ticket roadmap and working strategy

Repository: [LordZordack/disDorktion](https://github.com/LordZordack/disDorktion).

The IDs below are planning identifiers, not GitHub issue numbers. Complete proposed titles, bodies, and labels are in [the ticket drafts](TICKET_DRAFTS.md). Replace publication status with issue links after approved publication.

| ID | Ticket | Dependencies | Publication |
| --- | --- | --- | --- |
| P0 | Establish the project roadmap and research conventions | None | Draft |
| F1 | Establish the JUCE build, module contract, and automated test foundation | P0 | Draft |
| F2 | Build the reusable audition and measurement harness | F1 | Draft |
| M1 | Design, implement, and validate the reusable gain module | F1, F2 | Draft |
| M2 | Design, implement, and validate bit crushing | F1, F2 | Draft |
| M3 | Design, implement, and validate tanh saturation | F1, F2 | Draft |
| M4 | Design, implement, and validate wave folding | F1, F2 | Draft |
| M5 | Design, implement, and validate high-pass and low-pass modules | F1, F2 | Draft |
| Q1 | Evaluate and approve middle-section anti-aliasing and latency design | M3, M4 | Draft |
| S1 | Assemble and qualify the input section | M1, M2 | Draft |
| S2 | Assemble and qualify the middle section | M3, M4, Q1 | Draft |
| S3 | Assemble and qualify the output section | M5 | Draft |
| C1 | Integrate the full chain, dry/wet mixing, and output gain | S1, S2, S3, M1 | Draft |
| U1 | Complete the plugin interface, automation, state, and presets | C1 | Draft |
| R1 | Qualify and package the first Windows VST3 release | U1 | Draft |
| X1 | Design, implement, and validate carrier modulation and demodulation | F1, F2 | Draft |
| X2 | Design, implement, and validate resonant comb filtering | F1, F2 | Draft |
| X3 | Design, implement, and validate frequency warping | F1, F2 | Draft |
| X4 | Design, implement, and validate the tone module | F1, F2, S3 | Draft |
| X5 | Integrate and qualify the experimental modules | X1, X2, X3, X4, R1 | Draft |

Dependencies represent accepted deliverables. Q1 consumes accepted baseline waveshapers and may send them back through validation after quality changes. X5 integrates only accepted experimental modules.

### Ticket execution and change control

- Use ticket-create for grounded issue drafts and publication. Each issue contains motivation, a bounded approach, dependency links, references, and observable acceptance criteria.
- Use existing labels: documentation for P0 and enhancement for the remaining tickets.
- Publish only after the complete drafts are explicitly approved, as ticket-create requires.
- Record returned numbers, populate issue dependency links, and update this table and the roadmap issue. Check existing issues before retrying interrupted publication to prevent duplicates.
- Produce a detailed design and implementation plan inside each ticket's scope before coding; this document is the overarching roadmap.
- Use ticket-get/ticket-start for execution and ticket-pr for completed work, following the repository's current workflow instructions.
- Run GitNexus impact analysis before existing-symbol edits. Resolve UNKNOWN findings and report HIGH/CRITICAL risk. Run graph change analysis before commits alongside relevant validation.
- Do not introduce unrelated refactors. Module changes prompted by integration return through the affected acceptance gates.

### Assumptions and defaults

Windows VST3, shared standalone audition harness, mono/stereo, fixed serial order, independent effect bypass, one global dry/wet control, final output gain, core-first release, and broad distortion character are agreed defaults. The initial quality baseline is fixed 4× FIR oversampling for the middle section, subject to Q1's evidence-based design gate.

No DSP implementation, build setup, module acceptance, or release qualification is claimed by the existence of this plan.
