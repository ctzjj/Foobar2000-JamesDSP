#pragma once
#include <cstdint>
#include <vector>
#include "jdsp_engine.h"
#include "jdsp_ipc_protocol.h"

class JdspIpcServer {
public:
    JdspIpcServer(JdspEngine& engine);
    bool Run();

private:
    bool HandleAudioData(const std::vector<uint8_t>& payload, std::vector<uint8_t>& response);
    bool HandleSetParam(const std::vector<uint8_t>& payload);

    JdspEngine& m_engine;
};
