#include "wav_loader.h"
#include <cstdio>
#include <cstring>

namespace {

uint16_t RdU16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

uint32_t RdU32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

bool ReadWholeFile(const std::wstring& path, std::vector<uint8_t>& out) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) return false;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return false; }
    long size = ftell(f);
    if (size <= 0) { fclose(f); return false; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return false; }
    out.resize((size_t)size);
    size_t got = fread(out.data(), 1, out.size(), f);
    fclose(f);
    return got == out.size();
}

const uint8_t kGuidTail[14] = {
    0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00,
    0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71
};

}  // namespace

bool LoadWavInterleaved(const std::wstring& path,
                        std::vector<float>& out_samples,
                        uint32_t& out_channels,
                        uint32_t& out_sample_rate) {
    std::vector<uint8_t> buf;
    if (!ReadWholeFile(path, buf)) return false;
    if (buf.size() < 44) return false;
    if (memcmp(buf.data(), "RIFF", 4) != 0) return false;
    if (memcmp(buf.data() + 8, "WAVE", 4) != 0) return false;

    uint16_t format = 0;
    uint16_t channels = 0;
    uint32_t sample_rate = 0;
    uint16_t bits = 0;
    bool have_fmt = false;

    const uint8_t* data_ptr = nullptr;
    size_t data_len = 0;

    size_t pos = 12;
    while (pos + 8 <= buf.size()) {
        const uint8_t* chunk = buf.data() + pos;
        uint32_t chunk_size = RdU32(chunk + 4);
        const uint8_t* body = chunk + 8;
        if (pos + 8 + (size_t)chunk_size > buf.size()) {
            // Truncated final chunk: only usable when it is the data chunk and the
            // declared size overruns, in which case clamp to what is present.
            if (memcmp(chunk, "data", 4) == 0) {
                data_ptr = body;
                data_len = buf.size() - (pos + 8);
            }
            break;
        }

        if (memcmp(chunk, "fmt ", 4) == 0 && chunk_size >= 16) {
            format = RdU16(body + 0);
            channels = RdU16(body + 2);
            sample_rate = RdU32(body + 4);
            bits = RdU16(body + 14);
            if (format == 0xFFFE && chunk_size >= 40) {
                // WAVE_FORMAT_EXTENSIBLE: the real format is the first 2 bytes of
                // the sub-format GUID at offset 24; the rest must be the standard tail.
                if (memcmp(body + 26, kGuidTail, sizeof(kGuidTail)) != 0) return false;
                format = RdU16(body + 24);
            }
            have_fmt = true;
        } else if (memcmp(chunk, "data", 4) == 0) {
            data_ptr = body;
            data_len = chunk_size;
        }

        // Chunks are word-aligned.
        pos += 8 + (size_t)chunk_size + (chunk_size & 1u);
    }

    if (!have_fmt || !data_ptr || data_len == 0) return false;
    if (channels == 0) return false;
    if (format != 1 && format != 3) return false;

    size_t bytes_per_sample = bits / 8;
    if (bytes_per_sample == 0 || bytes_per_sample > 4) return false;
    if (format == 3 && bytes_per_sample != 4) return false;

    size_t frame_bytes = bytes_per_sample * channels;
    size_t frames = data_len / frame_bytes;
    if (frames == 0) return false;

    std::vector<float> samples(frames * channels);
    const uint8_t* p = data_ptr;

    for (size_t i = 0; i < frames * channels; i++) {
        float v = 0.0f;
        if (format == 3) {
            float f = 0.0f;
            memcpy(&f, p, 4);
            v = f;
        } else if (bits == 8) {
            v = ((float)p[0] - 128.0f) / 128.0f;
        } else if (bits == 16) {
            int16_t s = (int16_t)RdU16(p);
            v = (float)s / 32768.0f;
        } else if (bits == 24) {
            int32_t s = (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16));
            if (s & 0x800000) s -= 0x1000000;
            v = (float)s / 8388608.0f;
        } else {
            int32_t s = (int32_t)RdU32(p);
            v = (float)s / 2147483648.0f;
        }
        samples[i] = v;
        p += bytes_per_sample;
    }

    out_samples.swap(samples);
    out_channels = channels;
    out_sample_rate = sample_rate;
    return true;
}
