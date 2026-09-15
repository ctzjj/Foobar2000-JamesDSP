#include "jdsp_engine.h"
#include "jdsp_ipc_server.h"

int main() {
    JdspEngine engine;
    if (!engine.Initialize(44100, 2)) {
        return 1;
    }

    JdspIpcServer server(engine);
    server.Run();

    engine.Shutdown();
    return 0;
}
