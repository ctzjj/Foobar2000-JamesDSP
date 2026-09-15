#include "stdafx.h"
#include "jdsp_config_dialog.h"
#include "resource.h"

JdspConfigDialog::JdspConfigDialog(JdspIpcClient& ipc) : m_ipc(ipc) {}

void JdspConfigDialog::Show(HWND parent) {
    DialogBoxParam(GetModuleHandle(NULL),
                   MAKEINTRESOURCE(IDD_JDSP_CONFIG),
                   parent, DialogProc, reinterpret_cast<LPARAM>(this));
}

INT_PTR CALLBACK JdspConfigDialog::DialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    JdspConfigDialog* dlg = reinterpret_cast<JdspConfigDialog*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

    switch (msg) {
        case WM_INITDIALOG:
            dlg = reinterpret_cast<JdspConfigDialog*>(lParam);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(dlg));
            dlg->OnInitDialog(hwnd);
            return TRUE;

        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK) {
                dlg->OnApply(hwnd);
                EndDialog(hwnd, IDOK);
                return TRUE;
            }
            if (LOWORD(wParam) == IDCANCEL) {
                EndDialog(hwnd, IDCANCEL);
                return TRUE;
            }
            break;
    }
    return FALSE;
}

void JdspConfigDialog::OnInitDialog(HWND hwnd) {
    m_hwnd = hwnd;
}

void JdspConfigDialog::OnApply(HWND hwnd) {
    // TODO: Collect settings and send via IPC
}
