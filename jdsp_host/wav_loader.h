#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Decodes a RIFF/WAVE file into interleaved float samples in [-1, 1].
// Supports PCM 8/16/24/32-bit integer and 32-bit IEEE float, mono or multi-channel,
// including WAVE_FORMAT_EXTENSIBLE containers around those two sample formats.
// Returns false and leaves the outputs untouched on any parse error.
bool LoadWavInterleaved(const std::wstring& path,
                        std::vector<float>& out_samples,
                        uint32_t& out_channels,
                        uint32_t& out_sample_rate);
