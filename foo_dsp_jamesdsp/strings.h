#pragma once
#include <windows.h>

struct StrTable {
    const wchar_t* window_title;
    const wchar_t* btn_ok;
    const wchar_t* btn_cancel;
    const wchar_t* btn_save_config;
    const wchar_t* btn_load_config;
    const wchar_t* btn_reset_all;
    const wchar_t* btn_browse;
    const wchar_t* tab_modules;
    const wchar_t* tab_eq;
    const wchar_t* tab_dynamics;
    const wchar_t* tab_effects;
    const wchar_t* tab_convolver;
    const wchar_t* tab_script;
    const wchar_t* lbl_enable;
    const wchar_t* lbl_disable;
    const wchar_t* lbl_frequency;
    const wchar_t* lbl_gain;
    const wchar_t* lbl_q_factor;
    const wchar_t* lbl_threshold;
    const wchar_t* lbl_ratio;
    const wchar_t* lbl_attack;
    const wchar_t* lbl_release;
    const wchar_t* lbl_strength;
    const wchar_t* lbl_width;
    const wchar_t* lbl_room_size;
    const wchar_t* lbl_damping;
    const wchar_t* lbl_wet_level;
    const wchar_t* lbl_dry_level;
    const wchar_t* lbl_drive;
    const wchar_t* lbl_feed;
    const wchar_t* lbl_ir_file;
    const wchar_t* lbl_ir_info;
    const wchar_t* lbl_gain_db;
    const wchar_t* lbl_script;
    const wchar_t* lbl_status;
    const wchar_t* lbl_language;
    const wchar_t* mod_analog;
    const wchar_t* mod_bs2b;
    const wchar_t* mod_ddc;
    const wchar_t* mod_limiter;
    const wchar_t* mod_compressor;
    const wchar_t* mod_convolver;
    const wchar_t* mod_reverb;
    const wchar_t* mod_bass_boost;
    const wchar_t* mod_stereo;
    const wchar_t* mod_iir;
    const wchar_t* mod_spectrum;
    const wchar_t* mod_dynamic;
    const wchar_t* mod_eel2;
};

static const StrTable g_str_en = {
    L"JamesDSP Settings",
    L"OK",
    L"Cancel",
    L"Save Config",
    L"Load Config",
    L"Reset All",
    L"Browse...",
    L"Modules",
    L"EQ",
    L"Dynamics",
    L"Effects",
    L"Convolver",
    L"Script",
    L"Enable",
    L"Disable",
    L"Frequency",
    L"Gain",
    L"Q Factor",
    L"Threshold",
    L"Ratio",
    L"Attack",
    L"Release",
    L"Strength",
    L"Width",
    L"Room Size",
    L"Damping",
    L"Wet Level",
    L"Dry Level",
    L"Drive",
    L"Feed",
    L"Impulse Response",
    L"IR Info",
    L"Gain (dB)",
    L"Script",
    L"Status",
    L"Language",
    L"Analog Modelling",
    L"BS2B Crossfeed",
    L"DDC",
    L"Limiter",
    L"Compressor",
    L"Convolver",
    L"Reverb",
    L"Bass Boost",
    L"Stereo Widener",
    L"IIR Filters",
    L"Spectrum Extender",
    L"Dynamic System",
    L"EEL2 Scripting",
};

static const StrTable g_str_zh = {
    L"\x004a\x0061\x006d\x0065\x0073\x0044\x0053\x0050 \x8bbe\x7f6e",
    L"\x786e\x5b9a",
    L"\x53d6\x6d88",
    L"\x4fdd\x5b58\x914d\x7f6e",
    L"\x52a0\x8f7d\x914d\x7f6e",
    L"\x91cd\x7f6e\x5168\x90e8",
    L"\x6d4f\x89c8...",
    L"\x6a21\x5757",
    L"\x5747\x8861\x5668",
    L"\x52a8\x6001",
    L"\x97f3\x6548",
    L"\x5377\x79ef\x5668",
    L"\x811a\x672c",
    L"\x542f\x7528",
    L"\x7981\x7528",
    L"\x9891\x7387",
    L"\x589e\x76ca",
    L"Q \x56e0\x5b50",
    L"\x9608\x503c",
    L"\x6bd4\x7387",
    L"\x8d77\x59cb\x65f6\x95f4",
    L"\x91ca\x653e\x65f6\x95f4",
    L"\x5f3a\x5ea6",
    L"\x5bbd\x5ea6",
    L"\x623f\x95f4\x5927\x5c0f",
    L"\x963b\x5c3c",
    L"\x6e7f\x58f0\x7535\u5e73",
    L"\x5e72\u58f0\u7535\u5e73",
    L"\u9a71\u52a8",
    L"\u9988\u9001",
    L"\u8109\u51b2\u54cd\u5e94",
    L"IR \u4fe1\u606f",
    L"\u589e\u76ca (dB)",
    L"\u811a\u672c",
    L"\u72b6\u6001",
    L"\u8bed\u8a00",
    L"\u7535\u5b50\u7ba1\u6a21\u62df",
    L"BS2B \u8de8\u9988",
    L"DDC",
    L"\u9650\u5e45\u5668",
    L"\u538b\u7f29\u5668",
    L"\u5377\u79ef\u5668",
    L"\u6df7\u54cd",
    L"\u4f4e\u97f3\u589e\u5f3a",
    L"\u7acb\u4f53\u58f0\u589e\u5f3a",
    L"IIR \u6ee4\u6ce2\u5668",
    L"\u9891\u8c31\u6269\u5c55",
    L"\u52a8\u6001\u7cfb\u7edf",
    L"EEL2 \u811a\u672c",
};
