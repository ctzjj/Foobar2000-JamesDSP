# AGENTS.md

Working notes for AI agents (and humans) continuing development of this repo.
Read this before changing anything — most of the entries below are bugs that have
already been hit and fixed once.

---

## 1. What this is

A foobar2000 DSP plugin that wraps **libjamesdsp** (JamesDSP audio engine), for
foobar2000 v1.x (x86) and v2.x (x64).

Two binaries:

| File | Role |
|---|---|
| `foo_dsp_jamesdsp.dll` | foobar2000 component: DSP class, config dialog, EQ curve widget, IPC client |
| `jdsp_host.exe` | Separate process hosting libjamesdsp; speaks a binary frame protocol over stdin/stdout |

The DLL launches `jdsp_host.exe` **from its own directory** (`GetModuleFileNameW` on its
own module + `jdsp_host.exe`). Both files must be deployed together, side by side.

Why out-of-process: a crash in the audio engine cannot take foobar2000 down, and the
engine (unmanaged C) stays isolated from foobar2000's C++ exception handling.
Historical note: subclassing a foobar2000 dialog control, or throwing C++ exceptions
across the DLL/EXE boundary, crashed the host process. Do not do either.

Protocol: [`jdsp_ipc_protocol.h`](jdsp_ipc_protocol.h) — `[u32 frame_type][u32 length][payload]`.
`AUDIO_DATA` = `{u32 sample_rate, u32 channels, u32 sample_count}` + interleaved float samples.

---

## 2. Build

Requires VS2019 (v142) or VS2022 (v143) with the C++ desktop workload. The foobar2000
SDK and pfc are **vendored** in `sdk/` (tracked in git), so no extra download.

```powershell
# full solution, x64
& "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\MSBuild\Current\Bin\MSBuild.exe" `
  "foobar2000-jamesdsp.sln" /p:Configuration=Release /p:Platform=x64 /v:minimal /nologo

# one project (faster when iterating)
#   /t:foo_dsp_jamesdsp      the DLL
#   /t:jdsp_host             the engine host
```

VS2022 without v142 installed: add `/p:PlatformToolset=v143`.

Outputs:

| Platform | directory | files |
|---|---|---|
| x86 | `Release\` | `foo_dsp_jamesdsp.dll`, `jdsp_host.exe` |
| x64 | `x64\Release\` | `foo_dsp_jamesdsp.dll`, `jdsp_host.exe` |

MSBuild output is **Chinese-localized** on this machine — grep for `error` and
`-> ` lines, not for English words like "Build succeeded".

The DLL project uses a precompiled header: **every `.cpp` under `foo_dsp_jamesdsp/`
must start with `#include "stdafx.h"`.** The host project does not use PCH.

`atl*.h` at the repo root are minimal ATL stubs so the DLL builds on Build Tools
installations without ATL. Keep them.

---

## 3. Deploy and test

Target install: `D:\Program Files\foobar2000` (v2.x, x64). Profile: `%APPDATA%\foobar2000-v2`.

```powershell
Get-Process foobar2000, jdsp_host -ErrorAction SilentlyContinue   # must be empty
Copy-Item x64\Release\foo_dsp_jamesdsp.dll,x64\Release\jdsp_host.exe `
          "D:\Program Files\foobar2000\components\" -Force
```

- foobar2000 must be **closed**; a stray `jdsp_host.exe` also holds the host binary
  locked (`taskkill /F /IM jdsp_host.exe`).
- A stray `jdsp_host.exe` has been observed surviving its parent — if a copy fails for
  no obvious reason, check for one.
- Enable via `Preferences → Playback → DSP Manager` → add **JamesDSP** to Active DSPs,
  then **Configure**.
- x86 foobar2000 needs the `Release\` (Win32) pair; x64 needs `x64\Release\`.

Logs (working directory = install dir):

| File | Written by | Content |
|---|---|---|
| `jdsp_host.log` | host | **error paths only** — IR/DDC/spectrum read failures, EEL2 compile errors, truncated frames. Normally empty. |

Debug logging was deliberately stripped (commit `d2c4dfc`). If you need it back, add
`EngineLog(...)` in the host (never `printf` — see gotchas) or an `fopen`-based logger in
the DLL, and remove it again before finishing.

---

## 4. Source map

```
foo_dsp_jamesdsp/
  foo_dsp_jamesdsp.cpp        component entry / dsp_factory_t registration
  jdsp_dsp.h/.cpp             dsp_impl_base subclass; on_chunk does the audio conversion
  jdsp_host_manager.h/.cpp    CreateProcess of jdsp_host.exe, pipe handles, CREATE_NO_WINDOW
  jdsp_ipc_client.h/.cpp      single worker thread, coalesced params, audio back-pressure
  jdsp_live_link.h/.cpp       process-wide "active client" registry (JdspSendToActive)
  jdsp_config_dialog.h/.cpp   the 7-tab dialog, serialization, live push
  jdsp_config_serializer.h/.cpp  .jdsp save/load (plain key=value text)
  jdsp_eq_widget.h/.cpp       the draggable EQ curve (custom window class WC_EQCURVE)
  resource.h                  every control/label ID
  foo_dsp_jamesdsp.rc         7 tab-page dialog templates
  strings.h / strings_en.h / strings_zh.h  (mostly legacy; only window_title/btn_ok/btn_cancel used)
  dllmain.cpp                 GetMyModule() (GetModuleHandleExW ...FROM_ADDRESS), class reg

jdsp_host/
  main.cpp                    binary mode on stdio, open jdsp_host.log, run the server
  jdsp_engine.h/.cpp          ALL parameter -> libjamesdsp API mapping lives here
  jdsp_ipc_server.cpp         frame loop, HandleAudioData, HandleSetParam (unescape + split on \n)
  wav_loader.h/.cpp           RIFF/WAVE -> interleaved float (convolver impulse responses)

jdsp_ipc_protocol.h           shared frame definitions
jdsp_reverb_presets.h         the 19 reverb presets (shared by engine and dialog)
docs/superpowers/             specs and plans for the design history
.github/workflows/build.yml   release CI (x86 + x64)
```

`jdsp_engine.cpp` is the single place where UI parameters become library calls. The
authoritative list of what JamesDSP exposes is
`libjamesdsp/jni/jamesdsp/jamesdsp.c` → `EffectDSPMainCommand()` (Android dispatcher).

---

## 5. Gotchas that have already caused bugs

**Read this section before editing.**

1. **`audio_sample` is `double` on x64**, `float` on x86
   (`sdk/foobar2000/shared/audio_math.h`). Never `memcpy` audio between an
   `audio_chunk` and a `float` buffer — convert per sample:
   `float v = (float)data[i];` in, `data[i] = (audio_sample)output[i];` out.
   Getting this wrong copied half the bytes and produced "sound plays but every effect
   sounds broken / chops once per second".

2. **Never `printf`/`std::cout` in `jdsp_host`** — stdout *is* the IPC pipe. Buffered
   output eventually flushes into the frame stream and desynchronises the protocol.
   Use `EngineLog()` (writes to `g_log`) or nothing.

3. **Wide-string `\x` escapes swallow following hex digits.** `L"\x201c100"` becomes
   `\x201C1` and fails to compile. Use `\u201c` with exactly 4 digits when the next
   character is a hex digit. Chinese label text is written as `\x` escapes throughout
   `jdsp_config_dialog.cpp` / the `.rc` — keep that discipline.

4. **The EQ frequency axis must be sorted and strictly increasing** before
   `MultimodalEqualizerAxisInterpolation` — it divides by the gap between neighbours.
   A non-monotonic or duplicated axis makes the engine block inside the call, which
   looked like "dragging EQ freezes playback for a few seconds and has no effect".
   `JdspEngine::ApplyEqualizer()` insertion-sorts a copy and bumps duplicates by 1 Hz.
   (Old presets can contain 10 bands with duplicate/unsorted frequencies.)

5. **`sf_advancereverb()` rebuilds and clears every internal delay/comb/allpass
   buffer.** Calling it for every `reverb.*` key made the reverb tail restart on every
   slider tick. Keep the split:
   - `ApplyReverbScalars()` — `wet`, `dry`, `width`, `bassb`, `ertolate` (plain field
     writes, safe to spam)
   - `ApplyReverb()` — `preset`, `rt60`, `damping`, `predelay` (full rebuild)
   `sf_advancereverb` is **not declared in `jdsp_header.h`** — the engine declares it
   itself with `extern "C"`.

6. **BS2B:** `CrossfeedChangeMode` memsets `bs2b[]` (zeroing both slots) and only
   re-inits one of them, so `bs2b[0]` is left zeroed for mode 0. `ApplyCrossfeed()`
   therefore calls `CrossfeedChangeMode` first and then its own `BS2BInit` with
   `BS2BCalculateflevel(fcut, feed*10)` (feed is in 0.1 dB units) — order matters.
   The UI has 7 modes but the library has 6; `LibBs2bMode()` maps them.

7. **Control refreshes re-enter the dialog.** `SetWindowTextW` fires
   `EN_CHANGE`/`EN_UPDATE` and `TBM_SETPOS`/`SetWindowTextW` can fire `WM_HSCROLL`,
   which are dispatched back into `OnCommand`/`OnHScroll`. A handler that resets the
   model and then refreshes the controls therefore had its own reset read back and
   undone (Reset All / Load Config / EQ Reset Flat all suffered). Set
   **`m_suppress_notify = true`** around bulk control refreshes; `OnCommand`,
   `OnNotify` and `OnHScroll` return immediately while it is set.

8. **Live pushes are throttled and diffed.** `PushLive()` only stages a blob; an 80 ms
   one-shot timer (`IDT_LIVE_PUSH`) flushes a **key-based diff** against the last sent
   blob. Never send the whole blob per event: `modules.eel2` triggers an EEL2
   recompile and `reverb.preset` rebuilds the reverb, so redundant repeats are audible.

9. **Tab pages are children of the *main* dialog**, positioned with
   `TabCtrl_AdjustRect` + `ClientToScreen`/`ScreenToClient`. Making them children of the
   tab control caused an infinite themed-paint/activation loop that froze the UI.

10. **IPC client rules** (`jdsp_ipc_client.cpp`): exactly one worker thread owns all
    reads; parameter frames are merged and written before the next audio frame; audio
    sends are back-pressured (if a frame is outstanding, the chunk passes through
    unchanged); a request is *never* abandoned, because `CancelIo` only cancels I/O
    issued by the calling thread — the old design left a zombie reader that permanently
    misaligned the stream (`ok=0` forever, effects dead but audio still playing).

11. **The wire blob escapes `\` and `\n`.** `SerializeSettings()` escapes values;
    the host's `UnescapeValue()` reverses it and strips trailing CR/LF. Windows paths
    and EEL2 scripts depend on this.

12. **Keep LTO/WPO off in the DLL project.** `WholeProgramOptimization`,
    `OptimizeReferences` and `EnableCOMDATFolding` strip the static
    `dsp_factory_t<jdsp_dsp>` registration, and the plugin silently never appears in
    the DSP list.

13. **Use `dsp_entry_v2` (default), not v3.** `show_config_popup_v3` is asynchronous;
    our implementation runs a blocking modal `DialogBoxParam` and returns nullptr,
    which made the dialog impossible to reopen.

14. **`.gitignore` caution:** a stray non-ASCII tail once turned a pattern into a bare
    `*`, silently ignoring everything untracked. `sdk/` is intentionally tracked now,
    with its build intermediates excluded by explicit path patterns.

---

## 6. Verification workflow

Two levels. Do the offline one first — it is much faster than a GUI round trip.

### Offline: drive the host directly with the IPC harness

`test_blob.exe` lives **outside the repo** at
`C:\Users\ctzjj\AppData\Local\Temp\opencode\` (with `test_blob.cpp`; rebuild with
`vcvars64.bat` + `cl /nologo /EHsc /O2 /Fe:test_blob.exe test_blob.cpp`). It starts
`jdsp_host.exe`, sends a blob file as one `SET_PARAM` frame, then N audio frames of a
test signal, and prints per-block `rms_in rms_out gain pkout pkin dc` plus a
`BOUNDARY:` line and round-trip ms.

```
test_blob.exe <host.exe> <blobfile> <samplesPerFrame> <frames> <amp> [sampleRate] [mode]
mode: 0 = 3 sparse sines, 1 = white noise, 2 = square, 3 = clipped noise (loud-master-like)
```

Blob files are plain `key=value` lines and **must use the escaped form** for
backslashes (`spectrum.path=C:\\Users\\...`), because the host unescapes. Fixtures used
so far: `blob_user.txt`, `b_bs2b0/1.txt`, `b_m0..b_m6.txt`, `b_bb/b_cp/b_eq/b_out/b_rv/b_sp/b_st/b_tb/b_xf.txt`,
`test.vdc`, `ir.wav`.

Because the harness manages its own IPC client, it verifies the **engine**, not the DLL
client. DLL-side changes (dialog, live link, client) can only be exercised in foobar2000.

A second harness `test_lim.exe` exists for limiter thresholds. The old PowerShell
harnesses (`test_host.ps1`, `test_identity*.ps1`, `test_timing.ps1`) are unreliable with
large frames — prefer the C++ clients.

### On-machine: foobar2000

1. Close foobar2000, deploy both binaries, start foobar2000, play something.
2. `Configure` → move controls → the change must be audible **immediately**.
3. Cancel must restore the pre-open state; OK must persist (reopen and check).
4. Reset All must reset the UI, the checkboxes and the audio at once.

---

## 7. Parameter surface (what the UI may expose)

Everything below maps 1:1 onto a library API. **Do not add controls that are not in
this table** — the UI previously had ~10 fake controls (DDC strength, compressor
threshold/ratio/attack/release, bass boost frequency, EQ Q, reverb room/damp/wet, BS2B
feed/freq, convolver gain) that did nothing; they were removed deliberately.

| Module | Parameters |
|---|---|
| Analog Modelling | enable, Drive -3..+12 dB |
| BS2B / Crossfeed | enable, Mode 0..6 (UI), Feed 1..15 dB, Cutoff 300..2000 Hz |
| DDC | enable, profile text file (`.vdc`) |
| Limiter | enable, Threshold <= -0.09 dB, Release >= 0.15 ms |
| Compressor | enable, Time constant (s), Granularity 0..3, TF Resolution 0..3, 7 band gains |
| Convolver | enable, IR WAV file |
| Reverb | enable, Preset 0..18, Wet, Dry, Width, RT60, Damping, Bass, Predelay, Early reflections |
| Bass Boost | enable, Max gain 0..15 dB |
| Stereo Widener | enable, Widening 0..100 % (50 % is identity) |
| Equalizer | enable, 15 bands (25 Hz..16 kHz) freq+gain, Filter type 0..5, Interpolation 0..1 |
| Spectrum Extender | enable, response text file (`frequency gain` per line) |
| EEL2 | enable, script text (needs `@init` and `@sample`; `spl0`/`spl1` are in/out) |
| (global) | Output gain -15..+15 dB |

Config keys are `modules.<name>`, `eq.band<N>.freq|.gain`, `eq.filtertype`,
`eq.interpolation`, `compressor.*`, `compressor.band<N>.gain`, `limiter.*`, `tube.drive`,
`bs2b.mode|feed|freq`, `bassboost.gain`, `stereo.width`, `reverb.*`, `output.gain`,
`convolver.path`, `ddc.profile`, `spectrum.path`, `script.text`, `ui.language`.
`SerializeSettings()` order is deterministic — the live diff depends on that.

---

## 8. Known limitations (by design, not bugs)

- **Bass Boost has no frequency control.** `dbb.c` adapts to the current bass peak via
  a 16-bin FFT; only the max gain is settable. Use the low EQ bands for fixed boosts.
- **The equalizer has no Q** — `MultimodalEQ` has no such field.
- **The convolver has no gain of its own** — use the global Output gain.
- Sample-rate changes and track changes restart the host (short gap).
- Crossfeed is inherently subtle; it is a listening-fatigue feature, not an effect.

## 9. Open / unfinished

- `strings.h` + `strings_en.h`/`strings_zh.h` still carry unused legacy `tab_*` / `mod_*`
  fields.
- `EqBand::q` is unused.
- File-backed modules (IR, DDC, spectrum, EEL2) are verified offline but were never
  exercised from the GUI end to end.
- The persisted DSP preset on the dev machine is still in the **pre-rewrite** format
  (it has never been re-saved with OK), so it carries obsolete keys. Those are ignored
  and fall back to defaults.

## 10. Git / CI

- Branch `master`, remote `origin https://github.com/ctzjj/Foobar2000-JamesDSP`.
- `.github/workflows/build.yml` builds x86 and x64 on `windows-2022` and attaches
  `foo_dsp_jamesdsp-x86.zip` / `foo_dsp_jamesdsp-x64.zip` to a Release. It triggers on
  **published** releases (draft releases have no reliable tag yet) and on
  `workflow_dispatch` (optional `tag` input to attach artifacts to an existing release).
- Commit messages in this repo are conventional (`feat:`, `fix:`, `chore:`, `docs:`).
- Do not commit `*.dll`, `*.exe`, `*.log` or SDK build intermediates.
