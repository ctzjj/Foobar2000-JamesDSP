#include "jdsp_engine.h"
#include "jdsp_ipc_server.h"
#include <cstdio>
#include <io.h>
#include <fcntl.h>
#include <windows.h>

FILE* g_log = NULL;

static void Log(const char* msg) {
    if (g_log) {
        fprintf(g_log, "%s\n", msg);
        fflush(g_log);
    }
}

int main() {
    g_log = fopen("jdsp_host.log", "w");
    if (!g_log) {
        // Try temp directory
        char tmppath[MAX_PATH];
        GetTempPathA(MAX_PATH, tmppath);
        strcat_s(tmppath, "jdsp_host.log");
        g_log = fopen(tmppath, "w");
    }
    Log("jdsp_host starting");

    int r1 = _setmode(_fileno(stdin), _O_BINARY);
    int r2 = _setmode(_fileno(stdout), _O_BINARY);
    char buf[128];
    sprintf_s(buf, "stdin binary mode: %d, stdout binary mode: %d", r1, r2);
    Log(buf);

    JdspEngine engine;
    Log("Initializing engine...");
    if (!engine.Initialize(44100, 2)) {
        Log("FAILED to initialize engine");
        if (g_log) fclose(g_log);
        return 1;
    }
    Log("Engine initialized OK, entering main loop");

    JdspIpcServer server(engine);
    bool ran = server.Run();
    sprintf_s(buf, "Server.Run() returned: %d", (int)ran);
    Log(buf);

    engine.Shutdown();
    Log("Engine shut down, exiting");
    if (g_log) fclose(g_log);
    return 0;
}
