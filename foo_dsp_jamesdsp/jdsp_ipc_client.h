#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <cstdint>
#include "jdsp_ipc_protocol.h"

// Talks to jdsp_host.exe over the two anonymous pipes.
//
// All pipe I/O happens on one dedicated worker thread. Callers only hand work
// over and wait for a result, so a slow or wedged host can never leave a
// half-finished request on the pipe (an abandoned read used to desync the frame
// stream permanently and make the DSP pass audio through untouched).
//
// Parameter updates are merged into a single pending blob, so a slider drag
// collapses into at most one SET_PARAM frame per audio frame instead of
// flooding the pipe.
class JdspIpcClient {
public:
    JdspIpcClient(class JdspHostManager& manager);
    ~JdspIpcClient();

    bool SendAudioData(uint32_t sample_rate, uint32_t channels,
                       uint32_t sample_count, const float* audio, float* output);
    bool SendSetParam(const std::string& key, const std::string& value);
    // Sends many "key=value\n" settings; merged with anything else pending.
    bool SendSetParams(const std::string& blob);
    bool SendShutdown();

private:
    static DWORD WINAPI WorkerEntry(LPVOID param);
    void WorkerLoop();

    bool WriteFrame(jdsp::FrameType type, const void* data, uint32_t length);
    bool ReadFrame(HANDLE hRead, jdsp::FrameHeader& header, std::vector<uint8_t>& payload);
    bool DoAudioFrame(uint32_t sample_rate, uint32_t channels, uint32_t sample_count,
                      const std::vector<float>& input, std::vector<float>& output);

    JdspHostManager& m_manager;
    CRITICAL_SECTION m_cs;
    HANDLE m_worker;
    HANDLE m_work_event;
    HANDLE m_done_event;
    bool m_stop;

    std::string m_param_blob;
    bool m_shutdown_pending;

    bool m_audio_pending;
    uint32_t m_req_rate;
    uint32_t m_req_channels;
    uint32_t m_req_count;
    std::vector<float> m_req_audio;

    bool m_result_ready;
    bool m_result_ok;
    std::vector<float> m_result_audio;

    ULONGLONG m_frame_started_ms;
};
