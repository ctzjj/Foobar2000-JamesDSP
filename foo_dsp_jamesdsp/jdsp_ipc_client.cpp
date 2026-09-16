#include "stdafx.h"
#include "jdsp_ipc_client.h"
#include "jdsp_host_manager.h"
#include <cstring>

static const DWORD IPC_TIMEOUT_MS = 3000;

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

JdspIpcClient::JdspIpcClient(JdspHostManager& manager) : m_manager(manager) {}

bool JdspIpcClient::WriteFrame(jdsp::FrameType type, const void* data, uint32_t length) {
    HANDLE hWrite = m_manager.GetStdinWrite();
    if (!hWrite) return false;

    jdsp::FrameHeader header;
    header.type = type;
    header.data_length = length;

    if (!WriteFull(hWrite, &header, sizeof(header))) return false;
    if (data && length > 0) {
        if (!WriteFull(hWrite, data, length)) return false;
    }
    return true;
}

struct IoThreadArgs {
    HANDLE hRead;
    HANDLE hWrite;
    std::vector<uint8_t> write_buf;
    jdsp::FrameType write_type;
    bool write_only;
    bool free_self;   // true = thread owns and frees args (fire-and-forget)
    bool write_ok;
    bool read_ok;
    jdsp::FrameHeader read_header;
    std::vector<uint8_t> read_payload;
};

// Thread does NOT free args unless free_self is set; the synchronous caller
// (SendAudioData) frees it after joining to avoid a double-free / use-after-free.
static void IoFinish(IoThreadArgs* a) { if (a->free_self) delete a; }

static DWORD WINAPI IoThread(LPVOID param) {
    IoThreadArgs* a = (IoThreadArgs*)param;

    // Write
    jdsp::FrameHeader wheader;
    wheader.type = a->write_type;
    wheader.data_length = static_cast<uint32_t>(a->write_buf.size());

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

    if (a->write_only) { IoFinish(a); return 0; }

    // Read response
    if (!ReadFull(a->hRead, &a->read_header, sizeof(a->read_header))) {
        a->read_ok = false;
        IoFinish(a);
        return 0;
    }

    a->read_payload.resize(a->read_header.data_length);
    if (a->read_header.data_length > 0) {
        if (!ReadFull(a->hRead, a->read_payload.data(), a->read_header.data_length)) {
            a->read_ok = false;
            IoFinish(a);
            return 0;
        }
    }
    a->read_ok = true;
    IoFinish(a);
    return 0;
}

bool JdspIpcClient::SendAudioData(uint32_t sample_rate, uint32_t channels,
                                   uint32_t sample_count, const float* audio,
                                   float* output) {
    HANDLE hRead = m_manager.GetStdoutRead();
    HANDLE hWrite = m_manager.GetStdinWrite();
    if (!hRead || !hWrite) return false;

    uint32_t data_size = sample_count * channels * sizeof(float);
    uint32_t total_size = sizeof(jdsp::AudioData) + data_size;

    std::vector<uint8_t> payload(total_size);
    jdsp::AudioData* hdr = reinterpret_cast<jdsp::AudioData*>(payload.data());
    hdr->sample_rate = sample_rate;
    hdr->channels = channels;
    hdr->sample_count = sample_count;
    memcpy(payload.data() + sizeof(jdsp::AudioData), audio, data_size);

    IoThreadArgs* args = new IoThreadArgs();
    args->hRead = hRead;
    args->hWrite = hWrite;
    args->write_buf.resize(sizeof(jdsp::AudioData) + data_size);
    memcpy(args->write_buf.data(), payload.data(), sizeof(jdsp::AudioData) + data_size);
    args->write_type = jdsp::FrameType::AUDIO_DATA;
    args->write_only = false;
    args->free_self = false;
    args->write_ok = false;
    args->read_ok = false;

    HANDLE hThread = CreateThread(NULL, 0, IoThread, args, 0, NULL);
    if (!hThread) { delete args; return false; }

    DWORD wait_result = WaitForSingleObject(hThread, IPC_TIMEOUT_MS);
    if (wait_result == WAIT_TIMEOUT) {
        CancelIo(hRead);
        CancelIo(hWrite);
        DWORD wr2 = WaitForSingleObject(hThread, 1000);
        CloseHandle(hThread);
        // Only free if the thread actually terminated; otherwise leak rather than corrupt.
        if (wr2 == WAIT_OBJECT_0) delete args;
        return false;
    }

    CloseHandle(hThread);

    bool ok = args->write_ok && args->read_ok &&
              args->read_header.type == jdsp::FrameType::AUDIO_DATA &&
              args->read_payload.size() >= sizeof(jdsp::AudioData) + data_size;
    if (ok) {
        memcpy(output, args->read_payload.data() + sizeof(jdsp::AudioData), data_size);
    }
    delete args;
    return ok;
}

bool JdspIpcClient::SendSetParam(const std::string& key, const std::string& value) {
    HANDLE hWrite = m_manager.GetStdinWrite();
    if (!hWrite) return false;

    std::string param = key + "=" + value;

    IoThreadArgs* args = new IoThreadArgs();
    args->hWrite = hWrite;
    args->write_buf.assign(param.begin(), param.end());
    args->write_type = jdsp::FrameType::SET_PARAM;
    args->write_only = true;
    args->free_self = true;
    args->write_ok = false;

    HANDLE hThread = CreateThread(NULL, 0, IoThread, args, 0, NULL);
    if (!hThread) { delete args; return false; }
    CloseHandle(hThread);
    return true;
}

bool JdspIpcClient::SendSetParams(const std::string& blob) {
    if (blob.empty()) return false;
    return WriteFrame(jdsp::FrameType::SET_PARAM, blob.data(), (uint32_t)blob.size());
}

bool JdspIpcClient::SendShutdown() {
    return WriteFrame(jdsp::FrameType::SHUTDOWN, nullptr, 0);
}
