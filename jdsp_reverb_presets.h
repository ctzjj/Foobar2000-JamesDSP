#pragma once

// Preset table for the JamesDSP reverb, mirrored from
// libjamesdsp/jni/jamesdsp/jdsp/Effects/reverb.c (sf_presetreverb, lines 741-759).
//
// The library only exposes "pick a preset" (Reverb_SetParam). This table lets the
// host rebuild the full parameter set through sf_advancereverb() so individual
// parameters can be overridden, and lets the dialog show the preset's values.

struct JdspReverbParams
{
    int   osf;        // oversampling factor
    float ertolate;   // early reflection level
    float erefwet;    // early reflection wet (dB)
    float dry;        // dry level (dB)
    float ereffactor; // early reflection factor
    float erefwidth;  // early reflection width
    float width;      // stereo width
    float wet;        // wet level (dB)
    float wander;     // modulation wander
    float bassb;      // bass boost (1.0 = 100 %)
    float spin;       // modulation spin
    float inputlpf;   // input low pass (Hz)
    float basslpf;    // bass low pass (Hz)
    float damplpf;    // damping low pass (Hz)
    float outputlpf;  // output low pass (Hz)
    float rt60;       // reverb time (seconds)
    float delay;      // predelay (seconds)
};

#define JDSP_REVERB_PRESET_COUNT 19

// Order matches the sf_reverb_preset enum: DEFAULT, SMALLHALL1, SMALLHALL2,
// MEDIUMHALL1, MEDIUMHALL2, LARGEHALL1, LARGEHALL2, SMALLROOM1, SMALLROOM2,
// MEDIUMROOM1, MEDIUMROOM2, LARGEROOM1, LARGEROOM2, MEDIUMER1, MEDIUMER2,
// PLATEHIGH, PLATELOW, LONGREVERB1, LONGREVERB2.
static const JdspReverbParams kJdspReverbPresets[JDSP_REVERB_PRESET_COUNT] =
{
    { 1, 0.40f,  -9.0f,  -7.0f, 1.6f,  0.7f, 1.0f,  -0.0f, 0.25f, 0.15f, 0.7f, 17000.0f,  500.0f,  7000.0f, 10000.0f,  3.2f, 0.020f },
    { 1, 0.30f,  -9.0f,  -7.0f, 1.0f,  0.7f, 1.0f,  -8.0f, 0.30f, 0.25f, 0.7f, 18000.0f,  600.0f,  9000.0f, 17000.0f,  2.1f, 0.010f },
    { 1, 0.30f,  -9.0f,  -7.0f, 1.0f,  0.7f, 1.0f,  -8.0f, 0.25f, 0.20f, 0.5f, 18000.0f,  600.0f,  7000.0f,  9000.0f,  2.3f, 0.010f },
    { 1, 0.30f,  -9.0f,  -7.0f, 1.2f,  0.7f, 1.0f,  -8.0f, 0.25f, 0.20f, 0.7f, 18000.0f,  500.0f,  8000.0f, 16000.0f,  2.8f, 0.010f },
    { 1, 0.30f,  -9.0f,  -7.0f, 1.2f,  0.7f, 1.0f,  -8.0f, 0.20f, 0.15f, 0.5f, 18000.0f,  500.0f,  6000.0f,  8000.0f,  2.9f, 0.010f },
    { 1, 0.20f,  -9.0f,  -7.0f, 1.4f,  0.7f, 1.0f,  -8.0f, 0.15f, 0.20f, 1.0f, 18000.0f,  400.0f,  9000.0f, 14000.0f,  3.8f, 0.018f },
    { 1, 0.20f,  -9.0f,  -7.0f, 1.5f,  0.7f, 1.0f,  -8.0f, 0.20f, 0.20f, 0.5f, 18000.0f,  400.0f,  5000.0f,  7000.0f,  4.2f, 0.018f },
    { 1, 0.70f,  -8.0f,  -7.0f, 0.7f, -0.4f, 0.8f,  -8.0f, 0.20f, 0.30f, 1.6f, 18000.0f, 1000.0f, 18000.0f, 18000.0f,  0.5f, 0.005f },
    { 1, 0.70f,  -8.0f,  -7.0f, 0.8f,  0.6f, 0.9f,  -8.0f, 0.30f, 0.30f, 0.4f, 18000.0f,  300.0f, 10000.0f, 18000.0f,  0.5f, 0.005f },
    { 1, 0.50f,  -8.0f,  -7.0f, 1.2f, -0.4f, 0.8f,  -8.0f, 0.20f, 0.10f, 1.6f, 18000.0f, 1000.0f, 18000.0f, 18000.0f,  0.8f, 0.008f },
    { 1, 0.50f,  -8.0f,  -7.0f, 1.2f,  0.6f, 0.9f,  -8.0f, 0.30f, 0.10f, 0.4f, 18000.0f,  300.0f, 10000.0f, 18000.0f,  1.2f, 0.016f },
    { 1, 0.20f,  -8.0f,  -7.0f, 2.2f, -0.4f, 0.9f,  -8.0f, 0.20f, 0.10f, 1.6f, 18000.0f, 1000.0f, 16000.0f, 18000.0f,  1.8f, 0.010f },
    { 1, 0.20f,  -8.0f,  -7.0f, 2.2f,  0.6f, 0.9f,  -8.0f, 0.30f, 0.10f, 0.4f, 18000.0f,  500.0f,  9000.0f, 18000.0f,  1.9f, 0.020f },
    { 1, 0.50f,  -7.0f,  -6.0f, 1.2f, -0.4f, 0.8f, -70.0f, 0.20f, 0.10f, 1.6f, 18000.0f, 1000.0f, 18000.0f, 18000.0f,  0.8f, 0.008f },
    { 1, 0.50f,  -7.0f,  -6.0f, 1.2f,  0.6f, 0.9f, -70.0f, 0.30f, 0.10f, 0.4f, 18000.0f,  300.0f, 10000.0f, 18000.0f,  1.2f, 0.016f },
    { 2, 0.00f, -30.0f, -12.0f, 1.0f,  1.0f, 1.0f,  -8.0f, 0.20f, 0.10f, 1.6f, 18000.0f, 1000.0f, 16000.0f, 18000.0f,  1.8f, 0.000f },
    { 2, 0.00f, -30.0f, -12.0f, 1.0f,  1.0f, 1.0f,  -8.0f, 0.30f, 0.20f, 0.4f, 18000.0f,  500.0f,  9000.0f, 18000.0f,  1.9f, 0.000f },
    { 2, 0.10f, -16.0f, -14.0f, 1.0f,  0.1f, 1.0f,  -5.0f, 0.35f, 0.05f, 1.0f, 18000.0f,  100.0f, 10000.0f, 18000.0f, 12.0f, 0.000f },
    { 2, 0.10f, -16.0f, -14.0f, 1.0f,  0.1f, 1.0f,  -5.0f, 0.40f, 0.05f, 1.0f, 18000.0f,  100.0f,  9000.0f, 18000.0f, 30.0f, 0.000f }
};
