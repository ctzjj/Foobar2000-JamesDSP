# JamesDSP foobar2000 Plugin — Real Parameter Surface (remove all fake controls)

Date: 2026-09-16
Status: approved

## Goal

Make every control in the configuration dialog drive a real library parameter.
Remove every control that has no backing API, and replace mis-modelled controls with
the parameters libjamesdsp actually exposes.

## Problem

An audit of `jdsp_host/jdsp_engine.cpp` `SetParam` against the real library APIs found
that most of the dialog was fictional:

- **BS2B** — `bs2b.feed` / `bs2b.freq` are stored but never applied. The API only
  offers `CrossfeedEnable` / `CrossfeedDisable` / `CrossfeedChangeMode`.
- **DDC Strength** — serialized by the dialog, no `SetParam` branch, no library
  parameter.
- **Compressor** — `threshold` / `ratio` / `attack` / `release` are stored but never
  applied. The real API is a 7-band spectral compander
  (`CompressorSetParam` + `CompressorSetGain`).
- **Convolver Gain** — actually calls `JamesDSPSetPostGain()`, i.e. the global output
  gain. The convolver has no gain of its own.
- **Reverb** — `roomsize` only re-applies the default preset; `damping` and `wet` are
  never applied. The only API is `Reverb_SetParam(jdsp, presetIndex)` with 19 presets.
- **Bass Boost Freq** — stored only; `BassBoostSetParam` takes a single max gain in dB.
- **Stereo Widening** — slider range 0..200 sending `width/100`, but the API expects
  `mix` in 0..1. 120 % produced mix 1.2 and gain 0.6.
- **FIR Equalizer** — only 10 bands, but `NUMPTS == 15`. Bands 11..15 kept their
  constructor defaults (2500/4000/6300/10000/16000 Hz), so the frequency axis became
  non-monotonic (index 10 = 16000 then index 11 = 2500) and the pchip/makima
  interpolation was wrong. `eq.band*.q` is entirely fake — `MultimodalEQ` has no Q.
- **Spectrum Extender** — enabling it does nothing because
  `ArbitraryResponseEqualizerStringParser()` is never called, and there is no file
  browser for it.
- **Dynamic System** — no API exists anywhere in libjamesdsp.

Also `ApplyAllParams()` is dead code (no call sites).

## Source of truth

`libjamesdsp/jni/jamesdsp/jamesdsp.c`, function `EffectDSPMainCommand()` (lines
141-788), is the Android effect command dispatcher and the authoritative list of what
JamesDSP exposes. Every parameter below maps to a command there.

## Target parameter surface

| Module | Parameters | Backing API |
|---|---|---|
| Analog Modelling | enable; Drive -3..+12 dB | `VacuumTubeSetGain` (cmd 150) |
| BS2B / Crossfeed | enable; Mode 0..5 | `CrossfeedChangeMode` (cmd 188); 0 = BS2B Lv1, 1 = BS2B Lv2, 2 = HRTF crossfeed, 3/4/5 = HRTF surround 1/2/3 |
| DDC | enable; profile text file | `DDCStringParser` (cmd 10009) |
| Limiter | enable; Threshold <= -0.09 dB; Release >= 0.15 ms | `JLimiterSetCoefficients` (cmd 1500) |
| Compressor | enable; Time constant; Granularity 0..3; TF resolution 0..3; 7-band gain axis | `CompressorSetParam` + `CompressorSetGain` (cmd 115) |
| Convolver | enable; IR WAV file | `Convolver1DLoadImpulseResponse` (cmd 10004) |
| Reverb | enable; Preset 0..18 | `Reverb_SetParam` (cmd 128) |
| Bass Boost | enable; Max gain 0..15 dB | `BassBoostSetParam` (cmd 112) |
| Stereo Widener | enable; Widening 0..100 % | `StereoEnhancementSetParam` (cmd 137, upstream divides by 100) |
| FIR Equalizer | enable; 15 bands freq+gain (gain clamped to +-64 dB); Filter type 0..5; Interpolation 0..1 | `MultimodalEqualizerAxisInterpolation` (cmd 116) |
| Spectrum Extender | enable; response text file | `ArbitraryResponseEqualizerStringParser` (cmd 10006) |
| EEL2 Script | enable; script text | `LiveProgStringParser` (cmd 10010) |
| Global Output gain | -15..+15 dB | `JamesDSPSetPostGain` (cmd 1500) |

Filter type 0 = FIR minimum phase, 1..5 = IIR 4/6/8/10/12th order.
Interpolation 0 = pchip, 1 = makima.

**Removed:** the Dynamic System module, DDC Strength, Compressor
threshold/ratio/attack/release, Bass Boost Freq, EQ Q, Reverb room/damp/wet, BS2B
feed/freq, and Convolver Gain (its role moves to the global Output gain).

## Dialog layout (7 tabs)

1. **Modules** — 12 checkboxes (Dynamic System dropped)
2. **Equalizer** — 15 band sliders on the library's default frequency axis
   (25/40/63/100/160/250/400/630/1k/1.6k/2.5k/4k/6.3k/10k/16k), plus Filter type
   combo, Interpolation combo, and the response curve widget
3. **Dynamics** — Limiter (threshold, release); Compressor (time constant,
   granularity, TF resolution, 7 band gains); DDC (profile file)
4. **Effects** — Analog Drive, Bass Boost, Stereo Widening, Reverb Preset combo,
   BS2B Mode combo, Output Gain
5. **Convolver** — enable + IR file
6. **Spectrum Extender** — enable + response file
7. **Script** — enable + EEL2 editor

## Configuration format

Keys change to match the new surface. Old keys found in an existing preset are ignored
and the corresponding parameter falls back to its default; no migration is performed.

New keys: `modules.<name>` for the 12 modules; `eq.band<N>.freq` / `eq.band<N>.gain`
for N = 0..14; `eq.filtertype`; `eq.interpolation`; `limiter.threshold`;
`limiter.release`; `compressor.timeconstant`; `compressor.granularity`;
`compressor.tfresolution`; `compressor.band<N>.gain` for N = 0..6; `tube.drive`;
`bassboost.gain`; `stereo.width`; `reverb.preset`; `bs2b.mode`; `output.gain`;
`convolver.path`; `spectrum.path`; `ddc.profile`; `script.text`; `ui.language`.

## Curve widget

The number of EQ bands grows from 10 to 15. The widget's band array, the slider block
in the resource script, and the per-band label IDs all grow accordingly. The
logarithmic 20 Hz-20 kHz x-axis and the 15 point frequency axis are compatible (the
lowest point is 25 Hz, the highest 16 kHz).

## Errors

A missing or corrupt IR / DDC / spectrum file is logged and leaves the module unarmed;
it never aborts the process. Out-of-range values are clamped to what the library
accepts (tube -3..+12 dB, limiter threshold <= -0.09 dB and release >= 0.15 ms, EQ gain
+-64 dB, output gain +-15 dB, bass boost 0..15 dB).

## Testing

- Engine, offline: extend the existing `test_blob.exe` harness in
  `C:\Users\ctzjj\AppData\Local\Temp\opencode\` to send each new key and assert the
  measured gain/RMS changes as expected (reverb preset, bass boost, stereo widening,
  output gain, EQ band, compressor band, BS2B mode, spectrum file).
- GUI, on the real player: every control on all 7 tabs must produce an audible change
  while the dialog is open (live push), Cancel must revert, OK must persist.
- Reconfirm the previously unverified points: Cancel revert, OK persistence, and live
  IR loading.

## Out of scope

Removal of the temporary diagnostics (`CfgLog`, `g_trace`, `TraceMsg`, per-chunk DLL
logging, host `PROC:` logging) is tracked separately and is done after this rewrite.
