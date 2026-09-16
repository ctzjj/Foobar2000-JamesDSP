# JamesDSP foobar2000 Plugin — Live (Dynamic) Parameter Application

Date: 2026-09-16
Status: approved (Approach A + point 3)

## Goal

While the DSP configuration dialog is open, moving any control (slider, checkbox,
EQ curve handle, combo box) must change the audio immediately, without pressing OK.
Pressing Cancel must restore the settings that were active when the dialog opened.

## Problem

Today the dialog is purely local. Control values are copied into member variables by
`SyncFromControls()` and only leave the dialog when the user presses OK, which calls
`dsp_preset_edit_callback::on_preset_changed()`. foobar2000 then rebuilds the DSP
instance, which tears down and restarts `jdsp_host.exe`. As a result:

- Nothing is audible until OK is pressed, so effects like the vacuum tube cannot be
  tuned by ear.
- The round trip needed to hear a change is heavy (host restart, possible audio gap).

The dialog already holds a reference to a `JdspIpcClient` (`m_ipc`), but it is a
throwaway instance created inside `RunDSPConfigPopup()` that was never started, so its
`SendSetParam*` calls are no-ops. The client that is actually attached to the running
host belongs to the live `jdsp_dsp` instance inside the playback chain.

## Approach A (chosen)

Let the dialog find and talk to the *live* host client, and push parameter changes
incrementally as the user edits.

### 1. Active-host registry

New translation unit `foo_dsp_jamesdsp/jdsp_live_link.h` / `.cpp`:

```cpp
void JdspSetActiveClient(JdspIpcClient* client);
void JdspClearActiveClient(JdspIpcClient* client);  // only clears if it matches
bool JdspSendToActive(const std::string& blob);     // safe, locked push
```

- `jdsp_dsp::EnsureHostRunning()` registers `&m_ipc_client` after a successful host
  start; `~jdsp_dsp()` calls `JdspClearActiveClient(&m_ipc_client)`.
- Storage is a single pointer plus a `CRITICAL_SECTION`. `JdspClearActiveClient`
  compares before clearing so a newer instance is never clobbered by an older one
  being destroyed. The dialog runs on the UI thread while destruction can happen on
  the playback thread, so the pointer access must be serialized.
- Nothing outside the registry ever holds the raw pointer. Callers push through
  `JdspSendToActive()`, which takes the lock, sends, and releases. `JdspClearActiveClient`
  takes the same lock, so a `jdsp_dsp` cannot be destroyed while a push is in flight —
  this is why the earlier `JdspGetActiveClient()` idea was dropped: returning a raw
  pointer would leave a use-after-free window between get and send.

### 2. Write serialization in `JdspIpcClient`

One `CRITICAL_SECTION` (or `std::mutex`) member held around every pipe write:

- `WriteFrame()` — around the header and payload writes.
- `IoThread()` — around the header and payload writes only. Never held across the
  response read, and never held across `WaitForSingleObject` on the thread handle,
  otherwise the audio thread would deadlock against a fire-and-forget param write.
- `SendSetParam()` / `SendSetParams()` — inherited from the above.

This makes a `SET_PARAM` frame unable to interleave into the middle of an `AUDIO_DATA`
frame. Three threads can currently write to the pipe (the audio thread via `IoThread`,
`SendSetParam`'s worker thread, and now the UI thread), so without this the frame
stream can desync. This also fixes a latent pre-existing corruption bug.

### 3. Dialog-side incremental live push

New private member `std::string m_live_blob;` plus

```cpp
void JdspConfigDialog::PushLive(bool full = false);
```

Behavior:

1. `SyncFromControls(m_hwnd);` (already idempotent and safe to call repeatedly)
2. `std::string blob = SerializeSettings();`
3. If `full`, the payload is `blob`. Otherwise it is the **key-based** diff against
   `m_live_blob`: the `key=value` lines whose key is new or whose value changed, plus
   `key=` for keys that existed before and are now gone. Lines joined with `\n`.
4. `m_live_blob = blob;`
5. `if (!payload.empty()) JdspSendToActive(payload);` — a no-op when no host is running.

The diff splits each blob line at the first `=` into key and value. `SerializeSettings()`
emits a fixed ordering, so the diff is stable.

Sending only the changed keys matters: `modules.eel2` triggers an EEL2 compile in the
host, so re-sending the whole blob on every slider tick would recompile the script
continuously.

Call sites:

- End of `OnCommand`, for every command except `IDOK` and `IDCANCEL`.
- End of `OnHScroll`.
- In `OnNotify`, after the `IDC_EQ_CURVE` / `NM_CLICK` branch (the widget also emits
  this during dragging, which is exactly when live feedback is wanted).
- `OnApply()` performs one final `PushLive(true)` so the host is guaranteed to match
  the preset that is about to be written.

### 4. Cancel reverts

- `Show()` snapshots the blob that was loaded (`m_orig_blob`) before entering
  `DialogBoxParam`, and sets `m_live_blob = m_orig_blob`.
- If `DialogBoxParam` returns anything other than `IDOK`, push `m_orig_blob` back to
  the active client and set `m_live_blob` to it.

### 5. Avoid pointless preset churn

In `RunDSPConfigPopup()`, after OK, compare the serialized blob with the incoming
preset data and only call `p_callback.on_preset_changed(new_preset)` when it actually
differs. This avoids a needless DSP rebuild and host restart when the user opens the
dialog and presses OK without changing anything.

## Point 3 — make file-backed modules actually load

`SerializeSettings()` emits `convolver.path`, `ddc.profile` and `script.text`, and
`DeserializeSettings()` parses them, but `JdspEngine::SetParam()` in
`jdsp_host/jdsp_engine.cpp` has no branches for these keys. The Convolver impulse
response, the DDC profile and the EEL2 script therefore never reach the engine.

The engine APIs these map onto (verified in
`libjamesdsp/jni/jamesdsp/jdsp/jdsp_header.h` and `jamesdsp.c`):

| key | API | input format |
| --- | --- | --- |
| `convolver.path` | `Convolver1DLoadImpulseResponse(jdsp, float* impulse, unsigned impChannels, size_t impulseLengthActual, char updateOld)` (jdsp_header.h:650) | **raw interleaved float samples**, not a file name. The Android layer parses the WAV in Java and sends floats (`jamesdsp.c:484`). The host must parse the WAV itself. |
| `ddc.profile` | `DDCStringParser(jdsp, char* text)` (jdsp_header.h:636) | the **text contents** of the `.vdc` file; `DDCParser` (vdc.c:44) requires both an `SR_44100` and an `SR_48000` section, comma-separated coefficients, 5 per biquad. Read the file as text and pass it verbatim. |
| `script.text` | `LiveProgStringParser(jdsp, char* eelCode)` (jdsp_header.h:627) | EEL2 **source text**; returns an error code that maps to an error string via `checkErrorCode()`. |

Add handling in `JdspEngine::SetParam()`:

- `convolver.path` — new helper `bool LoadImpulseResponse(const std::wstring& path)`
  in the host: read the file, parse a RIFF/WAVE header (PCM 16/24/32-bit int and
  32-bit float), de-interleave into the interleaved-float layout the API wants, and
  call `Convolver1DLoadImpulseResponse(..., updateOld=1)`. Empty path unloads by
  calling it with a zero-length impulse. Cache the last loaded path so an unchanged
  value is not re-parsed.
- `ddc.profile` — read the file as text, call `DDCStringParser`. Empty path clears.
  Cache the last path.
- `script.text` — call `LiveProgStringParser`. Recompiling is expensive, so this must
  only run when the text actually changes — which the incremental diff already
  guarantees.

Each branch must report failure through the existing logging without taking the
process down (a missing or corrupt file leaves the module unarmed and logs the
error).

Note that the Convolver WAV parsing is the largest single piece of new code here; the
impulse response is by far the biggest payload and the only one that needs a binary
format reader.

## Data flow (live edit)

```
UI thread                         audio thread
--------                          ------------
slider moves
  -> PushLive(false)
     -> SyncFromControls()
     -> SerializeSettings()
     -> key diff vs m_live_blob
     -> JdspSendToActive(diff)  --\
                                \  (pipe write mutex)
                                 -> SET_PARAM frame -> host SetParam()
                                                       engine state updated
                                 <-- no response (write-only path)
                                  /  audio frames keep flowing
  <- returns immediately        --/
```

Live pushes are synchronous `WriteFile` calls on the UI thread but write-only, so they
do not wait for a host response. With the 1 MB pipe buffers and a diff of at most a few
hundred bytes, this is a sub-millisecond operation.

## Error handling

- No active client (not playing, or host failed to start): `PushLive` is a no-op. The
  dialog still works and OK still persists everything.
- Host dies while the dialog is open: writes fail, `PushLive` ignores the failure, and
  the next `on_chunk` restarts the host and re-applies the full preset. The dialog has
  no way to know, so the user sees the effect resume on the next chunk.
- Bad file path for convolver/DDC/script: logged, module left unarmed, no crash.
- Cancel after a live session: the original blob is pushed back, so audio matches the
  persisted preset again.

## Testing

Manual, on the real player (foobar2000 v2 x64 at `D:\Program Files\foobar2000`):

1. Start playback with JamesDSP active, open Configure.
2. Drag the Analog Modelling drive slider while the dialog is open — the timbre must
   change in real time.
3. Toggle a module checkbox — the effect must switch on/off immediately.
4. Drag an EQ curve handle — the response must change immediately.
5. Press Cancel — the audio must return to the state before the dialog opened.
6. Press OK — the settings must persist and reappear on reopen (existing behavior).
7. Point 3: browse to an IR file, a DDC profile and type a script, press OK, and confirm
   in `jdsp_host.log` that the file was loaded and the module is armed.

Offline regression harnesses already exist in
`C:\Users\ctzjj\AppData\Local\Temp\opencode\` (`test_blob.exe`, `test_lim.exe`); they
speak the raw IPC protocol and can be extended to assert `convolver.path` /
`ddc.profile` / `script.text` handling without the GUI.

## Accepted limitations

- Live application works only while the host is running, i.e. while playing with
  JamesDSP in the active DSP chain. Opening the dialog without playback requires OK to
  persist, and no live preview is possible.
- Pressing OK still causes a brief audio gap: foobar2000 rebuilds the DSP instance and
  the host restarts. Removing that needs a ref-counted process-wide host singleton,
  deliberately deferred.
- Approach B (calling `on_preset_changed` on every control change) was rejected: it
  rebuilds the DSP instance and restarts the host on every tick, producing many
  audible glitches per second.

## Out of scope

- `InitDynamicsTab` hardcodes the Dynamics-tab Enable checkboxes
  (`IDC_CHK_COMP`=checked, `IDC_CHK_LIM`=unchecked, `IDC_CHK_DDC_ENABLE`=unchecked)
  and never reads them back; only the Modules-tab checkboxes drive `m_modules[]`. Dead
  UI, left as-is.
- Removing the temporary diagnostics (per-chunk DLL logging, host `PROC:` logging,
  `CfgLog`/`SvrLog`/`VecLog`, `g_trace`/`TraceMsg`) — to be done once everything is
  confirmed.
