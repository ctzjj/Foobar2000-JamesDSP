#pragma once
#include <windows.h>
#include "jdsp_ipc_client.h"

class JdspConfigDialog {
public:
    JdspConfigDialog(JdspIpcClient& ipc);
    void Show(HWND parent);

private:
    static INT_PTR CALLBACK DialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void OnInitDialog(HWND hwnd);
    void OnApply(HWND hwnd);

    JdspIpcClient& m_ipc;
    HWND m_hwnd = NULL;
};
