#pragma once
#include <windows.h>

struct StrTable {
    const wchar_t* window_title;
    const wchar_t* btn_ok;
    const wchar_t* btn_cancel;
    const wchar_t* btn_apply;
};

static const StrTable g_str_en = {
    L"JamesDSP Settings",
    L"OK",
    L"Cancel",
    L"Apply"
};
