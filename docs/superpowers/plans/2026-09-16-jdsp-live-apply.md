# JamesDSP Live Parameter Application Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make every control in the JamesDSP configuration dialog take effect on the audio in real time, and make the Convolver / DDC / EEL2 script file-backed modules actually load.

**Architecture:** The dialog currently only writes settings into the DSP preset on OK, which forces foobar2000 to rebuild the DSP chain. This plan adds a process-wide registry so the dialog can push `key=value` lines straight to the `JdspIpcClient` that owns the *live* `jdsp_host.exe`, sending only the keys that changed. It also adds the missing `JdspEngine::SetParam` branches for `convolver.path` / `ddc.profile` / `script.text`, plus a RIFF/WAVE reader that feeds `Convolver1DLoadImpulseResponse`.

**Tech Stack:** C++17, Win32 (MSBuild / VS2019 BuildTools), foobar2000 SDK, libjamesdsp (C), custom stdin/stdout binary IPC.

---

## Build / deploy reference (used by every task)

```
Build (from D:\fb2kJDSP):
& "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\MSBuild\Current\Bin\MSBuild.exe" foobar2000-jamesdsp.sln /p:Configuration=Release /p:Platform=x64 /v:minimal /nologo

Single project: add  /t:foo_dsp_jamesdsp   or  /t:jdsp_host

Outputs: D:\fb2kJDSP\x64\Release\foo_dsp_jamesdsp.dll   and   D:\fb2kJDSP\x64\Release\jdsp_host.exe
Deploy : copy both to "D:\Program Files\foobar2000\components\" with foobar2000 closed
         (taskkill /F /IM jdsp_host.exe first if a stray host is running)
Logs   : "D:\Program Files\foobar2000\jdsp_cfg.log" (DLL) and "jdsp_host.log" (host)
```

The DLL project uses a precompiled header, so **every new `.cpp` in `foo_dsp_jamesdsp/` must start with `#include "stdafx.h"`**. The host project does not use PCH.

## File structure

| File | Responsibility |
| --- | --- |
| `foo_dsp_jamesdsp/jdsp_ipc_client.h/.cpp` | modify: add a write `CRITICAL_SECTION` so SET_PARAM can never interleave into an AUDIO_DATA frame |
| `foo_dsp_jamesdsp/jdsp_live_link.h/.cpp` | create: process-wide registry of the client that owns the running host; locked `JdspSendToActive()` |
| `foo_dsp_jamesdsp/jdsp_dsp.cpp` | modify: register/unregister the active client |
| `foo_dsp_jamesdsp/jdsp_config_dialog.h/.cpp` | modify: `PushLive()`, key-based blob diff, live call sites, cancel revert, `OnApply` simplification |
| `jdsp_host/wav_loader.h/.cpp` | create: RIFF/WAVE → interleaved float samples |
| `jdsp_host/jdsp_engine.h/.cpp` | modify: `convolver.path`, `ddc.profile`, `script.text` handling |
| `foo_dsp_jamesdsp/foo_dsp_jamesdsp.vcxproj` | modify: add the two new DLL files |
| `jdsp_host/jdsp_host.vcxproj` | modify: add `wav_loader.cpp` |

---

### Task 1: Serialize pipe writes in `JdspIpcClient`

Without this, the new UI-thread `SET_PARAM` write can land in the middle of an
`AUDIO_DATA` frame written by the playback thread and desync the frame protocol.

**Files:**
- Modify: `foo_dsp_jamesdsp/jdsp_ipc_client.h`
- Modify: `foo_dsp_jamesdsp/jdsp_ipc_client.cpp`

- [ ] **Step 1: Add the lock members to the header**

Replace the whole of `foo_dsp_jamesdsp/jdsp_ipc_client.h` with:

```cpp
#pragma once
#include <windows.h>
#include <cstdint>
#include <vector>
#include <string>
#include "jdsp_ipc_protocol.h"

class JdspIpcClient {
public:
    JdspIpcClient(class JdspHostManager& manager);
    ~JdspIpcClient();

    bool SendAudioData(uint32_t sample_rate, uint32_t channels,
                       uint32_t sample_count, const float* audio,
                       float* output);
    bool SendSetParam(const std::string& key, const std::string& value);
    // Sends many "key=value\n" settings in a single frame, synchronously.
    bool SendSetParams(const std::string& blob);
    bool SendShutdown();

private:
    bool WriteFrame(jdsp::FrameType type, const void* data, uint32_t length);
    bool ReadFrameWithTimeout(jdsp::FrameHeader& header, std::vector<uint8_t>& payload);

    JdspHostManager& m_manager;
    // Held only around the bytes of a single frame. Never held across a read:
    // only SendAudioData issues AUDIO_DATA frames and it is called from one
    // thread, so responses stay paired with their request without the lock.
    CRITICAL_SECTION m_write_cs;
};
```

- [ ] **Step 2: Change the constructor and add a destructor**

In `foo_dsp_jamesdsp/jdsp_ipc_client.cpp`, replace:

```cpp
JdspIpcClient::JdspIpcClient(JdspHostManager& manager) : m_manager(manager) {}
```

with:

```cpp
JdspIpcClient::JdspIpcClient(JdspHostManager& manager) : m_manager(manager) {
    InitializeCriticalSection(&m_write_cs);
}

JdspIpcClient::~JdspIpcClient() {
    DeleteCriticalSection(&m_write_cs);
}
```

- [ ] **Step 3: Lock `WriteFrame`**

Replace the body of `JdspIpcClient::WriteFrame` with:

```cpp
bool JdspIpcClient::WriteFrame(jdsp::FrameType type, const void* data, uint32_t length) {
    HANDLE hWrite = m_manager.GetStdinWrite();
    if (!hWrite) return false;

    jdsp::FrameHeader header;
    header.type = type;
    header.data_length = length;

    EnterCriticalSection(&m_write_cs);
    bool ok = WriteFull(hWrite, &header, sizeof(header));
    if (ok && data && length > 0) {
        ok = WriteFull(hWrite, data, length);
    }
    LeaveCriticalSection(&m_write_cs);
    return ok;
}
```

- [ ] **Step 4: Add the lock pointer to `IoThreadArgs`**

In the `struct IoThreadArgs { ... };` definition add this member after `HANDLE hWrite;`:

```cpp
    CRITICAL_SECTION* write_cs;
```

- [ ] **Step 5: Lock the write section of `IoThread`**

Replace this block of `IoThread`:

```cpp
    if (!WriteFull(a->hWrite, &wheader, sizeof(wheader))) {
        a->write_ok = false;
        IoFinish(a);
        return 0;
    }
    if (!a->write_buf.empty()) {
        if (!WriteFull(a->hWrite, a->write_buf.data(), (DWORD)a->write_buf.size())) {
            a->write_ok = false;
            IoFinish(a);
            return 0;
        }
    }
    a->write_ok = true;
```

with:

```cpp
    EnterCriticalSection(a->write_cs);
    bool wok = WriteFull(a->hWrite, &wheader, sizeof(wheader));
    if (wok && !a->write_buf.empty()) {
        wok = WriteFull(a->hWrite, a->write_buf.data(), (DWORD)a->write_buf.size());
    }
    LeaveCriticalSection(a->write_cs);

    if (!wok) {
        a->write_ok = false;
        IoFinish(a);
        return 0;
    }
    a->write_ok = true;
```

- [ ] **Step 6: Hand the lock to both thread launchers**

In `SendAudioData`, after `args->hWrite = hWrite;` add:

```cpp
    args->write_cs = &m_write_cs;
```

In `SendSetParam`, after `args->hWrite = hWrite;` add:

```cpp
    args->write_cs = &m_write_cs;
```

- [ ] **Step 7: Build and verify it still compiles**

Run:
```
& "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\MSBuild\Current\Bin\MSBuild.exe" foobar2000-jamesdsp.sln /p:Configuration=Release /p:Platform=x64 /v:minimal /nologo /t:foo_dsp_jamesdsp
```
Expected: `0 Error(s)`, and `D:\fb2kJDSP\x64\Release\foo_dsp_jamesdsp.dll` rewritten.

- [ ] **Step 8: Commit**

```
git add foo_dsp_jamesdsp/jdsp_ipc_client.h foo_dsp_jamesdsp/jdsp_ipc_client.cpp
git commit -m "fix: serialize ipc pipe writes so SET_PARAM cannot split an AUDIO_DATA frame"
```

---

### Task 2: Active-host registry

**Files:**
- Create: `foo_dsp_jamesdsp/jdsp_live_link.h`
- Create: `foo_dsp_jamesdsp/jdsp_live_link.cpp`
- Modify: `foo_dsp_jamesdsp/jdsp_dsp.cpp`
- Modify: `foo_dsp_jamesdsp/foo_dsp_jamesdsp.vcxproj`

- [ ] **Step 1: Create the header**

`foo_dsp_jamesdsp/jdsp_live_link.h`:

```cpp
#pragma once
#include <string>

class JdspIpcClient;

// Registers the client whose host process is currently processing audio.
void JdspSetActiveClient(JdspIpcClient* client);

// Clears the registration, but only if it still points at the given client.
void JdspClearActiveClient(JdspIpcClient* client);

// Pushes a "key=value\n" blob to the active host. Returns false when no host is
// running. Locked, so it can run on the config dialog's thread while the
// playback thread is streaming audio.
bool JdspSendToActive(const std::string& blob);
```

- [ ] **Step 2: Create the implementation**

`foo_dsp_jamesdsp/jdsp_live_link.cpp`:

```cpp
#include "stdafx.h"
#include "jdsp_live_link.h"
#include "jdsp_ipc_client.h"

namespace {

struct LiveLinkState {
    CRITICAL_SECTION cs;
    JdspIpcClient* active;
    LiveLinkState() : active(nullptr) { InitializeCriticalSection(&cs); }
    ~LiveLinkState() { DeleteCriticalSection(&cs); }
};

LiveLinkState g_live;

}  // namespace

void JdspSetActiveClient(JdspIpcClient* client) {
    EnterCriticalSection(&g_live.cs);
    g_live.active = client;
    LeaveCriticalSection(&g_live.cs);
}

void JdspClearActiveClient(JdspIpcClient* client) {
    EnterCriticalSection(&g_live.cs);
    if (g_live.active == client) g_live.active = nullptr;
    LeaveCriticalSection(&g_live.cs);
}

bool JdspSendToActive(const std::string& blob) {
    if (blob.empty()) return false;
    EnterCriticalSection(&g_live.cs);
    bool ok = false;
    if (g_live.active) ok = g_live.active->SendSetParams(blob);
    LeaveCriticalSection(&g_live.cs);
    return ok;
}
```

- [ ] **Step 3: Register the client in `jdsp_dsp.cpp`**

Add the include next to the other project includes at the top of
`foo_dsp_jamesdsp/jdsp_dsp.cpp`:

```cpp
#include "jdsp_live_link.h"
```

In `jdsp_dsp::EnsureHostRunning()`, replace:

```cpp
    m_host_started = true;
    CfgLog("EnsureHostRunning: host STARTED");
    ApplyPresetToHost();
    return true;
```

with:

```cpp
    m_host_started = true;
    CfgLog("EnsureHostRunning: host STARTED");
    ApplyPresetToHost();
    JdspSetActiveClient(&m_ipc_client);
    return true;
```

- [ ] **Step 4: Unregister on destruction**

Replace the whole of `jdsp_dsp::~jdsp_dsp()` with:

```cpp
jdsp_dsp::~jdsp_dsp() {
    JdspClearActiveClient(&m_ipc_client);
    if (m_host_started) {
        CfgLog("jdsp_dsp: destroying -> shutdown host");
        m_ipc_client.SendShutdown();
        m_host_manager.Stop();
    } else {
        CfgLog("jdsp_dsp: destroyed (host never started)");
    }
}
```

- [ ] **Step 5: Add the new files to the DLL project**

In `foo_dsp_jamesdsp/foo_dsp_jamesdsp.vcxproj`, in the `<ItemGroup>` containing the
`<ClCompile Include=...>` list, after `<ClCompile Include="jdsp_ipc_client.cpp" />`
add:

```xml
    <ClCompile Include="jdsp_live_link.cpp" />
```

and after `<ClInclude Include="jdsp_ipc_client.h" />` add:

```xml
    <ClInclude Include="jdsp_live_link.h" />
```

- [ ] **Step 6: Build and verify**

Run:
```
& "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\MSBuild\Current\Bin\MSBuild.exe" foobar2000-jamesdsp.sln /p:Configuration=Release /p:Platform=x64 /v:minimal /nologo /t:foo_dsp_jamesdsp
```
Expected: `0 Error(s)` and the DLL is rewritten. A link error `unresolved external symbol "void __cdecl JdspSetActiveClient"` means Step 5 was skipped or misspelled.

- [ ] **Step 7: Commit**

```
git add foo_dsp_jamesdsp/jdsp_live_link.h foo_dsp_jamesdsp/jdsp_live_link.cpp foo_dsp_jamesdsp/jdsp_dsp.cpp foo_dsp_jamesdsp/foo_dsp_jamesdsp.vcxproj
git commit -m "feat: add process-wide active host client registry"
```

---

### Task 3: `PushLive()` and the incremental blob diff

**Files:**
- Modify: `foo_dsp_jamesdsp/jdsp_config_dialog.h`
- Modify: `foo_dsp_jamesdsp/jdsp_config_dialog.cpp`

- [ ] **Step 1: Declare the new members**

In `foo_dsp_jamesdsp/jdsp_config_dialog.h`, after the line
`void SyncFromControls(HWND hwnd);` add:

```cpp
    void PushLive(bool full = false);
```

and after `std::string m_settings_blob;` add:

```cpp
    std::string m_live_blob;   // last blob successfully derived from the controls
    std::string m_orig_blob;   // blob captured when the dialog opened (cancel revert)
```

- [ ] **Step 2: Add the includes**

At the top of `foo_dsp_jamesdsp/jdsp_config_dialog.cpp`, next to the existing
includes, add:

```cpp
#include "jdsp_live_link.h"
#include <map>
```

- [ ] **Step 3: Write the failing behaviour first — snapshot the opening blob and revert on cancel**

Replace `JdspConfigDialog::Show` with:

```cpp
bool JdspConfigDialog::Show(HWND parent) {
    m_live_blob = SerializeSettings();
    m_orig_blob = m_live_blob;

    INT_PTR r = DialogBoxParam(GetMyModule(),
                               MAKEINTRESOURCE(IDD_JDSP_CONFIG),
                               parent, DialogProc, reinterpret_cast<LPARAM>(this));

    if (r != IDOK) {
        CfgLog("Show: cancelled -> reverting live settings");
        m_live_blob = m_orig_blob;
        JdspSendToActive(m_orig_blob);
    }
    return r == IDOK;
}
```

- [ ] **Step 4: Write the diff helpers**

Immediately after the existing `UnescapeValue` function (which ends just before
`void JdspConfigDialog::SyncFromControls`), insert:

```cpp
static void SplitBlob(const std::string& blob, std::map<std::string, std::string>& out) {
    size_t pos = 0;
    while (pos < blob.size()) {
        size_t eol = blob.find('\n', pos);
        if (eol == std::string::npos) eol = blob.size();
        std::string line = blob.substr(pos, eol - pos);
        pos = eol + 1;
        if (line.empty()) continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        out[line.substr(0, eq)] = line.substr(eq + 1);
    }
}

// Only the keys whose value changed, plus "key=" for keys that disappeared.
// Sending the whole blob on every slider tick would make the host recompile the
// EEL2 script (modules.eel2 / script.text) thousands of times per drag.
static std::string DiffBlobs(const std::string& old_blob, const std::string& new_blob) {
    std::map<std::string, std::string> old_kv, new_kv;
    SplitBlob(old_blob, old_kv);
    SplitBlob(new_blob, new_kv);

    std::string out;
    size_t pos = 0;
    while (pos < new_blob.size()) {
        size_t eol = new_blob.find('\n', pos);
        if (eol == std::string::npos) eol = new_blob.size();
        std::string line = new_blob.substr(pos, eol - pos);
        pos = eol + 1;
        if (line.empty()) continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::map<std::string, std::string>::const_iterator it = old_kv.find(line.substr(0, eq));
        if (it == old_kv.end() || it->second != line.substr(eq + 1)) {
            out += line;
            out += '\n';
        }
    }

    for (std::map<std::string, std::string>::const_iterator it = old_kv.begin();
         it != old_kv.end(); ++it) {
        if (new_kv.find(it->first) == new_kv.end()) {
            out += it->first;
            out += "=\n";
        }
    }
    return out;
}
```

- [ ] **Step 5: Implement `PushLive`**

Insert immediately after `SyncFromControls` ends (just before
`std::string JdspConfigDialog::SerializeSettings() const {`):

```cpp
void JdspConfigDialog::PushLive(bool full) {
    if (!m_hwnd) return;

    SyncFromControls(m_hwnd);
    std::string blob = SerializeSettings();

    std::string payload;
    if (full) {
        payload = blob;
    } else {
        payload = DiffBlobs(m_live_blob, blob);
    }
    m_live_blob = blob;

    if (!payload.empty()) {
        JdspSendToActive(payload);
    }
}
```

- [ ] **Step 6: Confirm it compiles before the call sites exist**

At this point `PushLive` is defined but never called, so the dialog is still not live.
Build to prove the new code compiles cleanly:

```
& "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\MSBuild\Current\Bin\MSBuild.exe" foobar2000-jamesdsp.sln /p:Configuration=Release /p:Platform=x64 /v:minimal /nologo /t:foo_dsp_jamesdsp
```
Expected: PASS (0 errors). Do not deploy.

- [ ] **Step 7: Wire the live call sites into `OnCommand`**

Replace the beginning of `JdspConfigDialog::OnCommand` (the two `if (id == IDOK)` /
`else if (id == IDCANCEL)` branches) so both end the dialog and **return**, then add
`PushLive(false);` as the last statement of the function.

Replace:

```cpp
    if (id == IDOK) {
        OnApply(hwnd);
        EndDialog(hwnd, IDOK);
    } else if (id == IDCANCEL) {
        EndDialog(hwnd, IDCANCEL);
    } else if (id == IDC_COMBO_LANGUAGE && code == CBN_SELCHANGE) {
```

with:

```cpp
    if (id == IDOK) {
        OnApply(hwnd);
        EndDialog(hwnd, IDOK);
        return;
    }
    if (id == IDCANCEL) {
        EndDialog(hwnd, IDCANCEL);
        return;
    }
    if (id == IDC_COMBO_LANGUAGE && code == CBN_SELCHANGE) {
```

Inside the `IDC_COMBO_EQ_BAND` branch, replace:

```cpp
        HWND tab = m_tab_dialogs[1];
        if (!tab) return;
        // Save current band edits first
        ApplyEqTab(hwnd);
```

with:

```cpp
        HWND tab = m_tab_dialogs[1];
        if (!tab) { PushLive(false); return; }
        // Save current band edits first
        ApplyEqTab(hwnd);
```

Finally, add the live push at the very end of the function, replacing the closing of
the `EN_KILLFOCUS` branch and the function's closing brace:

```cpp
    } else if ((id == IDC_EDIT_EQ_FREQ || id == IDC_EDIT_EQ_Q) && code == EN_KILLFOCUS) {
        HWND tab = m_tab_dialogs[1];
        if (tab) {
            ApplyEqTab(tab);
            m_eq_widget.SetBands(m_eq_bands);
        }
    }

    PushLive(false);
}
```

- [ ] **Step 8: Wire the live call site into `OnHScroll`**

Add one line at the end of `JdspConfigDialog::OnHScroll`, after the EQ band branch's
closing brace:

```cpp
    PushLive(false);
}
```

- [ ] **Step 9: Wire the live call site into `OnNotify`**

Add one line at the end of `JdspConfigDialog::OnNotify`, after the `IDC_EQ_CURVE`
branch's closing brace:

```cpp
    PushLive(false);
}
```

- [ ] **Step 10: Build and verify**

```
& "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\MSBuild\Current\Bin\MSBuild.exe" foobar2000-jamesdsp.sln /p:Configuration=Release /p:Platform=x64 /v:minimal /nologo /t:foo_dsp_jamesdsp
```
Expected: `0 Error(s)`.

- [ ] **Step 11: Commit**

```
git add foo_dsp_jamesdsp/jdsp_config_dialog.h foo_dsp_jamesdsp/jdsp_config_dialog.cpp
git commit -m "feat: push dialog changes to the live host as an incremental key diff"
```

---

### Task 4: Make OK push a full snapshot, and stop pointless preset churn

**Files:**
- Modify: `foo_dsp_jamesdsp/jdsp_config_dialog.cpp` (`OnApply`)
- Modify: `foo_dsp_jamesdsp/jdsp_dsp.cpp` (`RunDSPConfigPopup`)

- [ ] **Step 1: Replace the dead per-key send storm in `OnApply`**

`OnApply` currently sends ~70 `SendSetParam` calls to `m_ipc`, a client created by
`RunDSPConfigPopup` that is never started, so every call is a silent no-op. Replace
the whole body of `JdspConfigDialog::OnApply` (everything from
`void JdspConfigDialog::OnApply(HWND hwnd) {` up to and including its closing brace,
i.e. the block ending right before `void JdspConfigDialog::OnLanguageChange`) with:

```cpp
void JdspConfigDialog::OnApply(HWND hwnd) {
    (void)hwnd;
    SyncFromControls(m_hwnd);
    m_settings_blob = SerializeSettings();
    // Full push so the running host is guaranteed to match the preset we write.
    m_live_blob = m_settings_blob;
    JdspSendToActive(m_settings_blob);
}
```

- [ ] **Step 2: Skip `on_preset_changed` when nothing changed**

In `foo_dsp_jamesp/jdsp_dsp.cpp` (`foo_dsp_jamesdsp/jdsp_dsp.cpp`), replace
`RunDSPConfigPopup` with:

```cpp
static void RunDSPConfigPopup(const dsp_preset& p_data, HWND p_parent, dsp_preset_edit_callback& p_callback) {
    JdspHostManager host_mgr;
    JdspIpcClient ipc(host_mgr);
    JdspConfigDialog dlg(ipc);

    std::string orig;
    if (p_data.get_data() && p_data.get_data_size() > 0) {
        orig.assign(static_cast<const char*>(p_data.get_data()), (size_t)p_data.get_data_size());
        dlg.DeserializeSettings(orig);
    }

    if (dlg.Show(p_parent)) {
        std::string blob = dlg.SerializeSettings();
        if (blob == orig) {
            CfgLog("RunDSPConfigPopup: settings unchanged, not notifying foobar2000");
            return;
        }
        dsp_preset_impl new_preset;
        new_preset.set_owner(g_jdsp_guid);
        new_preset.set_data(blob.data(), blob.size());
        p_callback.on_preset_changed(new_preset);
    }
}
```

- [ ] **Step 3: Build and verify**

```
& "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\MSBuild\Current\Bin\MSBuild.exe" foobar2000-jamesdsp.sln /p:Configuration=Release /p:Platform=x64 /v:minimal /nologo /t:foo_dsp_jamesdsp
```
Expected: `0 Error(s)`.

- [ ] **Step 4: Commit**

```
git add foo_dsp_jamesdsp/jdsp_config_dialog.cpp foo_dsp_jamesdsp/jdsp_dsp.cpp
git commit -m "feat: apply full settings on OK and skip preset rebuild when unchanged"
```

---

### Task 5: RIFF/WAVE loader for the convolver impulse response

`Convolver1DLoadImpulseResponse` takes raw interleaved floats, so the host has to
parse the file itself. This task is test-driven with a standalone console harness so
it can be verified without foobar2000.

**Files:**
- Create: `jdsp_host/wav_loader.h`
- Create: `jdsp_host/wav_loader.cpp`
- Create: `C:\Users\ctzjj\AppData\Local\Temp\opencode\test_wav.cpp`
- Modify: `jdsp_host/jdsp_host.vcxproj`

- [ ] **Step 1: Write the failing test**

`C:\Users\ctzjj\AppData\Local\Temp\opencode\test_wav.cpp`:

```cpp
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include "wav_loader.h"

static int g_fail = 0;

static void Check(bool ok, const char* what) {
    printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) g_fail++;
}

static void PutU16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back((uint8_t)(x & 0xFF)); v.push_back((uint8_t)(x >> 8));
}
static void PutU32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back((uint8_t)(x & 0xFF)); v.push_back((uint8_t)((x >> 8) & 0xFF));
    v.push_back((uint8_t)((x >> 16) & 0xFF)); v.push_back((uint8_t)((x >> 24) & 0xFF));
}

// Builds a minimal RIFF/WAVE file. format: 1 = PCM int, 3 = IEEE float.
static std::vector<uint8_t> MakeWav(uint16_t format, uint16_t channels,
                                    uint32_t sample_rate, uint16_t bits,
                                    const std::vector<uint8_t>& data) {
    std::vector<uint8_t> v;
    const char* riff = "RIFF"; v.insert(v.end(), riff, riff + 4);
    PutU32(v, 0);  // patched below
    const char* wave = "WAVE"; v.insert(v.end(), wave, wave + 4);
    const char* fmt = "fmt "; v.insert(v.end(), fmt, fmt + 4);
    PutU32(v, 16);
    PutU16(v, format);
    PutU16(v, channels);
    PutU32(v, sample_rate);
    uint16_t block_align = (uint16_t)(channels * bits / 8);
    PutU32(v, sample_rate * block_align);
    PutU16(v, block_align);
    PutU16(v, bits);
    const char* dat = "data"; v.insert(v.end(), dat, dat + 4);
    PutU32(v, (uint32_t)data.size());
    v.insert(v.end(), data.begin(), data.end());
    uint32_t riff_size = (uint32_t)(v.size() - 8);
    v[4] = (uint8_t)(riff_size & 0xFF);
    v[5] = (uint8_t)((riff_size >> 8) & 0xFF);
    v[6] = (uint8_t)((riff_size >> 16) & 0xFF);
    v[7] = (uint8_t)((riff_size >> 24) & 0xFF);
    return v;
}

static void WriteFile(const char* path, const std::vector<uint8_t>& bytes) {
    FILE* f = nullptr;
    fopen_s(&f, path, "wb");
    if (f) { fwrite(bytes.data(), 1, bytes.size(), f); fclose(f); }
}

static bool Near(float a, float b, float eps = 1e-6f) {
    float d = a - b; if (d < 0) d = -d;
    return d <= eps;
}

int main() {
    // 16-bit stereo, 4 frames: L/R pairs
    {
        std::vector<uint8_t> d;
        int16_t src[8] = { 0, 32767, -32768, 16384, 8192, -8192, 1, -1 };
        for (int i = 0; i < 8; i++) PutU16(d, (uint16_t)src[i]);
        WriteFile("t16.wav", MakeWav(1, 2, 48000, 16, d));

        std::vector<float> s;
        uint32_t ch = 0, sr = 0;
        bool ok = LoadWavInterleaved(L"t16.wav", s, ch, sr);
        Check(ok, "16-bit stereo loads");
        Check(ch == 2 && sr == 48000, "16-bit stereo header");
        Check(s.size() == 8, "16-bit stereo frame count");
        Check(Near(s[0], 0.0f) && Near(s[1], 32767.0f / 32768.0f), "16-bit values");
        Check(Near(s[2], -1.0f) && Near(s[3], 0.5f), "16-bit negative + half");
    }

    // 32-bit float mono, 3 frames
    {
        std::vector<uint8_t> d;
        float src[3] = { 0.25f, -0.5f, 1.0f };
        for (int i = 0; i < 3; i++) {
            uint32_t bits32;
            memcpy(&bits32, &src[i], 4);
            PutU32(d, bits32);
        }
        WriteFile("tf32.wav", MakeWav(3, 1, 44100, 32, d));

        std::vector<float> s;
        uint32_t ch = 0, sr = 0;
        bool ok = LoadWavInterleaved(L"tf32.wav", s, ch, sr);
        Check(ok && s.size() == 3, "32-bit float mono loads");
        Check(ch == 1 && sr == 44100, "32-bit float header");
        Check(Near(s[0], 0.25f) && Near(s[1], -0.5f) && Near(s[2], 1.0f), "32-bit float values");
    }

    // 8-bit mono is unsigned
    {
        std::vector<uint8_t> d;
        d.push_back(128);   // 0.0
        d.push_back(255);   // ~1.0
        d.push_back(0);     // -1.0
        WriteFile("t8.wav", MakeWav(1, 1, 8000, 8, d));

        std::vector<float> s;
        uint32_t ch = 0, sr = 0;
        bool ok = LoadWavInterleaved(L"t8.wav", s, ch, sr);
        Check(ok && s.size() == 3, "8-bit mono loads");
        Check(Near(s[0], 0.0f, 1e-6f), "8-bit midpoint is zero");
        Check(s[1] > 0.9f && s[2] < -0.9f, "8-bit unsigned range");
    }

    // 24-bit stereo
    {
        std::vector<uint8_t> d;
        // +8388607 = 0x7FFFFF, -8388608 = 0x800000
        uint8_t frames[12] = { 0xFF, 0xFF, 0x7F, 0x00, 0x00, 0x80,
                               0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF };
        d.insert(d.end(), frames, frames + 12);
        WriteFile("t24.wav", MakeWav(1, 2, 96000, 24, d));

        std::vector<float> s;
        uint32_t ch = 0, sr = 0;
        bool ok = LoadWavInterleaved(L"t24.wav", s, ch, sr);
        Check(ok && s.size() == 4, "24-bit stereo loads");
        Check(ch == 2 && sr == 96000, "24-bit header");
        Check(s[0] > 0.999f && s[1] < -0.999f, "24-bit extremes");
        Check(Near(s[2], 0.0f) && s[3] < -0.999f + 1e-4f, "24-bit remaining");
    }

    // Error paths
    {
        std::vector<float> s;
        uint32_t ch = 0, sr = 0;
        Check(!LoadWavInterleaved(L"does_not_exist_12345.wav", s, ch, sr), "missing file fails");

        std::vector<uint8_t> junk(64, 0x41);
        WriteFile("tjunk.wav", junk);
        Check(!LoadWavInterleaved(L"tjunk.wav", s, ch, sr), "non-RIFF fails");

        std::vector<uint8_t> t = MakeWav(1, 2, 48000, 16, std::vector<uint8_t>(32, 0));
        t.resize(t.size() / 2);  // truncate mid-data
        WriteFile("ttrunc.wav", t);
        Check(!LoadWavInterleaved(L"ttrunc.wav", s, ch, sr), "truncated data fails");
    }

    printf(g_fail == 0 ? "ALL PASS\n" : "%d FAILURE(S)\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
```

- [ ] **Step 2: Write the build script and run it to confirm it fails**

`C:\Users\ctzjj\AppData\Local\Temp\opencode\build_wav.bat`:

```bat
@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /EHsc /O2 /I"D:\fb2kJDSP\jdsp_host" /Fe:test_wav.exe test_wav.cpp "D:\fb2kJDSP\jdsp_host\wav_loader.cpp"
```

Run:
```
cmd /c "cd /d C:\Users\ctzjj\AppData\Local\Temp\opencode && build_wav.bat"
```
Expected: FAIL — `fatal error C1083: Cannot open include file: 'wav_loader.h'`.

- [ ] **Step 3: Add the header**

`jdsp_host/wav_loader.h`:

```cpp
#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Decodes a RIFF/WAVE file into interleaved float samples in [-1, 1].
// Supports PCM 8/16/24/32-bit integer and 32-bit IEEE float, mono or multi-channel,
// including WAVE_FORMAT_EXTENSIBLE containers around those two sample formats.
// Returns false and leaves the outputs untouched on any parse error.
bool LoadWavInterleaved(const std::wstring& path,
                        std::vector<float>& out_samples,
                        uint32_t& out_channels,
                        uint32_t& out_sample_rate);
```

- [ ] **Step 4: Add the implementation**

`jdsp_host/wav_loader.cpp`:

```cpp
#include "wav_loader.h"
#include <cstdio>
#include <cstring>

namespace {

uint16_t RdU16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

uint32_t RdU32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

bool ReadWholeFile(const std::wstring& path, std::vector<uint8_t>& out) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) return false;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return false; }
    long size = ftell(f);
    if (size <= 0) { fclose(f); return false; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return false; }
    out.resize((size_t)size);
    size_t got = fread(out.data(), 1, out.size(), f);
    fclose(f);
    return got == out.size();
}

const uint8_t kGuidTail[14] = {
    0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA,
    0x00, 0x38, 0x9B, 0x71
};

}  // namespace

bool LoadWavInterleaved(const std::wstring& path,
                        std::vector<float>& out_samples,
                        uint32_t& out_channels,
                        uint32_t& out_sample_rate) {
    std::vector<uint8_t> buf;
    if (!ReadWholeFile(path, buf)) return false;
    if (buf.size() < 44) return false;
    if (memcmp(buf.data(), "RIFF", 4) != 0) return false;
    if (memcmp(buf.data() + 8, "WAVE", 4) != 0) return false;

    uint16_t format = 0;
    uint16_t channels = 0;
    uint32_t sample_rate = 0;
    uint16_t bits = 0;
    bool have_fmt = false;

    const uint8_t* data_ptr = nullptr;
    size_t data_len = 0;

    size_t pos = 12;
    while (pos + 8 <= buf.size()) {
        const uint8_t* chunk = buf.data() + pos;
        uint32_t chunk_size = RdU32(chunk + 4);
        const uint8_t* body = chunk + 8;
        if (pos + 8 + (size_t)chunk_size > buf.size()) {
            // Truncated final chunk: only usable when it is the data chunk and the
            // declared size overruns, in which case clamp to what is present.
            if (memcmp(chunk, "data", 4) == 0) {
                data_ptr = body;
                data_len = buf.size() - (pos + 8);
            }
            break;
        }

        if (memcmp(chunk, "fmt ", 4) == 0 && chunk_size >= 16) {
            format = RdU16(body + 0);
            channels = RdU16(body + 2);
            sample_rate = RdU32(body + 4);
            bits = RdU16(body + 14);
            if (format == 0xFFFE && chunk_size >= 40) {
                // WAVE_FORMAT_EXTENSIBLE: the real format is the first 2 bytes of
                // the sub-format GUID at offset 24; the rest must be the standard tail.
                if (memcmp(body + 26, kGuidTail, sizeof(kGuidTail)) != 0) return false;
                format = RdU16(body + 24);
            }
            have_fmt = true;
        } else if (memcmp(chunk, "data", 4) == 0) {
            data_ptr = body;
            data_len = chunk_size;
        }

        // Chunks are word-aligned.
        pos += 8 + (size_t)chunk_size + (chunk_size & 1u);
    }

    if (!have_fmt || !data_ptr || data_len == 0) return false;
    if (channels == 0) return false;
    if (format != 1 && format != 3) return false;

    size_t bytes_per_sample = bits / 8;
    if (bytes_per_sample == 0 || bytes_per_sample > 4) return false;
    if (format == 3 && bytes_per_sample != 4) return false;

    size_t frame_bytes = bytes_per_sample * channels;
    size_t frames = data_len / frame_bytes;
    if (frames == 0) return false;

    std::vector<float> samples(frames * channels);
    const uint8_t* p = data_ptr;

    for (size_t i = 0; i < frames * channels; i++) {
        float v = 0.0f;
        if (format == 3) {
            float f = 0.0f;
            memcpy(&f, p, 4);
            v = f;
        } else if (bits == 8) {
            v = ((float)p[0] - 128.0f) / 128.0f;
        } else if (bits == 16) {
            int16_t s = (int16_t)RdU16(p);
            v = (float)s / 32768.0f;
        } else if (bits == 24) {
            int32_t s = (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16));
            if (s & 0x800000) s -= 0x1000000;
            v = (float)s / 8388608.0f;
        } else {
            int32_t s = (int32_t)RdU32(p);
            v = (float)s / 2147483648.0f;
        }
        samples[i] = v;
        p += bytes_per_sample;
    }

    out_samples.swap(samples);
    out_channels = channels;
    out_sample_rate = sample_rate;
    return true;
}
```

- [ ] **Step 5: Run the tests and confirm they pass**

```
cmd /c "cd /d C:\Users\ctzjj\AppData\Local\Temp\opencode && build_wav.bat && test_wav.exe"
```
Expected:
```
PASS 16-bit stereo loads
PASS 16-bit stereo header
PASS 16-bit stereo frame count
PASS 16-bit values
PASS 16-bit negative + half
PASS 32-bit float mono loads
PASS 32-bit float header
PASS 32-bit float values
PASS 8-bit mono loads
PASS 8-bit midpoint is zero
PASS 8-bit unsigned range
PASS 24-bit stereo loads
PASS 24-bit header
PASS 24-bit extremes
PASS 24-bit remaining
PASS missing file fails
PASS non-RIFF fails
PASS truncated data fails
ALL PASS
```

- [ ] **Step 6: Add the new file to the host project**

In `jdsp_host/jdsp_host.vcxproj`, in the `<ItemGroup>` that holds `main.cpp`, after
`<ClCompile Include="jdsp_ipc_server.cpp" />` add:

```xml
    <ClCompile Include="wav_loader.cpp" />
    <ClInclude Include="wav_loader.h" />
```

- [ ] **Step 7: Build the host and verify**

```
& "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\MSBuild\Current\Bin\MSBuild.exe" foobar2000-jamesdsp.sln /p:Configuration=Release /p:Platform=x64 /v:minimal /nologo /t:jdsp_host
```
Expected: `0 Error(s)`.

- [ ] **Step 8: Commit**

```
git add jdsp_host/wav_loader.h jdsp_host/wav_loader.cpp jdsp_host/jdsp_host.vcxproj
git commit -m "feat: add RIFF/WAVE loader for convolver impulse responses"
```

---

### Task 6: Implement `convolver.path`, `ddc.profile` and `script.text`

**Files:**
- Modify: `jdsp_host/jdsp_engine.h`
- Modify: `jdsp_host/jdsp_engine.cpp`
- Modify: `jdsp_host/jdsp_ipc_server.cpp`

Verified library contracts:
- `Convolver1DLoadImpulseResponse(jdsp, float* interleaved, unsigned impChannels, size_t impulseFrames, char updateOld)` returns `1` on success, `0` on failure (`Effects/convolver1D.c:59`). It does **not** set `jdsp->convolverEnabled`, so `Convolver1DEnable()` must be called afterwards (`Effects/convolver1D.c:7` only enables when `conv.process` is set).
- `DDCStringParser(jdsp, char* text)` returns `1` success, `0` unchanged, `-1` failure (`Effects/vdc.c:315`); the text must contain `SR_44100` and `SR_48000` sections (`Effects/vdc.c:44`).
- `LiveProgStringParser(jdsp, char* eel)` returns `1` success, `0`/`-2`/`-3` errors mapped by `checkErrorCode()` (`Effects/liveprogWrapper.c:95`).

- [ ] **Step 1: Add members and helpers to the header**

In `jdsp_host/jdsp_engine.h`, add `#include <vector>` and `#include <string>` (the
latter is already present), then add these declarations to the `private:` section,
just after `void ApplyAllParams();`:

```cpp
    bool LoadImpulseResponse(const std::wstring& path);
    bool LoadDdcProfile(const std::wstring& path);
    bool LoadEelScript(const std::string& text);
```

and just after `float m_conv_gain = 0.0f;`:

```cpp
    std::wstring m_ir_path_last;
    std::wstring m_ddc_path_last;
    std::string m_script_last;
```

- [ ] **Step 2: Add the file helpers**

At the top of `jdsp_host/jdsp_engine.cpp`, after the existing `parse_float` helper,
add:

```cpp
#include "wav_loader.h"
#include <windows.h>

static std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    if (n <= 0) return std::wstring();
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

// Reads a small text file (DDC profile / EEL2 script). Returns an empty string on
// any error.
static std::string ReadTextFile(const std::wstring& path) {
    std::string out;
    if (path.empty()) return out;
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) return out;
    char buf[4096];
    size_t got;
    while ((got = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, got);
    fclose(f);
    return out;
}
```

(The `#include` lines belong with the other includes at the top of the file; place
`#include "wav_loader.h"` and `#include <windows.h>` next to `#include <cmath>`.)

- [ ] **Step 3: Implement the three loaders**

Add these three methods to `jdsp_host/jdsp_engine.cpp` immediately after
`JdspEngine::TubeDriveDb()`:

```cpp
bool JdspEngine::LoadImpulseResponse(const std::wstring& path) {
    if (!m_jdsp) return false;
    if (path == m_ir_path_last) return true;

    JamesDSPLib* jdsp = JDSP(m_jdsp);

    if (path.empty()) {
        Convolver1DLoadImpulseResponse(jdsp, nullptr, 0, 0, 1);
        Convolver1DDisable(jdsp);
        m_ir_path_last.clear();
        return true;
    }

    std::vector<float> samples;
    uint32_t channels = 0;
    uint32_t sample_rate = 0;
    if (!LoadWavInterleaved(path, samples, channels, sample_rate)) {
        printf("LoadImpulseResponse: cannot read impulse response\n");
        return false;
    }
    (void)sample_rate;

    size_t frames = samples.size() / channels;
    int r = Convolver1DLoadImpulseResponse(jdsp, samples.data(), channels, frames, 1);
    if (!r) {
        printf("LoadImpulseResponse: Convolver1DLoadImpulseResponse failed\n");
        return false;
    }
    // Loading does not turn the convolver on; only the module flag does that.
    if (m_module_enabled[5]) Convolver1DEnable(jdsp);
    m_ir_path_last = path;
    printf("LoadImpulseResponse: ok (%u ch, %u frames)\n",
           (unsigned)channels, (unsigned)frames);
    return true;
}

bool JdspEngine::LoadDdcProfile(const std::wstring& path) {
    if (!m_jdsp) return false;
    if (path == m_ddc_path_last) return true;

    JamesDSPLib* jdsp = JDSP(m_jdsp);

    if (path.empty()) {
        m_ddc_path_last.clear();
        return true;
    }

    std::string text = ReadTextFile(path);
    if (text.empty()) {
        printf("LoadDdcProfile: cannot read profile\n");
        return false;
    }
    text.push_back('\0');  // DDCStringParser takes a C string

    int r = DDCStringParser(jdsp, &text[0]);
    if (r < 0) {
        printf("LoadDdcProfile: DDCStringParser failed\n");
        return false;
    }
    if (m_module_enabled[2]) DDCEnable(jdsp, 1);
    m_ddc_path_last = path;
    printf("LoadDdcProfile: ok\n");
    return true;
}

bool JdspEngine::LoadEelScript(const std::string& text) {
    if (!m_jdsp) return false;
    if (text == m_script_last) return true;

    JamesDSPLib* jdsp = JDSP(m_jdsp);
    m_script_last = text;

    if (text.empty()) {
        LiveProgDisable(jdsp);
        return true;
    }

    std::string code = text;
    code.push_back('\0');
    int err = LiveProgStringParser(jdsp, &code[0]);
    if (err != 1) {
        printf("LoadEelScript: %s\n", checkErrorCode(err));
        return false;
    }
    if (m_module_enabled[12]) LiveProgEnable(jdsp);
    printf("LoadEelScript: ok\n");
    return true;
}
```

- [ ] **Step 4: Unescape values on the host side (required for paths and scripts)**

`SerializeSettings()` escapes values before writing them: every `\` becomes `\\` and
every newline becomes `\n` (`jdsp_config_dialog.cpp`, `EscapeValue`). The host currently
passes the raw (still escaped) value to `SetParam`, so a Windows path arrives as
`C:\\Users\\...` and an EEL2 script arrives with literal `\n` two-character sequences.
Both would fail.

Two more includes are needed at the top of `jdsp_host/jdsp_ipc_server.cpp`:

```cpp
#include <string>
```

Then add this helper just above `bool JdspIpcServer::HandleSetParam`:

```cpp
// Mirror of JdspConfigDialog's EscapeValue: the blob on the wire stores every
// backslash as "\\" and every newline as "\n".
static std::string UnescapeValue(const std::string& v) {
    std::string o;
    o.reserve(v.size());
    for (size_t i = 0; i < v.size(); i++) {
        if (v[i] == '\\' && i + 1 < v.size()) {
            char n = v[++i];
            if (n == 'n') o += '\n';
            else o += n;
        } else {
            o += v[i];
        }
    }
    return o;
}
```

and change the final line of `HandleSetParam` from:

```cpp
        m_engine.SetParam(line.substr(0, eq), line.substr(eq + 1));
```

to:

```cpp
        m_engine.SetParam(line.substr(0, eq), UnescapeValue(line.substr(eq + 1)));
```

- [ ] **Step 5: Route the keys in `SetParam`**

In `JdspEngine::SetParam`, replace:

```cpp
    else if (key == "convolver.gain") { m_conv_gain = (float)dv; JamesDSPSetPostGain(jdsp, m_conv_gain); }
```

with:

```cpp
    else if (key == "convolver.gain") { m_conv_gain = (float)dv; JamesDSPSetPostGain(jdsp, m_conv_gain); }
    else if (key == "convolver.path") { LoadImpulseResponse(Utf8ToWide(value)); }
    else if (key == "ddc.profile") { LoadDdcProfile(Utf8ToWide(value)); }
    else if (key == "script.text") { LoadEelScript(value); }
```

- [ ] **Step 6: Make `modules.convolver` actually switch the convolver**

Replace:

```cpp
    else if (key == "modules.convolver") { m_module_enabled[5] = (atoi(val) != 0); }
```

with:

```cpp
    else if (key == "modules.convolver") {
        m_module_enabled[5] = (atoi(val) != 0);
        if (m_module_enabled[5]) Convolver1DEnable(jdsp); else Convolver1DDisable(jdsp);
    }
```

- [ ] **Step 7: Build and verify**

```
& "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\MSBuild\Current\Bin\MSBuild.exe" foobar2000-jamesdsp.sln /p:Configuration=Release /p:Platform=x64 /v:minimal /nologo /t:jdsp_host
```
Expected: `0 Error(s)`.

- [ ] **Step 8: Verify offline with the existing blob harness**

Create an impulse response file and a DDC profile, then drive the host directly.
From `C:\Users\ctzjj\AppData\Local\Temp\opencode`:

`blob_conv.txt` (a WAV of any supported format, e.g. `ir.wav`, must exist):
```
modules.convolver=1
convolver.path=C:\Users\ctzjj\AppData\Local\Temp\opencode\ir.wav
```

`blob_ddc.txt` (a `.vdc` text file containing both `SR_44100` and `SR_48000`):
```
modules.ddc=1
ddc.profile=C:\Users\ctzjj\AppData\Local\Temp\opencode\test.vdc
```

`blob_script.txt` (the value is one line; the `\n` escapes below must be written as
real newlines inside the EEL2 source, and the whole `script.text=` line must contain
no literal newline other than the escaped ones your editor inserts — the dialog writes
them as `\n` escapes, so use `script.text=@init\nin1=0;\n@sample\nin1=in1*0.5;\n`):
```
modules.eel2=1
script.text=@init\nin1=0;\n@sample\nin1=in1*0.5;\n
```

Run and check `D:\fb2kJDSP\jdsp_host.log` (the host's stdout is not attached when
launched by the DLL, so use the log file; when launched by hand the `printf` output
goes to the console):
```
test_blob.exe "D:\fb2kJDSP\x64\Release\jdsp_host.exe" blob_conv.txt 4096 4 0.5 44100 3
test_blob.exe "D:\fb2kJDSP\x64\Release\jdsp_host.exe" blob_ddc.txt 4096 4 0.5 44100 3
test_blob.exe "D:\fb2kJDSP\x64\Release\jdsp_host.exe" blob_script.txt 4096 4 0.5 44100 3
```
Expected: the `blob_script.txt` run reports a non-zero `maxdiff` (the script halves the
signal). The convolver run reports a non-zero `maxdiff` when `ir.wav` exists and a
garbage-free passthrough when it does not. A missing/unparseable file prints a
`LoadImpulseResponse: ...` / `LoadDdcProfile: ...` line and leaves the audio untouched.

- [ ] **Step 9: Commit**

```
git add jdsp_host/jdsp_engine.h jdsp_host/jdsp_engine.cpp jdsp_host/jdsp_ipc_server.cpp
git commit -m "feat: load convolver IR, DDC profile and EEL2 script from files"
```

---

### Task 7: Deploy and verify end to end

**Files:** none (build + deploy + manual verification)

- [ ] **Step 1: Build the whole solution**

```
& "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\MSBuild\Current\Bin\MSBuild.exe" foobar2000-jamesdsp.sln /p:Configuration=Release /p:Platform=x64 /v:minimal /nologo
```
Expected: `0 Error(s)`; `x64\Release\foo_dsp_jamesdsp.dll` and
`x64\Release\jdsp_host.exe` both rewritten.

- [ ] **Step 2: Deploy with foobar2000 closed**

Ask the user to close foobar2000, then:

```
taskkill /F /IM jdsp_host.exe 2>$null
Copy-Item "D:\fb2kJDSP\x64\Release\foo_dsp_jamesdsp.dll" "D:\Program Files\foobar2000\components\" -Force
Copy-Item "D:\fb2kJDSP\x64\Release\jdsp_host.exe" "D:\Program Files\foobar2000\components\" -Force
Remove-Item "D:\Program Files\foobar2000\jdsp_cfg.log","D:\Program Files\foobar2000\jdsp_host.log" -ErrorAction SilentlyContinue
```
Expected: no error, and both files show a fresh `LastWriteTime`.

- [ ] **Step 3: Manual verification while playing**

Ask the user to start playback and open Configure, then confirm:

1. Dragging Analog Modelling drive changes the sound immediately, no OK needed.
2. Checking/unchecking a module switches that effect immediately.
3. Dragging an EQ curve handle or a band slider changes the sound immediately.
4. Pressing Cancel restores the sound to what it was before the dialog opened.
5. Pressing OK persists the settings; reopening shows the same values.
6. Browsing to an IR file on the Convolver tab changes the sound immediately.

- [ ] **Step 4: Inspect the logs**

```
Get-Content "D:\Program Files\foobar2000\jdsp_host.log" | Select-String "LoadImpulseResponse|LoadDdcProfile|LoadEelScript|PROC" | Select-Object -Last 20
Get-Content "D:\Program Files\foobar2000\jdsp_cfg.log" | Select-Object -Last 30
```
Expected: `LoadImpulseResponse: ok (...)` after picking an IR file; `PROC:` lines keep
flowing with `fail=0`-equivalent continuity (no gaps longer than a frame) and no crash.

- [ ] **Step 5: Commit any fixes found during verification, otherwise**

If verification passes with no changes:

```
git status --short
git log --oneline -6
```
Expected: clean tree, six new commits from tasks 1-6.

---

## Notes for the implementer

- `jdsp_config_dialog.cpp` still contains temporary diagnostics (`CfgLog`, `g_trace`,
  `ShouldTrace`, `TraceMsg`, per-message logging). Leave them in place for now; they are
  the only way to see what the dialog does inside foobar2000. Removing them is a
  separate cleanup task.
- `InitDynamicsTab` hardcodes the Dynamics-tab Enable checkboxes and never reads them
  back. That is pre-existing dead UI and is deliberately **not** part of this plan.
- The host's `printf` output goes nowhere useful when it is launched by the DLL
  (its stderr/stdout are pipes). Rely on `jdsp_host.log` there.
