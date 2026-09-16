#include "stdafx.h"
#include "jdsp_config_dialog.h"
#include "jdsp_config_serializer.h"
#include "resource.h"
#include "jdsp_live_link.h"
#include <cstdio>
#include <map>

extern void EnsureEqClassRegistered();
HMODULE GetMyModule();

static FILE* g_cfg_log = NULL;
void CfgLog(const char* msg) {
    if (!g_cfg_log) g_cfg_log = fopen("jdsp_cfg.log", "a");
    if (g_cfg_log) { fprintf(g_cfg_log, "%s\n", msg); fflush(g_cfg_log); }
}

static bool g_trace = false;

static bool ShouldTrace(UINT msg) {
    switch (msg) {
        case WM_MOUSEMOVE: case WM_NCHITTEST: case WM_SETCURSOR:
        case WM_GETDLGCODE: case WM_PAINT: case WM_ERASEBKGND:
        case WM_CTLCOLORBTN: case WM_CTLCOLORSTATIC: case WM_CTLCOLORDLG:
        case WM_CTLCOLOREDIT: case WM_CTLCOLORLISTBOX: case WM_CTLCOLORSCROLLBAR:
        case WM_NCMOUSEMOVE: case WM_MOUSELEAVE: case WM_NCMOUSELEAVE:
        case WM_SETFOCUS: case WM_KILLFOCUS: case WM_ACTIVATE: case WM_ACTIVATEAPP:
        case WM_NCACTIVATE: case WM_WINDOWPOSCHANGING: case WM_WINDOWPOSCHANGED:
        case WM_SHOWWINDOW: case WM_SIZE: case WM_MOVE: case WM_ENABLE:
        case WM_STYLECHANGED: case WM_STYLECHANGING: case WM_TIMER:
        case WM_SYSCOMMAND: case WM_GETMINMAXINFO: case WM_CANCELMODE:
        case WM_CAPTURECHANGED: case WM_NCPAINT: case 0x0093 /*WM_UAHDRAWMENU*/:
            return false;
    }
    return true;
}

static HWND g_last_trace_hwnd = NULL;
static UINT g_last_trace_msg = 0;

static void TraceMsg(const char* tag, HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (!g_trace) return;
    if (!ShouldTrace(msg)) return;
    if (hwnd == g_last_trace_hwnd && msg == g_last_trace_msg) return;
    g_last_trace_hwnd = hwnd;
    g_last_trace_msg = msg;
    char b[128];
    sprintf_s(b, "%s hwnd=%p msg=0x%04X wp=%llu lp=%llu", tag, (void*)hwnd, msg,
              (unsigned long long)wParam, (unsigned long long)lParam);
    CfgLog(b);
}

#ifndef Button_SetCheck
#define Button_SetCheck(hCtrl, uCheck) SendMessage((hCtrl), BM_SETCHECK, (WPARAM)(uCheck), 0)
#endif
#ifndef Button_GetCheck
#define Button_GetCheck(hCtrl) ((LRESULT)SendMessage((hCtrl), BM_GETCHECK, 0, 0))
#endif

static INT_PTR CALLBACK TabPageDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    JdspConfigDialog* dlg = NULL;
    if (msg == WM_INITDIALOG) {
        dlg = reinterpret_cast<JdspConfigDialog*>(lParam);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(dlg));
        return TRUE;
    }
    dlg = reinterpret_cast<JdspConfigDialog*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    if (!dlg) return FALSE;
    TraceMsg("TabDlg", hwnd, msg, wParam, lParam);
    switch (msg) {
        case WM_COMMAND: {
            char b[96];
            sprintf_s(b, "TabDlg: WM_COMMAND id=%u code=%u lparam=%p", (unsigned)LOWORD(wParam), (unsigned)HIWORD(wParam), (void*)lParam);
            CfgLog(b);
            dlg->HandleTabCommand(wParam, lParam);
            CfgLog("TabDlg: WM_COMMAND done");
            return TRUE;
        }
        case WM_HSCROLL:
        case WM_VSCROLL: {
            char b[96];
            sprintf_s(b, "TabDlg: WM_HSCROLL id=%u code=%u lparam=%p", (unsigned)LOWORD(wParam), (unsigned)HIWORD(wParam), (void*)lParam);
            CfgLog(b);
            dlg->HandleTabScroll(wParam, lParam);
            CfgLog("TabDlg: WM_HSCROLL done");
            return TRUE;
        }
        case WM_NOTIFY: {
            NMHDR* nm = reinterpret_cast<NMHDR*>(lParam);
            if (nm->idFrom == IDC_EQ_CURVE && nm->code == NM_CLICK) {
                CfgLog("TabDlg: EQ_CURVE NM_CLICK");
                dlg->HandleTabNotify(wParam, lParam);
                CfgLog("TabDlg: EQ_CURVE NM_CLICK done");
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
    { IDC_CHK_IIR,           L"IIR Filters",      L"IIR \x6ee4\x6ce2\x5668" },
    { IDC_CHK_SPECTRUM,      L"Spectrum Extender", L"\x9891\x8c31\x6269\x5c55" },
    { IDC_CHK_DYNAMIC_SYS,   L"Dynamic System",   L"\x52a8\x6001\x7cfb\x7edf" },
    { IDC_CHK_EEL2,          L"EEL2 Scripting",   L"EEL2 \x811a\x672c" },
    // EQ tab
    { IDL_EQ_BAND,           L"Band:",        L"\x9891\x6bb5:" },
    { IDL_EQ_FREQ,           L"Freq:",        L"\x9891\x7387:" },
    { IDL_EQ_FREQ_UNIT,      L"(20-20000 Hz)", L"(20-20000 Hz)" },
    { IDL_EQ_Q,              L"Q:",           L"Q \x503c:" },
    { IDL_EQ_Q_UNIT,         L"(0.1-10.0)",   L"(0.1-10.0)" },
    { IDC_BTN_EQ_RESET,      L"Reset Flat",   L"\x91cd\x7f6e\x4e3a\x5e73\x76f4" },
    // Dynamics tab
    { IDL_DYN_COMP_GRP,      L"Compressor",      L"\x538b\x7f29\x5668" },
    { IDC_CHK_COMP,          L"Enable",          L"\x542f\x7528" },
    { IDL_DYN_COMP_THRESH,   L"Threshold (dB):", L"\x9608\x503c (dB):" },
    { IDL_DYN_COMP_RATIO,    L"Ratio:",          L"\x6bd4\x7387:" },
    { IDL_DYN_COMP_ATTACK,   L"Attack (ms):",    L"\x8d77\x59cb\x65f6\x95f4 (ms):" },
    { IDL_DYN_COMP_RELEASE,  L"Release (ms):",   L"\x91ca\x653e\x65f6\x95f4 (ms):" },
    { IDL_DYN_LIM_GRP,       L"Limiter",         L"\x9650\x5e45\x5668" },
    { IDC_CHK_LIM,           L"Enable",          L"\x542f\x7528" },
    { IDL_DYN_LIM_THRESH,    L"Threshold (dB):", L"\x9608\x503c (dB):" },
    { IDL_DYN_LIM_RELEASE,   L"Release (ms):",   L"\x91ca\x653e\x65f6\x95f4 (ms):" },
    { IDL_DYN_DDC_GRP,       L"DDC",             L"DDC" },
    { IDC_CHK_DDC_ENABLE,    L"Enable",          L"\x542f\x7528" },
    { IDL_DYN_DDC_STRENGTH,  L"Strength:",       L"\x5f3a\x5ea6:" },
    { IDL_DYN_DDC_PROFILE,   L"Profile:",        L"\x914d\x7f6e:" },
    { IDC_BTN_DDC_BROWSE,    L"Browse...",       L"\x6d4f\x89c8..." },
    // Effects tab
    { IDL_FX_BASS_GRP,       L"Bass Boost",     L"\x4f4e\x97f3\x589e\x5f3a" },
    { IDC_CHK_BASS_EFFECT,   L"Enable",         L"\x542f\x7528" },
    { IDL_FX_BASS_BOOST,     L"Boost (dB):",    L"\x589e\x5f3a (dB):" },
    { IDL_FX_BASS_FREQ,      L"Freq (Hz):",     L"\x9891\x7387 (Hz):" },
    { IDL_FX_STEREO_GRP,     L"Stereo Widener", L"\x7acb\x4f53\x58f0\x589e\x5f3a" },
    { IDC_CHK_STEREO_EFFECT, L"Enable",         L"\x542f\x7528" },
    { IDL_FX_STEREO_WIDTH,   L"Width (%):",     L"\x5bbd\x5ea6 (%):" },
    { IDL_FX_REVERB_GRP,     L"Reverb",         L"\x6df7\x54cd" },
    { IDC_CHK_REVERB_EFFECT, L"Enable",         L"\x542f\x7528" },
    { IDL_FX_REVERB_ROOM,    L"Room:",          L"\x623f\x95f4:" },
    { IDL_FX_REVERB_DAMP,    L"Damp:",          L"\x963b\x5c3c:" },
    { IDL_FX_REVERB_WET,     L"Wet:",           L"\x6e7f\x58f0:" },
    { IDL_FX_TUBE_GRP,       L"Analog Modelling", L"\x7535\x5b50\x7ba1\x6a21\x62df" },
    { IDC_CHK_TUBE_EFFECT,   L"Enable",         L"\x542f\x7528" },
    { IDL_FX_TUBE_DRIVE,     L"Drive:",         L"\x9a71\x52a8:" },
    { IDL_FX_BS2B_GRP,       L"BS2B Crossfeed", L"BS2B \x8de8\x9988" },
    { IDC_CHK_BS2B_EFFECT,   L"Enable",         L"\x542f\x7528" },
    { IDL_FX_BS2B_FEED,      L"Feed:",          L"\x9988\x9001:" },
    { IDL_FX_BS2B_FREQ,      L"Freq:",          L"\x9891\x7387:" },
    // Convolver tab
    { IDC_CHK_CONV_ENABLE,   L"Enable Convolver", L"\x542f\x7528\x5377\x79ef\x5668" },
    { IDL_CV_IR_GRP,         L"Impulse Response", L"\x8109\x51b2\x54cd\x5e94" },
    { IDL_CV_FILE,           L"File:",            L"\x6587\x4ef6:" },
    { IDC_BTN_CONV_BROWSE,   L"Browse...",        L"\x6d4f\x89c8..." },
    { IDL_CV_GAIN_GRP,       L"Gain",             L"\x589e\x76ca" },
    { IDL_CV_GAIN,           L"Gain (dB):",       L"\x589e\x76ca (dB):" },
    // Script tab
    { IDC_CHK_SCRIPT_ENABLE, L"Enable EEL2 Scripting", L"\x542f\x7528 EEL2 \x811a\x672c" },
    { IDL_SC_SCRIPT,         L"Script:",              L"\x811a\x672c:" },
    { IDC_BTN_SCRIPT_LOAD,   L"Load Script...",       L"\x52a0\x8f7d\x811a\x672c..." },
    { IDC_BTN_SCRIPT_SAVE,   L"Save Script...",       L"\x4fdd\x5b58\x811a\x672c..." },
    { IDL_SC_STATUS,         L"Status: Ready",        L"\x72b6\x6001: \x5c31\x7eea" },
};

void JdspConfigDialog::ApplyLanguage() {
    HWND pages[7];
    pages[0] = m_hwnd;
    for (int i = 0; i < 6; i++) pages[i + 1] = m_tab_dialogs[i];

    for (int p = 0; p < 7; p++) {
        if (!pages[p]) continue;
        for (size_t i = 0; i < _countof(kUiLabels); i++) {
            HWND c = GetDlgItem(pages[p], kUiLabels[i].id);
            if (c) SetWindowTextW(c, m_current_lang == 0 ? kUiLabels[i].en : kUiLabels[i].zh);
        }
    }
}

JdspConfigDialog::JdspConfigDialog(JdspIpcClient& ipc) : m_ipc(ipc) {
    memset(m_modules, 0, sizeof(m_modules));
    float freqs[] = {31, 62, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};
    for (int i = 0; i < 10; i++) {
        m_eq_bands[i].enabled = true;
        m_eq_bands[i].frequency = freqs[i];
        m_eq_bands[i].gain = 0.0f;
        m_eq_bands[i].q = 0.707f;
    }
}

bool JdspConfigDialog::Show(HWND parent) {
    CfgLog("Show: begin");
    m_live_blob = SerializeSettings();
    m_orig_blob = m_live_blob;

    INT_PTR r = DialogBoxParam(GetMyModule(),
                               MAKEINTRESOURCE(IDD_JDSP_CONFIG),
                               parent, DialogProc, reinterpret_cast<LPARAM>(this));
    CfgLog("Show: dialog closed");

    if (r != IDOK) {
        CfgLog("Show: cancelled -> reverting live settings");
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
        CfgLog("WM_INITDIALOG");
        dlg->OnInitDialog(hwnd);
        return TRUE;
    }
    dlg = reinterpret_cast<JdspConfigDialog*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    if (!dlg) return FALSE;
    TraceMsg("MainDlg", hwnd, msg, wParam, lParam);

    switch (msg) {
        case WM_COMMAND: {
            char b[96];
            sprintf_s(b, "MainDlg: WM_COMMAND id=%u code=%u", (unsigned)LOWORD(wParam), (unsigned)HIWORD(wParam));
            CfgLog(b);
            dlg->OnCommand(hwnd, wParam, lParam);
            CfgLog("MainDlg: WM_COMMAND done");
            return TRUE;
        }
        case WM_NOTIFY: {
            NMHDR* nm = reinterpret_cast<NMHDR*>(lParam);
            if (g_trace && nm->code != NM_CUSTOMDRAW) {
                char b[96];
                sprintf_s(b, "MainDlg: WM_NOTIFY from=%u code=%d", (unsigned)nm->idFrom, (int)nm->code);
                CfgLog(b);
            }
            if (nm->idFrom == IDC_TAB_MAIN && nm->code == TCN_SELCHANGE) {
                dlg->OnNotify(hwnd, wParam, lParam);
                return TRUE;
            }
            return FALSE;
        }
        case WM_HSCROLL:
            CfgLog("MainDlg: WM_HSCROLL");
            dlg->OnHScroll(hwnd, wParam, lParam);
            CfgLog("MainDlg: WM_HSCROLL done");
            return TRUE;
        case WM_CLOSE:
            EndDialog(hwnd, IDCANCEL);
            return TRUE;
    }
    return FALSE;
}

void JdspConfigDialog::OnInitDialog(HWND hwnd) {
    m_hwnd = hwnd;
    CfgLog("OnInitDialog: begin");

    const StrTable* str = (m_current_lang == 0) ? &g_str_en : &g_str_zh;
    SetWindowTextW(hwnd, str->window_title);
    SetDlgItemTextW(hwnd, IDOK, str->btn_ok);
    SetDlgItemTextW(hwnd, IDCANCEL, str->btn_cancel);

    HWND combo = GetDlgItem(hwnd, IDC_COMBO_LANGUAGE);
    SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"English");
    SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)L"\x4e2d\x6587");
    SendMessageW(combo, CB_SETCURSEL, m_current_lang, 0);

    CfgLog("OnInitDialog: InitTabs");
    InitTabs(hwnd);
    CfgLog("OnInitDialog: CreateTabDialogs");
    CreateTabDialogs(hwnd);

    CfgLog("OnInitDialog: InitModulesTab");
    InitModulesTab(hwnd);
    CfgLog("OnInitDialog: InitEqTab");
    InitEqTab(hwnd);
    CfgLog("OnInitDialog: InitDynamicsTab");
    InitDynamicsTab(hwnd);
    CfgLog("OnInitDialog: InitEffectsTab");
    InitEffectsTab(hwnd);
    CfgLog("OnInitDialog: InitConvolverTab");
    InitConvolverTab(hwnd);
    CfgLog("OnInitDialog: InitScriptTab");
    InitScriptTab(hwnd);

    CfgLog("OnInitDialog: ApplyLanguage");
    ApplyLanguage();
    CfgLog("OnInitDialog: ShowTab");
    ShowTab(0);
    CfgLog("OnInitDialog: done");
    g_trace = true;
}

void JdspConfigDialog::InitTabs(HWND hwnd) {
    HWND tab = GetDlgItem(hwnd, IDC_TAB_MAIN);
    const StrTable* str = (m_current_lang == 0) ? &g_str_en : &g_str_zh;

    const wchar_t* tab_names[] = {
        str->tab_modules, str->tab_eq, str->tab_dynamics,
        str->tab_effects, str->tab_convolver, str->tab_script
    };

    for (int i = 0; i < 6; i++) {
        TCITEMW tie = {};
        tie.mask = TCIF_TEXT;
        tie.pszText = (LPWSTR)tab_names[i];
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

    int tab_ids[] = {IDD_TAB_MODULES, IDD_TAB_EQ, IDD_TAB_DYNAMICS, IDD_TAB_EFFECTS, IDD_TAB_CONVOLVER, IDD_TAB_SCRIPT};

    for (int i = 0; i < 6; i++) {
        m_tab_dialogs[i] = CreateDialogParam(GetMyModule(),
            MAKEINTRESOURCE(tab_ids[i]), hwnd, TabPageDlgProc, reinterpret_cast<LPARAM>(this));
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
    for (int i = 0; i < 6; i++) {
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
        // Save current band edits first
        ApplyEqTab(hwnd);
        // Switch to new band
        HWND combo = GetDlgItem(tab, IDC_COMBO_EQ_BAND);
        int sel = (int)SendMessageW(combo, CB_GETCURSEL, 0, 0);
        if (sel >= 0 && sel < 10) {
            m_eq_widget.SetSelectedBand(sel);
            const EqBand& b = m_eq_bands[sel];
            wchar_t buf[32];
            HWND freq_e = GetDlgItem(tab, IDC_EDIT_EQ_FREQ);
            HWND q_e = GetDlgItem(tab, IDC_EDIT_EQ_Q);
            if (freq_e) { swprintf_s(buf, L"%.0f", b.frequency); SetWindowTextW(freq_e, buf); }
            if (q_e) { swprintf_s(buf, L"%.2f", b.q); SetWindowTextW(q_e, buf); }
        }
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
            InitScriptTab(hwnd);
        }
    } else if (id == IDC_BTN_RESET_ALL && code == BN_CLICKED) {
        for (int i = 0; i < 13; i++) m_modules[i] = false;
        float freqs[] = {31, 62, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};
        for (int i = 0; i < 10; i++) {
            m_eq_bands[i].enabled = true;
            m_eq_bands[i].frequency = freqs[i];
            m_eq_bands[i].gain = 0.0f;
            m_eq_bands[i].q = 0.707f;
        }
        m_comp_threshold = -20.0f; m_comp_ratio = 4.0f; m_comp_attack = 5.0f; m_comp_release = 50.0f;
        m_lim_threshold = 0.0f; m_lim_release = 50.0f; m_ddc_strength = 50.0f;
        m_bass_boost = 6.0f; m_bass_freq = 100.0f; m_stereo_width = 120.0f;
        m_reverb_room = 0.7f; m_reverb_damp = 0.5f; m_reverb_wet = 0.3f;
        m_tube_drive = 60.0f; m_bs2b_feed = 70.0f; m_bs2b_freq = 650.0f;
        m_conv_gain = 0.0f;
        InitModulesTab(hwnd);
        InitEqTab(hwnd);
        InitDynamicsTab(hwnd);
        InitEffectsTab(hwnd);
        InitConvolverTab(hwnd);
        InitScriptTab(hwnd);
    } else if (id == IDC_BTN_EQ_RESET && code == BN_CLICKED) {
        float freqs[] = {31, 62, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};
        for (int i = 0; i < 10; i++) {
            m_eq_bands[i].enabled = true;
            m_eq_bands[i].frequency = freqs[i];
            m_eq_bands[i].gain = 0.0f;
            m_eq_bands[i].q = 0.707f;
        }
        m_eq_widget.SetBands(m_eq_bands);
        m_eq_widget.Refresh();
        HWND tab = m_tab_dialogs[1];
        if (tab) {
            for (int i = 0; i < 10; i++) UpdateEqBandLabel(tab, i);
            int sel = m_eq_widget.GetSelectedBand();
            if (sel < 0 || sel > 9) sel = 0;
            const EqBand& b = m_eq_bands[sel];
            wchar_t buf[32];
            HWND freq_e = GetDlgItem(tab, IDC_EDIT_EQ_FREQ);
            HWND q_e = GetDlgItem(tab, IDC_EDIT_EQ_Q);
            if (freq_e) { swprintf_s(buf, L"%.0f", b.frequency); SetWindowTextW(freq_e, buf); }
            if (q_e) { swprintf_s(buf, L"%.2f", b.q); SetWindowTextW(q_e, buf); }
        }
    } else if ((id == IDC_EDIT_EQ_FREQ || id == IDC_EDIT_EQ_Q) && code == EN_KILLFOCUS) {
        HWND tab = m_tab_dialogs[1];
        if (tab) {
            ApplyEqTab(tab);
            m_eq_widget.SetBands(m_eq_bands);
        }
    }

    PushLive(false);
}

void JdspConfigDialog::OnNotify(HWND hwnd, WPARAM wParam, LPARAM lParam) {
    NMHDR* nmhdr = reinterpret_cast<NMHDR*>(lParam);
    if (nmhdr->idFrom == IDC_TAB_MAIN && nmhdr->code == TCN_SELCHANGE) {
        int sel = TabCtrl_GetCurSel(GetDlgItem(hwnd, IDC_TAB_MAIN));
        if (sel >= 0 && sel < 6) {
            ShowTab(sel);
        }
    } else if (nmhdr->idFrom == IDC_EQ_CURVE && nmhdr->code == NM_CLICK) {
        EqBand bands[10];
        m_eq_widget.GetBands(bands);
        memcpy(m_eq_bands, bands, sizeof(m_eq_bands));
        HWND tab = m_tab_dialogs[1];
        if (tab) {
            int sel = m_eq_widget.GetSelectedBand();
            for (int i = 0; i < 10; i++) UpdateEqBandLabel(tab, i);
            HWND combo = GetDlgItem(tab, IDC_COMBO_EQ_BAND);
            if (combo) SendMessageW(combo, CB_SETCURSEL, sel, 0);
            if (sel >= 0 && sel < 10) {
                const EqBand& b = m_eq_bands[sel];
                wchar_t buf[32];
                HWND freq_e = GetDlgItem(tab, IDC_EDIT_EQ_FREQ);
                HWND q_e = GetDlgItem(tab, IDC_EDIT_EQ_Q);
                if (freq_e) { swprintf_s(buf, L"%.0f", b.frequency); SetWindowTextW(freq_e, buf); }
                if (q_e) { swprintf_s(buf, L"%.2f", b.q); SetWindowTextW(q_e, buf); }
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
    const StrTable* str = (m_current_lang == 0) ? &g_str_en : &g_str_zh;

    if (id == IDC_SLIDER_COMP_THRESH) UpdateSliderLabel(tab, id, IDC_STATIC_COMP_THRESH, L"%.0f dB", (float)pos);
    else if (id == IDC_SLIDER_COMP_RATIO) UpdateSliderLabel(tab, id, IDC_STATIC_COMP_RATIO, L"%.0f:1", (float)pos);
    else if (id == IDC_SLIDER_COMP_ATTACK) UpdateSliderLabel(tab, id, IDC_STATIC_COMP_ATTACK, L"%.0f ms", (float)pos);
    else if (id == IDC_SLIDER_COMP_RELEASE) UpdateSliderLabel(tab, id, IDC_STATIC_COMP_RELEASE, L"%.0f ms", (float)pos);
    else if (id == IDC_SLIDER_LIM_THRESH) UpdateSliderLabel(tab, id, IDC_STATIC_LIM_THRESH, L"%.0f dB", (float)pos);
    else if (id == IDC_SLIDER_LIM_RELEASE) UpdateSliderLabel(tab, id, IDC_STATIC_LIM_RELEASE, L"%.0f ms", (float)pos);
    else if (id == IDC_SLIDER_DDC_STRENGTH) UpdateSliderLabel(tab, id, IDC_STATIC_DDC_STRENGTH, L"%.0f%%", (float)pos);
    else if (id == IDC_SLIDER_BASS_BOOST) UpdateSliderLabel(tab, id, IDC_STATIC_BASS_BOOST, L"%.0f dB", (float)pos);
    else if (id == IDC_SLIDER_BASS_FREQ) UpdateSliderLabel(tab, id, IDC_STATIC_BASS_FREQ, L"%.0f Hz", (float)pos);
    else if (id == IDC_SLIDER_STEREO_WIDTH) UpdateSliderLabel(tab, id, IDC_STATIC_STEREO_WIDTH, L"%.0f%%", (float)pos);
    else if (id == IDC_SLIDER_REVERB_ROOM) UpdateSliderLabel(tab, id, IDC_STATIC_REVERB_ROOM, L"%.2f", pos / 100.0f);
    else if (id == IDC_SLIDER_REVERB_DAMP) UpdateSliderLabel(tab, id, IDC_STATIC_REVERB_DAMP, L"%.2f", pos / 100.0f);
    else if (id == IDC_SLIDER_REVERB_WET) UpdateSliderLabel(tab, id, IDC_STATIC_REVERB_WET, L"%.2f", pos / 100.0f);
    else if (id == IDC_SLIDER_TUBE_DRIVE) UpdateSliderLabel(tab, id, IDC_STATIC_TUBE_DRIVE, L"%.0f%%", (float)pos);
    else if (id == IDC_SLIDER_BS2B_FEED) UpdateSliderLabel(tab, id, IDC_STATIC_BS2B_FEED, L"%.0f", (float)pos);
    else if (id == IDC_SLIDER_BS2B_FREQ) UpdateSliderLabel(tab, id, IDC_STATIC_BS2B_FREQ, L"%.0f Hz", (float)pos);
    else if (id == IDC_SLIDER_CONV_GAIN) UpdateSliderLabel(tab, id, IDC_STATIC_CONV_GAIN, L"%.0f dB", (float)pos);
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
        EqBand bands[10];
        m_eq_widget.GetBands(bands);
        bands[band].gain = g;
        m_eq_widget.SetBands(bands);
    }

    PushLive(false);
}

// ===== Settings (de)serialization =====

static const char* kModuleKeys[13] = {
    "modules.analog", "modules.bs2b", "modules.ddc", "modules.limiter",
    "modules.compressor", "modules.convolver", "modules.reverb", "modules.bassboost",
    "modules.stereo", "modules.iir", "modules.spectrum", "modules.dynamic", "modules.eel2"
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
// EEL2 script (modules.eel2 / script.text) thousands of times per drag.
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
    ApplyModulesTab(hwnd);
    ApplyEqTab(hwnd);
    ApplyDynamicsTab(hwnd);
    ApplyEffectsTab(hwnd);
    ApplyConvolverTab(hwnd);
    ApplyScriptTab(hwnd);

    HWND tab;
    tab = m_tab_dialogs[3];
    if (tab) {
        HWND chk = GetDlgItem(tab, IDC_CHK_BASS_EFFECT);
        (void)chk;
    }
    tab = m_tab_dialogs[4];
    if (tab) {
        HWND chk = GetDlgItem(tab, IDC_CHK_CONV_ENABLE);
        if (chk) m_conv_enabled = (Button_GetCheck(chk) == BST_CHECKED);
        HWND e = GetDlgItem(tab, IDC_EDIT_CONV_IR);
        if (e) GetWindowTextW(e, m_ir_path, MAX_PATH);
    }
    tab = m_tab_dialogs[5];
    if (tab) {
        HWND chk = GetDlgItem(tab, IDC_CHK_SCRIPT_ENABLE);
        if (chk) m_script_enabled = (Button_GetCheck(chk) == BST_CHECKED);
    }
    tab = m_tab_dialogs[2];
    if (tab) {
        HWND e = GetDlgItem(tab, IDC_EDIT_DDC_PROFILE);
        if (e) GetWindowTextW(e, m_ddc_profile, MAX_PATH);
    }
}

void JdspConfigDialog::PushLive(bool full) {
    if (!m_hwnd) return;

    SyncFromControls(m_hwnd);
    std::string blob = SerializeSettings();

    std::string payload;
    if (full) {
        payload = blob;
    } else {
        payload = DiffBlobs(m_live_blob, blob);
    }
    m_live_blob = blob;

    if (!payload.empty()) {
        JdspSendToActive(payload);
    }
}

std::string JdspConfigDialog::SerializeSettings() const {
    std::string s;
    char buf[64];
    auto kv = [&](const char* k, const char* v) { s += k; s += '='; s += v; s += '\n'; };
    auto kvi = [&](const std::string& k, const char* v) { s += k; s += '='; s += v; s += '\n'; };

    for (int i = 0; i < 13; i++) kv(kModuleKeys[i], m_modules[i] ? "1" : "0");

    for (int i = 0; i < 10; i++) {
        char kk[32];
        sprintf_s(kk, "eq.band%d", i);
        std::string base(kk);
        sprintf_s(buf, "%.3f", m_eq_bands[i].frequency); kvi(base + ".freq", buf);
        sprintf_s(buf, "%.3f", m_eq_bands[i].gain);      kvi(base + ".gain", buf);
        sprintf_s(buf, "%.4f", m_eq_bands[i].q);         kvi(base + ".q", buf);
        kv((base + ".enable").c_str(), m_eq_bands[i].enabled ? "1" : "0");
    }

    sprintf_s(buf, "%.2f", m_comp_threshold); kv("compressor.threshold", buf);
    sprintf_s(buf, "%.2f", m_comp_ratio);     kv("compressor.ratio", buf);
    sprintf_s(buf, "%.2f", m_comp_attack);    kv("compressor.attack", buf);
    sprintf_s(buf, "%.2f", m_comp_release);   kv("compressor.release", buf);
    sprintf_s(buf, "%.2f", m_lim_threshold);  kv("limiter.threshold", buf);
    sprintf_s(buf, "%.2f", m_lim_release);    kv("limiter.release", buf);
    sprintf_s(buf, "%.2f", m_ddc_strength);   kv("ddc.strength", buf);
    sprintf_s(buf, "%.2f", m_bass_boost);     kv("bassboost.gain", buf);
    sprintf_s(buf, "%.2f", m_bass_freq);      kv("bassboost.freq", buf);
    sprintf_s(buf, "%.2f", m_stereo_width);   kv("stereo.width", buf);
    sprintf_s(buf, "%.4f", m_reverb_room);    kv("reverb.roomsize", buf);
    sprintf_s(buf, "%.4f", m_reverb_damp);    kv("reverb.damping", buf);
    sprintf_s(buf, "%.4f", m_reverb_wet);     kv("reverb.wet", buf);
    sprintf_s(buf, "%.2f", m_tube_drive);     kv("tube.drive", buf);
    sprintf_s(buf, "%.2f", m_bs2b_feed);      kv("bs2b.feed", buf);
    sprintf_s(buf, "%.2f", m_bs2b_freq);      kv("bs2b.freq", buf);
    sprintf_s(buf, "%.2f", m_conv_gain);      kv("convolver.gain", buf);
    kv("convolver.enable", m_conv_enabled ? "1" : "0");
    kv("script.enable", m_script_enabled ? "1" : "0");

    sprintf_s(buf, "%d", m_current_lang);     kv("ui.language", buf);

    char u8[8192 * 3];
    if (m_ir_path[0]) {
        WideCharToMultiByte(CP_UTF8, 0, m_ir_path, -1, u8, sizeof(u8), NULL, NULL);
        kvi("convolver.path", EscapeValue(u8).c_str());
    }
    if (m_ddc_profile[0]) {
        WideCharToMultiByte(CP_UTF8, 0, m_ddc_profile, -1, u8, sizeof(u8), NULL, NULL);
        kvi("ddc.profile", EscapeValue(u8).c_str());
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
        for (int i = 0; i < 13; i++) {
            if (k == kModuleKeys[i]) { m_modules[i] = (v == "1" || v == "true"); handled = true; break; }
        }
        if (handled) continue;

        if (k.compare(0, 8, "eq.band") == 0) {
            int band = atoi(k.c_str() + 8);
            if (band >= 0 && band < 10) {
                size_t dot = k.rfind('.');
                std::string p = (dot == std::string::npos) ? std::string() : k.substr(dot + 1);
                if (p == "freq") m_eq_bands[band].frequency = (float)atof(v.c_str());
                else if (p == "gain") m_eq_bands[band].gain = (float)atof(v.c_str());
                else if (p == "q") m_eq_bands[band].q = (float)atof(v.c_str());
                else if (p == "enable") m_eq_bands[band].enabled = (v == "1" || v == "true");
            }
        }
        else if (k == "compressor.threshold") m_comp_threshold = (float)atof(v.c_str());
        else if (k == "compressor.ratio")     m_comp_ratio = (float)atof(v.c_str());
        else if (k == "compressor.attack")    m_comp_attack = (float)atof(v.c_str());
        else if (k == "compressor.release")   m_comp_release = (float)atof(v.c_str());
        else if (k == "limiter.threshold")    m_lim_threshold = (float)atof(v.c_str());
        else if (k == "limiter.release")      m_lim_release = (float)atof(v.c_str());
        else if (k == "ddc.strength")         m_ddc_strength = (float)atof(v.c_str());
        else if (k == "bassboost.gain")       m_bass_boost = (float)atof(v.c_str());
        else if (k == "bassboost.freq")       m_bass_freq = (float)atof(v.c_str());
        else if (k == "stereo.width")         m_stereo_width = (float)atof(v.c_str());
        else if (k == "reverb.roomsize")      m_reverb_room = (float)atof(v.c_str());
        else if (k == "reverb.damping")       m_reverb_damp = (float)atof(v.c_str());
        else if (k == "reverb.wet")           m_reverb_wet = (float)atof(v.c_str());
        else if (k == "tube.drive")           m_tube_drive = (float)atof(v.c_str());
        else if (k == "bs2b.feed")            m_bs2b_feed = (float)atof(v.c_str());
        else if (k == "bs2b.freq")            m_bs2b_freq = (float)atof(v.c_str());
        else if (k == "convolver.gain")       m_conv_gain = (float)atof(v.c_str());
        else if (k == "convolver.enable")     m_conv_enabled = (v == "1" || v == "true");
        else if (k == "script.enable")        m_script_enabled = (v == "1" || v == "true");
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
        else if (k == "script.text") {
            MultiByteToWideChar(CP_UTF8, 0, v.c_str(), -1, m_script_text, _countof(m_script_text));
        }
    }
}

void JdspConfigDialog::OnApply(HWND hwnd) {
    SyncFromControls(hwnd);
    m_settings_blob = SerializeSettings();

    m_ipc.SendSetParam("modules.analog", m_modules[0] ? "1" : "0");
    m_ipc.SendSetParam("modules.bs2b", m_modules[1] ? "1" : "0");
    m_ipc.SendSetParam("modules.ddc", m_modules[2] ? "1" : "0");
    m_ipc.SendSetParam("modules.limiter", m_modules[3] ? "1" : "0");
    m_ipc.SendSetParam("modules.compressor", m_modules[4] ? "1" : "0");
    m_ipc.SendSetParam("modules.convolver", m_modules[5] ? "1" : "0");
    m_ipc.SendSetParam("modules.reverb", m_modules[6] ? "1" : "0");
    m_ipc.SendSetParam("modules.bassboost", m_modules[7] ? "1" : "0");
    m_ipc.SendSetParam("modules.stereo", m_modules[8] ? "1" : "0");
    m_ipc.SendSetParam("modules.iir", m_modules[9] ? "1" : "0");

    for (int i = 0; i < 10; i++) {
        char key[64], val[64];
        sprintf_s(key, "eq.band%d.freq", i);
        sprintf_s(val, "%.1f", m_eq_bands[i].frequency);
        m_ipc.SendSetParam(key, val);
        sprintf_s(key, "eq.band%d.gain", i);
        sprintf_s(val, "%.2f", m_eq_bands[i].gain);
        m_ipc.SendSetParam(key, val);
        sprintf_s(key, "eq.band%d.q", i);
        sprintf_s(val, "%.3f", m_eq_bands[i].q);
        m_ipc.SendSetParam(key, val);
        sprintf_s(key, "eq.band%d.enable", i);
        m_ipc.SendSetParam(key, m_eq_bands[i].enabled ? "1" : "0");
    }

    char val[64];
    sprintf_s(val, "%.1f", m_comp_threshold);
    m_ipc.SendSetParam("compressor.threshold", val);
    sprintf_s(val, "%.1f", m_comp_ratio);
    m_ipc.SendSetParam("compressor.ratio", val);
    sprintf_s(val, "%.1f", m_comp_attack);
    m_ipc.SendSetParam("compressor.attack", val);
    sprintf_s(val, "%.1f", m_comp_release);
    m_ipc.SendSetParam("compressor.release", val);
    sprintf_s(val, "%.1f", m_lim_threshold);
    m_ipc.SendSetParam("limiter.threshold", val);
    sprintf_s(val, "%.1f", m_lim_release);
    m_ipc.SendSetParam("limiter.release", val);

    sprintf_s(val, "%.1f", m_bass_boost);
    m_ipc.SendSetParam("bassboost.gain", val);
    sprintf_s(val, "%.1f", m_bass_freq);
    m_ipc.SendSetParam("bassboost.freq", val);
    sprintf_s(val, "%.1f", m_stereo_width);
    m_ipc.SendSetParam("stereo.width", val);
    sprintf_s(val, "%.2f", m_reverb_room);
    m_ipc.SendSetParam("reverb.roomsize", val);
    sprintf_s(val, "%.2f", m_reverb_damp);
    m_ipc.SendSetParam("reverb.damping", val);
    sprintf_s(val, "%.2f", m_reverb_wet);
    m_ipc.SendSetParam("reverb.wet", val);
    sprintf_s(val, "%.1f", m_tube_drive);
    m_ipc.SendSetParam("tube.drive", val);
    sprintf_s(val, "%.1f", m_bs2b_feed);
    m_ipc.SendSetParam("bs2b.feed", val);
    sprintf_s(val, "%.1f", m_bs2b_freq);
    m_ipc.SendSetParam("bs2b.freq", val);
    sprintf_s(val, "%.1f", m_conv_gain);
    m_ipc.SendSetParam("convolver.gain", val);
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
    HWND tab = m_tab_dialogs[0];
    if (!tab) return;
    const StrTable* str = (m_current_lang == 0) ? &g_str_en : &g_str_zh;

    int checkboxes[] = {
        IDC_CHK_ANALOG, IDC_CHK_BS2B, IDC_CHK_DDC, IDC_CHK_LIMITER,
        IDC_CHK_COMPRESSOR, IDC_CHK_CONVOLVER, IDC_CHK_REVERB,
        IDC_CHK_BASS_BOOST, IDC_CHK_STEREO, IDC_CHK_IIR,
        IDC_CHK_SPECTRUM, IDC_CHK_DYNAMIC_SYS, IDC_CHK_EEL2
    };

    const wchar_t* labels[] = {
        str->mod_analog, str->mod_bs2b, str->mod_ddc, str->mod_limiter,
        str->mod_compressor, str->mod_convolver, str->mod_reverb,
        str->mod_bass_boost, str->mod_stereo, str->mod_iir,
        str->mod_spectrum, str->mod_dynamic, str->mod_eel2
    };

    for (int i = 0; i < 13; i++) {
        HWND chk = GetDlgItem(tab, checkboxes[i]);
        if (chk) {
            SetWindowTextW(chk, labels[i]);
            Button_SetCheck(chk, m_modules[i] ? BST_CHECKED : BST_UNCHECKED);
        }
    }
}

void JdspConfigDialog::ApplyModulesTab(HWND hwnd) {
    HWND tab = m_tab_dialogs[0];
    if (!tab) return;
    int checkboxes[] = {
        IDC_CHK_ANALOG, IDC_CHK_BS2B, IDC_CHK_DDC, IDC_CHK_LIMITER,
        IDC_CHK_COMPRESSOR, IDC_CHK_CONVOLVER, IDC_CHK_REVERB,
        IDC_CHK_BASS_BOOST, IDC_CHK_STEREO, IDC_CHK_IIR,
        IDC_CHK_SPECTRUM, IDC_CHK_DYNAMIC_SYS, IDC_CHK_EEL2
    };
    for (int i = 0; i < 13; i++) {
        HWND chk = GetDlgItem(tab, checkboxes[i]);
        if (chk) m_modules[i] = (Button_GetCheck(chk) == BST_CHECKED);
    }
}

// ===== EQ Tab =====
void JdspConfigDialog::UpdateEqBandLabel(HWND tab, int band) {
    if (!tab || band < 0 || band >= 10) return;
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
    HWND tab = m_tab_dialogs[1];
    if (!tab) return;
    m_eq_widget.SetBands(m_eq_bands);

    EnsureEqClassRegistered();

    if (!m_eq_widget.GetHWND()) {
        RECT rc = {};
        GetClientRect(tab, &rc);
        int cw = rc.right - rc.left;
        m_eq_widget.Create(tab, 8, 8, cw > 60 ? cw - 16 : 440, 250);
    } else {
        m_eq_widget.Refresh();
    }

    for (int i = 0; i < 10; i++) {
        HWND sl = GetDlgItem(tab, IDC_SLIDER_EQ_BAND0 + i);
        if (sl) {
            SendMessageW(sl, TBM_SETRANGE, TRUE, MAKELONG(-120, 120));
            SendMessageW(sl, TBM_SETTICFREQ, 20, 0);
            SendMessageW(sl, TBM_SETPOS, TRUE, (LPARAM)(int)(-m_eq_bands[i].gain * 10.0f));
        }
        UpdateEqBandLabel(tab, i);
    }

    HWND combo = GetDlgItem(tab, IDC_COMBO_EQ_BAND);
    if (combo) {
        SendMessageW(combo, CB_RESETCONTENT, 0, 0);
        for (int i = 0; i < 10; i++) {
            wchar_t buf[32];
            swprintf_s(buf, L"Band %d", i + 1);
            SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)buf);
        }
        SendMessageW(combo, CB_SETCURSEL, m_eq_widget.GetSelectedBand(), 0);
    }
    const EqBand& b = m_eq_bands[m_eq_widget.GetSelectedBand()];
    wchar_t buf[32];
    HWND freq_e = GetDlgItem(tab, IDC_EDIT_EQ_FREQ);
    HWND q_e = GetDlgItem(tab, IDC_EDIT_EQ_Q);
    if (freq_e) { swprintf_s(buf, L"%.0f", b.frequency); SetWindowTextW(freq_e, buf); }
    if (q_e) { swprintf_s(buf, L"%.2f", b.q); SetWindowTextW(q_e, buf); }
}

void JdspConfigDialog::ApplyEqTab(HWND hwnd) {
    HWND tab = m_tab_dialogs[1];
    if (!tab) return;

    for (int i = 0; i < 10; i++) {
        HWND sl = GetDlgItem(tab, IDC_SLIDER_EQ_BAND0 + i);
        if (sl) m_eq_bands[i].gain = -(float)SendMessageW(sl, TBM_GETPOS, 0, 0) / 10.0f;
    }

    int sel = m_eq_widget.GetSelectedBand();
    if (sel < 0 || sel > 9) sel = 0;
    wchar_t buf[32];
    HWND freq_e = GetDlgItem(tab, IDC_EDIT_EQ_FREQ);
    if (freq_e) {
        GetWindowTextW(freq_e, buf, 32);
        float f = (float)_wtof(buf);
        if (f < 20.0f) f = 20.0f;
        if (f > 20000.0f) f = 20000.0f;
        m_eq_bands[sel].frequency = f;
        swprintf_s(buf, L"%.0f", f);
        SetWindowTextW(freq_e, buf);
    }
    HWND q_e = GetDlgItem(tab, IDC_EDIT_EQ_Q);
    if (q_e) {
        GetWindowTextW(q_e, buf, 32);
        float q = (float)_wtof(buf);
        if (q < 0.1f) q = 0.1f;
        if (q > 10.0f) q = 10.0f;
        m_eq_bands[sel].q = q;
        swprintf_s(buf, L"%.2f", q);
        SetWindowTextW(q_e, buf);
    }
    m_eq_widget.SetBands(m_eq_bands);
    UpdateEqBandLabel(tab, sel);
}

// ===== Dynamics Tab =====
void JdspConfigDialog::InitDynamicsTab(HWND hwnd) {
    HWND tab = m_tab_dialogs[2];
    if (!tab) return;

    auto set_slider = [&](int id, int min_v, int max_v, int pos) {
        HWND sl = GetDlgItem(tab, id);
        if (sl) { SendMessageW(sl, TBM_SETRANGE, TRUE, MAKELONG(min_v, max_v)); SendMessageW(sl, TBM_SETPOS, TRUE, pos); }
    };

    HWND chk;
    chk = GetDlgItem(tab, IDC_CHK_COMP);
    if (chk) Button_SetCheck(chk, BST_CHECKED);
    set_slider(IDC_SLIDER_COMP_THRESH, -60, 0, (int)m_comp_threshold);
    set_slider(IDC_SLIDER_COMP_RATIO, 1, 20, (int)m_comp_ratio);
    set_slider(IDC_SLIDER_COMP_ATTACK, 1, 100, (int)m_comp_attack);
    set_slider(IDC_SLIDER_COMP_RELEASE, 1, 500, (int)m_comp_release);

    chk = GetDlgItem(tab, IDC_CHK_LIM);
    if (chk) Button_SetCheck(chk, BST_UNCHECKED);
    set_slider(IDC_SLIDER_LIM_THRESH, -20, 0, (int)m_lim_threshold);
    set_slider(IDC_SLIDER_LIM_RELEASE, 1, 200, (int)m_lim_release);

    chk = GetDlgItem(tab, IDC_CHK_DDC_ENABLE);
    if (chk) Button_SetCheck(chk, BST_UNCHECKED);
    set_slider(IDC_SLIDER_DDC_STRENGTH, 0, 100, (int)m_ddc_strength);

    UpdateSliderLabel(tab, IDC_SLIDER_COMP_THRESH, IDC_STATIC_COMP_THRESH, L"%.0f dB", m_comp_threshold);
    UpdateSliderLabel(tab, IDC_SLIDER_COMP_RATIO, IDC_STATIC_COMP_RATIO, L"%.0f:1", m_comp_ratio);
    UpdateSliderLabel(tab, IDC_SLIDER_COMP_ATTACK, IDC_STATIC_COMP_ATTACK, L"%.0f ms", m_comp_attack);
    UpdateSliderLabel(tab, IDC_SLIDER_COMP_RELEASE, IDC_STATIC_COMP_RELEASE, L"%.0f ms", m_comp_release);
    UpdateSliderLabel(tab, IDC_SLIDER_LIM_THRESH, IDC_STATIC_LIM_THRESH, L"%.0f dB", m_lim_threshold);
    UpdateSliderLabel(tab, IDC_SLIDER_LIM_RELEASE, IDC_STATIC_LIM_RELEASE, L"%.0f ms", m_lim_release);
    UpdateSliderLabel(tab, IDC_SLIDER_DDC_STRENGTH, IDC_STATIC_DDC_STRENGTH, L"%.0f%%", m_ddc_strength);
}

void JdspConfigDialog::ApplyDynamicsTab(HWND hwnd) {
    HWND tab = m_tab_dialogs[2];
    if (!tab) return;
    auto get_slider = [&](int id) -> float { HWND sl = GetDlgItem(tab, id); return sl ? (float)SendMessageW(sl, TBM_GETPOS, 0, 0) : 0; };
    m_comp_threshold = get_slider(IDC_SLIDER_COMP_THRESH);
    m_comp_ratio = get_slider(IDC_SLIDER_COMP_RATIO);
    m_comp_attack = get_slider(IDC_SLIDER_COMP_ATTACK);
    m_comp_release = get_slider(IDC_SLIDER_COMP_RELEASE);
    m_lim_threshold = get_slider(IDC_SLIDER_LIM_THRESH);
    m_lim_release = get_slider(IDC_SLIDER_LIM_RELEASE);
    m_ddc_strength = get_slider(IDC_SLIDER_DDC_STRENGTH);
}

// ===== Effects Tab =====
void JdspConfigDialog::InitEffectsTab(HWND hwnd) {
    HWND tab = m_tab_dialogs[3];
    if (!tab) return;

    auto set_slider = [&](int id, int min_v, int max_v, int pos) {
        HWND sl = GetDlgItem(tab, id);
        if (sl) { SendMessageW(sl, TBM_SETRANGE, TRUE, MAKELONG(min_v, max_v)); SendMessageW(sl, TBM_SETPOS, TRUE, pos); }
    };

    HWND chk;
    chk = GetDlgItem(tab, IDC_CHK_BASS_EFFECT);
    if (chk) Button_SetCheck(chk, BST_CHECKED);
    set_slider(IDC_SLIDER_BASS_BOOST, 0, 20, (int)m_bass_boost);
    set_slider(IDC_SLIDER_BASS_FREQ, 20, 200, (int)m_bass_freq);

    chk = GetDlgItem(tab, IDC_CHK_STEREO_EFFECT);
    if (chk) Button_SetCheck(chk, BST_CHECKED);
    set_slider(IDC_SLIDER_STEREO_WIDTH, 0, 200, (int)m_stereo_width);

    chk = GetDlgItem(tab, IDC_CHK_REVERB_EFFECT);
    if (chk) Button_SetCheck(chk, BST_CHECKED);
    set_slider(IDC_SLIDER_REVERB_ROOM, 0, 100, (int)(m_reverb_room * 100));
    set_slider(IDC_SLIDER_REVERB_DAMP, 0, 100, (int)(m_reverb_damp * 100));
    set_slider(IDC_SLIDER_REVERB_WET, 0, 100, (int)(m_reverb_wet * 100));

    chk = GetDlgItem(tab, IDC_CHK_TUBE_EFFECT);
    if (chk) Button_SetCheck(chk, BST_CHECKED);
    set_slider(IDC_SLIDER_TUBE_DRIVE, 0, 100, (int)m_tube_drive);

    chk = GetDlgItem(tab, IDC_CHK_BS2B_EFFECT);
    if (chk) Button_SetCheck(chk, BST_CHECKED);
    set_slider(IDC_SLIDER_BS2B_FEED, 0, 150, (int)m_bs2b_feed);
    set_slider(IDC_SLIDER_BS2B_FREQ, 300, 2000, (int)m_bs2b_freq);

    UpdateSliderLabel(tab, IDC_SLIDER_BASS_BOOST, IDC_STATIC_BASS_BOOST, L"%.0f dB", m_bass_boost);
    UpdateSliderLabel(tab, IDC_SLIDER_BASS_FREQ, IDC_STATIC_BASS_FREQ, L"%.0f Hz", m_bass_freq);
    UpdateSliderLabel(tab, IDC_SLIDER_STEREO_WIDTH, IDC_STATIC_STEREO_WIDTH, L"%.0f%%", m_stereo_width);
    UpdateSliderLabel(tab, IDC_SLIDER_REVERB_ROOM, IDC_STATIC_REVERB_ROOM, L"%.2f", m_reverb_room);
    UpdateSliderLabel(tab, IDC_SLIDER_REVERB_DAMP, IDC_STATIC_REVERB_DAMP, L"%.2f", m_reverb_damp);
    UpdateSliderLabel(tab, IDC_SLIDER_REVERB_WET, IDC_STATIC_REVERB_WET, L"%.2f", m_reverb_wet);
    UpdateSliderLabel(tab, IDC_SLIDER_TUBE_DRIVE, IDC_STATIC_TUBE_DRIVE, L"%.0f%%", m_tube_drive);
    UpdateSliderLabel(tab, IDC_SLIDER_BS2B_FEED, IDC_STATIC_BS2B_FEED, L"%.0f", m_bs2b_feed);
    UpdateSliderLabel(tab, IDC_SLIDER_BS2B_FREQ, IDC_STATIC_BS2B_FREQ, L"%.0f Hz", m_bs2b_freq);
}

void JdspConfigDialog::ApplyEffectsTab(HWND hwnd) {
    HWND tab = m_tab_dialogs[3];
    if (!tab) return;
    auto get_slider = [&](int id) -> float { HWND sl = GetDlgItem(tab, id); return sl ? (float)SendMessageW(sl, TBM_GETPOS, 0, 0) : 0; };
    m_bass_boost = get_slider(IDC_SLIDER_BASS_BOOST);
    m_bass_freq = get_slider(IDC_SLIDER_BASS_FREQ);
    m_stereo_width = get_slider(IDC_SLIDER_STEREO_WIDTH);
    m_reverb_room = get_slider(IDC_SLIDER_REVERB_ROOM) / 100.0f;
    m_reverb_damp = get_slider(IDC_SLIDER_REVERB_DAMP) / 100.0f;
    m_reverb_wet = get_slider(IDC_SLIDER_REVERB_WET) / 100.0f;
    m_tube_drive = get_slider(IDC_SLIDER_TUBE_DRIVE);
    m_bs2b_feed = get_slider(IDC_SLIDER_BS2B_FEED);
    m_bs2b_freq = get_slider(IDC_SLIDER_BS2B_FREQ);
}

// ===== Convolver Tab =====
void JdspConfigDialog::InitConvolverTab(HWND hwnd) {
    HWND tab = m_tab_dialogs[4];
    if (!tab) return;

    HWND sl = GetDlgItem(tab, IDC_SLIDER_CONV_GAIN);
    if (sl) { SendMessageW(sl, TBM_SETRANGE, TRUE, MAKELONG(-20, 20)); SendMessageW(sl, TBM_SETPOS, TRUE, (int)m_conv_gain); }
    UpdateSliderLabel(tab, IDC_SLIDER_CONV_GAIN, IDC_STATIC_CONV_GAIN, L"%.0f dB", m_conv_gain);
}

void JdspConfigDialog::ApplyConvolverTab(HWND hwnd) {
    HWND tab = m_tab_dialogs[4];
    if (!tab) return;
    HWND sl = GetDlgItem(tab, IDC_SLIDER_CONV_GAIN);
    if (sl) m_conv_gain = (float)SendMessageW(sl, TBM_GETPOS, 0, 0);
}

// ===== Script Tab =====
void JdspConfigDialog::InitScriptTab(HWND hwnd) {
    HWND tab = m_tab_dialogs[5];
    if (!tab) return;
    HWND edit = GetDlgItem(tab, IDC_EDIT_SCRIPT);
    if (edit && m_script_text[0]) {
        SetWindowTextW(edit, m_script_text);
    }
}

void JdspConfigDialog::ApplyScriptTab(HWND hwnd) {
    HWND tab = m_tab_dialogs[5];
    if (!tab) return;
    HWND edit = GetDlgItem(tab, IDC_EDIT_SCRIPT);
    if (edit) {
        GetWindowTextW(edit, m_script_text, _countof(m_script_text));
    }
}
