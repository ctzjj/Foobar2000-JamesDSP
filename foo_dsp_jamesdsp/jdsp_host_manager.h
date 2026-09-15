#pragma once
#include <windows.h>

class JdspHostManager {
public:
    JdspHostManager();
    ~JdspHostManager();

    bool Start(const wchar_t* host_exe_path);
    void Stop();
    bool IsRunning() const;

    HANDLE GetStdinWrite() const { return m_hStdinWrite; }
    HANDLE GetStdoutRead() const { return m_hStdoutRead; }

private:
    HANDLE m_hProcess = NULL;
    HANDLE m_hStdinRead = NULL;
    HANDLE m_hStdinWrite = NULL;
    HANDLE m_hStdoutRead = NULL;
    HANDLE m_hStdoutWrite = NULL;
};
