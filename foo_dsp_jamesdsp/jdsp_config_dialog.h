#pragma once
#include <windows.h>
#include <commctrl.h>
#include <string>
#include "jdsp_ipc_client.h"
#include "jdsp_eq_widget.h"
#include "strings.h"

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
    void SetEqBands(const EqBand bands[10]) { memcpy(m_eq_bands, bands, sizeof(m_eq_bands)); }

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
    void InitScriptTab(HWND hwnd);
    void ApplyScriptTab(HWND hwnd);

    void UpdateSliderLabel(HWND tab, int slider_id, int label_id, const wchar_t* fmt, float val);
    void UpdateEqBandLabel(HWND tab, int band);

    JdspIpcClient& m_ipc;
    HWND m_hwnd = NULL;
    int m_current_lang = 0;
    int m_current_tab = 0;

    HWND m_tab_dialogs[6] = {};
    JdspEqWidget m_eq_widget;

    bool m_modules[13] = {};
    EqBand m_eq_bands[10];

    float m_comp_threshold = -20.0f;
    float m_comp_ratio = 4.0f;
    float m_comp_attack = 5.0f;
    float m_comp_release = 50.0f;
    float m_lim_threshold = 0.0f;
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
    wchar_t m_ir_path[MAX_PATH] = {};
    wchar_t m_ddc_profile[MAX_PATH] = {};
    wchar_t m_script_text[8192] = {};
    bool m_script_enabled = false;
    bool m_conv_enabled = false;
    std::string m_settings_blob;
    std::string m_live_blob;   // last blob successfully derived from the controls
    std::string m_orig_blob;   // blob captured when the dialog opened (cancel revert)
};
