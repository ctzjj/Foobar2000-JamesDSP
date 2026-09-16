#include "jdsp_ipc_server.h"
#include <cstdio>
#include <cstring>
#include <chrono>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#define READ_FD _read
#define WRITE_FD _write
#define FD_STDIN  _fileno(stdin)
#define FD_STDOUT _fileno(stdout)
#else
#include <unistd.h>
#define READ_FD read
#define WRITE_FD write
#define FD_STDIN  STDIN_FILENO
#define FD_STDOUT STDOUT_FILENO
#endif

extern FILE* g_log;

static void SvrLog(const char* msg) {
    if (g_log) { fprintf(g_log, "%s\n", msg); fflush(g_log); }
}

JdspIpcServer::JdspIpcServer(JdspEngine& engine) : m_engine(engine) {}

// Pipes may return short reads/writes; always loop until the full count is done.
static bool ReadFull(int fd, void* dst, size_t n) {
    size_t got = 0;
    char* p = static_cast<char*>(dst);
    while (got < n) {
        int r = READ_FD(fd, p + got, (unsigned)(n - got));
        if (r <= 0) return false;
        got += (size_t)r;
    }
    return true;
}

static bool WriteFull(int fd, const void* src, size_t n) {
    size_t sent = 0;
    const char* p = static_cast<const char*>(src);
    while (sent < n) {
        int r = WRITE_FD(fd, p + sent, (unsigned)(n - sent));
        if (r <= 0) return false;
        sent += (size_t)r;
    }
    return true;
}

static bool ReadFrame(int fd, jdsp::FrameHeader& header, std::vector<uint8_t>& payload) {
    if (!ReadFull(fd, &header, sizeof(header))) {
        SvrLog("ReadFrame: header read failed");
        return false;
    }

    payload.resize(header.data_length);
    if (header.data_length > 0) {
        if (!ReadFull(fd, payload.data(), header.data_length)) {
            char buf[64];
            sprintf_s(buf, "ReadFrame: payload read failed, len=%u", header.data_length);
            SvrLog(buf);
            return false;
        }
    }
    return true;
}

static bool WriteFrame(int fd, jdsp::FrameType type, const void* data, uint32_t length) {
    jdsp::FrameHeader header;
    header.type = type;
    header.data_length = length;

    if (!WriteFull(fd, &header, sizeof(header))) return false;
    if (data && length > 0) {
        if (!WriteFull(fd, data, length)) return false;
    }
    return true;
}

bool JdspIpcServer::Run() {
    SvrLog("Server::Run() entered");
    while (true) {
        jdsp::FrameHeader header;
        std::vector<uint8_t> payload;

        if (!ReadFrame(FD_STDIN, header, payload)) {
            SvrLog("Server: read failed, exiting loop");
            break;
        }

        switch (header.type) {
            case jdsp::FrameType::AUDIO_DATA: {
                std::vector<uint8_t> response;
                if (HandleAudioData(payload, response)) {
                    WriteFrame(FD_STDOUT, jdsp::FrameType::AUDIO_DATA,
                              response.data(), static_cast<uint32_t>(response.size()));
                }
                break;
            }
            case jdsp::FrameType::SET_PARAM:
                HandleSetParam(payload);
                break;
            case jdsp::FrameType::SHUTDOWN:
                SvrLog("Server: received shutdown");
                return true;
            default:
                break;
        }
    }
    return false;
}

bool JdspIpcServer::HandleAudioData(const std::vector<uint8_t>& payload, std::vector<uint8_t>& response) {
    if (payload.size() < sizeof(jdsp::AudioData)) return false;

    const jdsp::AudioData* hdr = reinterpret_cast<const jdsp::AudioData*>(payload.data());
    const float* audio = reinterpret_cast<const float*>(payload.data() + sizeof(jdsp::AudioData));

    uint32_t total_samples = hdr->sample_count * hdr->channels;
    response.resize(sizeof(jdsp::AudioData) + total_samples * sizeof(float));

    jdsp::AudioData* resp_hdr = reinterpret_cast<jdsp::AudioData*>(response.data());
    resp_hdr->sample_rate = hdr->sample_rate;
    resp_hdr->channels = hdr->channels;
    resp_hdr->sample_count = hdr->sample_count;

    float* resp_audio = reinterpret_cast<float*>(response.data() + sizeof(jdsp::AudioData));
    memcpy(resp_audio, audio, total_samples * sizeof(float));

    static double s_proc_ms = 0.0;
    static long long s_proc_n = 0;
    static std::vector<float> s_orig;
    s_orig.assign(resp_audio, resp_audio + total_samples);

    auto t0 = std::chrono::steady_clock::now();
    m_engine.Process(resp_audio, hdr->sample_count, hdr->channels);
    auto t1 = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    s_proc_ms += ms;
    s_proc_n++;

    double maxd = 0.0;
    long long mi = -1;
    long long ndiff = 0;
    double maxin = 0.0, maxout = 0.0;
    for (size_t i = 0; i < total_samples; i++) {
        double a = (double)s_orig[i]; if (a < 0) a = -a; if (a > maxin) maxin = a;
        double b = (double)resp_audio[i]; if (b < 0) b = -b; if (b > maxout) maxout = b;
        double d = (double)resp_audio[i] - (double)s_orig[i];
        if (d < 0) d = -d;
        if (d > 0.0) ndiff++;
        if (d > maxd) { maxd = d; mi = (long long)i; }
    }

    if (s_proc_n <= 120 || s_proc_n % 10 == 0) {
        char b[360];
        sprintf_s(b, "PROC: n=%lld avg=%.3fms samples=%u pkin=%.4f pkout=%.4f maxdiff=%.6f ndiff=%lld modules=%d",
                  s_proc_n, s_proc_ms / (double)s_proc_n,
                  hdr->sample_count, maxin, maxout, maxd,
                  ndiff, m_engine.AnyModuleEnabled() ? 1 : 0);
        SvrLog(b);
    }
    return true;
}

bool JdspIpcServer::HandleSetParam(const std::vector<uint8_t>& payload) {
    std::string blob(payload.begin(), payload.end());
    // Payload is one or more "key=value" entries separated by newlines.
    size_t pos = 0;
    while (pos < blob.size()) {
        size_t eol = blob.find('\n', pos);
        if (eol == std::string::npos) eol = blob.size();
        std::string line = blob.substr(pos, eol - pos);
        pos = eol + 1;
        if (line.empty()) continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        m_engine.SetParam(line.substr(0, eq), line.substr(eq + 1));
    }
    return true;
}
