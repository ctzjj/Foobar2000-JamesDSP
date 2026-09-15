#include "stdafx.h"
#include "jdsp_host_manager.h"

JdspHostManager::JdspHostManager() {}

JdspHostManager::~JdspHostManager() {
    Stop();
}

bool JdspHostManager::Start(const wchar_t* host_exe_path) {
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    if (!CreatePipe(&m_hStdinRead, &m_hStdinWrite, &sa, 0)) return false;
    SetHandleInformation(m_hStdinWrite, HANDLE_FLAG_INHERIT, 0);

    if (!CreatePipe(&m_hStdoutRead, &m_hStdoutWrite, &sa, 0)) return false;
    SetHandleInformation(m_hStdoutRead, HANDLE_FLAG_INHERIT, 0);

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.hStdInput = m_hStdinRead;
    si.hStdOutput = m_hStdoutWrite;
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    si.dwFlags = STARTF_USESTDHANDLES;

    if (!CreateProcessW(host_exe_path, NULL, NULL, NULL,
                        TRUE, 0, NULL, NULL, &si, &pi)) {
        Stop();
        return false;
    }

    m_hProcess = pi.hProcess;
    CloseHandle(pi.hThread);
    CloseHandle(m_hStdinRead); m_hStdinRead = NULL;
    CloseHandle(m_hStdoutWrite); m_hStdoutWrite = NULL;

    return true;
}

void JdspHostManager::Stop() {
    if (m_hProcess) {
        TerminateProcess(m_hProcess, 0);
        WaitForSingleObject(m_hProcess, 1000);
        CloseHandle(m_hProcess);
        m_hProcess = NULL;
    }
    if (m_hStdinWrite) { CloseHandle(m_hStdinWrite); m_hStdinWrite = NULL; }
    if (m_hStdoutRead) { CloseHandle(m_hStdoutRead); m_hStdoutRead = NULL; }
    if (m_hStdinRead) { CloseHandle(m_hStdinRead); m_hStdinRead = NULL; }
    if (m_hStdoutWrite) { CloseHandle(m_hStdoutWrite); m_hStdoutWrite = NULL; }
}

bool JdspHostManager::IsRunning() const {
    if (!m_hProcess) return false;
    DWORD exit_code;
    return GetExitCodeProcess(m_hProcess, &exit_code) && exit_code == STILL_ACTIVE;
}
