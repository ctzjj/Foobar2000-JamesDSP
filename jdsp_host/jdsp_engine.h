#pragma once
#include <cstdint>
#include <string>

class JdspEngine {
public:
    JdspEngine();
    ~JdspEngine();

    bool Initialize(uint32_t sample_rate, uint32_t channels);
    void Shutdown();
    bool Process(float* audio, uint32_t sample_count, uint32_t channels);
    void SetParam(const std::string& key, const std::string& value);

private:
    void* m_engine = nullptr;
    bool m_initialized = false;
};
