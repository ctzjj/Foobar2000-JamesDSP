#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include "jdsp_ipc_protocol.h"

class JdspIpcClient {
public:
    JdspIpcClient(class JdspHostManager& manager);

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
};
