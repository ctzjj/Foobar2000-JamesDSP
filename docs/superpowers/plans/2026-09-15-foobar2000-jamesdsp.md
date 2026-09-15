# foobar2000 JamesDSP Plugin Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a foobar2000 DSP plugin that integrates JamesDSP audio effects via an out-of-process architecture, supporting both v1.x (x86) and v2.x (x64), with GUI configuration, i18n (EN/ZH), and full JamesDSP feature set.

**Architecture:** Two-component design — `foo_dsp_jamesdsp.dll` (foobar2000 DSP plugin) communicates via stdin/stdout binary IPC with `jdsp_host.exe` (JamesDSP engine wrapper). Crash isolation through process separation.

**Tech Stack:** C++, foobar2000 SDK (2025-03-07), libjamesdsp, Win32 API, Visual Studio 2019

---

## File Structure

```
foobar2000-jamesdsp/
├── sdk/                              # foobar2000 SDK (vendored)
├── libjamesdsp/                      # libjamesdsp source (vendored/patched)
│   └── jni/                          # Core DSP source files
├── shared/                           # Shared code between DLL and EXE
│   ├── jdsp_ipc_protocol.h           # IPC frame definitions and serialization
│   └── jdsp_ipc_protocol.cpp
├── foo_dsp_jamesdsp/                 # DSP plugin project
│   ├── foo_dsp_jamesdsp.vcxproj      # VS project file
│   ├── foo_dsp_jamesdsp.cpp          # Main DSP entry, service factory
│   ├── jdsp_dsp.h / .cpp             # DSP implementation (on_chunk, etc.)
│   ├── jdsp_host_manager.h / .cpp    # Subprocess lifecycle
│   ├── jdsp_ipc_client.h / .cpp      # IPC client wrapper
│   ├── jdsp_config_dialog.h / .cpp   # Main config dialog
│   ├── jdsp_eq_widget.h / .cpp       # EQ curve custom widget
│   ├── resource.h                    # Dialog resource IDs
│   ├── foo_dsp_jamesdsp.rc           # Dialog templates
│   ├── strings_en.h                  # English strings
│   └── strings_zh.h                  # Chinese strings
├── jdsp_host/                        # Host process project
│   ├── jdsp_host.vcxproj             # VS project file
│   ├── main.cpp                      # Entry point, IPC loop
│   ├── jdsp_ipc_server.h / .cpp      # IPC server wrapper
│   └── jdsp_engine.h / .cpp          # JamesDSP wrapper
├── foobar2000-jamesdsp.sln           # VS solution file
└── docs/
    └── superpowers/
        ├── specs/                    # Design specs
        └── plans/                    # This file
```

---

## Task 1: Project Setup and SDK Integration

**Files:**
- Create: `foobar2000-jamesdsp.sln`
- Create: `foo_dsp_jamesdsp/foo_dsp_jamesdsp.vcxproj`
- Create: `jdsp_host/jdsp_host.vcxproj`

- [ ] **Step 1: Download and vendor foobar2000 SDK**

Download SDK from https://www.foobar2000.org/downloads/SDK-2025-03-07.7z and extract to `sdk/` directory.

```bash
# In PowerShell
Invoke-WebRequest -Uri "https://www.foobar2000.org/downloads/SDK-2025-03-07.7z" -OutFile "sdk.zip"
# Extract with 7-Zip
& "C:\Program Files\7-Zip\7z.exe" x sdk.zip -osdk
```

- [ ] **Step 2: Clone and vendor libjamesdsp**

```bash
git clone https://github.com/james34602/JamesDSPManager.git temp_jdsp
# Copy only the libjamesdsp source
xcopy /E /I temp_jdsp\Main\libjamesdsp libjamesdsp
rmdir /S /Q temp_jdsp
```

- [ ] **Step 3: Create VS solution with two projects**

Create `foobar2000-jamesdsp.sln` with:
- `foo_dsp_jamesdsp` — DLL project (Win32 Application, DLL)
- `jdsp_host` — EXE project (Win32 Application, Console)

Both projects configure:
- Include paths: `sdk/foobar2000/SDK/`, `libjamesdsp/jni/`
- Library paths: `sdk/foobar2000/shared/`
- Character set: Unicode
- Runtime: Multi-threaded (Release), Multi-threaded Debug (Debug)

- [ ] **Step 4: Configure foo_dsp_jamesdsp project dependencies**

Right-click project → Properties → Dependencies:
- `foobar2000_component_client`
- `foobar2000_SDK`
- `pfc`

Linker → Input → Additional Dependencies:
```
$(SolutionDir)sdk\foobar2000\shared\shared.lib shlwapi.lib
```

Post-Build Event:
```
copy "$(TargetPath)" "$(SolutionDir)test\components\"
```

- [ ] **Step 5: Verify build configuration compiles**

Create minimal `foo_dsp_jamesdsp.cpp`:
```cpp
#include "SDK/foobar2000.h"

DECLARE_COMPONENT_VERSION(
    "JamesDSP",
    "0.1.0",
    "foobar2000 JamesDSP Plugin"
);

VALIDATE_COMPONENT_FILENAME("foo_dsp_jamesdsp.dll");
```

Build both x86 and x64 configurations. Verify DLL is produced.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "feat: initial project setup with foobar2000 SDK and libjamesdsp"
```

---

## Task 2: IPC Protocol Implementation

**Files:**
- Create: `shared/jdsp_ipc_protocol.h`
- Create: `shared/jdsp_ipc_protocol.cpp`

- [ ] **Step 1: Define IPC protocol header**

Create `shared/jdsp_ipc_protocol.h`:
```cpp
#pragma once
#include <cstdint>
#include <vector>
#include <string>

namespace jdsp {

enum class FrameType : uint32_t {
    AUDIO_DATA    = 0x01,
    SET_PARAM     = 0x10,
    LOAD_PRESET   = 0x20,
    SAVE_PRESET   = 0x21,
    PRESET_DATA   = 0x22,
    STATUS        = 0x30,
    SHUTDOWN      = 0xFF,
};

#pragma pack(push, 1)
struct FrameHeader {
    FrameType type;
    uint32_t data_length;
};
#pragma pack(pop)

struct AudioData {
    uint32_t sample_rate;
    uint32_t channels;
    uint32_t sample_count;
    // followed by float samples (interleaved)
};

struct ParamData {
    // followed by "key=value" string
};

class IpcSerializer {
public:
    static std::vector<uint8_t> SerializeFrame(FrameType type, const void* data, uint32_t data_length);
    static bool ReadFrame(int fd, FrameHeader& header, std::vector<uint8_t>& payload);
    static bool WriteFrame(int fd, FrameType type, const void* data, uint32_t data_length);
};

} // namespace jdsp
```

- [ ] **Step 2: Implement IPC serialization**

Create `shared/jdsp_ipc_protocol.cpp`:
```cpp
#include "jdsp_ipc_protocol.h"
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#define READ_FD _read
#define WRITE_FD _write
#else
#include <unistd.h>
#define READ_FD read
#define WRITE_FD write
#endif

namespace jdsp {

std::vector<uint8_t> IpcSerializer::SerializeFrame(FrameType type, const void* data, uint32_t data_length) {
    std::vector<uint8_t> frame(sizeof(FrameHeader) + data_length);
    FrameHeader* header = reinterpret_cast<FrameHeader*>(frame.data());
    header->type = type;
    header->data_length = data_length;
    if (data && data_length > 0) {
        memcpy(frame.data() + sizeof(FrameHeader), data, data_length);
    }
    return frame;
}

bool IpcSerializer::ReadFrame(int fd, FrameHeader& header, std::vector<uint8_t>& payload) {
    int bytes = READ_FD(fd, &header, sizeof(FrameHeader));
    if (bytes != sizeof(FrameHeader)) return false;

    payload.resize(header.data_length);
    if (header.data_length > 0) {
        bytes = READ_FD(fd, payload.data(), header.data_length);
        if (bytes != static_cast<int>(header.data_length)) return false;
    }
    return true;
}

bool IpcSerializer::WriteFrame(int fd, FrameType type, const void* data, uint32_t data_length) {
    auto frame = SerializeFrame(type, data, data_length);
    int written = WRITE_FD(fd, frame.data(), static_cast<unsigned int>(frame.size()));
    return written == static_cast<int>(frame.size());
}

} // namespace jdsp
```

- [ ] **Step 3: Add shared project to solution**

Create `shared/shared.vcxproj` as a static library project, add to solution. Both `foo_dsp_jamesdsp` and `jdsp_host` depend on `shared`.

- [ ] **Step 4: Write IPC round-trip test**

Create `shared/test_ipc.cpp`:
```cpp
#include "jdsp_ipc_protocol.h"
#include <cassert>
#include <cstdio>

#ifdef _WIN32
#include <windows.h>
#endif

void test_serialize_frame() {
    float audio[] = {0.5f, -0.5f, 0.3f, -0.3f};
    auto frame = jdsp::IpcSerializer::SerializeFrame(
        jdsp::FrameType::AUDIO_DATA, audio, sizeof(audio));

    assert(frame.size() == sizeof(jdsp::FrameHeader) + sizeof(audio));
    jdsp::FrameHeader* h = reinterpret_cast<jdsp::FrameHeader*>(frame.data());
    assert(h->type == jdsp::FrameType::AUDIO_DATA);
    assert(h->data_length == sizeof(audio));
    assert(memcmp(frame.data() + sizeof(jdsp::FrameHeader), audio, sizeof(audio)) == 0);
    printf("PASS: test_serialize_frame\n");
}

int main() {
    test_serialize_frame();
    return 0;
}
```

Build and run. Expected output: `PASS: test_serialize_frame`

- [ ] **Step 5: Commit**

```bash
git add shared/
git commit -m "feat: add IPC protocol serialization layer"
```

---

## Task 3: JamesDSP Host Process

**Files:**
- Create: `jdsp_host/jdsp_engine.h`
- Create: `jdsp_host/jdsp_engine.cpp`
- Create: `jdsp_host/jdsp_ipc_server.h`
- Create: `jdsp_host/jdsp_ipc_server.cpp`
- Create: `jdsp_host/main.cpp`

- [ ] **Step 1: Create JamesDSP engine wrapper**

Create `jdsp_host/jdsp_engine.h`:
```cpp
#pragma once
#include <string>
#include <vector>

struct jdsp_object; // forward declare from libjamesdsp

class JdspEngine {
public:
    JdspEngine();
    ~JdspEngine();

    bool Initialize(uint32_t sample_rate, uint32_t channels);
    void Shutdown();

    // Process audio in-place
    bool Process(float* audio, uint32_t sample_count, uint32_t channels);

    // Parameter control
    void SetParam(const std::string& key, const std::string& value);
    void SetEnabled(bool enabled);

    // Preset
    bool LoadPreset(const std::vector<uint8_t>& blob);
    std::vector<uint8_t> SavePreset();

private:
    jdsp_object* m_engine = nullptr;
    bool m_initialized = false;
};
```

- [ ] **Step 2: Implement JamesDSP engine wrapper**

Create `jdsp_host/jdsp_engine.cpp`:
```cpp
#include "jdsp_engine.h"
#include "jni/JamesDSPManager.h" // libjamesdsp header

JdspEngine::JdspEngine() {}

JdspEngine::~JdspEngine() {
    Shutdown();
}

bool JdspEngine::Initialize(uint32_t sample_rate, uint32_t channels) {
    m_engine = jdspCreate();
    if (!m_engine) return false;

    jdspSetSampleRate(m_engine, sample_rate);
    jdspSetChannels(m_engine, channels);
    m_initialized = true;
    return true;
}

void JdspEngine::Shutdown() {
    if (m_engine) {
        jdspDestroy(m_engine);
        m_engine = nullptr;
    }
    m_initialized = false;
}

bool JdspEngine::Process(float* audio, uint32_t sample_count, uint32_t channels) {
    if (!m_initialized || !m_engine) return false;
    jdspProcess(m_engine, audio, sample_count, channels);
    return true;
}

void JdspEngine::SetParam(const std::string& key, const std::string& value) {
    if (!m_engine) return;
    // Parse and apply parameter to JamesDSP engine
    // Implementation depends on libjamesdsp API
}

void JdspEngine::SetEnabled(bool enabled) {
    if (!m_engine) return;
    jdspSetEnabled(m_engine, enabled ? 1 : 0);
}

bool JdspEngine::LoadPreset(const std::vector<uint8_t>& blob) {
    if (!m_engine || blob.empty()) return false;
    // Load preset blob into engine
    return true;
}

std::vector<uint8_t> JdspEngine::SavePreset() {
    std::vector<uint8_t> blob;
    if (!m_engine) return blob;
    // Save engine state to blob
    return blob;
}
```

- [ ] **Step 3: Create IPC server for host process**

Create `jdsp_host/jdsp_ipc_server.h`:
```cpp
#pragma once
#include "shared/jdsp_ipc_protocol.h"
#include "jdsp_engine.h"

class JdspIpcServer {
public:
    JdspIpcServer(JdspEngine& engine);
    bool Run(); // Main IPC loop, blocks until shutdown

private:
    bool HandleAudioData(const std::vector<uint8_t>& payload, std::vector<uint8_t>& response);
    bool HandleSetParam(const std::vector<uint8_t>& payload);
    bool HandleLoadPreset(const std::vector<uint8_t>& payload, std::vector<uint8_t>& response);

    JdspEngine& m_engine;
};
```

- [ ] **Step 4: Implement IPC server main loop**

Create `jdsp_host/jdsp_ipc_server.cpp`:
```cpp
#include "jdsp_ipc_server.h"
#include <cstdio>

#ifdef _WIN32
#include <io.h>
#define FD_STDIN  _fileno(stdin)
#define FD_STDOUT _fileno(stdout)
#else
#include <unistd.h>
#define FD_STDIN  STDIN_FILENO
#define FD_STDOUT STDOUT_FILENO
#endif

JdspIpcServer::JdspIpcServer(JdspEngine& engine) : m_engine(engine) {}

bool JdspIpcServer::Run() {
    while (true) {
        jdsp::FrameHeader header;
        std::vector<uint8_t> payload;

        if (!jdsp::IpcSerializer::ReadFrame(FD_STDIN, header, payload)) {
            break; // Pipe closed or error
        }

        switch (header.type) {
            case jdsp::FrameType::AUDIO_DATA: {
                std::vector<uint8_t> response;
                if (HandleAudioData(payload, response)) {
                    jdsp::IpcSerializer::WriteFrame(FD_STDOUT,
                        jdsp::FrameType::AUDIO_DATA, response.data(),
                        static_cast<uint32_t>(response.size()));
                }
                break;
            }
            case jdsp::FrameType::SET_PARAM:
                HandleSetParam(payload);
                break;
            case jdsp::FrameType::LOAD_PRESET: {
                std::vector<uint8_t> response;
                HandleLoadPreset(payload, response);
                jdsp::IpcSerializer::WriteFrame(FD_STDOUT,
                    jdsp::FrameType::PRESET_DATA, response.data(),
                    static_cast<uint32_t>(response.size()));
                break;
            }
            case jdsp::FrameType::SHUTDOWN:
                return true;
            default:
                break;
        }
    }
    return false;
}

bool JdspIpcServer::HandleAudioData(const std::vector<uint8_t>& payload, std::vector<uint8_t>& response) {
    if (payload.size() < sizeof(jdsp::AudioData)) return false;

    const jdsp::AudioData* hdr = reinterpret_cast<const jdsp::AudioData*>(payload.data());
    const float* audio = reinterpret_cast<const float*>(payload.data() + sizeof(jdsp::AudioData));

    // Copy audio data for processing
    uint32_t total_samples = hdr->sample_count * hdr->channels;
    response.resize(sizeof(jdsp::AudioData) + total_samples * sizeof(float));

    jdsp::AudioData* resp_hdr = reinterpret_cast<jdsp::AudioData*>(response.data());
    resp_hdr->sample_rate = hdr->sample_rate;
    resp_hdr->channels = hdr->channels;
    resp_hdr->sample_count = hdr->sample_count;

    float* resp_audio = reinterpret_cast<float*>(response.data() + sizeof(jdsp::AudioData));
    memcpy(resp_audio, audio, total_samples * sizeof(float));

    m_engine.Process(resp_audio, hdr->sample_count, hdr->channels);
    return true;
}

bool JdspIpcServer::HandleSetParam(const std::vector<uint8_t>& payload) {
    std::string param(payload.begin(), payload.end());
    size_t eq_pos = param.find('=');
    if (eq_pos == std::string::npos) return false;

    std::string key = param.substr(0, eq_pos);
    std::string value = param.substr(eq_pos + 1);
    m_engine.SetParam(key, value);
    return true;
}

bool JdspIpcServer::HandleLoadPreset(const std::vector<uint8_t>& payload, std::vector<uint8_t>& response) {
    bool ok = m_engine.LoadPreset(payload);
    response.resize(sizeof(uint32_t));
    uint32_t status = ok ? 1 : 0;
    memcpy(response.data(), &status, sizeof(uint32_t));
    return ok;
}
```

- [ ] **Step 5: Create host process entry point**

Create `jdsp_host/main.cpp`:
```cpp
#include "jdsp_engine.h"
#include "jdsp_ipc_server.h"

int main() {
    JdspEngine engine;
    if (!engine.Initialize(44100, 2)) {
        return 1;
    }

    JdspIpcServer server(engine);
    server.Run();

    engine.Shutdown();
    return 0;
}
```

- [ ] **Step 6: Build and test host process**

Build jdsp_host project. Verify `jdsp_host.exe` is produced for both x86 and x64.

- [ ] **Step 7: Commit**

```bash
git add jdsp_host/
git commit -m "feat: add JamesDSP host process with IPC server"
```

---

## Task 4: foobar2000 DSP Plugin Core

**Files:**
- Create: `foo_dsp_jamesdsp/jdsp_host_manager.h`
- Create: `foo_dsp_jamesdsp/jdsp_host_manager.cpp`
- Create: `foo_dsp_jamesdsp/jdsp_ipc_client.h`
- Create: `foo_dsp_jamesdsp/jdsp_ipc_client.cpp`
- Create: `foo_dsp_jamesdsp/jdsp_dsp.h`
- Create: `foo_dsp_jamesdsp/jdsp_dsp.cpp`
- Create: `foo_dsp_jamesdsp/foo_dsp_jamesdsp.cpp`

- [ ] **Step 1: Create host process manager**

Create `foo_dsp_jamesdsp/jdsp_host_manager.h`:
```cpp
#pragma once
#include <windows.h>
#include <string>

class JdspHostManager {
public:
    JdspHostManager();
    ~JdspHostManager();

    bool Start(const std::wstring& host_exe_path);
    void Stop();
    bool IsRunning() const;

    HANDLE GetStdinWrite() const { return m_hStdinWrite; }
    HANDLE GetStdoutRead() const { return m_hStdoutRead; }

private:
    HANDLE m_hProcess = NULL;
    HANDLE m_hStdinRead = NULL;
    HANDLE m_hStdinWrite = NULL;
    HANDLE m_hStdoutRead = NULL;
    HANDLE m_hStdoutWrite = NULL;
};
```

- [ ] **Step 2: Implement host process manager**

Create `foo_dsp_jamesdsp/jdsp_host_manager.cpp`:
```cpp
#include "jdsp_host_manager.h"

JdspHostManager::JdspHostManager() {}

JdspHostManager::~JdspHostManager() {
    Stop();
}

bool JdspHostManager::Start(const std::wstring& host_exe_path) {
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    // Create stdin pipe (plugin writes -> host reads)
    if (!CreatePipe(&m_hStdinRead, &m_hStdinWrite, &sa, 0)) return false;
    SetHandleInformation(m_hStdinWrite, HANDLE_FLAG_INHERIT, 0);

    // Create stdout pipe (host writes -> plugin reads)
    if (!CreatePipe(&m_hStdoutRead, &m_hStdoutWrite, &sa, 0)) return false;
    SetHandleInformation(m_hStdoutRead, HANDLE_FLAG_INHERIT, 0);

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.hStdInput = m_hStdinRead;
    si.hStdOutput = m_hStdoutWrite;
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    si.dwFlags = STARTF_USESTDHANDLES;

    if (!CreateProcessW(host_exe_path.c_str(), NULL, NULL, NULL,
                        TRUE, 0, NULL, NULL, &si, &pi)) {
        Stop();
        return false;
    }

    m_hProcess = pi.hProcess;
    CloseHandle(pi.hThread);
    CloseHandle(m_hStdinRead); m_hStdinRead = NULL;
    CloseHandle(m_hStdoutWrite); m_hStdoutWrite = NULL;

    return true;
}

void JdspHostManager::Stop() {
    if (m_hProcess) {
        TerminateProcess(m_hProcess, 0);
        WaitForSingleObject(m_hProcess, 1000);
        CloseHandle(m_hProcess);
        m_hProcess = NULL;
    }
    if (m_hStdinWrite) { CloseHandle(m_hStdinWrite); m_hStdinWrite = NULL; }
    if (m_hStdoutRead) { CloseHandle(m_hStdoutRead); m_hStdoutRead = NULL; }
    if (m_hStdinRead) { CloseHandle(m_hStdinRead); m_hStdinRead = NULL; }
    if (m_hStdoutWrite) { CloseHandle(m_hStdoutWrite); m_hStdoutWrite = NULL; }
}

bool JdspHostManager::IsRunning() const {
    if (!m_hProcess) return false;
    DWORD exit_code;
    return GetExitCodeProcess(m_hProcess, &exit_code) && exit_code == STILL_ACTIVE;
}
```

- [ ] **Step 3: Create IPC client**

Create `foo_dsp_jamesdsp/jdsp_ipc_client.h`:
```cpp
#pragma once
#include "shared/jdsp_ipc_protocol.h"
#include "jdsp_host_manager.h"

class JdspIpcClient {
public:
    JdspIpcClient(JdspHostManager& manager);

    bool SendAudioData(uint32_t sample_rate, uint32_t channels,
                       uint32_t sample_count, const float* audio,
                       float* output);
    bool SendSetParam(const std::string& key, const std::string& value);
    bool SendLoadPreset(const std::vector<uint8_t>& blob, std::vector<uint8_t>& response);
    bool SendShutdown();

private:
    bool WriteFrame(jdsp::FrameType type, const void* data, uint32_t length);
    bool ReadFrame(jdsp::FrameHeader& header, std::vector<uint8_t>& payload);

    JdspHostManager& m_manager;
};
```

- [ ] **Step 4: Implement IPC client**

Create `foo_dsp_jamesdsp/jdsp_ipc_client.cpp`:
```cpp
#include "jdsp_ipc_client.h"
#include <cstring>

JdspIpcClient::JdspIpcClient(JdspHostManager& manager) : m_manager(manager) {}

bool JdspIpcClient::WriteFrame(jdsp::FrameType type, const void* data, uint32_t length) {
    auto frame = jdsp::IpcSerializer::SerializeFrame(type, data, length);
    HANDLE hWrite = m_manager.GetStdinWrite();
    if (!hWrite) return false;

    DWORD written;
    return WriteFile(hWrite, frame.data(), static_cast<DWORD>(frame.size()), &written, NULL)
           && written == frame.size();
}

bool JdspIpcClient::ReadFrame(jdsp::FrameHeader& header, std::vector<uint8_t>& payload) {
    HANDLE hRead = m_manager.GetStdoutRead();
    if (!hRead) return false;

    DWORD bytes_read;
    if (!ReadFile(hRead, &header, sizeof(header), &bytes_read, NULL)
        || bytes_read != sizeof(header)) return false;

    payload.resize(header.data_length);
    if (header.data_length > 0) {
        if (!ReadFile(hRead, payload.data(), header.data_length, &bytes_read, NULL)
            || bytes_read != header.data_length) return false;
    }
    return true;
}

bool JdspIpcClient::SendAudioData(uint32_t sample_rate, uint32_t channels,
                                   uint32_t sample_count, const float* audio,
                                   float* output) {
    uint32_t data_size = sample_count * channels * sizeof(float);
    uint32_t total_size = sizeof(jdsp::AudioData) + data_size;

    std::vector<uint8_t> payload(total_size);
    jdsp::AudioData* hdr = reinterpret_cast<jdsp::AudioData*>(payload.data());
    hdr->sample_rate = sample_rate;
    hdr->channels = channels;
    hdr->sample_count = sample_count;
    memcpy(payload.data() + sizeof(jdsp::AudioData), audio, data_size);

    if (!WriteFrame(jdsp::FrameType::AUDIO_DATA, payload.data(), total_size)) return false;

    jdsp::FrameHeader resp_header;
    std::vector<uint8_t> resp_payload;
    if (!ReadFrame(resp_header, resp_payload)) return false;
    if (resp_header.type != jdsp::FrameType::AUDIO_DATA) return false;

    memcpy(output, resp_payload.data() + sizeof(jdsp::AudioData), data_size);
    return true;
}

bool JdspIpcClient::SendSetParam(const std::string& key, const std::string& value) {
    std::string param = key + "=" + value;
    return WriteFrame(jdsp::FrameType::SET_PARAM, param.c_str(),
                      static_cast<uint32_t>(param.size()));
}

bool JdspIpcClient::SendLoadPreset(const std::vector<uint8_t>& blob, std::vector<uint8_t>& response) {
    if (!WriteFrame(jdsp::FrameType::LOAD_PRESET, blob.data(),
                    static_cast<uint32_t>(blob.size()))) return false;

    jdsp::FrameHeader header;
    return ReadFrame(header, response) && header.type == jdsp::FrameType::PRESET_DATA;
}

bool JdspIpcClient::SendShutdown() {
    return WriteFrame(jdsp::FrameType::SHUTDOWN, nullptr, 0);
}
```

- [ ] **Step 5: Create DSP implementation class**

Create `foo_dsp_jamesdsp/jdsp_dsp.h`:
```cpp
#pragma once
#include "SDK/foobar2000.h"
#include "jdsp_host_manager.h"
#include "jdsp_ipc_client.h"

class jdsp_dsp : public dsp_impl_base {
public:
    jdsp_dsp();
    ~jdsp_dsp();

    static void g_get_name(pfc::string_base& p_out);

    // dsp implementation
    virtual void on_endoftrack(abort_callback& p_abort);
    virtual void on_endofplayback(abort_callback& p_abort);
    virtual bool on_chunk(audio_chunk* p_chunk, abort_callback& p_abort);
    virtual bool have_configpopup() { return true; }
    virtual void show_config_popup(HWND parent, abort_callback& p_abort);

    // Preset support
    virtual void get_preset(dsp_preset& p_out);
    virtual void set_preset(const dsp_preset& p_in);
    virtual bool is_preset_current(const dsp_preset& p_in);

    static const dsp_preset_guid& g_get_guid();

private:
    JdspHostManager m_host_manager;
    JdspIpcClient m_ipc_client;
    bool m_host_started = false;

    bool EnsureHostRunning();
};
```

- [ ] **Step 6: Implement DSP core logic**

Create `foo_dsp_jamesdsp/jdsp_dsp.cpp`:
```cpp
#include "jdsp_dsp.h"
#include "jdsp_config_dialog.h"

static const char* JAMESDSP_DLL_NAME = "jdsp_host.exe";

static const GUID g_jdsp_guid =
{ 0x12345678, 0x1234, 0x1234, { 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0 } };

const dsp_preset_guid& jdsp_dsp::g_get_guid() {
    static const dsp_preset_guid guid = g_jdsp_guid;
    return guid;
}

void jdsp_dsp::g_get_name(pfc::string_base& p_out) {
    p_out = "JamesDSP";
}

jdsp_dsp::jdsp_dsp() : m_ipc_client(m_host_manager) {}

jdsp_dsp::~jdsp_dsp() {
    if (m_host_started) {
        m_ipc_client.SendShutdown();
        m_host_manager.Stop();
    }
}

bool jdsp_dsp::EnsureHostRunning() {
    if (m_host_manager.IsRunning()) return true;

    // Find jdsp_host.exe relative to DLL location
    pfc::string8 dll_path;
    component_loader::g_get_full_path(dll_path, "foo_dsp_jamesdsp.dll");
    pfc::string8 host_path = pfc::string_filename(dll_path);
    host_path += "\\jdsp_host.exe";

    pfc::wstring host_path_w;
    host_path_w.set_string(host_path, pfc::utf8_to_wide(host_path));

    if (!m_host_manager.Start(host_path_w.get_ptr())) return false;

    m_host_started = true;
    return true;
}

bool jdsp_dsp::on_chunk(audio_chunk* p_chunk, abort_callback& p_abort) {
    if (!EnsureHostRunning()) return false;

    audio_sample* data = p_chunk->get_data();
    uint32_t sample_count = p_chunk->get_sample_count();
    uint32_t channels = p_chunk->get_channels();
    uint32_t sample_rate = p_chunk->get_sample_rate();

    std::vector<float> input(sample_count * channels);
    std::vector<float> output(sample_count * channels);

    memcpy(input.data(), data, input.size() * sizeof(float));

    if (m_ipc_client.SendAudioData(sample_rate, channels, sample_count,
                                    input.data(), output.data())) {
        memcpy(data, output.data(), output.size() * sizeof(float));
    }

    return true;
}

void jdsp_dsp::on_endoftrack(abort_callback& p_abort) {}
void jdsp_dsp::on_endofplayback(abort_callback& p_abort) {}

void jdsp_dsp::show_config_popup(HWND parent, abort_callback& p_abort) {
    JdspConfigDialog dlg(m_ipc_client);
    dlg.Show(parent);
}

void jdsp_dsp::get_preset(dsp_preset& p_out) {
    p_out.guid = g_get_guid();
    // Serialize current state to p_out
}

void jdsp_dsp::set_preset(const dsp_preset& p_in) {
    // Deserialize state from p_in
}

bool jdsp_dsp::is_preset_current(const dsp_preset& p_in) {
    return false; // simplified
}

static dsp_factory_single_t<jdsp_dsp> g_jdsp_dsp_factory;
```

- [ ] **Step 7: Commit**

```bash
git add foo_dsp_jamesdsp/jdsp_host_manager.* foo_dsp_jamesdsp/jdsp_ipc_client.* foo_dsp_jamesdsp/jdsp_dsp.*
git commit -m "feat: add DSP plugin core with host management and IPC"
```

---

## Task 5: Configuration Dialog Base

**Files:**
- Create: `foo_dsp_jamesdsp/resource.h`
- Create: `foo_dsp_jamesdsp/jdsp_config_dialog.h`
- Create: `foo_dsp_jamesdsp/jdsp_config_dialog.cpp`

- [ ] **Step 1: Define dialog resource IDs**

Create `foo_dsp_jamesdsp/resource.h`:
```cpp
#pragma once

// Main dialog
#define IDD_JDSP_CONFIG         1000

// Top toolbar controls
#define IDC_COMBO_LANGUAGE      1001
#define IDC_BTN_SAVE_CONFIG     1002
#define IDC_BTN_LOAD_CONFIG     1003
#define IDC_BTN_RESET_ALL       1004

// Tab control
#define IDC_TAB_MAIN            1100

// Tab: Modules
#define IDD_TAB_MODULES         1200
#define IDC_CHK_ANALOG          1201
#define IDC_CHK_BS2B            1202
#define IDC_CHK_DDC             1203
#define IDC_CHK_LIMITER         1204
#define IDC_CHK_COMPRESSOR      1205
#define IDC_CHK_CONVOLVER       1206
#define IDC_CHK_REVERB          1207
#define IDC_CHK_BASS_BOOST      1208
#define IDC_CHK_STEREO          1209
#define IDC_CHK_IIR             1210
#define IDC_CHK_SPECTRUM        1211
#define IDC_CHK_DYNAMIC_SYS     1212
#define IDC_CHK_EEL2            1213

// Tab: EQ
#define IDD_TAB_EQ              1300
#define IDC_STATIC_EQ_CURVE     1301
#define IDC_COMBO_EQ_BAND       1302
#define IDC_EDIT_EQ_FREQ        1303
#define IDC_EDIT_EQ_GAIN        1304
#define IDC_EDIT_EQ_Q           1305
#define IDC_BTN_EQ_RESET        1306
#define IDC_BTN_EQ_COPY         1307
#define IDC_BTN_EQ_PASTE        1308
#define IDC_BTN_EQ_LOAD_AUTO    1309

// Tab: Dynamics
#define IDD_TAB_DYNAMICS        1400
#define IDC_CHK_COMP            1401
#define IDC_SLIDER_COMP_THRESH  1402
#define IDC_SLIDER_COMP_RATIO   1403
#define IDC_SLIDER_COMP_ATTACK  1404
#define IDC_SLIDER_COMP_RELEASE 1405
#define IDC_CHK_LIM             1406
#define IDC_SLIDER_LIM_THRESH   1407
#define IDC_SLIDER_LIM_RELEASE  1408
#define IDC_CHK_DDC_ENABLE      1409
#define IDC_SLIDER_DDC_STRENGTH 1410
#define IDC_EDIT_DDC_PROFILE    1411
#define IDC_BTN_DDC_BROWSE      1412
#define IDC_BTN_DDC_RELOAD      1413

// Tab: Effects
#define IDD_TAB_EFFECTS         1500
// ... (similar pattern for each effect module)

// Tab: Convolver
#define IDD_TAB_CONVOLVER       1600
#define IDC_CHK_CONV_ENABLE     1601
#define IDC_EDIT_CONV_IR        1602
#define IDC_BTN_CONV_BROWSE     1603
#define IDC_STATIC_CONV_INFO    1604
#define IDC_SLIDER_CONV_GAIN    1605
#define IDC_BTN_CONV_RELOAD     1606

// Tab: Script
#define IDD_TAB_SCRIPT          1700
#define IDC_CHK_SCRIPT_ENABLE   1701
#define IDC_EDIT_SCRIPT         1702
#define IDC_BTN_SCRIPT_LOAD     1703
#define IDC_BTN_SCRIPT_SAVE     1704
#define IDC_BTN_SCRIPT_VALIDATE 1705
#define IDC_STATIC_SCRIPT_STATUS 1706

// Buttons
#define IDOK                    1
#define IDCANCEL                2
#define IDC_BTN_APPLY           1700
```

- [ ] **Step 2: Create config dialog header**

Create `foo_dsp_jamesdsp/jdsp_config_dialog.h`:
```cpp
#pragma once
#include <windows.h>
#include "jdsp_ipc_client.h"

class JdspConfigDialog {
public:
    JdspConfigDialog(JdspIpcClient& ipc);
    void Show(HWND parent);

private:
    static INT_PTR CALLBACK DialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void OnInitDialog(HWND hwnd);
    void OnTabChange(HWND hwnd);
    void OnApply(HWND hwnd);
    void OnLanguageChange(HWND hwnd, HWND combo);

    void SwitchLanguage(HWND hwnd, int lang_id);

    JdspIpcClient& m_ipc;
    HWND m_hwnd = NULL;
    int m_current_lang = 0; // 0=EN, 1=ZH
};
```

- [ ] **Step 3: Implement config dialog**

Create `foo_dsp_jamesdsp/jdsp_config_dialog.cpp`:
```cpp
#include "jdsp_config_dialog.h"
#include "resource.h"
#include "strings_en.h"
#include "strings_zh.h"

JdspConfigDialog::JdspConfigDialog(JdspIpcClient& ipc) : m_ipc(ipc) {}

void JdspConfigDialog::Show(HWND parent) {
    DialogBoxParam(GetModuleHandle(NULL),
                   MAKEINTRESOURCE(IDD_JDSP_CONFIG),
                   parent, DialogProc, reinterpret_cast<LPARAM>(this));
}

INT_PTR CALLBACK JdspConfigDialog::DialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    JdspConfigDialog* dlg = reinterpret_cast<JdspConfigDialog*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

    switch (msg) {
        case WM_INITDIALOG:
            dlg = reinterpret_cast<JdspConfigDialog*>(lParam);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(dlg));
            dlg->OnInitDialog(hwnd);
            return TRUE;

        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK) {
                dlg->OnApply(hwnd);
                EndDialog(hwnd, IDOK);
                return TRUE;
            }
            if (LOWORD(wParam) == IDCANCEL) {
                EndDialog(hwnd, IDCANCEL);
                return TRUE;
            }
            if (LOWORD(wParam) == IDC_BTN_APPLY) {
                dlg->OnApply(hwnd);
                return TRUE;
            }
            if (LOWORD(wParam) == IDC_COMBO_LANGUAGE && HIWORD(wParam) == CBN_SELCHANGE) {
                dlg->OnLanguageChange(hwnd, reinterpret_cast<HWND>(lParam));
                return TRUE;
            }
            break;
    }
    return FALSE;
}

void JdspConfigDialog::OnInitDialog(HWND hwnd) {
    m_hwnd = hwnd;

    // Initialize language combo
    HWND combo = GetDlgItem(hwnd, IDC_COMBO_LANGUAGE);
    SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"EN");
    SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"ZH");
    SendMessageW(combo, CB_SETCURSEL, 0, 0);

    // Set initial tab
    HWND tab = GetDlgItem(hwnd, IDC_TAB_MAIN);
    // Add tab items...
    TabCtrl_InsertItem(tab, 0, &(TCITEM{TCIF_TEXT, 0, 0, "Modules", 0, 0, 0, 0}));
    TabCtrl_InsertItem(tab, 1, &(TCITEM{TCIF_TEXT, 0, 0, "EQ", 0, 0, 0, 0}));
    TabCtrl_InsertItem(tab, 2, &(TCITEM{TCIF_TEXT, 0, 0, "Dynamics", 0, 0, 0, 0}));
    TabCtrl_InsertItem(tab, 3, &(TCITEM{TCIF_TEXT, 0, 0, "Effects", 0, 0, 0, 0}));
    TabCtrl_InsertItem(tab, 4, &(TCITEM{TCIF_TEXT, 0, 0, "Convolver", 0, 0, 0, 0}));
    TabCtrl_InsertItem(tab, 5, &(TCITEM{TCIF_TEXT, 0, 0, "Script", 0, 0, 0, 0}));
}

void JdspConfigDialog::OnApply(HWND hwnd) {
    // Collect all settings and send to host via IPC
    // This will be expanded in later tasks
}

void JdspConfigDialog::OnLanguageChange(HWND hwnd, HWND combo) {
    int sel = (int)SendMessageW(combo, CB_GETCURSEL, 0, 0);
    SwitchLanguage(hwnd, sel);
}

void JdspConfigDialog::SwitchLanguage(HWND hwnd, int lang_id) {
    m_current_lang = lang_id;
    const StrTable* table = (lang_id == 0) ? &g_str_en : &g_str_zh;

    SetWindowTextW(hwnd, table->window_title);
    SetDlgItemTextW(hwnd, IDC_BTN_APPLY, table->btn_apply);
    SetDlgItemTextW(hwnd, IDOK, table->btn_ok);
    SetDlgItemTextW(hwnd, IDCANCEL, table->btn_cancel);
    // Update all other controls...
}
```

- [ ] **Step 4: Create string tables**

Create `foo_dsp_jamesdsp/strings_en.h`:
```cpp
#pragma once
#include <windows.h>

struct StrTable {
    const wchar_t* window_title;
    const wchar_t* btn_ok;
    const wchar_t* btn_cancel;
    const wchar_t* btn_apply;
    // ... more strings
};

static const StrTable g_str_en = {
    L"JamesDSP Settings",
    L"OK",
    L"Cancel",
    L"Apply",
    // ...
};
```

Create `foo_dsp_jamesdsp/strings_zh.h`:
```cpp
#pragma once
#include "strings_en.h"

static const StrTable g_str_zh = {
    L"JamesDSP 设置",
    L"确定",
    L"取消",
    L"应用",
    // ...
};
```

- [ ] **Step 5: Commit**

```bash
git add foo_dsp_jamesdsp/resource.h foo_dsp_jamesdsp/jdsp_config_dialog.* foo_dsp_jamesdsp/strings_*.h
git commit -m "feat: add configuration dialog base with i18n support"
```

---

## Task 6: EQ Curve Widget

**Files:**
- Create: `foo_dsp_jamesdsp/jdsp_eq_widget.h`
- Create: `foo_dsp_jamesdsp/jdsp_eq_widget.cpp`

- [ ] **Step 1: Create EQ widget header**

Create `foo_dsp_jamesdsp/jdsp_eq_widget.h`:
```cpp
#pragma once
#include <windows.h>

struct EqBand {
    bool enabled = true;
    float frequency = 1000.0f;
    float gain = 0.0f;
    float q = 0.707f;
};

class JdspEqWidget {
public:
    JdspEqWidget();

    void Attach(HWND parent, int ctrl_id);
    void Detach();

    void SetBands(const EqBand bands[10]);
    void GetBands(EqBand bands[10]) const;

    void SetSelectedBand(int band);
    int GetSelectedBand() const;

private:
    static LRESULT CALLBACK SubclassProc(HWND hwnd, UINT msg, WPARAM wParam,
                                          LPARAM lParam, UINT_PTR subclass_id, DWORD_PTR ref_data);
    void OnPaint(HWND hwnd);
    void OnMouseDown(HWND hwnd, int x, int y);
    void OnMouseMove(HWND hwnd, int x, int y);
    void OnMouseUp(HWND hwnd);
    void OnContextMenu(HWND hwnd, int x, int y);

    void DrawGrid(HDC hdc, const RECT& rc);
    void DrawCurve(HDC hdc, const RECT& rc);
    void DrawHandles(HDC hdc, const RECT& rc);

    POINT FreqGainToPixel(float freq, float gain, const RECT& rc) const;
    void PixelToFreqGain(int px, int py, const RECT& rc, float& freq, float& gain) const;
    int HitTestHandle(int x, int y, const RECT& rc) const;

    EqBand m_bands[10];
    int m_selected_band = 0;
    int m_dragging_band = -1;
    HWND m_hwnd = NULL;
    WNDPROC m_original_proc = NULL;

    static const int HANDLE_RADIUS = 6;
    static const float MIN_FREQ = 20.0f;
    static const float MAX_FREQ = 20000.0f;
    static const float MIN_GAIN = -12.0f;
    static const float MAX_GAIN = 12.0f;
};
```

- [ ] **Step 2: Implement EQ widget**

Create `foo_dsp_jamesdsp/jdsp_eq_widget.cpp`:
```cpp
#include "jdsp_eq_widget.h"
#include <cmath>
#include <algorithm>

JdspEqWidget::JdspEqWidget() {
    // Default 10-band frequencies
    float freqs[] = {31, 62, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};
    for (int i = 0; i < 10; i++) {
        m_bands[i].frequency = freqs[i];
    }
}

void JdspEqWidget::Attach(HWND parent, int ctrl_id) {
    m_hwnd = GetDlgItem(parent, ctrl_id);
    m_original_proc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtr(m_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(SubclassProc)));
    SetWindowLongPtr(m_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
}

void JdspEqWidget::Detach() {
    if (m_hwnd && m_original_proc) {
        SetWindowLongPtr(m_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(m_original_proc));
    }
    m_hwnd = NULL;
}

void JdspEqWidget::SetBands(const EqBand bands[10]) {
    memcpy(m_bands, bands, sizeof(m_bands));
    if (m_hwnd) InvalidateRect(m_hwnd, NULL, TRUE);
}

void JdspEqWidget::GetBands(EqBand bands[10]) const {
    memcpy(bands, m_bands, sizeof(m_bands));
}

void JdspEqWidget::SetSelectedBand(int band) {
    m_selected_band = band;
    if (m_hwnd) InvalidateRect(m_hwnd, NULL, TRUE);
}

int JdspEqWidget::GetSelectedBand() const {
    return m_selected_band;
}

LRESULT CALLBACK JdspEqWidget::SubclassProc(HWND hwnd, UINT msg, WPARAM wParam,
                                              LPARAM lParam, UINT_PTR, DWORD_PTR ref_data) {
    JdspEqWidget* self = reinterpret_cast<JdspEqWidget*>(ref_data);

    switch (msg) {
        case WM_PAINT:
            self->OnPaint(hwnd);
            return 0;
        case WM_LBUTTONDOWN:
            self->OnMouseDown(hwnd, LOWORD(lParam), HIWORD(lParam));
            return 0;
        case WM_MOUSEMOVE:
            if (wParam & MK_LBUTTON)
                self->OnMouseMove(hwnd, LOWORD(lParam), HIWORD(lParam));
            return 0;
        case WM_LBUTTONUP:
            self->OnMouseUp(hwnd);
            return 0;
        case WM_RBUTTONUP:
            self->OnContextMenu(hwnd, LOWORD(lParam), HIWORD(lParam));
            return 0;
    }
    return CallWindowProc(self->original_proc, hwnd, msg, wParam, lParam);
}

void JdspEqWidget::OnPaint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);

    // Double buffer
    HDC hdcMem = CreateCompatibleDC(hdc);
    HBITMAP hbmMem = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
    SelectObject(hdcMem, hbmMem);

    FillRect(hdcMem, &rc, (HBRUSH)GetStockObject(WHITE_BRUSH));
    DrawGrid(hdcMem, rc);
    DrawCurve(hdcMem, rc);
    DrawHandles(hdcMem, rc);

    BitBlt(hdc, 0, 0, rc.right, rc.bottom, hdcMem, 0, 0, SRCCOPY);
    DeleteObject(hbmMem);
    DeleteDC(hdcMem);
    EndPaint(hwnd, &ps);
}

void JdspEqWidget::DrawGrid(HDC hdc, const RECT& rc) {
    HPEN hPen = CreatePen(PS_DOT, 1, RGB(200, 200, 200));
    SelectObject(hdc, hPen);

    // Horizontal grid lines (gain)
    for (int g = -12; g <= 12; g += 6) {
        int y = FreqGainToPixel(1000, (float)g, rc).y;
        MoveToEx(hdc, rc.left, y, NULL);
        LineTo(hdc, rc.right, y);
    }

    // Vertical grid lines (frequency)
    float freqs[] = {50, 100, 200, 500, 1000, 2000, 5000, 10000};
    for (float f : freqs) {
        int x = FreqGainToPixel(f, 0, rc).x;
        MoveToEx(hdc, x, rc.top, NULL);
        LineTo(hdc, x, rc.bottom);
    }

    DeleteObject(hPen);
}

void JdspEqWidget::DrawCurve(HDC hdc, const RECT& rc) {
    HPEN hPen = CreatePen(PS_SOLID, 2, RGB(0, 120, 215));
    SelectObject(hdc, hPen);

    bool first = true;
    for (int px = rc.left; px <= rc.right; px++) {
        float freq, gain;
        PixelToFreqGain(px, (rc.top + rc.bottom) / 2, rc, freq, gain);

        // Calculate combined gain from all enabled bands
        float total_gain = 0;
        for (int i = 0; i < 10; i++) {
            if (!m_bands[i].enabled) continue;
            float diff = log2f(freq / m_bands[i].frequency);
            float exponent = diff * m_bands[i].q * 2;
            total_gain += m_bands[i].gain * expf(-exponent * exponent);
        }

        POINT pt = FreqGainToPixel(freq, total_gain, rc);
        if (first) {
            MoveToEx(hdc, pt.x, pt.y, NULL);
            first = false;
        } else {
            LineTo(hdc, pt.x, pt.y);
        }
    }

    DeleteObject(hPen);
}

void JdspEqWidget::DrawHandles(HDC hdc, const RECT& rc) {
    for (int i = 0; i < 10; i++) {
        if (!m_bands[i].enabled) continue;

        POINT pt = FreqGainToPixel(m_bands[i].frequency, m_bands[i].gain, rc);
        HBRUSH hBrush;
        if (i == m_selected_band)
            hBrush = CreateSolidBrush(RGB(255, 100, 100));
        else
            hBrush = CreateSolidBrush(RGB(0, 120, 215));

        SelectObject(hdc, hBrush);
        SelectObject(hdc, GetStockObject(BLACK_PEN));
        Ellipse(hdc, pt.x - HANDLE_RADIUS, pt.y - HANDLE_RADIUS,
                pt.x + HANDLE_RADIUS, pt.y + HANDLE_RADIUS);
        DeleteObject(hBrush);
    }
}

POINT JdspEqWidget::FreqGainToPixel(float freq, float gain, const RECT& rc) const {
    float log_min = log10f(MIN_FREQ);
    float log_max = log10f(MAX_FREQ);
    float log_freq = log10f(freq);

    int x = rc.left + (int)((log_freq - log_min) / (log_max - log_min) * (rc.right - rc.left));
    int y = rc.bottom - (int)((gain - MIN_GAIN) / (MAX_GAIN - MIN_GAIN) * (rc.bottom - rc.top));
    return {x, y};
}

void JdspEqWidget::PixelToFreqGain(int px, int py, const RECT& rc, float& freq, float& gain) const {
    float log_min = log10f(MIN_FREQ);
    float log_max = log10f(MAX_FREQ);
    float ratio = (float)(px - rc.left) / (rc.right - rc.left);

    freq = powf(10.0f, log_min + ratio * (log_max - log_min));
    gain = MIN_GAIN + (1.0f - (float)(py - rc.top) / (rc.bottom - rc.top)) * (MAX_GAIN - MIN_GAIN);
}

int JdspEqWidget::HitTestHandle(int x, int y, const RECT& rc) const {
    for (int i = 0; i < 10; i++) {
        if (!m_bands[i].enabled) continue;
        POINT pt = FreqGainToPixel(m_bands[i].frequency, m_bands[i].gain, rc);
        int dx = x - pt.x, dy = y - pt.y;
        if (dx * dx + dy * dy <= HANDLE_RADIUS * HANDLE_RADIUS * 4)
            return i;
    }
    return -1;
}

void JdspEqWidget::OnMouseDown(HWND hwnd, int x, int y) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    int hit = HitTestHandle(x, y, rc);
    if (hit >= 0) {
        m_dragging_band = hit;
        m_selected_band = hit;
        InvalidateRect(hwnd, NULL, TRUE);
    }
}

void JdspEqWidget::OnMouseMove(HWND hwnd, int x, int y) {
    if (m_dragging_band < 0) return;
    RECT rc;
    GetClientRect(hwnd, &rc);
    float freq, gain;
    PixelToFreqGain(x, y, rc, freq, gain);
    m_bands[m_dragging_band].frequency = freq;
    m_bands[m_dragging_band].gain = gain;
    InvalidateRect(hwnd, NULL, TRUE);
}

void JdspEqWidget::OnMouseUp(HWND hwnd) {
    m_dragging_band = -1;
}

void JdspEqWidget::OnContextMenu(HWND hwnd, int x, int y) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    int hit = HitTestHandle(x, y, rc);
    if (hit < 0) return;

    POINT pt = {x, y};
    ClientToScreen(hwnd, &pt);

    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING, 1, L"Q: 0.5");
    AppendMenuW(hMenu, MF_STRING, 2, L"Q: 0.7");
    AppendMenuW(hMenu, MF_STRING, 3, L"Q: 1.0");
    AppendMenuW(hMenu, MF_STRING, 4, L"Q: 2.0");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, 10, m_bands[hit].enabled ? L"Disable" : L"Enable");

    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD, pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(hMenu);

    if (cmd >= 1 && cmd <= 4) {
        float q_values[] = {0.5f, 0.7f, 1.0f, 2.0f};
        m_bands[hit].q = q_values[cmd - 1];
        InvalidateRect(hwnd, NULL, TRUE);
    }
    if (cmd == 10) {
        m_bands[hit].enabled = !m_bands[hit].enabled;
        InvalidateRect(hwnd, NULL, TRUE);
    }
}
```

- [ ] **Step 3: Commit**

```bash
git add foo_dsp_jamesdsp/jdsp_eq_widget.*
git commit -m "feat: add EQ curve widget with drag-to-adjust and context menu"
```

---

## Task 7: Complete Configuration Dialog

**Files:**
- Modify: `foo_dsp_jamesdsp/jdsp_config_dialog.cpp`
- Modify: `foo_dsp_jamesdsp/jdsp_config_dialog.h`

- [ ] **Step 1: Add all tab page implementations**

Expand `jdsp_config_dialog.cpp` to implement all 6 tab pages:
- Modules tab: checkboxes for each module
- EQ tab: integrate JdspEqWidget
- Dynamics tab: sliders for compressor/limiter/DDC
- Effects tab: sliders for bass boost, stereo, reverb, etc.
- Convolver tab: IR file browser
- Script tab: text editor for EEL2

Each tab is created as a child dialog resource and managed by the tab control.

- [ ] **Step 2: Wire up parameter synchronization**

When Apply is clicked:
1. Read all UI values
2. Send SET_PARAM frames to host for each changed parameter
3. Update local state

When dialog opens:
1. Read current state from local cache
2. Populate UI controls

- [ ] **Step 3: Commit**

```bash
git add foo_dsp_jamesdsp/jdsp_config_dialog.*
git commit -m "feat: complete all configuration dialog tab pages"
```

---

## Task 8: Configuration Save/Load

**Files:**
- Create: `foo_dsp_jamesdsp/jdsp_config_serializer.h`
- Create: `foo_dsp_jamesdsp/jdsp_config_serializer.cpp`

- [ ] **Step 1: Create config serializer**

Implement JSON-based configuration save/load:
```cpp
// Format:
{
  "version": 1,
  "language": "en",
  "modules": {
    "analog_modelling": { "enabled": true, "tube_drive": 0.6 },
    "bs2b": { "enabled": true, "feed": 0.7, "frequency": 650 },
    ...
  },
  "eq": {
    "bands": [
      { "enabled": true, "freq": 31, "gain": 2.0, "q": 0.7 },
      ...
    ]
  }
}
```

- [ ] **Step 2: Implement file dialog integration**

Add Save/Load buttons to top toolbar that open standard Windows file dialogs.

- [ ] **Step 3: Commit**

```bash
git add foo_dsp_jamesdsp/jdsp_config_serializer.*
git commit -m "feat: add configuration save/load with JSON format"
```

---

## Task 9: Final Integration and Testing

- [ ] **Step 1: Build both x86 and x64 configurations**

```bash
msbuild foobar2000-jamesdsp.sln /p:Configuration=Release /p:Platform=x86
msbuild foobar2000-jamesdsp.sln /p:Configuration=Release /p:Platform=x64
```

- [ ] **Step 2: Test with foobar2000 v1.x**

1. Copy DLL + EXE to foobar2000 components folder
2. Launch foobar2000
3. Add JamesDSP to DSP chain
4. Open config dialog, verify all tabs work
5. Test audio processing with a music file

- [ ] **Step 3: Test with foobar2000 v2.x**

Repeat test with 64-bit foobar2000.

- [ ] **Step 4: Test crash isolation**

Kill jdsp_host.exe manually, verify foobar2000 continues playing and plugin restarts host.

- [ ] **Step 5: Test i18n**

Switch language in config dialog, verify all labels update.

- [ ] **Step 6: Test config save/load**

Save configuration to .jdsp file, restart foobar2000, load configuration, verify settings restored.

- [ ] **Step 7: Final commit**

```bash
git add -A
git commit -m "feat: complete foobar2000 JamesDSP plugin v0.1.0"
```
