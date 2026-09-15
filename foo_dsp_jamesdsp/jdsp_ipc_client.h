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
};

} // namespace jdsp

class JdspIpcClient {
public:
    JdspIpcClient(class JdspHostManager& manager);

    bool SendAudioData(uint32_t sample_rate, uint32_t channels,
                       uint32_t sample_count, const float* audio,
                       float* output);
    bool SendSetParam(const std::string& key, const std::string& value);
    bool SendShutdown();

private:
    bool WriteFrame(jdsp::FrameType type, const void* data, uint32_t length);
    bool ReadFrame(jdsp::FrameHeader& header, std::vector<uint8_t>& payload);

    JdspHostManager& m_manager;
};
