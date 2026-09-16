#include "stdafx.h"
#include "jdsp_live_link.h"
#include "jdsp_ipc_client.h"

namespace {

struct LiveLinkState {
    CRITICAL_SECTION cs;
    JdspIpcClient* active;
    LiveLinkState() : active(nullptr) { InitializeCriticalSection(&cs); }
    ~LiveLinkState() { DeleteCriticalSection(&cs); }
};

LiveLinkState g_live;

}  // namespace

void JdspSetActiveClient(JdspIpcClient* client) {
    EnterCriticalSection(&g_live.cs);
    g_live.active = client;
    LeaveCriticalSection(&g_live.cs);
}

void JdspClearActiveClient(JdspIpcClient* client) {
    EnterCriticalSection(&g_live.cs);
    if (g_live.active == client) g_live.active = nullptr;
    LeaveCriticalSection(&g_live.cs);
}

bool JdspSendToActive(const std::string& blob) {
    if (blob.empty()) return false;
    EnterCriticalSection(&g_live.cs);
    bool ok = false;
    if (g_live.active) ok = g_live.active->SendSetParams(blob);
    LeaveCriticalSection(&g_live.cs);
    return ok;
}
