#pragma once
#include <windows.h>
#include <commctrl.h>
#include <string>
#include "jdsp_ipc_client.h"
#include "jdsp_eq_widget.h"
#include "strings.h"

// Module indices must match the JdspModule enum in jdsp_host/jdsp_engine.h.
enum {
    kDlgModAnalog = 0,
    kDlgModBs2b,
    kDlgModDdc,
    kDlgModLimiter,
    kDlgModCompressor,
    kDlgModConvolver,
    kDlgModReverb,
    kDlgModBassBoost,
    kDlgModStereo,
    kDlgModEqualizer,
    kDlgModSpectrum,
    kDlgModEel2,
    kDlgModCount
};

#define JDSP_COMP_BANDS 7
#define JDSP_TAB_COUNT  7

class JdspConfigDialog {
public:
    JdspConfigDialog(JdspIpcClient& ipc);
    // Returns true if the user pressed OK, false if cancelled/closed.
    bool Show(HWND parent);

    // Full settings as a "key=value\n" blob (used for the DSP preset and file save/load).
    std::string SerializeSettings() const;
    void DeserializeSettings(const std::string& blob);

    int GetLanguage() const { return m_current_lang; }
    void SetLanguage(int lang) { m_current_lang = lang; }

    bool GetModuleEnabled(int index) const { return m_modules[index]; }
    void SetModuleEnabled(int index, bool v) { m_modules[index] = v; }
    const EqBand* GetEqBands() const { return m_eq_bands; }
    void SetEqBands(const EqBand bands[JDSP_EQ_BANDS]) { memcpy(m_eq_bands, bands, sizeof(m_eq_bands)); }

    // Called by tab-page dialog procedures
    void HandleTabCommand(WPARAM wParam, LPARAM lParam);
    void HandleTabScroll(WPARAM wParam, LPARAM lParam);
    void HandleTabNotify(WPARAM wParam, LPARAM lParam);

private:
    static INT_PTR CALLBACK DialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void OnInitDialog(HWND hwnd);
    void OnCommand(HWND hwnd, WPARAM wParam, LPARAM lParam);
    void OnNotify(HWND hwnd, WPARAM wParam, LPARAM lParam);
    void OnApply(HWND hwnd);
    void SyncFromControls(HWND hwnd);
    void PushLive(bool full = false);
    void OnHScroll(HWND hwnd, WPARAM wParam, LPARAM lParam);
    void OnLanguageChange(HWND hwnd);
    void SwitchLanguage(int lang_id);

    void InitTabs(HWND hwnd);
    void CreateTabDialogs(HWND hwnd);
    void ShowTab(int tab);
    void ApplyLanguage();

    void InitModulesTab(HWND hwnd);
    void ApplyModulesTab(HWND hwnd);
    void InitEqTab(HWND hwnd);
    void ApplyEqTab(HWND hwnd);
    void InitDynamicsTab(HWND hwnd);
    void ApplyDynamicsTab(HWND hwnd);
    void InitEffectsTab(HWND hwnd);
    void ApplyEffectsTab(HWND hwnd);
    void InitConvolverTab(HWND hwnd);
    void ApplyConvolverTab(HWND hwnd);
    void InitSpectrumTab(HWND hwnd);
    void ApplySpectrumTab(HWND hwnd);
    void InitScriptTab(HWND hwnd);
    void ApplyScriptTab(HWND hwnd);

    void UpdateSliderLabel(HWND tab, int slider_id, int label_id, const wchar_t* fmt, float val);
    void UpdateEqBandLabel(HWND tab, int band);
    void UpdateCompBandLabel(HWND tab, int band);

    JdspIpcClient& m_ipc;
    HWND m_hwnd = NULL;
    int m_current_lang = 0;
    int m_current_tab = 0;

    HWND m_tab_dialogs[JDSP_TAB_COUNT] = {};
    JdspEqWidget m_eq_widget;

    bool m_modules[kDlgModCount] = {};
    EqBand m_eq_bands[JDSP_EQ_BANDS];
    int m_eq_filter_type = 0;      // 0 = FIR minimum phase, 1..5 = IIR
    int m_eq_interpolation = 0;    // 0 = pchip, 1 = makima

    double m_comp_time = 0.1;                 // seconds
    int m_comp_granularity = 1;               // 0..3
    int m_comp_tfresolution = 2;              // 0..3
    double m_comp_band_gain[JDSP_COMP_BANDS] = {};

    double m_lim_threshold = -1.0;            // dB, library requires <= -0.09
    double m_lim_release = 50.0;              // ms, library requires >= 0.15

    double m_tube_drive_db = 3.0;             // dB, library clamps -3..+12
    int m_bs2b_mode = 0;                      // 0..5
    double m_bass_boost = 6.0;                // dB 0..15
    double m_stereo_width = 50.0;             // %, 50 is the library identity
    int m_reverb_preset = 0;                  // 0..18
    double m_reverb_wet = -8.0;               // dB -70..0
    double m_reverb_dry = -7.0;               // dB -30..0
    double m_reverb_width = 1.0;              // 0..1
    double m_reverb_rt60 = 2.8;               // room size, seconds 0.5..30
    double m_reverb_damp = 8000.0;            // damping, Hz 1000..18000
    double m_reverb_bass = 0.2;               // bass boost, 1.0 = 100 %
    double m_reverb_predelay = 0.01;          // seconds 0..0.1
    double m_reverb_er = 0.3;                 // early reflection level 0..1
    double m_output_gain = 0.0;               // dB -15..+15

    wchar_t m_ir_path[MAX_PATH] = {};
    wchar_t m_ddc_profile[MAX_PATH] = {};
    wchar_t m_spectrum_path[MAX_PATH] = {};
    wchar_t m_script_text[8192] = {};

    std::string m_settings_blob;
    std::string m_live_blob;   // last blob successfully derived from the controls
    std::string m_orig_blob;   // blob captured when the dialog opened (cancel revert)
    bool m_in_live_push = false;  // guards the live-push feedback loop
};
