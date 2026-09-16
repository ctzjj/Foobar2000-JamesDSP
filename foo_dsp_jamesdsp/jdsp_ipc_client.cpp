#include "stdafx.h"
#include "jdsp_ipc_client.h"
#include "jdsp_host_manager.h"
#include <cstring>

// How long the audio thread waits for a frame before giving up on it. The
// worker keeps the request alive, so the stream stays in sync; the chunk is
// simply played back untouched.
static const DWORD IPC_TIMEOUT_MS = 1000;

// If a frame has been outstanding for longer than this the host is considered
// wedged and is terminated so the DSP layer can start a fresh one.
static const ULONGLONG IPC_WEDGE_MS = 5000;

// Pipes may transfer fewer bytes than requested; loop until complete.
static bool WriteFull(HANDLE h, const void* src, DWORD n) {
    DWORD sent = 0;
    const char* p = static_cast<const char*>(src);
    while (sent < n) {
        DWORD w = 0;
        if (!WriteFile(h, p + sent, n - sent, &w, NULL) || w == 0) return false;
        sent += w;
    }
    return true;
}

static bool ReadFull(HANDLE h, void* dst, DWORD n) {
    DWORD got = 0;
    char* p = static_cast<char*>(dst);
    while (got < n) {
        DWORD r = 0;
        if (!ReadFile(h, p + got, n - got, &r, NULL) || r == 0) return false;
        got += r;
    }
    return true;
}

JdspIpcClient::JdspIpcClient(JdspHostManager& manager)
    : m_manager(manager), m_worker(NULL), m_work_event(NULL), m_done_event(NULL),
      m_stop(false), m_shutdown_pending(false), m_audio_pending(false),
      m_req_rate(0), m_req_channels(0), m_req_count(0), m_result_ready(false),
      m_result_ok(false), m_frame_started_ms(0) {
    InitializeCriticalSection(&m_cs);
    m_work_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    m_done_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (m_work_event && m_done_event) {
        m_worker = CreateThread(NULL, 0, WorkerEntry, this, 0, NULL);
    }
}

JdspIpcClient::~JdspIpcClient() {
    if (m_worker) {
        EnterCriticalSection(&m_cs);
        m_stop = true;
        LeaveCriticalSection(&m_cs);
        SetEvent(m_work_event);
        if (WaitForSingleObject(m_worker, 500) != WAIT_OBJECT_0) {
            // Worker is blocked on the host; killing it closes the pipes.
            m_manager.Stop();
            WaitForSingleObject(m_worker, 1000);
        }
        CloseHandle(m_worker);
        m_worker = NULL;
    }
    if (m_work_event) { CloseHandle(m_work_event); m_work_event = NULL; }
    if (m_done_event) { CloseHandle(m_done_event); m_done_event = NULL; }
    DeleteCriticalSection(&m_cs);
}

DWORD WINAPI JdspIpcClient::WorkerEntry(LPVOID param) {
    static_cast<JdspIpcClient*>(param)->WorkerLoop();
    return 0;
}

void JdspIpcClient::WorkerLoop() {
    for (;;) {
        WaitForSingleObject(m_work_event, INFINITE);

        std::string params;
        bool shutdown = false;
        bool has_audio = false;
        uint32_t rate = 0, channels = 0, count = 0;
        std::vector<float> audio;

        EnterCriticalSection(&m_cs);
        if (m_stop) { LeaveCriticalSection(&m_cs); return; }
        params.swap(m_param_blob);
        shutdown = m_shutdown_pending;
        m_shutdown_pending = false;
        has_audio = m_audio_pending;
        if (has_audio) {
            rate = m_req_rate;
            channels = m_req_channels;
            count = m_req_count;
            audio.swap(m_req_audio);
        }
        LeaveCriticalSection(&m_cs);

        // Parameters first: they are cheap and the next audio frame should
        // already reflect them.
        if (!params.empty()) {
            WriteFrame(jdsp::FrameType::SET_PARAM, params.data(), (uint32_t)params.size());
        }
        if (shutdown) {
            WriteFrame(jdsp::FrameType::SHUTDOWN, nullptr, 0);
        }

        if (has_audio) {
            std::vector<float> out;
            bool ok = DoAudioFrame(rate, channels, count, audio, out);
            EnterCriticalSection(&m_cs);
            m_audio_pending = false;
            m_result_ok = ok;
            m_result_audio.swap(out);
            m_result_ready = true;
            LeaveCriticalSection(&m_cs);
            SetEvent(m_done_event);
        }
    }
}

bool JdspIpcClient::WriteFrame(jdsp::FrameType type, const void* data, uint32_t length) {
    HANDLE hWrite = m_manager.GetStdinWrite();
    if (!hWrite) return false;

    jdsp::FrameHeader header;
    header.type = type;
    header.data_length = length;

    bool ok = WriteFull(hWrite, &header, sizeof(header));
    if (ok && data && length > 0) {
        ok = WriteFull(hWrite, data, length);
    }
    return ok;
}

bool JdspIpcClient::ReadFrame(HANDLE hRead, jdsp::FrameHeader& header,
                              std::vector<uint8_t>& payload) {
    if (!ReadFull(hRead, &header, sizeof(header))) return false;
    payload.resize(header.data_length);
    if (header.data_length > 0) {
        if (!ReadFull(hRead, payload.data(), header.data_length)) return false;
    }
    return true;
}

bool JdspIpcClient::DoAudioFrame(uint32_t sample_rate, uint32_t channels,
                                 uint32_t sample_count, const std::vector<float>& input,
                                 std::vector<float>& output) {
    HANDLE hRead = m_manager.GetStdoutRead();
    HANDLE hWrite = m_manager.GetStdinWrite();
    if (!hRead || !hWrite) return false;

    uint32_t data_size = sample_count * channels * sizeof(float);
    std::vector<uint8_t> payload(sizeof(jdsp::AudioData) + data_size);
    jdsp::AudioData* hdr = reinterpret_cast<jdsp::AudioData*>(payload.data());
    hdr->sample_rate = sample_rate;
    hdr->channels = channels;
    hdr->sample_count = sample_count;
    if (data_size > 0) {
        memcpy(payload.data() + sizeof(jdsp::AudioData), input.data(), data_size);
    }

    if (!WriteFrame(jdsp::FrameType::AUDIO_DATA, payload.data(), (uint32_t)payload.size())) {
        return false;
    }

    jdsp::FrameHeader rh;
    std::vector<uint8_t> rp;
    if (!ReadFrame(hRead, rh, rp)) return false;
    if (rh.type != jdsp::FrameType::AUDIO_DATA) return false;
    if (rp.size() < sizeof(jdsp::AudioData) + data_size) return false;

    output.resize((size_t)sample_count * channels);
    if (data_size > 0) {
        memcpy(output.data(), rp.data() + sizeof(jdsp::AudioData), data_size);
    }
    return true;
}

bool JdspIpcClient::SendAudioData(uint32_t sample_rate, uint32_t channels,
                                  uint32_t sample_count, const float* audio,
                                  float* output) {
    size_t samples = (size_t)sample_count * channels;

    EnterCriticalSection(&m_cs);
    if (m_audio_pending) {
        ULONGLONG started = m_frame_started_ms;
        LeaveCriticalSection(&m_cs);
        // The host is still busy with the previous frame. Pass this chunk
        // through untouched rather than queueing more work; if it stays busy
        // for too long, kill it so a fresh one can be started.
        if (GetTickCount64() - started > IPC_WEDGE_MS) {
            m_manager.Stop();
        }
        return false;
    }

    m_audio_pending = true;
    m_req_rate = sample_rate;
    m_req_channels = channels;
    m_req_count = sample_count;
    m_req_audio.assign(audio, audio + samples);
    m_result_ready = false;
    m_result_ok = false;
    m_result_audio.clear();
    m_frame_started_ms = GetTickCount64();
    ResetEvent(m_done_event);
    LeaveCriticalSection(&m_cs);

    SetEvent(m_work_event);

    ULONGLONG deadline = GetTickCount64() + IPC_TIMEOUT_MS;
    for (;;) {
        ULONGLONG now = GetTickCount64();
        DWORD remain = (now < deadline) ? (DWORD)(deadline - now) : 0;
        WaitForSingleObject(m_done_event, remain);

        bool ready = false, ok = false;
        EnterCriticalSection(&m_cs);
        if (m_result_ready) {
            ready = true;
            ok = m_result_ok;
            if (ok) {
                if (m_result_audio.size() >= samples) {
                    memcpy(output, m_result_audio.data(), samples * sizeof(float));
                } else {
                    ok = false;
                }
            }
            m_result_ready = false;
            m_result_audio.clear();
        }
        LeaveCriticalSection(&m_cs);

        if (ready) return ok;

        // Timed out. Deliberately leave the request in flight: cancelling the
        // worker's read from this thread does nothing and would leave a stuck
        // reader that steals the next response.
        if (GetTickCount64() >= deadline) return false;
    }
}

bool JdspIpcClient::SendSetParam(const std::string& key, const std::string& value) {
    return SendSetParams(key + "=" + value);
}

bool JdspIpcClient::SendSetParams(const std::string& blob) {
    if (blob.empty()) return false;
    EnterCriticalSection(&m_cs);
    if (!m_param_blob.empty()) m_param_blob += '\n';
    m_param_blob += blob;
    LeaveCriticalSection(&m_cs);
    SetEvent(m_work_event);
    return true;
}

bool JdspIpcClient::SendShutdown() {
    EnterCriticalSection(&m_cs);
    m_shutdown_pending = true;
    LeaveCriticalSection(&m_cs);
    SetEvent(m_work_event);
    // Give the worker a moment to push the frame out before the pipes close.
    Sleep(50);
    return true;
}
