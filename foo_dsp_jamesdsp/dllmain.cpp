#include "stdafx.h"
#include "jdsp_eq_widget.h"

HMODULE GetMyModule() {
    HMODULE hMod = NULL;
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (LPCWSTR)&GetMyModule, &hMod);
    return hMod;
}

static BOOL g_eq_class_registered = FALSE;

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        GetMyModule();
    }
    return TRUE;
}

void EnsureEqClassRegistered() {
    if (g_eq_class_registered) return;
    JdspEqWidget::RegisterClass(GetMyModule());
    g_eq_class_registered = TRUE;
}
