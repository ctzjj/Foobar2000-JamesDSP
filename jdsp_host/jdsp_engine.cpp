#include "jdsp_engine.h"
#include "wav_loader.h"
#include <windows.h>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cstdarg>
#include <cmath>

extern "C" {
#include "jdsp_header.h"
}

// stdout is the IPC pipe, so nothing may be written there outside the frame
// protocol. All diagnostics go to the log file instead.
extern FILE* g_log;

static void EngineLog(const char* fmt, ...) {
    if (!g_log) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}

static inline JamesDSPLib* JDSP(void* p) { return reinterpret_cast<JamesDSPLib*>(p); }

static double parse_float(const char* s) {
    return s ? atof(s) : 0.0;
}

static std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    if (n <= 0) return std::wstring();
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

// Reads a small text file (DDC profile / EEL2 script). Returns an empty string on
// any error.
static std::string ReadTextFile(const std::wstring& path) {
    std::string out;
    if (path.empty()) return out;
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) return out;
    char buf[4096];
    size_t got;
    while ((got = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, got);
    fclose(f);
    return out;
}

JdspEngine::JdspEngine() {}

JdspEngine::~JdspEngine() {
    Shutdown();
}

bool JdspEngine::Initialize(uint32_t sample_rate, uint32_t channels) {
    if (m_initialized) Shutdown();

    m_sample_rate = sample_rate;
    m_channels = channels;
    m_block_size = 4096;

    m_jdsp = malloc(sizeof(JamesDSPLib));
    if (!m_jdsp) return false;

    memset(m_jdsp, 0, sizeof(JamesDSPLib));

    JamesDSPGlobalMemoryAllocation();
    JamesDSPInit(JDSP(m_jdsp), (int)m_block_size, (float)m_sample_rate);
    JamesDSPReallocateBlock(JDSP(m_jdsp), m_block_size);

    CompressorConstructor(JDSP(m_jdsp));
    BassBoostConstructor(JDSP(m_jdsp));
    MultimodalEqualizerConstructor(JDSP(m_jdsp));
    StereoEnhancementConstructor(JDSP(m_jdsp));
    CrossfeedConstructor(JDSP(m_jdsp));
    DDCConstructor(JDSP(m_jdsp));
    Convolver1DConstructor(JDSP(m_jdsp));
    LiveProgConstructor(JDSP(m_jdsp));
    ArbitraryResponseEqualizerConstructor(JDSP(m_jdsp));

    UpdateLimiter();

    m_initialized = true;
    return true;
}

// The JamesDSP output limiter has no enable flag and always runs on every
// block (jdspController.c "Output" loop). Upstream JamesDSP always keeps it at
// ~0 dBFS so that effects which add gain (analog modelling, bass boost, ...)
// cannot push the output past full scale. We do the same: when the Limiter
// module is unchecked the threshold stays at 0 dBFS, which is bit-exact
// transparent for anything below full scale and only catches overshoot.
void JdspEngine::UpdateLimiter() {
    if (!m_jdsp) return;
    JamesDSPLib* jdsp = JDSP(m_jdsp);
    double threshold_db = m_module_enabled[3] ? (double)m_lim_threshold : 0.0;
    JLimiterSetCoefficients(jdsp, threshold_db, m_lim_release);
}

// The UI exposes the drive as a 0..100 percentage, while the library API takes
// dB and clamps to [-3, +12].
double JdspEngine::TubeDriveDb() const {
    double pct = (double)m_tube_drive;
    if (pct < 0.0) pct = 0.0;
    if (pct > 100.0) pct = 100.0;
    return -3.0 + (pct / 100.0) * 15.0;
}

bool JdspEngine::LoadImpulseResponse(const std::wstring& path) {
    if (!m_jdsp) return false;
    if (path == m_ir_path_last) return true;

    JamesDSPLib* jdsp = JDSP(m_jdsp);

    if (path.empty()) {
        Convolver1DLoadImpulseResponse(jdsp, nullptr, 0, 0, 1);
        Convolver1DDisable(jdsp);
        m_ir_path_last.clear();
        return true;
    }

    std::vector<float> samples;
    uint32_t channels = 0;
    uint32_t sample_rate = 0;
    if (!LoadWavInterleaved(path, samples, channels, sample_rate)) {
        EngineLog("LoadImpulseResponse: cannot read impulse response");
        return false;
    }
    (void)sample_rate;

    size_t frames = samples.size() / channels;
    int r = Convolver1DLoadImpulseResponse(jdsp, samples.data(), channels, frames, 1);
    if (!r) {
        EngineLog("LoadImpulseResponse: Convolver1DLoadImpulseResponse failed");
        return false;
    }
    // Loading does not turn the convolver on; only the module flag does that.
    if (m_module_enabled[5]) Convolver1DEnable(jdsp);
    m_ir_path_last = path;
    EngineLog("LoadImpulseResponse: ok (%u ch, %u frames)",
           (unsigned)channels, (unsigned)frames);
    return true;
}

bool JdspEngine::LoadDdcProfile(const std::wstring& path) {
    if (!m_jdsp) return false;
    if (path == m_ddc_path_last) return true;

    JamesDSPLib* jdsp = JDSP(m_jdsp);

    if (path.empty()) {
        m_ddc_path_last.clear();
        return true;
    }

    std::string text = ReadTextFile(path);
    if (text.empty()) {
        EngineLog("LoadDdcProfile: cannot read profile");
        return false;
    }
    text.push_back('\0');  // DDCStringParser takes a C string

    int r = DDCStringParser(jdsp, &text[0]);
    if (r < 0) {
        EngineLog("LoadDdcProfile: DDCStringParser failed");
        return false;
    }
    if (m_module_enabled[2]) DDCEnable(jdsp, 1);
    m_ddc_path_last = path;
    EngineLog("LoadDdcProfile: ok");
    return true;
}

bool JdspEngine::LoadEelScript(const std::string& text) {
    if (!m_jdsp) return false;
    if (text == m_script_last) return true;

    JamesDSPLib* jdsp = JDSP(m_jdsp);
    m_script_last = text;

    if (text.empty()) {
        LiveProgDisable(jdsp);
        return true;
    }

    std::string code = text;
    code.push_back('\0');
    int err = LiveProgStringParser(jdsp, &code[0]);
    if (err != 1) {
        EngineLog("LoadEelScript: %s", checkErrorCode(err));
        return false;
    }
    if (m_module_enabled[12]) LiveProgEnable(jdsp);
    EngineLog("LoadEelScript: ok");
    return true;
}

void JdspEngine::Shutdown() {
    if (!m_jdsp) return;

    if (m_initialized) {
        CompressorDestructor(JDSP(m_jdsp));
        MultimodalEqualizerDestructor(JDSP(m_jdsp));
        StereoEnhancementDestructor(JDSP(m_jdsp));
        CrossfeedDestructor(JDSP(m_jdsp));
        DDCDestructor(JDSP(m_jdsp));
        Convolver1DDestructor(JDSP(m_jdsp));
        LiveProgDestructor(JDSP(m_jdsp));
        ArbitraryResponseEqualizerDestructor(JDSP(m_jdsp));
        JamesDSPFree(JDSP(m_jdsp));
    }

    free(m_jdsp);
    m_jdsp = nullptr;
    m_initialized = false;
}

bool JdspEngine::Process(float* audio, uint32_t sample_count, uint32_t channels) {
    if (!m_initialized || !m_jdsp) return false;
    if (sample_count == 0 || channels == 0) return false;

    JamesDSPLib* jdsp = JDSP(m_jdsp);
    size_t n = sample_count;

    if (jdsp->processFloatMultiplexd) {
        jdsp->processFloatMultiplexd(jdsp, audio, audio, n);
    }

    return true;
}

void JdspEngine::SetParam(const std::string& key, const std::string& value) {
    if (!m_jdsp) return;
    JamesDSPLib* jdsp = JDSP(m_jdsp);
    const char* val = value.c_str();
    double dv = parse_float(val);

    if (key == "modules.analog") {
        m_module_enabled[0] = (atoi(val) != 0);
        // VacuumTubeEnable() runs VTInit() which resets pregain/postgain, so the
        // drive must be applied afterwards (otherwise the slider does nothing).
        if (m_module_enabled[0]) { VacuumTubeEnable(jdsp); VacuumTubeSetGain(jdsp, TubeDriveDb()); }
        else VacuumTubeDisable(jdsp);
    }
    else if (key == "modules.bs2b") { m_module_enabled[1] = (atoi(val) != 0); CrossfeedEnable(jdsp, m_module_enabled[1]); }
    else if (key == "modules.ddc") { m_module_enabled[2] = (atoi(val) != 0); DDCEnable(jdsp, m_module_enabled[2]); }
    else if (key == "modules.limiter") { m_module_enabled[3] = (atoi(val) != 0); UpdateLimiter(); }
    else if (key == "modules.compressor") { m_module_enabled[4] = (atoi(val) != 0); CompressorEnable(jdsp, m_module_enabled[4]); }
    else if (key == "modules.convolver") {
        m_module_enabled[5] = (atoi(val) != 0);
        if (m_module_enabled[5]) Convolver1DEnable(jdsp); else Convolver1DDisable(jdsp);
    }
    else if (key == "modules.reverb") { m_module_enabled[6] = (atoi(val) != 0); if (m_module_enabled[6]) ReverbEnable(jdsp); else ReverbDisable(jdsp); }
    else if (key == "modules.bassboost") { m_module_enabled[7] = (atoi(val) != 0); if (m_module_enabled[7]) BassBoostEnable(jdsp); else BassBoostDisable(jdsp); }
    else if (key == "modules.stereo") { m_module_enabled[8] = (atoi(val) != 0); if (m_module_enabled[8]) StereoEnhancementEnable(jdsp); else StereoEnhancementDisable(jdsp); }
    else if (key == "modules.iir") { m_module_enabled[9] = (atoi(val) != 0); MultimodalEqualizerEnable(jdsp, m_module_enabled[9]); }
    else if (key == "modules.spectrum") { m_module_enabled[10] = (atoi(val) != 0); ArbitraryResponseEqualizerEnable(jdsp, m_module_enabled[10]); }
    else if (key == "modules.dynamic") { m_module_enabled[11] = (atoi(val) != 0); }
    else if (key == "modules.eel2") { m_module_enabled[12] = (atoi(val) != 0); if (m_module_enabled[12]) LiveProgEnable(jdsp); else LiveProgDisable(jdsp); }

    else if (key == "compressor.threshold") { m_comp_threshold = (float)dv; }
    else if (key == "compressor.ratio") { m_comp_ratio = (float)dv; }
    else if (key == "compressor.attack") { m_comp_attack = (float)dv; }
    else if (key == "compressor.release") { m_comp_release = (float)dv; }
    else if (key == "limiter.threshold") { m_lim_threshold = (float)dv; UpdateLimiter(); }
    else if (key == "limiter.release") { m_lim_release = (float)dv; UpdateLimiter(); }

    else if (key == "bassboost.gain") { m_bass_boost = (float)dv; BassBoostSetParam(jdsp, m_bass_boost); }
    else if (key == "bassboost.freq") { m_bass_freq = (float)dv; }
    else if (key == "stereo.width") { m_stereo_width = (float)dv; StereoEnhancementSetParam(jdsp, m_stereo_width / 100.0f); }
    else if (key == "reverb.roomsize") { m_reverb_room = (float)dv; Reverb_SetParam(jdsp, SF_REVERB_PRESET_DEFAULT); }
    else if (key == "reverb.damping") { m_reverb_damp = (float)dv; }
    else if (key == "reverb.wet") { m_reverb_wet = (float)dv; }
    else if (key == "tube.drive") { m_tube_drive = (float)dv; if (m_module_enabled[0]) VacuumTubeSetGain(jdsp, TubeDriveDb()); }
    else if (key == "bs2b.feed") { m_bs2b_feed = (float)dv; }
    else if (key == "bs2b.freq") { m_bs2b_freq = (float)dv; }
    else if (key == "convolver.gain") { m_conv_gain = (float)dv; JamesDSPSetPostGain(jdsp, m_conv_gain); }
    else if (key == "convolver.path") { LoadImpulseResponse(Utf8ToWide(value)); }
    else if (key == "ddc.profile") { LoadDdcProfile(Utf8ToWide(value)); }
    else if (key == "script.text") { LoadEelScript(value); }

    else if (key.find("eq.band") == 0) {
        int band = atoi(key.c_str() + 7);
        if (band >= 0 && band < 10) {
            size_t dot = key.rfind('.');
            std::string param = key.substr(dot + 1);
            if (param == "freq") jdsp->mEQ.freq[band + 1] = dv;
            else if (param == "gain") jdsp->mEQ.gain[band + 1] = dv;
            jdsp->equalizerForceRefresh = 1;
        }
    }
}

bool JdspEngine::AnyModuleEnabled() const {
    for (int i = 0; i < 13; i++) {
        if (m_module_enabled[i]) return true;
    }
    return false;
}

void JdspEngine::ApplyAllParams() {
    if (!m_jdsp) return;
    JamesDSPLib* jdsp = JDSP(m_jdsp);
    CompressorSetParam(jdsp, m_comp_ratio, 10, 20, 1);
    UpdateLimiter();
    BassBoostSetParam(jdsp, m_bass_boost);
    StereoEnhancementSetParam(jdsp, m_stereo_width / 100.0f);
    VacuumTubeSetGain(jdsp, TubeDriveDb());
    JamesDSPSetPostGain(jdsp, m_conv_gain);
}
