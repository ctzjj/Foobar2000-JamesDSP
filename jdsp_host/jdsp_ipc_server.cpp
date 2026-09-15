#include "jdsp_ipc_server.h"
#include <cstdio>
#include <cstring>

#ifdef _WIN32
#include <io.h>
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

JdspIpcServer::JdspIpcServer(JdspEngine& engine) : m_engine(engine) {}

static bool ReadFrame(int fd, jdsp::FrameHeader& header, std::vector<uint8_t>& payload) {
    int bytes = READ_FD(fd, &header, sizeof(header));
    if (bytes != sizeof(header)) return false;

    payload.resize(header.data_length);
    if (header.data_length > 0) {
        bytes = READ_FD(fd, payload.data(), header.data_length);
        if (bytes != static_cast<int>(header.data_length)) return false;
    }
    return true;
}

static bool WriteFrame(int fd, jdsp::FrameType type, const void* data, uint32_t length) {
    jdsp::FrameHeader header;
    header.type = type;
    header.data_length = length;

    int written = WRITE_FD(fd, &header, sizeof(header));
    if (written != sizeof(header)) return false;

    if (data && length > 0) {
        written = WRITE_FD(fd, data, length);
        if (written != static_cast<int>(length)) return false;
    }
    return true;
}

bool JdspIpcServer::Run() {
    while (true) {
        jdsp::FrameHeader header;
        std::vector<uint8_t> payload;

        if (!ReadFrame(FD_STDIN, header, payload)) {
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

    m_engine.Process(resp_audio, hdr->sample_count, hdr->channels);
    return true;
}

bool JdspIpcServer::HandleSetParam(const std::vector<uint8_t>& payload) {
    std::string param(payload.begin(), payload.end());
    size_t eq_pos = param.find('=');
    if (eq_pos == std::string::npos) return false;

    std::string key = param.substr(0, eq_pos);
    std::string value = param.substr(eq_pos + 1);
    m_engine.SetParam(key, value);
    return true;
}
