#pragma once
#include <cstdint>
#include <string>
#include <vector>

class JdspEngine {
public:
    JdspEngine();
    ~JdspEngine();

    bool Initialize(uint32_t sample_rate, uint32_t channels);
    void Shutdown();
    bool Process(float* audio, uint32_t sample_count, uint32_t channels);
    void SetParam(const std::string& key, const std::string& value);
    bool AnyModuleEnabled() const;

private:
    void ApplyAllParams();
    void UpdateLimiter();
    double TubeDriveDb() const;
    bool LoadImpulseResponse(const std::wstring& path);
    bool LoadDdcProfile(const std::wstring& path);
    bool LoadEelScript(const std::string& text);
    void* m_jdsp = nullptr;
    bool m_initialized = false;
    uint32_t m_sample_rate = 44100;
    uint32_t m_channels = 2;
    size_t m_block_size = 4096;

    float m_comp_threshold = -20.0f;
    float m_comp_ratio = 4.0f;
    float m_comp_attack = 5.0f;
    float m_comp_release = 50.0f;
    float m_lim_threshold = -1.0f;
    float m_lim_release = 50.0f;
    float m_ddc_strength = 50.0f;
    float m_bass_boost = 6.0f;
    float m_bass_freq = 100.0f;
    float m_stereo_width = 120.0f;
    float m_reverb_room = 0.7f;
    float m_reverb_damp = 0.5f;
    float m_reverb_wet = 0.3f;
    float m_tube_drive = 60.0f;
    float m_bs2b_feed = 70.0f;
    float m_bs2b_freq = 650.0f;
    float m_conv_gain = 0.0f;

    std::wstring m_ir_path_last;
    std::wstring m_ddc_path_last;
    std::string m_script_last;

    bool m_module_enabled[13] = {};
};
