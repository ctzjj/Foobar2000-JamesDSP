#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Module indices (the order the dialog and the preset keys use).
enum JdspModule {
    kModAnalog = 0,
    kModBs2b,
    kModDdc,
    kModLimiter,
    kModCompressor,
    kModConvolver,
    kModReverb,
    kModBassBoost,
    kModStereo,
    kModEqualizer,
    kModSpectrum,
    kModEel2,
    kModCount
};

// The FIR equalizer is fixed at 15 points by the library (NUMPTS in multimodalEQ).
const int kEqBands = 15;
// The compressor is a 7 band spectral compander (NUMPTS_DRS in dynamic.c).
const int kCompBands = 7;

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
    void UpdateLimiter();
    void ApplyEqualizer();
    void ApplyCompressor();
    void ApplyReverb();
    void ApplyReverbScalars();
    void ApplyCrossfeed();
    void LoadBs2bModeDefaults();
    // Copies the preset's values into the individual reverb parameters.
    void LoadReverbPresetDefaults();
    bool LoadImpulseResponse(const std::wstring& path);
    bool LoadDdcProfile(const std::wstring& path);
    bool LoadSpectrumProfile(const std::wstring& path);
    bool LoadEelScript(const std::string& text);

    void* m_jdsp = nullptr;
    bool m_initialized = false;
    uint32_t m_sample_rate = 44100;
    uint32_t m_channels = 2;
    size_t m_block_size = 4096;

    bool m_module_enabled[kModCount] = {};

    // Analog modelling: drive in dB, library clamps to [-3, +12].
    double m_tube_drive_db = 3.0;

    // BS2B / crossfeed: mode 0..5.
    int m_bs2b_mode = 0;
    // BS2B level parameters, used when the mode is 0 or 1 (BS2B): feed in dB
    // (1.0..15.0) and cutoff frequency in Hz (300..2000). The library ships the
    // BS2B_*_CLEVEL presets only; these let the user dial the crossfeed in.
    double m_bs2b_feed = 6.0;
    double m_bs2b_fcut = 700.0;

    // Limiter: library requires threshold <= -0.09 dB and release >= 0.15 ms.
    double m_lim_threshold = -1.0;
    double m_lim_release = 50.0;

    // Global output gain in dB, +-15.
    double m_output_gain = 0.0;

    // Compressor (spectral compander).
    double m_comp_time = 0.1;   // time constant in seconds
    int m_comp_granularity = 1; // 0..3
    int m_comp_tfresolution = 2;// 0..3
    double m_comp_band_freq[kCompBands] = { 95.0, 200.0, 400.0, 800.0, 1600.0, 3400.0, 7500.0 };
    double m_comp_band_gain[kCompBands] = {};

    // FIR equalizer (15 points).
    double m_eq_freq[kEqBands] = { 25.0, 40.0, 63.0, 100.0, 160.0, 250.0, 400.0, 630.0,
                                   1000.0, 1600.0, 2500.0, 4000.0, 6300.0, 10000.0, 16000.0 };
    double m_eq_gain[kEqBands] = {};
    int m_eq_filter_type = 0;      // 0 = FIR minimum phase, 1..5 = IIR 4/6/8/10/12 order
    int m_eq_interpolation = 0;    // 0 = pchip, 1 = makima

    // Reverb preset index (0..18) plus the individual parameters the preset fills in.
    int m_reverb_preset = 0;
    double m_reverb_wet = -8.0;      // dB, -70..0
    double m_reverb_dry = -7.0;      // dB, -30..0
    double m_reverb_width = 1.0;     // 0..1
    double m_reverb_rt60 = 2.8;      // seconds, 0.5..30
    double m_reverb_damp = 8000.0;   // damping low pass Hz, 1000..18000
    double m_reverb_bass = 0.2;      // bass boost, 1.0 = 100 %
    double m_reverb_predelay = 0.01; // seconds, 0..0.1
    double m_reverb_er = 0.3;        // early reflection level, 0..1

    // Bass boost max gain in dB (0..15).
    double m_bass_boost = 6.0;

    // Stereo widening, 0..100 %. 50 % is the library's identity setting.
    double m_stereo_width = 50.0;

    std::wstring m_ir_path_last;
    std::wstring m_ddc_path_last;
    std::wstring m_spectrum_path_last;
    std::string m_script_last;
};
