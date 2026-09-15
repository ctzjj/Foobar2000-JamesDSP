#pragma once
#include <cstdint>
#include <vector>
#include "jdsp_engine.h"

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

class JdspIpcServer {
public:
    JdspIpcServer(JdspEngine& engine);
    bool Run();

private:
    bool HandleAudioData(const std::vector<uint8_t>& payload, std::vector<uint8_t>& response);
    bool HandleSetParam(const std::vector<uint8_t>& payload);

    JdspEngine& m_engine;
};
