#pragma once
#include <string>

class JdspIpcClient;

// Registers the client whose host process is currently processing audio.
void JdspSetActiveClient(JdspIpcClient* client);

// Clears the registration, but only if it still points at the given client.
void JdspClearActiveClient(JdspIpcClient* client);

// Pushes a "key=value\n" blob to the active host. Returns false when no host is
// running. Locked, so it can run on the config dialog's thread while the
// playback thread is streaming audio.
bool JdspSendToActive(const std::string& blob);
