#include "stdafx.h"
#include "jdsp_ipc_client.h"
#include "jdsp_host_manager.h"
#include <cstring>

JdspIpcClient::JdspIpcClient(JdspHostManager& manager) : m_manager(manager) {}

bool JdspIpcClient::WriteFrame(jdsp::FrameType type, const void* data, uint32_t length) {
    HANDLE hWrite = m_manager.GetStdinWrite();
    if (!hWrite) return false;

    jdsp::FrameHeader header;
    header.type = type;
    header.data_length = length;

    DWORD written;
    if (!WriteFile(hWrite, &header, sizeof(header), &written, NULL))
        return false;
    if (written != sizeof(header)) return false;

    if (data && length > 0) {
        if (!WriteFile(hWrite, data, length, &written, NULL))
            return false;
        if (written != length) return false;
    }
    return true;
}

bool JdspIpcClient::ReadFrame(jdsp::FrameHeader& header, std::vector<uint8_t>& payload) {
    HANDLE hRead = m_manager.GetStdoutRead();
    if (!hRead) return false;

    DWORD bytes_read;
    if (!ReadFile(hRead, &header, sizeof(header), &bytes_read, NULL))
        return false;
    if (bytes_read != sizeof(header)) return false;

    payload.resize(header.data_length);
    if (header.data_length > 0) {
        if (!ReadFile(hRead, payload.data(), header.data_length, &bytes_read, NULL))
            return false;
        if (bytes_read != header.data_length) return false;
    }
    return true;
}

bool JdspIpcClient::SendAudioData(uint32_t sample_rate, uint32_t channels,
                                   uint32_t sample_count, const float* audio,
                                   float* output) {
    uint32_t data_size = sample_count * channels * sizeof(float);
    uint32_t total_size = sizeof(jdsp::AudioData) + data_size;

    std::vector<uint8_t> payload(total_size);
    jdsp::AudioData* hdr = reinterpret_cast<jdsp::AudioData*>(payload.data());
    hdr->sample_rate = sample_rate;
    hdr->channels = channels;
    hdr->sample_count = sample_count;
    memcpy(payload.data() + sizeof(jdsp::AudioData), audio, data_size);

    if (!WriteFrame(jdsp::FrameType::AUDIO_DATA, payload.data(), total_size))
        return false;

    jdsp::FrameHeader resp_header;
    std::vector<uint8_t> resp_payload;
    if (!ReadFrame(resp_header, resp_payload))
        return false;
    if (resp_header.type != jdsp::FrameType::AUDIO_DATA)
        return false;

    memcpy(output, resp_payload.data() + sizeof(jdsp::AudioData), data_size);
    return true;
}

bool JdspIpcClient::SendSetParam(const std::string& key, const std::string& value) {
    std::string param = key + "=" + value;
    return WriteFrame(jdsp::FrameType::SET_PARAM, param.c_str(),
                      static_cast<uint32_t>(param.size()));
}

bool JdspIpcClient::SendShutdown() {
    return WriteFrame(jdsp::FrameType::SHUTDOWN, nullptr, 0);
}
