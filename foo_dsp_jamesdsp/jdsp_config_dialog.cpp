#include "stdafx.h"
#include "jdsp_config_dialog.h"
#include "jdsp_config_serializer.h"
#include "resource.h"
#include "jdsp_live_link.h"
#include "jdsp_reverb_presets.h"
#include <cstdio>
#include <map>

extern void EnsureEqClassRegistered();
HMODULE GetMyModule();

// Live parameter pushes are coalesced to at most one per this interval. Windows
// generates WM_HSCROLL far faster than the host can absorb EQ rebuilds, and a
// flooded pipe stalls the audio thread.
#define IDT_LIVE_PUSH 1
#define JDSP_LIVE_PUSH_MS 80

static FILE* g_cfg_log = NULL;
void CfgLog(const char* msg) {
    if (!g_cfg_log) g_cfg_log = fopen("jdsp_cfg.log", "a");
    if (g_cfg_log) { fprintf(g_cfg_log, "%s\n", msg); fflush(g_cfg_log); }
}

#ifndef Button_SetCheck
#define Button_SetCheck(hCtrl, uCheck) SendMessage((hCtrl), BM_SETCHECK, (WPARAM)(uCheck), 0)
#endif
#ifndef Button_GetCheck
#define Button_GetCheck(hCtrl) ((LRESULT)SendMessage((hCtrl), BM_GETCHECK, 0, 0))
#endif

// SetWindowTextW always notifies the parent, even when the text is identical, so
// only write when something actually changes.
static void SetTextIfChanged(HWND w, const wchar_t* text) {
    if (!w) return;
    wchar_t cur[512];
    cur[0] = L'\0';
    GetWindowTextW(w, cur, 512);
    if (wcscmp(cur, text) == 0) return;
    SetWindowTextW(w, text);
}

// ===== Module checkbox tables =====

// Order must match kDlgMod*.
static const int kModuleCheckboxes[kDlgModCount] = {
    IDC_CHK_ANALOG, IDC_CHK_BS2B, IDC_CHK_DDC, IDC_CHK_LIMITER,
    IDC_CHK_COMPRESSOR, IDC_CHK_CONVOLVER, IDC_CHK_REVERB, IDC_CHK_BASS_BOOST,
    IDC_CHK_STEREO, IDC_CHK_IIR, IDC_CHK_SPECTRUM, IDC_CHK_EEL2
};

// "Enable" checkboxes on the other tabs are aliases for the module flags.
struct ModAlias { int alias_id; int mod_index; };
static const ModAlias kModAliases[] = {
    { IDC_CHK_TUBE_EFFECT,     kDlgModAnalog },
    { IDC_CHK_BASS_EFFECT,     kDlgModBassBoost },
    { IDC_CHK_STEREO_EFFECT,   kDlgModStereo },
    { IDC_CHK_REVERB_EFFECT,   kDlgModReverb },
    { IDC_CHK_BS2B_EFFECT,     kDlgModBs2b },
    { IDC_CHK_COMP,            kDlgModCompressor },
    { IDC_CHK_LIM,             kDlgModLimiter },
    { IDC_CHK_DDC_ENABLE,      kDlgModDdc },
    { IDC_CHK_CONV_ENABLE,     kDlgModConvolver },
    { IDC_CHK_SPECTRUM_ENABLE, kDlgModSpectrum },
    { IDC_CHK_SCRIPT_ENABLE,   kDlgModEel2 },
    { IDC_CHK_EQ_ENABLE,       kDlgModEqualizer },
};

static const float kEqDefaultFreqs[JDSP_EQ_BANDS] = {
    25, 40, 63, 100, 160, 250, 400, 630, 1000, 1600, 2500, 4000, 6300, 10000, 16000
};

static const int kTabIds[JDSP_TAB_COUNT] = {
    IDD_TAB_MODULES, IDD_TAB_EQ, IDD_TAB_DYNAMICS, IDD_TAB_EFFECTS,
    IDD_TAB_CONVOLVER, IDD_TAB_SPECTRUM, IDD_TAB_SCRIPT
};

static const wchar_t* kTabNamesEn[JDSP_TAB_COUNT] = {
    L"Modules", L"Equalizer", L"Dynamics", L"Effects",
    L"Convolver", L"Spectrum Extender", L"Script"
};
static const wchar_t* kTabNamesZh[JDSP_TAB_COUNT] = {
    L"\x6a21\x5757", L"\x5747\x8861\x5668", L"\x52a8\x6001", L"\x6548\x679c",
    L"\x5377\x79ef\x5668", L"\x9891\x8c31\x6269\x5c55", L"\x811a\x672c"
};

static INT_PTR CALLBACK TabPageDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    JdspConfigDialog* dlg = NULL;
    if (msg == WM_INITDIALOG) {
        dlg = reinterpret_cast<JdspConfigDialog*>(lParam);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(dlg));
        return TRUE;
    }
    dlg = reinterpret_cast<JdspConfigDialog*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    if (!dlg) return FALSE;
    switch (msg) {
        case WM_COMMAND:
            dlg->HandleTabCommand(wParam, lParam);
            return TRUE;
        case WM_HSCROLL:
        case WM_VSCROLL:
            dlg->HandleTabScroll(wParam, lParam);
            return TRUE;
        case WM_NOTIFY: {
            NMHDR* nm = reinterpret_cast<NMHDR*>(lParam);
            if (nm->idFrom == IDC_EQ_CURVE && nm->code == NM_CLICK) {
                dlg->HandleTabNotify(wParam, lParam);
                return TRUE;
            }
            return FALSE;
        }
    }
    return FALSE;
}

struct UiLabelDef { int id; const wchar_t* en; const wchar_t* zh; };

static const UiLabelDef kUiLabels[] = {
    { IDC_BTN_SAVE_CONFIG,   L"Save Config", L"\x4fdd\x5b58\x914d\x7f6e" },
    { IDC_BTN_LOAD_CONFIG,   L"Load Config", L"\x52a0\x8f7d\x914d\x7f6e" },
    { IDC_BTN_RESET_ALL,     L"Reset All",   L"\x91cd\x7f6e\x5168\x90e8" },
    // Modules tab
    { IDC_CHK_ANALOG,        L"Analog Modelling", L"\x7535\x5b50\x7ba1\x6a21\x62df" },
    { IDC_CHK_BS2B,          L"BS2B Crossfeed",   L"BS2B \x8de8\x9988" },
    { IDC_CHK_DDC,           L"DDC",              L"DDC" },
    { IDC_CHK_LIMITER,       L"Limiter",          L"\x9650\x5e45\x5668" },
    { IDC_CHK_COMPRESSOR,    L"Compressor",       L"\x538b\x7f29\x5668" },
    { IDC_CHK_CONVOLVER,     L"Convolver",        L"\x5377\x79ef\x5668" },
    { IDC_CHK_REVERB,        L"Reverb",           L"\x6df7\x54cd" },
    { IDC_CHK_BASS_BOOST,    L"Bass Boost",       L"\x4f4e\x97f3\x589e\x5f3a" },
    { IDC_CHK_STEREO,        L"Stereo Widener",   L"\x7acb\x4f53\x58f0\x589e\x5f3a" },
    { IDC_CHK_IIR,           L"Equalizer",        L"\x5747\x8861\x5668" },
    { IDC_CHK_SPECTRUM,      L"Spectrum Extender", L"\x9891\x8c31\x6269\x5c55" },
    { IDC_CHK_EEL2,          L"EEL2 Scripting",   L"EEL2 \x811a\x672c" },
    // Equalizer tab
    { IDL_EQ_BAND,           L"Band:",           L"\x9891\x6bb5:" },
    { IDL_EQ_FREQ,           L"Freq (Hz):",      L"\x9891\x7387 (Hz):" },
    { IDL_EQ_GAIN,           L"Gain (dB):",      L"\x589e\x76ca (dB):" },
    { IDL_EQ_FILTERTYPE,     L"Filter type:",    L"\x6ee4\x6ce2\x5668\x7c7b\x578b:" },
    { IDL_EQ_INTERP,         L"Interpolation:",  L"\x63d2\x503c:" },
    { IDC_BTN_EQ_RESET,      L"Reset Flat",      L"\x91cd\x7f6e\x4e3a\x5e73\x76f4" },
    { IDC_CHK_EQ_ENABLE,     L"Enable",          L"\x542f\x7528" },
    // Dynamics tab
    { IDL_DYN_COMP_GRP,      L"Compressor",      L"\x538b\x7f29\x5668" },
    { IDC_CHK_COMP,          L"Enable",          L"\x542f\x7528" },
    { IDL_DYN_COMP_TIME,     L"Time constant (s):", L"\x65f6\x95f4\x5e38\x6570 (s):" },
    { IDL_DYN_COMP_GRAN,     L"Granularity (0-3):", L"\x7c92\x5ea6 (0-3):" },
    { IDL_DYN_COMP_TFRES,    L"TF resolution (0-3):", L"TF \x5206\x8fa8\x7387 (0-3):" },
    { IDL_DYN_COMP_BANDS,    L"Band gains (dB):", L"\x9891\x6bb5\x589e\x76ca (dB):" },
    { IDL_DYN_COMP_BANDGRP,  L"Compressor band gains", L"\x538b\x7f29\x5668\x9891\x6bb5\x589e\x76ca" },
    { IDL_DYN_LIM_GRP,       L"Limiter",         L"\x9650\x5e45\x5668" },
    { IDC_CHK_LIM,           L"Enable",          L"\x542f\x7528" },
    { IDL_DYN_LIM_THRESH,    L"Threshold (dB):", L"\x9608\x503c (dB):" },
    { IDL_DYN_LIM_RELEASE,   L"Release (ms):",   L"\x91ca\x653e\x65f6\x95f4 (ms):" },
    { IDL_DYN_DDC_GRP,       L"DDC",             L"DDC" },
    { IDC_CHK_DDC_ENABLE,    L"Enable",          L"\x542f\x7528" },
    { IDL_DYN_DDC_PROFILE,   L"Profile:",        L"\x914d\x7f6e:" },
    { IDC_BTN_DDC_BROWSE,    L"Browse...",       L"\x6d4f\x89c8..." },
    // Effects tab
    { IDL_FX_TUBE_GRP,       L"Analog Modelling", L"\x7535\x5b50\x7ba1\x6a21\x62df" },
    { IDC_CHK_TUBE_EFFECT,   L"Enable",         L"\x542f\x7528" },
    { IDL_FX_TUBE_DRIVE,     L"Drive (dB):",    L"\x9a71\x52a8 (dB):" },
    { IDL_FX_STEREO_GRP,     L"Stereo Widener", L"\x7acb\x4f53\x58f0\x589e\x5f3a" },
    { IDC_CHK_STEREO_EFFECT, L"Enable",         L"\x542f\x7528" },
    { IDL_FX_STEREO_WIDTH,   L"Width (%):",     L"\x5bbd\x5ea6 (%):" },
    { IDL_FX_BASS_GRP,       L"Bass Boost",     L"\x4f4e\x97f3\x589e\x5f3a" },
    { IDC_CHK_BASS_EFFECT,   L"Enable",         L"\x542f\x7528" },
    { IDL_FX_BASS_BOOST,     L"Boost (dB):",    L"\x589e\x5f3a (dB):" },
    { IDL_FX_REVERB_GRP,     L"Reverb",         L"\x6df7\x54cd" },
    { IDC_CHK_REVERB_EFFECT, L"Enable",         L"\x542f\x7528" },
    { IDL_FX_REVERB_PRESET,  L"Preset:",        L"\x9884\x8bbe:" },
    { IDL_FX_REVERB_WET,     L"Wet (dB)",       L"\x6e7f\x58f0 (dB)" },
    { IDL_FX_REVERB_DRY,     L"Dry (dB)",       L"\x5e72\x58f0 (dB)" },
    { IDL_FX_REVERB_WIDTH,   L"Width %",        L"\x5bbd\x5ea6 %" },
    { IDL_FX_REVERB_RT60,    L"Room (s)",       L"\x6df7\x54cd\x65f6\x95f4 (\x79d2)" },
    { IDL_FX_REVERB_DAMP,    L"Damping",        L"\x963b\x5c3c" },
    { IDL_FX_REVERB_BASS,    L"Bass %",         L"\x4f4e\x97f3 %" },
    { IDL_FX_REVERB_PREDELAY, L"Predelay",      L"\x9884\x5ef6\x65f6" },
    { IDL_FX_REVERB_ER,      L"Early refl.",    L"\x65e9\x671f\x53cd\x5c04" },
    { IDL_FX_BS2B_GRP,       L"BS2B Crossfeed", L"BS2B \x8de8\x9988" },
    { IDC_CHK_BS2B_EFFECT,   L"Enable",         L"\x542f\x7528" },
    { IDL_FX_BS2B_MODE,      L"Mode:",          L"\x6a21\x5f0f:" },
    { IDL_FX_OUTPUT_GRP,     L"Output",         L"\x8f93\x51fa" },
    { IDL_FX_OUTPUT_GAIN,    L"Output gain (dB):", L"\x8f93\x51fa\x589e\x76ca (dB):" },
    // Convolver tab
    { IDC_CHK_CONV_ENABLE,   L"Enable Convolver", L"\x542f\x7528\x5377\x79ef\x5668" },
    { IDL_CV_IR_GRP,         L"Impulse Response", L"\x8109\x51b2\x54cd\x5e94" },
    { IDL_CV_FILE,           L"File:",            L"\x6587\x4ef6:" },
    { IDC_BTN_CONV_BROWSE,   L"Browse...",        L"\x6d4f\x89c8..." },
    // Spectrum tab
    { IDC_CHK_SPECTRUM_ENABLE, L"Enable Spectrum Extender", L"\x542f\x7528\x9891\x8c31\x6269\x5c55" },
    { IDL_SP_GRP,            L"Response File",     L"\x54cd\x5e94\x6587\x4ef6" },
    { IDL_SP_FILE,           L"File:",             L"\x6587\x4ef6:" },
    { IDC_BTN_SPECTRUM_BROWSE, L"Browse...",       L"\x6d4f\x89c8..." },
    // Script tab
    { IDC_CHK_SCRIPT_ENABLE, L"Enable EEL2 Scripting", L"\x542f\x7528 EEL2 \x811a\x672c" },
    { IDL_SC_SCRIPT,         L"Script:",              L"\x811a\x672c:" },
    { IDC_BTN_SCRIPT_LOAD,   L"Load Script...",       L"\x52a0\x8f7d\x811a\x672c..." },
    { IDC_BTN_SCRIPT_SAVE,   L"Save Script...",       L"\x4fdd\x5b58\x811a\x672c..." },
    { IDL_SC_STATUS,         L"Status: Ready",        L"\x72b6\x6001: \x5c31\x7eea" },
};

void JdspConfigDialog::ApplyLanguage() {
    const wchar_t* hint = (m_current_lang == 0)
        ? L"Wave or raw PCM impulse response. The convolver has no gain of its own; use Output gain on the Effects tab."
        : L"\u652f\u6301 WAV / \u539f\u59cb PCM \u8109\u51b2\u54cd\u5e94\u3002\u5377\u79ef\u5668\u672c\u8eab\u65e0\u589e\u76ca\uff0c\u8bf7\u4f7f\u7528\u6548\u679c\u9875\u7684\u8f93\u51fa\u589e\u76ca\u3002";
    const wchar_t* fmt_hint = (m_current_lang == 0)
        ? L"Text file of \"frequency gain\" pairs, e.g. \"100 6\" and \"8000 -12\"."
        : L"\u6587\u672c\u6587\u4ef6\uff0c\u6bcf\u884c\u4e3a\u201c\u9891\u7387 \u589e\u76ca\u201d\uff0c\u4f8b\u5982 \u201c100 6\u201d\u3001\u201c8000 -12\u201d\u3002";

    HWND pages[JDSP_TAB_COUNT + 1];
    pages[0] = m_hwnd;
    for (int i = 0; i < JDSP_TAB_COUNT; i++) pages[i + 1] = m_tab_dialogs[i];

    for (int p = 0; p < JDSP_TAB_COUNT + 1; p++) {
        if (!pages[p]) continue;
        for (size_t i = 0; i < _countof(kUiLabels); i++) {
            HWND c = GetDlgItem(pages[p], kUiLabels[i].id);
            if (c) SetWindowTextW(c, m_current_lang == 0 ? kUiLabels[i].en : kUiLabels[i].zh);
        }
    }
    if (m_tab_dialogs[4]) SetDlgItemTextW(m_tab_dialogs[4], IDL_CV_HINT, hint);
    if (m_tab_dialogs[5]) SetDlgItemTextW(m_tab_dialogs[5], IDL_SP_FORMAT, fmt_hint);
    const wchar_t* rev_hint = (m_current_lang == 1)
        ? L"\x9009\x9884\x8bbe\x4f1a\x91cd\x7f6e\x4e0b\x65b9\x53c2\x6570"
        : L"Preset resets the sliders.";
    if (m_tab_dialogs[3]) SetDlgItemTextW(m_tab_dialogs[3], IDL_FX_REVERB_HINT, rev_hint);
}

JdspConfigDialog::JdspConfigDialog(JdspIpcClient& ipc) : m_ipc(ipc) {
    memset(m_modules, 0, sizeof(m_modules));
    for (int i = 0; i < JDSP_EQ_BANDS; i++) {
        m_eq_bands[i].enabled = true;
        m_eq_bands[i].frequency = kEqDefaultFreqs[i];
        m_eq_bands[i].gain = 0.0f;
        m_eq_bands[i].q = 0.0f;
    }
}

bool JdspConfigDialog::Show(HWND parent) {
    m_live_blob = SerializeSettings();
    m_orig_blob = m_live_blob;
    m_live_dirty = false;
    m_live_pending_full = false;
    m_live_pending_blob.clear();
    m_live_last_send_ms = 0;

    INT_PTR r = DialogBoxParam(GetMyModule(),
                               MAKEINTRESOURCE(IDD_JDSP_CONFIG),
                               parent, DialogProc, reinterpret_cast<LPARAM>(this));

    // The dialog window is gone, so any timer died with it; just drop the state.
    m_live_timer = false;
    m_live_dirty = false;
    m_live_pending_full = false;
    m_live_pending_blob.clear();

    if (r != IDOK) {
        m_live_blob = m_orig_blob;
        JdspSendToActive(m_orig_blob);
    }
    return r == IDOK;
}

INT_PTR CALLBACK JdspConfigDialog::DialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    JdspConfigDialog* dlg = NULL;
    if (msg == WM_INITDIALOG) {
        dlg = reinterpret_cast<JdspConfigDialog*>(lParam);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(dlg));
        dlg->OnInitDialog(hwnd);
        return TRUE;
    }
    dlg = reinterpret_cast<JdspConfigDialog*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    if (!dlg) return FALSE;

    switch (msg) {
        case WM_COMMAND:
            dlg->OnCommand(hwnd, wParam, lParam);
            return TRUE;
        case WM_NOTIFY: {
            NMHDR* nm = reinterpret_cast<NMHDR*>(lParam);
            if (nm->idFrom == IDC_TAB_MAIN && nm->code == TCN_SELCHANGE) {
                dlg->OnNotify(hwnd, wParam, lParam);
                return TRUE;
            }
            return FALSE;
        }
        case WM_HSCROLL:
            dlg->OnHScroll(hwnd, wParam, lParam);
            return TRUE;
        case WM_TIMER:
            if (wParam == IDT_LIVE_PUSH) {
                dlg->FlushLiveNow();
                return TRUE;
            }
            return FALSE;
        case WM_CLOSE:
            EndDialog(hwnd, IDCANCEL);
            return TRUE;
    }
    return FALSE;
}

void JdspConfigDialog::OnInitDialog(HWND hwnd) {
    m_hwnd = hwnd;

    const StrTable* str = (m_current_lang == 0) ? &g_str_en : &g_str_zh;
    SetWindowTextW(hwnd, str->window_title);
    SetDlgItemTextW(hwnd, IDOK, str->btn_ok);
    SetDlgItemTextW(hwnd, IDCANCEL, str->btn_cancel);

    HWND combo = GetDlgItem(hwnd, IDC_COMBO_LANGUAGE);
    SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"English");
    SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"\x4e2d\x6587");
    SendMessageW(combo, CB_SETCURSEL, m_current_lang, 0);

    InitTabs(hwnd);
    CreateTabDialogs(hwnd);

    InitModulesTab(hwnd);
    InitEqTab(hwnd);
    InitDynamicsTab(hwnd);
    InitEffectsTab(hwnd);
    InitConvolverTab(hwnd);
    InitSpectrumTab(hwnd);
    InitScriptTab(hwnd);

    ApplyLanguage();
    ShowTab(0);
}

void JdspConfigDialog::InitTabs(HWND hwnd) {
    HWND tab = GetDlgItem(hwnd, IDC_TAB_MAIN);
    const wchar_t** names = (m_current_lang == 0) ? kTabNamesEn : kTabNamesZh;

    for (int i = 0; i < JDSP_TAB_COUNT; i++) {
        TCITEMW tie = {};
        tie.mask = TCIF_TEXT;
        tie.pszText = (LPWSTR)names[i];
        TabCtrl_InsertItem(tab, i, &tie);
    }
}

void JdspConfigDialog::CreateTabDialogs(HWND hwnd) {
    HWND tab = GetDlgItem(hwnd, IDC_TAB_MAIN);
    RECT rc;
    GetClientRect(tab, &rc);
    TabCtrl_AdjustRect(tab, FALSE, &rc);

    POINT pt = { rc.left, rc.top };
    ClientToScreen(tab, &pt);
    ScreenToClient(hwnd, &pt);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;

    for (int i = 0; i < JDSP_TAB_COUNT; i++) {
        m_tab_dialogs[i] = CreateDialogParam(GetMyModule(),
            MAKEINTRESOURCE(kTabIds[i]), hwnd, TabPageDlgProc, reinterpret_cast<LPARAM>(this));
        if (m_tab_dialogs[i]) {
            SetWindowPos(m_tab_dialogs[i], HWND_TOP, pt.x, pt.y, w, h, 0);
        }
    }
}

void JdspConfigDialog::HandleTabCommand(WPARAM wParam, LPARAM lParam) {
    OnCommand(m_hwnd, wParam, lParam);
}

void JdspConfigDialog::HandleTabScroll(WPARAM wParam, LPARAM lParam) {
    OnHScroll(m_hwnd, wParam, lParam);
}

void JdspConfigDialog::HandleTabNotify(WPARAM wParam, LPARAM lParam) {
    OnNotify(m_hwnd, wParam, lParam);
}

void JdspConfigDialog::ShowTab(int tab) {
    for (int i = 0; i < JDSP_TAB_COUNT; i++) {
        if (m_tab_dialogs[i]) {
            if (i == tab) {
                SetWindowPos(m_tab_dialogs[i], HWND_TOP, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
            } else {
                ShowWindow(m_tab_dialogs[i], SW_HIDE);
            }
        }
    }
    m_current_tab = tab;
}

// ===== file pickers =====

static bool PickFile(HWND owner, const wchar_t* filter, const wchar_t* defext, wchar_t* path) {
    path[0] = L'\0';
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = defext;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameW(&ofn) != FALSE;
}

static bool ReadTextFileUtf8(const wchar_t* path, wchar_t* out, int out_chars) {
    FILE* f = NULL;
    if (_wfopen_s(&f, path, L"rb") != 0 || !f) return false;
    std::string data;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) data.append(buf, n);
    fclose(f);
    if (data.empty()) { out[0] = L'\0'; return true; }
    int w = MultiByteToWideChar(CP_UTF8, 0, data.c_str(), (int)data.size(), out, out_chars - 1);
    if (w < 0) w = 0;
    out[w] = L'\0';
    return true;
}

static bool WriteTextFileUtf8(const wchar_t* path, const wchar_t* text) {
    int need = WideCharToMultiByte(CP_UTF8, 0, text, -1, NULL, 0, NULL, NULL);
    if (need <= 1) return false;
    std::string data;
    data.resize(need - 1);
    WideCharToMultiByte(CP_UTF8, 0, text, -1, &data[0], need, NULL, NULL);
    FILE* f = NULL;
    if (_wfopen_s(&f, path, L"wb") != 0 || !f) return false;
    fwrite(data.data(), 1, data.size(), f);
    fclose(f);
    return true;
}

void JdspConfigDialog::OnCommand(HWND hwnd, WPARAM wParam, LPARAM lParam) {
    WORD id = LOWORD(wParam);
    WORD code = HIWORD(wParam);

    if (id == IDOK) {
        OnApply(hwnd);
        EndDialog(hwnd, IDOK);
        return;
    }
    if (id == IDCANCEL) {
        EndDialog(hwnd, IDCANCEL);
        return;
    }
    if (id == IDC_COMBO_LANGUAGE && code == CBN_SELCHANGE) {
        OnLanguageChange(hwnd);
    } else if (id == IDC_COMBO_EQ_BAND && code == CBN_SELCHANGE) {
        HWND tab = m_tab_dialogs[1];
        if (!tab) { PushLive(false); return; }
        ApplyEqTab(hwnd);
        HWND combo = GetDlgItem(tab, IDC_COMBO_EQ_BAND);
        int sel = (int)SendMessageW(combo, CB_GETCURSEL, 0, 0);
        if (sel >= 0 && sel < JDSP_EQ_BANDS) {
            m_eq_widget.SetSelectedBand(sel);
            const EqBand& b = m_eq_bands[sel];
            wchar_t buf[32];
            HWND freq_e = GetDlgItem(tab, IDC_EDIT_EQ_FREQ);
            HWND gain_e = GetDlgItem(tab, IDC_EDIT_EQ_GAIN);
            if (freq_e) { swprintf_s(buf, L"%.0f", b.frequency); SetWindowTextW(freq_e, buf); }
            if (gain_e) { swprintf_s(buf, L"%.1f", b.gain); SetWindowTextW(gain_e, buf); }
        }
    } else if (id == IDC_COMBO_REVERB_PRESET && code == CBN_SELCHANGE) {
        // Picking a preset resets the detail sliders to that preset's values.
        ApplyEffectsTab(hwnd);
        const JdspReverbParams& p = kJdspReverbPresets[m_reverb_preset];
        m_reverb_wet = p.wet;
        m_reverb_dry = p.dry;
        m_reverb_width = p.width;
        m_reverb_rt60 = p.rt60;
        m_reverb_damp = p.damplpf;
        m_reverb_bass = p.bassb;
        m_reverb_predelay = p.delay;
        m_reverb_er = p.ertolate;
        InitEffectsTab(hwnd);
    } else if (id == IDC_BTN_SAVE_CONFIG && code == BN_CLICKED) {
        SyncFromControls(hwnd);
        wchar_t path[MAX_PATH] = {};
        OPENFILENAMEW ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hwnd;
        ofn.lpstrFilter = L"JamesDSP Config (*.jdsp)\0*.jdsp\0All Files (*.*)\0*.*\0";
        ofn.lpstrFile = path;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrDefExt = L"jdsp";
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
        if (GetSaveFileNameW(&ofn)) {
            JdspConfig::SaveToFile(path, *this);
        }
    } else if (id == IDC_BTN_LOAD_CONFIG && code == BN_CLICKED) {
        wchar_t path[MAX_PATH] = {};
        OPENFILENAMEW ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hwnd;
        ofn.lpstrFilter = L"JamesDSP Config (*.jdsp)\0*.jdsp\0All Files (*.*)\0*.*\0";
        ofn.lpstrFile = path;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrDefExt = L"jdsp";
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
        if (GetOpenFileNameW(&ofn)) {
            JdspConfig::LoadFromFile(path, *this);
            HWND lang = GetDlgItem(hwnd, IDC_COMBO_LANGUAGE);
            if (lang) SendMessageW(lang, CB_SETCURSEL, m_current_lang, 0);
            HWND tabc = GetDlgItem(hwnd, IDC_TAB_MAIN);
            int saved = m_current_tab;
            TabCtrl_DeleteAllItems(tabc);
            InitTabs(hwnd);
            ShowTab(saved);
            ApplyLanguage();
            InitModulesTab(hwnd);
            InitEqTab(hwnd);
            InitDynamicsTab(hwnd);
            InitEffectsTab(hwnd);
            InitConvolverTab(hwnd);
            InitSpectrumTab(hwnd);
            InitScriptTab(hwnd);
        }
    } else if (id == IDC_BTN_RESET_ALL && code == BN_CLICKED) {
        memset(m_modules, 0, sizeof(m_modules));
        for (int i = 0; i < JDSP_EQ_BANDS; i++) {
            m_eq_bands[i].enabled = true;
            m_eq_bands[i].frequency = kEqDefaultFreqs[i];
            m_eq_bands[i].gain = 0.0f;
        }
        m_eq_filter_type = 0;
        m_eq_interpolation = 0;
        m_comp_time = 0.1; m_comp_granularity = 1; m_comp_tfresolution = 2;
        for (int i = 0; i < JDSP_COMP_BANDS; i++) m_comp_band_gain[i] = 0.0;
        m_lim_threshold = -1.0; m_lim_release = 50.0;
        m_tube_drive_db = 3.0; m_bs2b_mode = 0; m_bass_boost = 6.0;
        m_stereo_width = 50.0; m_reverb_preset = 0; m_output_gain = 0.0;
        {
            const JdspReverbParams& p = kJdspReverbPresets[0];
            m_reverb_wet = p.wet;
            m_reverb_dry = p.dry;
            m_reverb_width = p.width;
            m_reverb_rt60 = p.rt60;
            m_reverb_damp = p.damplpf;
            m_reverb_bass = p.bassb;
            m_reverb_predelay = p.delay;
            m_reverb_er = p.ertolate;
        }
        m_ir_path[0] = L'\0'; m_ddc_profile[0] = L'\0'; m_spectrum_path[0] = L'\0';
        m_script_text[0] = L'\0';
        CfgLog("ResetAll: resetting every parameter to its default");
        SetDlgItemTextW(m_tab_dialogs[4], IDC_EDIT_CONV_IR, L"");
        SetDlgItemTextW(m_tab_dialogs[2], IDC_EDIT_DDC_PROFILE, L"");
        SetDlgItemTextW(m_tab_dialogs[5], IDC_EDIT_SPECTRUM_FILE, L"");
        SetDlgItemTextW(m_tab_dialogs[6], IDC_EDIT_SCRIPT, L"");
        InitModulesTab(hwnd);
        InitEqTab(hwnd);
        InitDynamicsTab(hwnd);
        InitEffectsTab(hwnd);
        InitConvolverTab(hwnd);
        InitSpectrumTab(hwnd);
        InitScriptTab(hwnd);
    } else if (id == IDC_BTN_EQ_RESET && code == BN_CLICKED) {
        for (int i = 0; i < JDSP_EQ_BANDS; i++) {
            m_eq_bands[i].enabled = true;
            m_eq_bands[i].gain = 0.0f;
        }
        m_eq_widget.SetBands(m_eq_bands);
        m_eq_widget.Refresh();
        HWND tab = m_tab_dialogs[1];
        if (tab) {
            for (int i = 0; i < JDSP_EQ_BANDS; i++) UpdateEqBandLabel(tab, i);
            int sel = m_eq_widget.GetSelectedBand();
            if (sel < 0 || sel >= JDSP_EQ_BANDS) sel = 0;
            const EqBand& b = m_eq_bands[sel];
            wchar_t buf[32];
            HWND freq_e = GetDlgItem(tab, IDC_EDIT_EQ_FREQ);
            HWND gain_e = GetDlgItem(tab, IDC_EDIT_EQ_GAIN);
            if (freq_e) { swprintf_s(buf, L"%.0f", b.frequency); SetTextIfChanged(freq_e, buf); }
            if (gain_e) { swprintf_s(buf, L"%.1f", b.gain); SetTextIfChanged(gain_e, buf); }
        }
    } else if ((id == IDC_EDIT_EQ_FREQ || id == IDC_EDIT_EQ_GAIN) && code == EN_KILLFOCUS) {
        HWND tab = m_tab_dialogs[1];
        if (tab) {
            ApplyEqTab(tab);
            m_eq_widget.SetBands(m_eq_bands);
        }
    } else if (id == IDC_BTN_CONV_BROWSE && code == BN_CLICKED) {
        wchar_t path[MAX_PATH];
        if (PickFile(hwnd, L"Impulse Response (*.wav)\0*.wav\0All Files (*.*)\0*.*\0", L"wav", path)) {
            wcscpy_s(m_ir_path, path);
            SetDlgItemTextW(m_tab_dialogs[4], IDC_EDIT_CONV_IR, path);
        }
    } else if (id == IDC_BTN_DDC_BROWSE && code == BN_CLICKED) {
        wchar_t path[MAX_PATH];
        if (PickFile(hwnd, L"DDC Profile (*.vdc)\0*.vdc\0All Files (*.*)\0*.*\0", L"vdc", path)) {
            wcscpy_s(m_ddc_profile, path);
            SetDlgItemTextW(m_tab_dialogs[2], IDC_EDIT_DDC_PROFILE, path);
        }
    } else if (id == IDC_BTN_SPECTRUM_BROWSE && code == BN_CLICKED) {
        wchar_t path[MAX_PATH];
        if (PickFile(hwnd, L"Response File (*.txt)\0*.txt\0All Files (*.*)\0*.*\0", L"txt", path)) {
            wcscpy_s(m_spectrum_path, path);
            SetDlgItemTextW(m_tab_dialogs[5], IDC_EDIT_SPECTRUM_FILE, path);
        }
    } else if (id == IDC_BTN_SCRIPT_LOAD && code == BN_CLICKED) {
        wchar_t path[MAX_PATH];
        if (PickFile(hwnd, L"EEL2 Script (*.eel)\0*.eel\0Text (*.txt)\0*.txt\0All Files (*.*)\0*.*\0", L"eel", path)) {
            if (ReadTextFileUtf8(path, m_script_text, _countof(m_script_text))) {
                SetDlgItemTextW(m_tab_dialogs[6], IDC_EDIT_SCRIPT, m_script_text);
            }
        }
    } else if (id == IDC_BTN_SCRIPT_SAVE && code == BN_CLICKED) {
        wchar_t text[8192] = {};
        GetDlgItemTextW(m_tab_dialogs[6], IDC_EDIT_SCRIPT, text, _countof(text));
        wchar_t path[MAX_PATH] = {};
        OPENFILENAMEW ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hwnd;
        ofn.lpstrFilter = L"EEL2 Script (*.eel)\0*.eel\0Text (*.txt)\0*.txt\0All Files (*.*)\0*.*\0";
        ofn.lpstrFile = path;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrDefExt = L"eel";
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
        if (GetSaveFileNameW(&ofn)) WriteTextFileUtf8(path, text);
    } else {
        // "Enable" checkboxes on the per-module tabs are aliases for the module flags.
        for (int i = 0; i < _countof(kModAliases); i++) {
            if (id == kModAliases[i].alias_id && code == BN_CLICKED) {
                bool v = (Button_GetCheck(reinterpret_cast<HWND>(lParam)) == BST_CHECKED);
                m_modules[kModAliases[i].mod_index] = v;
                HWND mt = GetDlgItem(m_tab_dialogs[0], kModuleCheckboxes[kModAliases[i].mod_index]);
                if (mt) Button_SetCheck(mt, v ? BST_CHECKED : BST_UNCHECKED);
                break;
            }
        }
    }

    // The Modules tab and the per-tab "Enable" checkboxes are two views of the same
    // flags. Re-read the Modules tab, then mirror the result back into every
    // checkbox so switching tabs can never show a stale state.
    ApplyModulesTab(m_hwnd);
    SyncModuleCheckboxes();

    // Text edits commit on EN_KILLFOCUS. Pushing on the intermediate EN_CHANGE /
    // EN_UPDATE notifications would clamp the value while the user is still typing.
    if ((id == IDC_EDIT_EQ_FREQ || id == IDC_EDIT_EQ_GAIN ||
         id == IDC_EDIT_DDC_PROFILE || id == IDC_EDIT_SCRIPT) &&
        (code == EN_CHANGE || code == EN_UPDATE)) {
        return;
    }

    PushLive(false);
}

void JdspConfigDialog::OnNotify(HWND hwnd, WPARAM wParam, LPARAM lParam) {
    NMHDR* nmhdr = reinterpret_cast<NMHDR*>(lParam);
    if (nmhdr->idFrom == IDC_TAB_MAIN && nmhdr->code == TCN_SELCHANGE) {
        int sel = TabCtrl_GetCurSel(GetDlgItem(hwnd, IDC_TAB_MAIN));
        if (sel >= 0 && sel < JDSP_TAB_COUNT) {
            ShowTab(sel);
        }
    } else if (nmhdr->idFrom == IDC_EQ_CURVE && nmhdr->code == NM_CLICK) {
        EqBand bands[JDSP_EQ_BANDS];
        m_eq_widget.GetBands(bands);
        memcpy(m_eq_bands, bands, sizeof(m_eq_bands));
        HWND tab = m_tab_dialogs[1];
        if (tab) {
            int sel = m_eq_widget.GetSelectedBand();
            for (int i = 0; i < JDSP_EQ_BANDS; i++) UpdateEqBandLabel(tab, i);
            HWND combo = GetDlgItem(tab, IDC_COMBO_EQ_BAND);
            if (combo) SendMessageW(combo, CB_SETCURSEL, sel, 0);
            if (sel >= 0 && sel < JDSP_EQ_BANDS) {
                const EqBand& b = m_eq_bands[sel];
                wchar_t buf[32];
                HWND freq_e = GetDlgItem(tab, IDC_EDIT_EQ_FREQ);
                HWND gain_e = GetDlgItem(tab, IDC_EDIT_EQ_GAIN);
                if (freq_e) { swprintf_s(buf, L"%.0f", b.frequency); SetTextIfChanged(freq_e, buf); }
                if (gain_e) { swprintf_s(buf, L"%.1f", b.gain); SetTextIfChanged(gain_e, buf); }
            }
        }
    }

    PushLive(false);
}

void JdspConfigDialog::UpdateSliderLabel(HWND tab, int slider_id, int label_id, const wchar_t* fmt, float val) {
    HWND sl = GetDlgItem(tab, slider_id);
    HWND lbl = GetDlgItem(tab, label_id);
    if (sl && lbl) {
        wchar_t buf[64];
        swprintf_s(buf, fmt, val);
        SetWindowTextW(lbl, buf);
    }
}

void JdspConfigDialog::OnHScroll(HWND hwnd, WPARAM wParam, LPARAM lParam) {
    HWND slider = reinterpret_cast<HWND>(lParam);
    int pos = (int)SendMessageW(slider, TBM_GETPOS, 0, 0);
    HWND tab = GetParent(slider);
    if (!tab) return;

    int id = GetDlgCtrlID(slider);

    if (id == IDC_SLIDER_COMP_TIME) UpdateSliderLabel(tab, id, IDC_STATIC_COMP_TIME, L"%.2f s", pos / 100.0f);
    else if (id == IDC_SLIDER_COMP_GRAN) UpdateSliderLabel(tab, id, IDC_STATIC_COMP_GRAN, L"%.0f", (float)pos);
    else if (id == IDC_SLIDER_COMP_TFRES) UpdateSliderLabel(tab, id, IDC_STATIC_COMP_TFRES, L"%.0f", (float)pos);
    else if (id == IDC_SLIDER_LIM_THRESH) UpdateSliderLabel(tab, id, IDC_STATIC_LIM_THRESH, L"%.1f dB", pos / 10.0f);
    else if (id == IDC_SLIDER_LIM_RELEASE) UpdateSliderLabel(tab, id, IDC_STATIC_LIM_RELEASE, L"%.0f ms", (float)pos);
    else if (id == IDC_SLIDER_TUBE_DRIVE) UpdateSliderLabel(tab, id, IDC_STATIC_TUBE_DRIVE, L"%.1f dB", pos / 10.0f);
    else if (id == IDC_SLIDER_BASS_BOOST) UpdateSliderLabel(tab, id, IDC_STATIC_BASS_BOOST, L"%.1f dB", pos / 10.0f);
    else if (id == IDC_SLIDER_STEREO_WIDTH) UpdateSliderLabel(tab, id, IDC_STATIC_STEREO_WIDTH, L"%.0f%%", (float)pos);
    else if (id == IDC_SLIDER_OUTPUT_GAIN) UpdateSliderLabel(tab, id, IDC_STATIC_OUTPUT_GAIN, L"%.1f dB", pos / 10.0f);
    else if (id == IDC_SLIDER_REVERB_WET) UpdateSliderLabel(tab, id, IDC_STATIC_REVERB_WET, L"%.1f dB", pos / 10.0f);
    else if (id == IDC_SLIDER_REVERB_DRY) UpdateSliderLabel(tab, id, IDC_STATIC_REVERB_DRY, L"%.1f dB", pos / 10.0f);
    else if (id == IDC_SLIDER_REVERB_WIDTH) UpdateSliderLabel(tab, id, IDC_STATIC_REVERB_WIDTH, L"%.0f%%", (float)pos);
    else if (id == IDC_SLIDER_REVERB_RT60) UpdateSliderLabel(tab, id, IDC_STATIC_REVERB_RT60, L"%.1f s", pos / 10.0f);
    else if (id == IDC_SLIDER_REVERB_DAMP) UpdateSliderLabel(tab, id, IDC_STATIC_REVERB_DAMP, L"%.0f Hz", (float)pos);
    else if (id == IDC_SLIDER_REVERB_BASS) UpdateSliderLabel(tab, id, IDC_STATIC_REVERB_BASS, L"%.0f%%", (float)pos);
    else if (id == IDC_SLIDER_REVERB_PREDELAY) UpdateSliderLabel(tab, id, IDC_STATIC_REVERB_PREDELAY, L"%.0f ms", (float)pos);
    else if (id == IDC_SLIDER_REVERB_ER) UpdateSliderLabel(tab, id, IDC_STATIC_REVERB_ER, L"%.0f%%", (float)pos);
    else if (id >= IDC_SLIDER_COMP_BAND0 && id < IDC_SLIDER_COMP_BAND0 + JDSP_COMP_BANDS) {
        UpdateCompBandLabel(tab, id - IDC_SLIDER_COMP_BAND0);
    }
    else if (id >= IDC_SLIDER_EQ_BAND0 && id <= IDC_SLIDER_EQ_BAND_LAST) {
        int band = id - IDC_SLIDER_EQ_BAND0;
        float g = -(float)pos / 10.0f;
        m_eq_bands[band].gain = g;
        HWND vl = GetDlgItem(tab, IDC_STATIC_EQ_BANDVAL0 + band);
        if (vl) {
            wchar_t buf[32];
            swprintf_s(buf, L"%+.1f", g);
            SetWindowTextW(vl, buf);
        }
        EqBand bands[JDSP_EQ_BANDS];
        m_eq_widget.GetBands(bands);
        bands[band].gain = g;
        m_eq_widget.SetBands(bands);
    }

    PushLive(false);
}

// ===== Settings (de)serialization =====

static const char* kModuleKeys[kDlgModCount] = {
    "modules.analog", "modules.bs2b", "modules.ddc", "modules.limiter",
    "modules.compressor", "modules.convolver", "modules.reverb", "modules.bassboost",
    "modules.stereo", "modules.iir", "modules.spectrum", "modules.eel2"
};

static std::string EscapeValue(const std::string& v) {
    std::string o;
    o.reserve(v.size());
    for (size_t i = 0; i < v.size(); i++) {
        char c = v[i];
        if (c == '\\') o += "\\\\";
        else if (c == '\n') o += "\\n";
        else o += c;
    }
    return o;
}

static std::string UnescapeValue(const std::string& v) {
    std::string o;
    o.reserve(v.size());
    for (size_t i = 0; i < v.size(); i++) {
        if (v[i] == '\\' && i + 1 < v.size()) {
            char n = v[++i];
            if (n == 'n') o += '\n';
            else o += n;
        } else {
            o += v[i];
        }
    }
    return o;
}

static void SplitBlob(const std::string& blob, std::map<std::string, std::string>& out) {
    size_t pos = 0;
    while (pos < blob.size()) {
        size_t eol = blob.find('\n', pos);
        if (eol == std::string::npos) eol = blob.size();
        std::string line = blob.substr(pos, eol - pos);
        pos = eol + 1;
        if (line.empty()) continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        out[line.substr(0, eq)] = line.substr(eq + 1);
    }
}

// Only the keys whose value changed, plus "key=" for keys that disappeared.
// Sending the whole blob on every slider tick would make the host recompile the
// EEL2 script and rebuild the IR/DDC convolutions thousands of times per drag.
static std::string DiffBlobs(const std::string& old_blob, const std::string& new_blob) {
    std::map<std::string, std::string> old_kv, new_kv;
    SplitBlob(old_blob, old_kv);
    SplitBlob(new_blob, new_kv);

    std::string out;
    size_t pos = 0;
    while (pos < new_blob.size()) {
        size_t eol = new_blob.find('\n', pos);
        if (eol == std::string::npos) eol = new_blob.size();
        std::string line = new_blob.substr(pos, eol - pos);
        pos = eol + 1;
        if (line.empty()) continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::map<std::string, std::string>::const_iterator it = old_kv.find(line.substr(0, eq));
        if (it == old_kv.end() || it->second != line.substr(eq + 1)) {
            out += line;
            out += '\n';
        }
    }

    for (std::map<std::string, std::string>::const_iterator it = old_kv.begin();
         it != old_kv.end(); ++it) {
        if (new_kv.find(it->first) == new_kv.end()) {
            out += it->first;
            out += "=\n";
        }
    }
    return out;
}

void JdspConfigDialog::SyncFromControls(HWND hwnd) {
    (void)hwnd;
    ApplyModulesTab(m_hwnd);
    ApplyEqTab(m_hwnd);
    ApplyDynamicsTab(m_hwnd);
    ApplyEffectsTab(m_hwnd);
    ApplyConvolverTab(m_hwnd);
    ApplySpectrumTab(m_hwnd);
    ApplyScriptTab(m_hwnd);

    HWND tab = m_tab_dialogs[2];
    if (tab) {
        HWND e = GetDlgItem(tab, IDC_EDIT_DDC_PROFILE);
        if (e) GetWindowTextW(e, m_ddc_profile, MAX_PATH);
    }
}

void JdspConfigDialog::PushLive(bool full) {
    if (!m_hwnd) return;
    // SyncFromControls writes clamped values back into the edit controls, which
    // notifies us again. Without this guard that feedback loop recurses until the
    // stack overflows.
    if (m_in_live_push) return;
    m_in_live_push = true;

    SyncFromControls(m_hwnd);
    m_live_pending_blob = SerializeSettings();
    m_live_pending_full = m_live_pending_full || full;
    m_live_dirty = true;
    MaybeFlushLive();

    m_in_live_push = false;
}

void JdspConfigDialog::MaybeFlushLive() {
    if (!m_live_dirty) return;
    ULONGLONG now = GetTickCount64();
    if (now - m_live_last_send_ms < JDSP_LIVE_PUSH_MS) {
        // Too soon: coalesce. Dragging a slider would otherwise send a frame per
        // WM_HSCROLL and swamp the host, which stalls audio.
        if (!m_live_timer) {
            SetTimer(m_hwnd, IDT_LIVE_PUSH, JDSP_LIVE_PUSH_MS, NULL);
            m_live_timer = true;
        }
        return;
    }
    FlushLiveNow();
}

void JdspConfigDialog::FlushLiveNow() {
    if (m_live_timer) {
        KillTimer(m_hwnd, IDT_LIVE_PUSH);
        m_live_timer = false;
    }
    if (!m_live_dirty) return;

    std::string payload;
    if (m_live_pending_full) {
        payload = m_live_pending_blob;
    } else {
        payload = DiffBlobs(m_live_blob, m_live_pending_blob);
    }
    m_live_blob = m_live_pending_blob;
    m_live_pending_full = false;
    m_live_dirty = false;
    m_live_last_send_ms = GetTickCount64();

    if (!payload.empty()) {
        bool sent = JdspSendToActive(payload);
        static int s_live_n = 0;
        if (s_live_n < 200) {
            char b[128];
            sprintf_s(b, "live: n=%d bytes=%u active=%d", s_live_n, (unsigned)payload.size(),
                      (int)sent);
            CfgLog(b);
            s_live_n++;
        }
    }
}

std::string JdspConfigDialog::SerializeSettings() const {
    std::string s;
    char buf[64];
    auto kv = [&](const char* k, const char* v) { s += k; s += '='; s += v; s += '\n'; };
    auto kvi = [&](const std::string& k, const char* v) { s += k; s += '='; s += v; s += '\n'; };

    for (int i = 0; i < kDlgModCount; i++) kv(kModuleKeys[i], m_modules[i] ? "1" : "0");

    for (int i = 0; i < JDSP_EQ_BANDS; i++) {
        char kk[32];
        sprintf_s(kk, "eq.band%d", i);
        std::string base(kk);
        sprintf_s(buf, "%.3f", m_eq_bands[i].frequency); kvi(base + ".freq", buf);
        sprintf_s(buf, "%.3f", m_eq_bands[i].gain);      kvi(base + ".gain", buf);
    }
    sprintf_s(buf, "%d", m_eq_filter_type);   kv("eq.filtertype", buf);
    sprintf_s(buf, "%d", m_eq_interpolation); kv("eq.interpolation", buf);

    sprintf_s(buf, "%.3f", m_comp_time);       kv("compressor.timeconstant", buf);
    sprintf_s(buf, "%d", m_comp_granularity);  kv("compressor.granularity", buf);
    sprintf_s(buf, "%d", m_comp_tfresolution); kv("compressor.tfresolution", buf);
    for (int i = 0; i < JDSP_COMP_BANDS; i++) {
        char kk[40];
        sprintf_s(kk, "compressor.band%d.gain", i);
        sprintf_s(buf, "%.3f", m_comp_band_gain[i]);
        kv(kk, buf);
    }

    sprintf_s(buf, "%.3f", m_lim_threshold); kv("limiter.threshold", buf);
    sprintf_s(buf, "%.3f", m_lim_release);   kv("limiter.release", buf);
    sprintf_s(buf, "%.3f", m_tube_drive_db); kv("tube.drive", buf);
    sprintf_s(buf, "%d", m_bs2b_mode);       kv("bs2b.mode", buf);
    sprintf_s(buf, "%.3f", m_bass_boost);    kv("bassboost.gain", buf);
    sprintf_s(buf, "%.3f", m_stereo_width);  kv("stereo.width", buf);
    sprintf_s(buf, "%d", m_reverb_preset);   kv("reverb.preset", buf);
    sprintf_s(buf, "%.3f", m_reverb_wet);      kv("reverb.wet", buf);
    sprintf_s(buf, "%.3f", m_reverb_dry);      kv("reverb.dry", buf);
    sprintf_s(buf, "%.4f", m_reverb_width);    kv("reverb.width", buf);
    sprintf_s(buf, "%.3f", m_reverb_rt60);     kv("reverb.rt60", buf);
    sprintf_s(buf, "%.1f", m_reverb_damp);     kv("reverb.damping", buf);
    sprintf_s(buf, "%.4f", m_reverb_bass);     kv("reverb.bassboost", buf);
    sprintf_s(buf, "%.4f", m_reverb_predelay); kv("reverb.predelay", buf);
    sprintf_s(buf, "%.4f", m_reverb_er);       kv("reverb.er", buf);
    sprintf_s(buf, "%.3f", m_output_gain);   kv("output.gain", buf);
    sprintf_s(buf, "%d", m_current_lang);    kv("ui.language", buf);

    char u8[8192 * 3];
    if (m_ir_path[0]) {
        WideCharToMultiByte(CP_UTF8, 0, m_ir_path, -1, u8, sizeof(u8), NULL, NULL);
        kvi("convolver.path", EscapeValue(u8).c_str());
    }
    if (m_ddc_profile[0]) {
        WideCharToMultiByte(CP_UTF8, 0, m_ddc_profile, -1, u8, sizeof(u8), NULL, NULL);
        kvi("ddc.profile", EscapeValue(u8).c_str());
    }
    if (m_spectrum_path[0]) {
        WideCharToMultiByte(CP_UTF8, 0, m_spectrum_path, -1, u8, sizeof(u8), NULL, NULL);
        kvi("spectrum.path", EscapeValue(u8).c_str());
    }
    if (m_script_text[0]) {
        WideCharToMultiByte(CP_UTF8, 0, m_script_text, -1, u8, sizeof(u8), NULL, NULL);
        kvi("script.text", EscapeValue(u8).c_str());
    }

    return s;
}

void JdspConfigDialog::DeserializeSettings(const std::string& blob) {
    size_t pos = 0;
    while (pos < blob.size()) {
        size_t eol = blob.find('\n', pos);
        if (eol == std::string::npos) eol = blob.size();
        std::string line = blob.substr(pos, eol - pos);
        pos = eol + 1;
        if (line.empty()) continue;

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq);
        std::string v = UnescapeValue(line.substr(eq + 1));

        bool handled = false;
        for (int i = 0; i < kDlgModCount; i++) {
            if (k == kModuleKeys[i]) { m_modules[i] = (v == "1" || v == "true"); handled = true; break; }
        }
        if (handled) continue;

        if (k.compare(0, 8, "eq.band") == 0) {
            int band = atoi(k.c_str() + 8);
            if (band >= 0 && band < JDSP_EQ_BANDS) {
                size_t dot = k.rfind('.');
                std::string p = (dot == std::string::npos) ? std::string() : k.substr(dot + 1);
                if (p == "freq") m_eq_bands[band].frequency = (float)atof(v.c_str());
                else if (p == "gain") m_eq_bands[band].gain = (float)atof(v.c_str());
                else if (p == "enable") m_eq_bands[band].enabled = (v == "1" || v == "true");
            }
        }
        else if (k.compare(0, 15, "compressor.band") == 0) {
            int band = atoi(k.c_str() + 15);
            if (band >= 0 && band < JDSP_COMP_BANDS) m_comp_band_gain[band] = atof(v.c_str());
        }
        else if (k == "eq.filtertype")           { m_eq_filter_type = atoi(v.c_str()); if (m_eq_filter_type < 0 || m_eq_filter_type > 5) m_eq_filter_type = 0; }
        else if (k == "eq.interpolation")        { m_eq_interpolation = atoi(v.c_str()); if (m_eq_interpolation < 0 || m_eq_interpolation > 1) m_eq_interpolation = 0; }
        else if (k == "compressor.timeconstant") m_comp_time = atof(v.c_str());
        else if (k == "compressor.granularity")  { m_comp_granularity = atoi(v.c_str()); if (m_comp_granularity < 0 || m_comp_granularity > 3) m_comp_granularity = 1; }
        else if (k == "compressor.tfresolution") { m_comp_tfresolution = atoi(v.c_str()); if (m_comp_tfresolution < 0 || m_comp_tfresolution > 3) m_comp_tfresolution = 2; }
        else if (k == "limiter.threshold")       m_lim_threshold = atof(v.c_str());
        else if (k == "limiter.release")         m_lim_release = atof(v.c_str());
        else if (k == "tube.drive")              m_tube_drive_db = atof(v.c_str());
        else if (k == "bs2b.mode")               { m_bs2b_mode = atoi(v.c_str()); if (m_bs2b_mode < 0 || m_bs2b_mode > 5) m_bs2b_mode = 0; }
        else if (k == "bassboost.gain")          m_bass_boost = atof(v.c_str());
        else if (k == "stereo.width")            m_stereo_width = atof(v.c_str());
        else if (k == "reverb.preset")           { m_reverb_preset = atoi(v.c_str()); if (m_reverb_preset < 0 || m_reverb_preset > 18) m_reverb_preset = 0; }
    else if (k == "reverb.wet")              m_reverb_wet = atof(v.c_str());
    else if (k == "reverb.dry")              m_reverb_dry = atof(v.c_str());
    else if (k == "reverb.width")            m_reverb_width = atof(v.c_str());
    else if (k == "reverb.rt60")             m_reverb_rt60 = atof(v.c_str());
    else if (k == "reverb.damping")          m_reverb_damp = atof(v.c_str());
    else if (k == "reverb.bassboost")        m_reverb_bass = atof(v.c_str());
    else if (k == "reverb.predelay")         m_reverb_predelay = atof(v.c_str());
    else if (k == "reverb.er")               m_reverb_er = atof(v.c_str());
        else if (k == "output.gain")             m_output_gain = atof(v.c_str());
        else if (k == "ui.language") {
            m_current_lang = atoi(v.c_str());
            if (m_current_lang < 0 || m_current_lang > 1) m_current_lang = 0;
        }
        else if (k == "convolver.path") {
            MultiByteToWideChar(CP_UTF8, 0, v.c_str(), -1, m_ir_path, MAX_PATH);
        }
        else if (k == "ddc.profile") {
            MultiByteToWideChar(CP_UTF8, 0, v.c_str(), -1, m_ddc_profile, MAX_PATH);
        }
        else if (k == "spectrum.path") {
            MultiByteToWideChar(CP_UTF8, 0, v.c_str(), -1, m_spectrum_path, MAX_PATH);
        }
        else if (k == "script.text") {
            MultiByteToWideChar(CP_UTF8, 0, v.c_str(), -1, m_script_text, _countof(m_script_text));
        }
    }
}

void JdspConfigDialog::OnApply(HWND hwnd) {
    (void)hwnd;
    SyncFromControls(m_hwnd);
    m_settings_blob = SerializeSettings();
    // Full push so the running host is guaranteed to match the preset we write.
    m_live_blob = m_settings_blob;
    JdspSendToActive(m_settings_blob);
}

void JdspConfigDialog::OnLanguageChange(HWND hwnd) {
    HWND combo = GetDlgItem(hwnd, IDC_COMBO_LANGUAGE);
    int sel = (int)SendMessageW(combo, CB_GETCURSEL, 0, 0);
    SwitchLanguage(sel);
}

void JdspConfigDialog::SwitchLanguage(int lang_id) {
    m_current_lang = lang_id;
    const StrTable* str = (lang_id == 0) ? &g_str_en : &g_str_zh;

    SetWindowTextW(m_hwnd, str->window_title);
    SetDlgItemTextW(m_hwnd, IDOK, str->btn_ok);
    SetDlgItemTextW(m_hwnd, IDCANCEL, str->btn_cancel);

    HWND tab = GetDlgItem(m_hwnd, IDC_TAB_MAIN);
    int saved_tab = m_current_tab;
    TabCtrl_DeleteAllItems(tab);
    InitTabs(m_hwnd);
    ShowTab(saved_tab);

    ApplyLanguage();
}

// ===== Modules Tab =====
void JdspConfigDialog::InitModulesTab(HWND hwnd) {
    (void)hwnd;
    HWND tab = m_tab_dialogs[0];
    if (!tab) return;
    for (int i = 0; i < kDlgModCount; i++) {
        HWND chk = GetDlgItem(tab, kModuleCheckboxes[i]);
        if (chk) Button_SetCheck(chk, m_modules[i] ? BST_CHECKED : BST_UNCHECKED);
    }
}

void JdspConfigDialog::SyncModuleCheckboxes() {
    HWND mods = m_tab_dialogs[0];
    if (mods) {
        for (int i = 0; i < kDlgModCount; i++) {
            HWND chk = GetDlgItem(mods, kModuleCheckboxes[i]);
            if (chk) Button_SetCheck(chk, m_modules[i] ? BST_CHECKED : BST_UNCHECKED);
        }
    }
    // The alias checkboxes live on their own tab pages, so search each page.
    for (int t = 0; t < JDSP_TAB_COUNT; t++) {
        HWND page = m_tab_dialogs[t];
        if (!page) continue;
        for (int i = 0; i < _countof(kModAliases); i++) {
            HWND chk = GetDlgItem(page, kModAliases[i].alias_id);
            if (chk) {
                Button_SetCheck(chk, m_modules[kModAliases[i].mod_index]
                                     ? BST_CHECKED : BST_UNCHECKED);
            }
        }
    }
}

void JdspConfigDialog::ApplyModulesTab(HWND hwnd) {
    (void)hwnd;
    HWND tab = m_tab_dialogs[0];
    if (!tab) return;
    for (int i = 0; i < kDlgModCount; i++) {
        HWND chk = GetDlgItem(tab, kModuleCheckboxes[i]);
        if (chk) m_modules[i] = (Button_GetCheck(chk) == BST_CHECKED);
    }
}

// ===== Equalizer Tab =====
void JdspConfigDialog::UpdateEqBandLabel(HWND tab, int band) {
    if (!tab || band < 0 || band >= JDSP_EQ_BANDS) return;
    wchar_t buf[32];
    HWND vl = GetDlgItem(tab, IDC_STATIC_EQ_BANDVAL0 + band);
    if (vl) {
        swprintf_s(buf, L"%+.1f", m_eq_bands[band].gain);
        SetWindowTextW(vl, buf);
    }
    HWND fl = GetDlgItem(tab, IDC_STATIC_EQ_BANDFREQ0 + band);
    if (fl) {
        float f = m_eq_bands[band].frequency;
        if (f >= 1000.0f) swprintf_s(buf, L"%.1fk", f / 1000.0f);
        else swprintf_s(buf, L"%.0f", f);
        SetWindowTextW(fl, buf);
    }
    HWND sl = GetDlgItem(tab, IDC_SLIDER_EQ_BAND0 + band);
    if (sl) SendMessageW(sl, TBM_SETPOS, TRUE, (LPARAM)(int)(-m_eq_bands[band].gain * 10.0f));
}

void JdspConfigDialog::InitEqTab(HWND hwnd) {
    (void)hwnd;
    HWND tab = m_tab_dialogs[1];
    if (!tab) return;
    HWND eqchk = GetDlgItem(tab, IDC_CHK_EQ_ENABLE);
    if (eqchk) Button_SetCheck(eqchk, m_modules[kDlgModEqualizer] ? BST_CHECKED : BST_UNCHECKED);
    m_eq_widget.SetBands(m_eq_bands);

    EnsureEqClassRegistered();

    if (!m_eq_widget.GetHWND()) {
        RECT rc = {};
        GetClientRect(tab, &rc);
        int cw = rc.right - rc.left;
        m_eq_widget.Create(tab, 8, 8, cw > 60 ? cw - 16 : 440, 150);
    } else {
        m_eq_widget.Refresh();
    }

    for (int i = 0; i < JDSP_EQ_BANDS; i++) {
        HWND sl = GetDlgItem(tab, IDC_SLIDER_EQ_BAND0 + i);
        if (sl) {
            SendMessageW(sl, TBM_SETRANGE, TRUE, MAKELONG(-240, 240));
            SendMessageW(sl, TBM_SETTICFREQ, 20, 0);
            SendMessageW(sl, TBM_SETPOS, TRUE, (LPARAM)(int)(-m_eq_bands[i].gain * 10.0f));
        }
        UpdateEqBandLabel(tab, i);
    }

    HWND combo = GetDlgItem(tab, IDC_COMBO_EQ_BAND);
    if (combo) {
        SendMessageW(combo, CB_RESETCONTENT, 0, 0);
        for (int i = 0; i < JDSP_EQ_BANDS; i++) {
            wchar_t buf[32];
            swprintf_s(buf, L"Band %d", i + 1);
            SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)buf);
        }
        SendMessageW(combo, CB_SETCURSEL, m_eq_widget.GetSelectedBand(), 0);
    }

    // Keep the freq/gain editors in step with the selected band. Without this the
    // next ApplyEqTab would read the stale text back into m_eq_bands.
    {
        int sel = m_eq_widget.GetSelectedBand();
        if (sel < 0 || sel >= JDSP_EQ_BANDS) sel = 0;
        wchar_t buf[32];
        HWND freq_e = GetDlgItem(tab, IDC_EDIT_EQ_FREQ);
        HWND gain_e = GetDlgItem(tab, IDC_EDIT_EQ_GAIN);
        if (freq_e) {
            swprintf_s(buf, L"%.0f", m_eq_bands[sel].frequency);
            SetTextIfChanged(freq_e, buf);
        }
        if (gain_e) {
            swprintf_s(buf, L"%.1f", m_eq_bands[sel].gain);
            SetTextIfChanged(gain_e, buf);
        }
    }

    HWND ft = GetDlgItem(tab, IDC_COMBO_EQ_FILTERTYPE);
    if (ft) {
        SendMessageW(ft, CB_RESETCONTENT, 0, 0);
        const wchar_t* ft_names[] = {
            L"FIR minimum phase", L"IIR 4th order", L"IIR 6th order",
            L"IIR 8th order", L"IIR 10th order", L"IIR 12th order"
        };
        for (int i = 0; i < 6; i++) SendMessageW(ft, CB_ADDSTRING, 0, (LPARAM)ft_names[i]);
        SendMessageW(ft, CB_SETCURSEL, m_eq_filter_type, 0);
    }
    HWND ip = GetDlgItem(tab, IDC_COMBO_EQ_INTERP);
    if (ip) {
        SendMessageW(ip, CB_RESETCONTENT, 0, 0);
        SendMessageW(ip, CB_ADDSTRING, 0, (LPARAM)L"PCHIP");
        SendMessageW(ip, CB_ADDSTRING, 0, (LPARAM)L"Makima");
        SendMessageW(ip, CB_SETCURSEL, m_eq_interpolation, 0);
    }

    int sel = m_eq_widget.GetSelectedBand();
    if (sel < 0 || sel >= JDSP_EQ_BANDS) sel = 0;
    const EqBand& b = m_eq_bands[sel];
    wchar_t buf[32];
    HWND freq_e = GetDlgItem(tab, IDC_EDIT_EQ_FREQ);
    HWND gain_e = GetDlgItem(tab, IDC_EDIT_EQ_GAIN);
    if (freq_e) { swprintf_s(buf, L"%.0f", b.frequency); SetTextIfChanged(freq_e, buf); }
    if (gain_e) { swprintf_s(buf, L"%.1f", b.gain); SetTextIfChanged(gain_e, buf); }
}

void JdspConfigDialog::ApplyEqTab(HWND hwnd) {
    (void)hwnd;
    HWND tab = m_tab_dialogs[1];
    if (!tab) return;

    for (int i = 0; i < JDSP_EQ_BANDS; i++) {
        HWND sl = GetDlgItem(tab, IDC_SLIDER_EQ_BAND0 + i);
        if (sl) m_eq_bands[i].gain = -(float)SendMessageW(sl, TBM_GETPOS, 0, 0) / 10.0f;
    }

    HWND ft = GetDlgItem(tab, IDC_COMBO_EQ_FILTERTYPE);
    if (ft) {
        int s = (int)SendMessageW(ft, CB_GETCURSEL, 0, 0);
        if (s >= 0 && s <= 5) m_eq_filter_type = s;
    }
    HWND ip = GetDlgItem(tab, IDC_COMBO_EQ_INTERP);
    if (ip) {
        int s = (int)SendMessageW(ip, CB_GETCURSEL, 0, 0);
        if (s >= 0 && s <= 1) m_eq_interpolation = s;
    }

    int sel = m_eq_widget.GetSelectedBand();
    if (sel < 0 || sel >= JDSP_EQ_BANDS) sel = 0;
    wchar_t buf[32];
    HWND freq_e = GetDlgItem(tab, IDC_EDIT_EQ_FREQ);
    if (freq_e) {
        GetWindowTextW(freq_e, buf, 32);
        float f = (float)_wtof(buf);
        if (f < 20.0f) f = 20.0f;
        if (f > 20000.0f) f = 20000.0f;
        m_eq_bands[sel].frequency = f;
        swprintf_s(buf, L"%.0f", f);
        SetTextIfChanged(freq_e, buf);
    }
    HWND gain_e = GetDlgItem(tab, IDC_EDIT_EQ_GAIN);
    if (gain_e) {
        GetWindowTextW(gain_e, buf, 32);
        float g = (float)_wtof(buf);
        if (g < -24.0f) g = -24.0f;
        if (g > 24.0f) g = 24.0f;
        m_eq_bands[sel].gain = g;
        swprintf_s(buf, L"%.1f", g);
        SetTextIfChanged(gain_e, buf);
    }
    m_eq_widget.SetBands(m_eq_bands);
    UpdateEqBandLabel(tab, sel);
}

// ===== Dynamics Tab =====
void JdspConfigDialog::UpdateCompBandLabel(HWND tab, int band) {
    if (!tab || band < 0 || band >= JDSP_COMP_BANDS) return;
    HWND sl = GetDlgItem(tab, IDC_SLIDER_COMP_BAND0 + band);
    if (sl) SendMessageW(sl, TBM_SETPOS, TRUE, (LPARAM)(int)(-m_comp_band_gain[band] * 10.0));
    HWND vl = GetDlgItem(tab, IDC_STATIC_COMP_BANDVAL0 + band);
    if (vl) {
        wchar_t buf[32];
        swprintf_s(buf, L"%+.1f", m_comp_band_gain[band]);
        SetWindowTextW(vl, buf);
    }
}

void JdspConfigDialog::InitDynamicsTab(HWND hwnd) {
    (void)hwnd;
    HWND tab = m_tab_dialogs[2];
    if (!tab) return;

    auto set_slider = [&](int id, int min_v, int max_v, int pos) {
        HWND sl = GetDlgItem(tab, id);
        if (sl) { SendMessageW(sl, TBM_SETRANGE, TRUE, MAKELONG(min_v, max_v)); SendMessageW(sl, TBM_SETPOS, TRUE, pos); }
    };

    HWND chk = GetDlgItem(tab, IDC_CHK_COMP);
    if (chk) Button_SetCheck(chk, m_modules[kDlgModCompressor] ? BST_CHECKED : BST_UNCHECKED);
    set_slider(IDC_SLIDER_COMP_TIME, 0, 200, (int)(m_comp_time * 100.0));
    set_slider(IDC_SLIDER_COMP_GRAN, 0, 3, m_comp_granularity);
    set_slider(IDC_SLIDER_COMP_TFRES, 0, 3, m_comp_tfresolution);
    UpdateSliderLabel(tab, IDC_SLIDER_COMP_TIME, IDC_STATIC_COMP_TIME, L"%.2f s", (float)m_comp_time);
    UpdateSliderLabel(tab, IDC_SLIDER_COMP_GRAN, IDC_STATIC_COMP_GRAN, L"%.0f", (float)m_comp_granularity);
    UpdateSliderLabel(tab, IDC_SLIDER_COMP_TFRES, IDC_STATIC_COMP_TFRES, L"%.0f", (float)m_comp_tfresolution);

    for (int i = 0; i < JDSP_COMP_BANDS; i++) {
        HWND sl = GetDlgItem(tab, IDC_SLIDER_COMP_BAND0 + i);
        if (sl) {
            SendMessageW(sl, TBM_SETRANGE, TRUE, MAKELONG(-240, 240));
            SendMessageW(sl, TBM_SETTICFREQ, 20, 0);
        }
        UpdateCompBandLabel(tab, i);
    }

    chk = GetDlgItem(tab, IDC_CHK_LIM);
    if (chk) Button_SetCheck(chk, m_modules[kDlgModLimiter] ? BST_CHECKED : BST_UNCHECKED);
    set_slider(IDC_SLIDER_LIM_THRESH, -600, -1, (int)(m_lim_threshold * 10.0));
    set_slider(IDC_SLIDER_LIM_RELEASE, 1, 5000, (int)m_lim_release);
    UpdateSliderLabel(tab, IDC_SLIDER_LIM_THRESH, IDC_STATIC_LIM_THRESH, L"%.1f dB", (float)m_lim_threshold);
    UpdateSliderLabel(tab, IDC_SLIDER_LIM_RELEASE, IDC_STATIC_LIM_RELEASE, L"%.0f ms", (float)m_lim_release);

    chk = GetDlgItem(tab, IDC_CHK_DDC_ENABLE);
    if (chk) Button_SetCheck(chk, m_modules[kDlgModDdc] ? BST_CHECKED : BST_UNCHECKED);
    SetDlgItemTextW(tab, IDC_EDIT_DDC_PROFILE, m_ddc_profile);
}

void JdspConfigDialog::ApplyDynamicsTab(HWND hwnd) {
    (void)hwnd;
    HWND tab = m_tab_dialogs[2];
    if (!tab) return;
    auto get_pos = [&](int id) -> double { HWND sl = GetDlgItem(tab, id); return sl ? (double)SendMessageW(sl, TBM_GETPOS, 0, 0) : 0.0; };
    m_comp_time = get_pos(IDC_SLIDER_COMP_TIME) / 100.0;
    m_comp_granularity = (int)get_pos(IDC_SLIDER_COMP_GRAN);
    m_comp_tfresolution = (int)get_pos(IDC_SLIDER_COMP_TFRES);
    for (int i = 0; i < JDSP_COMP_BANDS; i++) {
        m_comp_band_gain[i] = -get_pos(IDC_SLIDER_COMP_BAND0 + i) / 10.0;
    }
    m_lim_threshold = get_pos(IDC_SLIDER_LIM_THRESH) / 10.0;
    m_lim_release = get_pos(IDC_SLIDER_LIM_RELEASE);
}

// ===== Effects Tab =====
void JdspConfigDialog::InitEffectsTab(HWND hwnd) {
    (void)hwnd;
    HWND tab = m_tab_dialogs[3];
    if (!tab) return;

    auto set_slider = [&](int id, int min_v, int max_v, int pos) {
        HWND sl = GetDlgItem(tab, id);
        if (sl) { SendMessageW(sl, TBM_SETRANGE, TRUE, MAKELONG(min_v, max_v)); SendMessageW(sl, TBM_SETPOS, TRUE, pos); }
    };
    auto set_chk = [&](int id, int mod) {
        HWND chk = GetDlgItem(tab, id);
        if (chk) Button_SetCheck(chk, m_modules[mod] ? BST_CHECKED : BST_UNCHECKED);
    };

    set_chk(IDC_CHK_TUBE_EFFECT, kDlgModAnalog);
    set_slider(IDC_SLIDER_TUBE_DRIVE, -30, 120, (int)(m_tube_drive_db * 10.0));
    UpdateSliderLabel(tab, IDC_SLIDER_TUBE_DRIVE, IDC_STATIC_TUBE_DRIVE, L"%.1f dB", (float)m_tube_drive_db);

    set_chk(IDC_CHK_STEREO_EFFECT, kDlgModStereo);
    set_slider(IDC_SLIDER_STEREO_WIDTH, 0, 100, (int)m_stereo_width);
    UpdateSliderLabel(tab, IDC_SLIDER_STEREO_WIDTH, IDC_STATIC_STEREO_WIDTH, L"%.0f%%", (float)m_stereo_width);

    set_chk(IDC_CHK_BASS_EFFECT, kDlgModBassBoost);
    set_slider(IDC_SLIDER_BASS_BOOST, 0, 150, (int)(m_bass_boost * 10.0));
    UpdateSliderLabel(tab, IDC_SLIDER_BASS_BOOST, IDC_STATIC_BASS_BOOST, L"%.1f dB", (float)m_bass_boost);

    set_chk(IDC_CHK_REVERB_EFFECT, kDlgModReverb);
    HWND rev = GetDlgItem(tab, IDC_COMBO_REVERB_PRESET);
    if (rev) {
        SendMessageW(rev, CB_RESETCONTENT, 0, 0);
        static const wchar_t* preset_names[19] = {
            L"Default", L"Small Hall 1", L"Small Hall 2", L"Medium Hall 1", L"Medium Hall 2",
            L"Large Hall 1", L"Large Hall 2", L"Small Room 1", L"Small Room 2",
            L"Medium Room 1", L"Medium Room 2", L"Large Room 1", L"Large Room 2",
            L"Medium ER 1", L"Medium ER 2", L"Plate High", L"Plate Low",
            L"Long Reverb 1", L"Long Reverb 2"
        };
        for (int i = 0; i < 19; i++) SendMessageW(rev, CB_ADDSTRING, 0, (LPARAM)preset_names[i]);
        SendMessageW(rev, CB_SETCURSEL, m_reverb_preset, 0);
    }

    set_slider(IDC_SLIDER_REVERB_WET, -700, 0, (int)(m_reverb_wet * 10.0));
    UpdateSliderLabel(tab, IDC_SLIDER_REVERB_WET, IDC_STATIC_REVERB_WET, L"%.1f dB", (float)m_reverb_wet);
    set_slider(IDC_SLIDER_REVERB_DRY, -300, 0, (int)(m_reverb_dry * 10.0));
    UpdateSliderLabel(tab, IDC_SLIDER_REVERB_DRY, IDC_STATIC_REVERB_DRY, L"%.1f dB", (float)m_reverb_dry);
    set_slider(IDC_SLIDER_REVERB_WIDTH, 0, 100, (int)(m_reverb_width * 100.0));
    UpdateSliderLabel(tab, IDC_SLIDER_REVERB_WIDTH, IDC_STATIC_REVERB_WIDTH, L"%.0f%%", (float)(m_reverb_width * 100.0));
    set_slider(IDC_SLIDER_REVERB_RT60, 5, 300, (int)(m_reverb_rt60 * 10.0));
    UpdateSliderLabel(tab, IDC_SLIDER_REVERB_RT60, IDC_STATIC_REVERB_RT60, L"%.1f s", (float)m_reverb_rt60);
    set_slider(IDC_SLIDER_REVERB_DAMP, 1000, 18000, (int)m_reverb_damp);
    UpdateSliderLabel(tab, IDC_SLIDER_REVERB_DAMP, IDC_STATIC_REVERB_DAMP, L"%.0f Hz", (float)m_reverb_damp);
    set_slider(IDC_SLIDER_REVERB_BASS, 0, 200, (int)(m_reverb_bass * 100.0));
    UpdateSliderLabel(tab, IDC_SLIDER_REVERB_BASS, IDC_STATIC_REVERB_BASS, L"%.0f%%", (float)(m_reverb_bass * 100.0));
    set_slider(IDC_SLIDER_REVERB_PREDELAY, 0, 100, (int)(m_reverb_predelay * 1000.0));
    UpdateSliderLabel(tab, IDC_SLIDER_REVERB_PREDELAY, IDC_STATIC_REVERB_PREDELAY, L"%.0f ms", (float)(m_reverb_predelay * 1000.0));
    set_slider(IDC_SLIDER_REVERB_ER, 0, 100, (int)(m_reverb_er * 100.0));
    UpdateSliderLabel(tab, IDC_SLIDER_REVERB_ER, IDC_STATIC_REVERB_ER, L"%.0f%%", (float)(m_reverb_er * 100.0));

    set_chk(IDC_CHK_BS2B_EFFECT, kDlgModBs2b);
    HWND xf = GetDlgItem(tab, IDC_COMBO_BS2B_MODE);
    if (xf) {
        SendMessageW(xf, CB_RESETCONTENT, 0, 0);
        static const wchar_t* mode_names[6] = {
            L"BS2B Level 1", L"BS2B Level 2", L"HRTF Crossfeed",
            L"HRTF Surround 1", L"HRTF Surround 2", L"HRTF Surround 3"
        };
        for (int i = 0; i < 6; i++) SendMessageW(xf, CB_ADDSTRING, 0, (LPARAM)mode_names[i]);
        SendMessageW(xf, CB_SETCURSEL, m_bs2b_mode, 0);
    }

    set_slider(IDC_SLIDER_OUTPUT_GAIN, -150, 150, (int)(m_output_gain * 10.0));
    UpdateSliderLabel(tab, IDC_SLIDER_OUTPUT_GAIN, IDC_STATIC_OUTPUT_GAIN, L"%.1f dB", (float)m_output_gain);
}

void JdspConfigDialog::ApplyEffectsTab(HWND hwnd) {
    (void)hwnd;
    HWND tab = m_tab_dialogs[3];
    if (!tab) return;
    auto get_pos = [&](int id) -> double { HWND sl = GetDlgItem(tab, id); return sl ? (double)SendMessageW(sl, TBM_GETPOS, 0, 0) : 0.0; };
    m_tube_drive_db = get_pos(IDC_SLIDER_TUBE_DRIVE) / 10.0;
    m_stereo_width = get_pos(IDC_SLIDER_STEREO_WIDTH);
    m_bass_boost = get_pos(IDC_SLIDER_BASS_BOOST) / 10.0;
    m_output_gain = get_pos(IDC_SLIDER_OUTPUT_GAIN) / 10.0;

    HWND rev = GetDlgItem(tab, IDC_COMBO_REVERB_PRESET);
    if (rev) {
        int s = (int)SendMessageW(rev, CB_GETCURSEL, 0, 0);
        if (s >= 0 && s <= 18) m_reverb_preset = s;
    }
    m_reverb_wet = get_pos(IDC_SLIDER_REVERB_WET) / 10.0;
    m_reverb_dry = get_pos(IDC_SLIDER_REVERB_DRY) / 10.0;
    m_reverb_width = get_pos(IDC_SLIDER_REVERB_WIDTH) / 100.0;
    m_reverb_rt60 = get_pos(IDC_SLIDER_REVERB_RT60) / 10.0;
    m_reverb_damp = get_pos(IDC_SLIDER_REVERB_DAMP);
    m_reverb_bass = get_pos(IDC_SLIDER_REVERB_BASS) / 100.0;
    m_reverb_predelay = get_pos(IDC_SLIDER_REVERB_PREDELAY) / 1000.0;
    m_reverb_er = get_pos(IDC_SLIDER_REVERB_ER) / 100.0;
    HWND xf = GetDlgItem(tab, IDC_COMBO_BS2B_MODE);
    if (xf) {
        int s = (int)SendMessageW(xf, CB_GETCURSEL, 0, 0);
        if (s >= 0 && s <= 5) m_bs2b_mode = s;
    }
}

// ===== Convolver Tab =====
void JdspConfigDialog::InitConvolverTab(HWND hwnd) {
    (void)hwnd;
    HWND tab = m_tab_dialogs[4];
    if (!tab) return;
    HWND chk = GetDlgItem(tab, IDC_CHK_CONV_ENABLE);
    if (chk) Button_SetCheck(chk, m_modules[kDlgModConvolver] ? BST_CHECKED : BST_UNCHECKED);
    SetDlgItemTextW(tab, IDC_EDIT_CONV_IR, m_ir_path);
    SetDlgItemTextW(tab, IDC_STATIC_CONV_INFO,
                    m_ir_path[0] ? L"IR loaded." : L"No IR loaded");
}

void JdspConfigDialog::ApplyConvolverTab(HWND hwnd) {
    (void)hwnd;
    HWND tab = m_tab_dialogs[4];
    if (!tab) return;
    HWND e = GetDlgItem(tab, IDC_EDIT_CONV_IR);
    if (e) GetWindowTextW(e, m_ir_path, MAX_PATH);
}

// ===== Spectrum Extender Tab =====
void JdspConfigDialog::InitSpectrumTab(HWND hwnd) {
    (void)hwnd;
    HWND tab = m_tab_dialogs[5];
    if (!tab) return;
    HWND chk = GetDlgItem(tab, IDC_CHK_SPECTRUM_ENABLE);
    if (chk) Button_SetCheck(chk, m_modules[kDlgModSpectrum] ? BST_CHECKED : BST_UNCHECKED);
    SetDlgItemTextW(tab, IDC_EDIT_SPECTRUM_FILE, m_spectrum_path);
    SetDlgItemTextW(tab, IDC_STATIC_SPECTRUM_INFO,
                    m_spectrum_path[0] ? L"Response file loaded." : L"No file loaded");
}

void JdspConfigDialog::ApplySpectrumTab(HWND hwnd) {
    (void)hwnd;
    HWND tab = m_tab_dialogs[5];
    if (!tab) return;
    HWND e = GetDlgItem(tab, IDC_EDIT_SPECTRUM_FILE);
    if (e) GetWindowTextW(e, m_spectrum_path, MAX_PATH);
}

// ===== Script Tab =====
void JdspConfigDialog::InitScriptTab(HWND hwnd) {
    (void)hwnd;
    HWND tab = m_tab_dialogs[6];
    if (!tab) return;
    HWND chk = GetDlgItem(tab, IDC_CHK_SCRIPT_ENABLE);
    if (chk) Button_SetCheck(chk, m_modules[kDlgModEel2] ? BST_CHECKED : BST_UNCHECKED);
    HWND edit = GetDlgItem(tab, IDC_EDIT_SCRIPT);
    if (edit) SetWindowTextW(edit, m_script_text);
}

void JdspConfigDialog::ApplyScriptTab(HWND hwnd) {
    (void)hwnd;
    HWND tab = m_tab_dialogs[6];
    if (!tab) return;
    HWND edit = GetDlgItem(tab, IDC_EDIT_SCRIPT);
    if (edit) GetWindowTextW(edit, m_script_text, _countof(m_script_text));
}
