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
