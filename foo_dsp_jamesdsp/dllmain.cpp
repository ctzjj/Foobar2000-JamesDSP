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

static void VecLog(const char* msg) {
    FILE* f = fopen("jdsp_exc.log", "a");
    if (f) { fprintf(f, "%s\n", msg); fclose(f); }
}

static LONG CALLBACK JdspVehHandler(EXCEPTION_POINTERS* ep) {
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code == 0x40010006 || code == 0x4001000A || code == 0x406D1388 ||
        code == 0xE06D7363) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    char b[256];
    sprintf_s(b, "EXC code=0x%08X addr=%p", code, ep->ExceptionRecord->ExceptionAddress);
    VecLog(b);
    return EXCEPTION_CONTINUE_SEARCH;
}

static LONG WINAPI JdspUnhandledFilter(EXCEPTION_POINTERS* ep) {
    char b[256];
    sprintf_s(b, "UNHANDLED code=0x%08X addr=%p", ep->ExceptionRecord->ExceptionCode,
              ep->ExceptionRecord->ExceptionAddress);
    VecLog(b);
    return EXCEPTION_EXECUTE_HANDLER;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        GetMyModule();
        AddVectoredExceptionHandler(1, JdspVehHandler);
        SetUnhandledExceptionFilter(JdspUnhandledFilter);
    }
    return TRUE;
}

void EnsureEqClassRegistered() {
    if (g_eq_class_registered) return;
    JdspEqWidget::RegisterClass(GetMyModule());
    g_eq_class_registered = TRUE;
}
