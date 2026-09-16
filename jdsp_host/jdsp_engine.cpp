#include "jdsp_engine.h"
#include "jdsp_reverb_presets.h"
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

// Declared in Effects/reverb.c but not exported through jdsp_header.h. It is the
// only way to reach the individual reverb parameters, because Reverb_SetParam()
// merely calls it with a preset's fixed values.
extern "C" void sf_advancereverb(sf_reverb_state_st *rv, int rate, int oversamplefactor,
    float ertolate, float erefwet, float dry, float ereffactor, float erefwidth, float width,
    float wet, float wander, float bassb, float spin, float inputlpf, float basslpf,
    float damplpf, float outputlpf, float rt60, float delay);

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

static int parse_int(const char* s) {
    return s ? atoi(s) : 0;
}

static double clamp_double(double v, double lo, double hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static int clamp_int(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    if (n <= 0) return std::wstring();
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

// Reads a small text file (DDC profile / spectrum response / EEL2 script).
// Returns an empty string on any error.
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

    JamesDSPLib* jdsp = JDSP(m_jdsp);

    CompressorConstructor(jdsp);
    BassBoostConstructor(jdsp);
    MultimodalEqualizerConstructor(jdsp);
    StereoEnhancementConstructor(jdsp);
    CrossfeedConstructor(jdsp);
    DDCConstructor(jdsp);
    Convolver1DConstructor(jdsp);
    LiveProgConstructor(jdsp);
    ArbitraryResponseEqualizerConstructor(jdsp);

    // The constructors leave most parameters at zero, which is not a usable
    // setting for several of them (a zero compressor time constant means an
    // instant envelope follower). Push the real defaults once.
    ApplyCompressor();
    ApplyEqualizer();
    UpdateLimiter();
    BassBoostSetParam(jdsp, (float)m_bass_boost);
    StereoEnhancementSetParam(jdsp, (float)(m_stereo_width / 100.0));
    LoadReverbPresetDefaults();
    ApplyReverb();
    CrossfeedChangeMode(jdsp, m_bs2b_mode);
    JamesDSPSetPostGain(jdsp, m_output_gain);

    m_initialized = true;
    return true;
}

// The JamesDSP output limiter has no enable flag and always runs on every
// block (jdspController.c "Output" loop). Upstream JamesDSP always keeps it at
// ~0 dBFS so that effects which add gain (analog modelling, bass boost, ...)
// cannot push the output past full scale. We do the same: when the Limiter
// module is unchecked the threshold stays at 0 dBFS, which is bit-exact
// transparent for anything below full scale and only catches overshoot.
// The library requires threshold <= -0.09 dB and release >= 0.15 ms.
void JdspEngine::UpdateLimiter() {
    if (!m_jdsp) return;
    JamesDSPLib* jdsp = JDSP(m_jdsp);

    double threshold_db = 0.0;
    double release_ms = 100.0;
    if (m_module_enabled[kModLimiter]) {
        threshold_db = clamp_double(m_lim_threshold, -60.0, -0.09);
        release_ms = clamp_double(m_lim_release, 0.15, 5000.0);
    }
    JLimiterSetCoefficients(jdsp, threshold_db, release_ms);
}

void JdspEngine::ApplyEqualizer() {
    if (!m_jdsp) return;
    // MultimodalEqualizerAxisInterpolation clamps the gains to +-64 dB itself
    // and expects exactly 15 frequency/gain pairs. It also divides by the
    // spacing between neighbouring points, so the axis it receives must be
    // strictly increasing. Presets written by older builds carry arbitrary
    // per-band frequencies (and the 15-band axis may then be out of order or
    // contain duplicates), which makes the interpolation ill-formed, so hand it
    // a stably sorted copy with the duplicates pushed apart.
    double freq[kEqBands];
    double gain[kEqBands];
    int order[kEqBands];
    for (int i = 0; i < kEqBands; i++) {
        freq[i] = m_eq_freq[i];
        gain[i] = m_eq_gain[i];
        order[i] = i;
    }
    for (int i = 1; i < kEqBands; i++) {
        int j = i;
        while (j > 0 && freq[order[j - 1]] > freq[order[j]]) {
            int t = order[j - 1];
            order[j - 1] = order[j];
            order[j] = t;
            j--;
        }
    }
    for (int i = 0; i < kEqBands; i++) {
        freq[i] = m_eq_freq[order[i]];
        gain[i] = m_eq_gain[order[i]];
        if (i > 0 && freq[i] <= freq[i - 1]) freq[i] = freq[i - 1] + 1.0;
    }
    MultimodalEqualizerAxisInterpolation(JDSP(m_jdsp), m_eq_interpolation, m_eq_filter_type,
                                         freq, gain);
}

void JdspEngine::ApplyCompressor() {
    if (!m_jdsp) return;
    JamesDSPLib* jdsp = JDSP(m_jdsp);
    CompressorSetParam(jdsp, (float)m_comp_time, m_comp_granularity, m_comp_tfresolution, 1);
    CompressorSetGain(jdsp, m_comp_band_freq, m_comp_band_gain, 1);
}

// The preset supplies the parameters that have no user-facing control (early
// reflection shape, modulation, low pass corners); the rest come from the
// individual reverb parameters so they can be tweaked after picking a preset.
void JdspEngine::ApplyReverb() {
    if (!m_jdsp) return;
    JamesDSPLib* jdsp = JDSP(m_jdsp);
    const JdspReverbParams& p =
        kJdspReverbPresets[clamp_int(m_reverb_preset, 0, JDSP_REVERB_PRESET_COUNT - 1)];
    sf_advancereverb(&jdsp->reverb, (int)jdsp->fs, p.osf,
                     (float)m_reverb_er, p.erefwet, (float)m_reverb_dry, p.ereffactor, p.erefwidth,
                     (float)m_reverb_width, (float)m_reverb_wet, p.wander, (float)m_reverb_bass,
                     p.spin, p.inputlpf, p.basslpf, (float)m_reverb_damp, p.outputlpf,
                     (float)m_reverb_rt60, (float)m_reverb_predelay);
}

// sf_advancereverb() rebuilds (and zeroes) every delay/comb/allpass buffer, so it must
// only run when a parameter that changes the topology moves (RT60, damping, predelay or
// the preset). Wet/dry/width/bass/ER are plain state fields, so they are written
// directly - otherwise dragging a slider would restart the reverb tail on every tick.
static float JdspDb2Lin(float db) { return powf(10.0f, 0.05f * db); }

void JdspEngine::ApplyReverbScalars() {
    if (!m_jdsp) return;
    JamesDSPLib* jdsp = JDSP(m_jdsp);
    sf_reverb_state_st* rv = &jdsp->reverb;
    const JdspReverbParams& p =
        kJdspReverbPresets[clamp_int(m_reverb_preset, 0, JDSP_REVERB_PRESET_COUNT - 1)];

    float wet = JdspDb2Lin((float)m_reverb_wet);
    rv->ertolate = (float)m_reverb_er;
    rv->erefwet = JdspDb2Lin(p.erefwet);
    rv->dry = JdspDb2Lin((float)m_reverb_dry);
    rv->wet1 = wet * ((float)m_reverb_width * 0.5f + 0.5f);
    rv->wet2 = wet * ((1.0f - (float)m_reverb_width) * 0.5f);
    rv->wander = p.wander;
    rv->bassb = (float)m_reverb_bass;
}

void JdspEngine::LoadReverbPresetDefaults() {    const JdspReverbParams& p =
        kJdspReverbPresets[clamp_int(m_reverb_preset, 0, JDSP_REVERB_PRESET_COUNT - 1)];
    m_reverb_wet = p.wet;
    m_reverb_dry = p.dry;
    m_reverb_width = p.width;
    m_reverb_rt60 = p.rt60;
    m_reverb_damp = p.damplpf;
    m_reverb_bass = p.bassb;
    m_reverb_predelay = p.delay;
    m_reverb_er = p.ertolate;
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
    if (m_module_enabled[kModConvolver]) Convolver1DEnable(jdsp);
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
    if (m_module_enabled[kModDdc]) DDCEnable(jdsp, 1);
    m_ddc_path_last = path;
    EngineLog("LoadDdcProfile: ok");
    return true;
}

// The spectrum extender takes a text file of "gain frequency" number pairs
// (see ArbFIRGen.c ArbitraryEqString2SortedNodes).
bool JdspEngine::LoadSpectrumProfile(const std::wstring& path) {
    if (!m_jdsp) return false;
    if (path == m_spectrum_path_last) return true;

    JamesDSPLib* jdsp = JDSP(m_jdsp);

    if (path.empty()) {
        m_spectrum_path_last.clear();
        return true;
    }

    std::string text = ReadTextFile(path);
    if (text.empty()) {
        EngineLog("LoadSpectrumProfile: cannot read profile");
        return false;
    }
    text.push_back('\0');

    ArbitraryResponseEqualizerStringParser(jdsp, &text[0]);
    if (m_module_enabled[kModSpectrum]) ArbitraryResponseEqualizerEnable(jdsp, 1);
    m_spectrum_path_last = path;
    EngineLog("LoadSpectrumProfile: ok");
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
    if (m_module_enabled[kModEel2]) LiveProgEnable(jdsp);
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
    int iv = parse_int(val);

    // ---- module enables ----
    if (key == "modules.analog") {
        m_module_enabled[kModAnalog] = (iv != 0);
        // VacuumTubeEnable() runs VTInit() which resets pregain/postgain, so the
        // drive must be applied afterwards (otherwise the slider does nothing).
        if (m_module_enabled[kModAnalog]) { VacuumTubeEnable(jdsp); VacuumTubeSetGain(jdsp, m_tube_drive_db); }
        else VacuumTubeDisable(jdsp);
    }
    else if (key == "modules.bs2b") {
        m_module_enabled[kModBs2b] = (iv != 0);
        CrossfeedEnable(jdsp, m_module_enabled[kModBs2b]);
    }
    else if (key == "modules.ddc") {
        m_module_enabled[kModDdc] = (iv != 0);
        DDCEnable(jdsp, m_module_enabled[kModDdc]);
    }
    else if (key == "modules.limiter") { m_module_enabled[kModLimiter] = (iv != 0); UpdateLimiter(); }
    else if (key == "modules.compressor") {
        m_module_enabled[kModCompressor] = (iv != 0);
        CompressorEnable(jdsp, m_module_enabled[kModCompressor]);
    }
    else if (key == "modules.convolver") {
        m_module_enabled[kModConvolver] = (iv != 0);
        if (m_module_enabled[kModConvolver]) Convolver1DEnable(jdsp); else Convolver1DDisable(jdsp);
    }
    else if (key == "modules.reverb") {
        m_module_enabled[kModReverb] = (iv != 0);
        if (m_module_enabled[kModReverb]) ReverbEnable(jdsp); else ReverbDisable(jdsp);
    }
    else if (key == "modules.bassboost") {
        m_module_enabled[kModBassBoost] = (iv != 0);
        if (m_module_enabled[kModBassBoost]) BassBoostEnable(jdsp); else BassBoostDisable(jdsp);
    }
    else if (key == "modules.stereo") {
        m_module_enabled[kModStereo] = (iv != 0);
        if (m_module_enabled[kModStereo]) StereoEnhancementEnable(jdsp); else StereoEnhancementDisable(jdsp);
    }
    else if (key == "modules.iir" || key == "modules.equalizer") {
        m_module_enabled[kModEqualizer] = (iv != 0);
        MultimodalEqualizerEnable(jdsp, m_module_enabled[kModEqualizer]);
    }
    else if (key == "modules.spectrum") {
        m_module_enabled[kModSpectrum] = (iv != 0);
        ArbitraryResponseEqualizerEnable(jdsp, m_module_enabled[kModSpectrum]);
    }
    else if (key == "modules.eel2") {
        m_module_enabled[kModEel2] = (iv != 0);
        if (m_module_enabled[kModEel2]) LiveProgEnable(jdsp); else LiveProgDisable(jdsp);
    }

    // ---- analog modelling ----
    else if (key == "tube.drive") {
        m_tube_drive_db = clamp_double(dv, -3.0, 12.0);
        if (m_module_enabled[kModAnalog]) VacuumTubeSetGain(jdsp, m_tube_drive_db);
    }

    // ---- crossfeed ----
    else if (key == "bs2b.mode") {
        m_bs2b_mode = clamp_int(iv, 0, 5);
        CrossfeedChangeMode(jdsp, m_bs2b_mode);
    }

    // ---- limiter ----
    else if (key == "limiter.threshold") {
        m_lim_threshold = clamp_double(dv, -60.0, -0.09);
        UpdateLimiter();
    }
    else if (key == "limiter.release") {
        m_lim_release = clamp_double(dv, 0.15, 5000.0);
        UpdateLimiter();
    }

    // ---- global output gain ----
    else if (key == "output.gain") {
        m_output_gain = clamp_double(dv, -15.0, 15.0);
        JamesDSPSetPostGain(jdsp, m_output_gain);
    }

    // ---- compressor (spectral compander) ----
    else if (key == "compressor.timeconstant") {
        m_comp_time = clamp_double(dv, 0.001, 10.0);
        ApplyCompressor();
    }
    else if (key == "compressor.granularity") {
        m_comp_granularity = clamp_int(iv, 0, 3);
        ApplyCompressor();
    }
    else if (key == "compressor.tfresolution") {
        m_comp_tfresolution = clamp_int(iv, 0, 3);
        ApplyCompressor();
    }
    else if (key.find("compressor.band") == 0) {
        int band = atoi(key.c_str() + 15);
        if (band >= 0 && band < kCompBands) {
            size_t dot = key.rfind('.');
            if (dot != std::string::npos && key.substr(dot + 1) == "gain") {
                m_comp_band_gain[band] = clamp_double(dv, -60.0, 60.0);
                ApplyCompressor();
            }
        }
    }

    // ---- FIR equalizer ----
    else if (key == "eq.filtertype") {
        m_eq_filter_type = clamp_int(iv, 0, 5);
        ApplyEqualizer();
    }
    else if (key == "eq.interpolation") {
        m_eq_interpolation = clamp_int(iv, 0, 1);
        ApplyEqualizer();
    }
    else if (key.find("eq.band") == 0) {
        int band = atoi(key.c_str() + 7);
        if (band >= 0 && band < kEqBands) {
            size_t dot = key.rfind('.');
            std::string param = (dot == std::string::npos) ? std::string() : key.substr(dot + 1);
            if (param == "freq") {
                m_eq_freq[band] = clamp_double(dv, 20.0, 20000.0);
                ApplyEqualizer();
            } else if (param == "gain") {
                m_eq_gain[band] = clamp_double(dv, -64.0, 64.0);
                ApplyEqualizer();
            }
        }
    }

    // ---- reverb ----
    else if (key == "reverb.preset") {
        m_reverb_preset = clamp_int(iv, 0, JDSP_REVERB_PRESET_COUNT - 1);
        // Picking a preset resets the individual parameters to that preset.
        LoadReverbPresetDefaults();
        ApplyReverb();
    }
    else if (key == "reverb.wet") {
        m_reverb_wet = clamp_double(dv, -70.0, 0.0);
        ApplyReverbScalars();
    }
    else if (key == "reverb.dry") {
        m_reverb_dry = clamp_double(dv, -30.0, 0.0);
        ApplyReverbScalars();
    }
    else if (key == "reverb.width") {
        m_reverb_width = clamp_double(dv, 0.0, 1.0);
        ApplyReverbScalars();
    }
    else if (key == "reverb.rt60") {
        m_reverb_rt60 = clamp_double(dv, 0.5, 30.0);
        ApplyReverb();
    }
    else if (key == "reverb.damping") {
        m_reverb_damp = clamp_double(dv, 1000.0, 18000.0);
        ApplyReverb();
    }
    else if (key == "reverb.bassboost") {
        m_reverb_bass = clamp_double(dv, 0.0, 2.0);
        ApplyReverbScalars();
    }
    else if (key == "reverb.predelay") {
        m_reverb_predelay = clamp_double(dv, 0.0, 0.1);
        ApplyReverb();
    }
    else if (key == "reverb.er") {
        m_reverb_er = clamp_double(dv, 0.0, 1.0);
        ApplyReverbScalars();
    }

    // ---- bass boost ----
    else if (key == "bassboost.gain") {
        m_bass_boost = clamp_double(dv, 0.0, 15.0);
        BassBoostSetParam(jdsp, (float)m_bass_boost);
    }

    // ---- stereo widener ----
    else if (key == "stereo.width") {
        m_stereo_width = clamp_double(dv, 0.0, 100.0);
        StereoEnhancementSetParam(jdsp, (float)(m_stereo_width / 100.0));
    }

    // ---- file backed modules ----
    else if (key == "convolver.path") { LoadImpulseResponse(Utf8ToWide(value)); }
    else if (key == "ddc.profile") { LoadDdcProfile(Utf8ToWide(value)); }
    else if (key == "spectrum.path") { LoadSpectrumProfile(Utf8ToWide(value)); }
    else if (key == "script.text") { LoadEelScript(value); }
}

bool JdspEngine::AnyModuleEnabled() const {
    for (int i = 0; i < kModCount; i++) {
        if (m_module_enabled[i]) return true;
    }
    return false;
}
