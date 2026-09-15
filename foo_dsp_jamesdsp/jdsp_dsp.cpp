#include "stdafx.h"
#include "jdsp_dsp.h"

static const GUID g_jdsp_guid =
{ 0x12345678, 0x1234, 0x1234, { 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0 } };

const GUID& jdsp_dsp::g_get_guid() {
    return g_jdsp_guid;
}

void jdsp_dsp::g_get_name(pfc::string_base& p_out) {
    p_out = "JamesDSP";
}

jdsp_dsp::jdsp_dsp() : m_ipc_client(m_host_manager) {}

jdsp_dsp::~jdsp_dsp() {
    if (m_host_started) {
        m_ipc_client.SendShutdown();
        m_host_manager.Stop();
    }
}

bool jdsp_dsp::EnsureHostRunning() {
    if (m_host_manager.IsRunning()) return true;

    pfc::string8 dll_path;
    component_loader::g_get_full_path(dll_path, "foo_dsp_jamesdsp.dll");
    pfc::string8 dir = pfc::string_filename(dll_path);
    pfc::string8 host_path = dir;
    host_path += "\\jdsp_host.exe";

    pfc::wchar_t host_path_w[MAX_PATH];
    pfc::utf8_to_wide(host_path, host_path_w, MAX_PATH);

    if (!m_host_manager.Start(host_path_w)) return false;

    m_host_started = true;
    return true;
}

bool jdsp_dsp::on_chunk(audio_chunk* p_chunk, abort_callback&) {
    if (!EnsureHostRunning()) return false;

    audio_sample* data = p_chunk->get_data();
    uint32_t sample_count = p_chunk->get_sample_count();
    uint32_t channels = p_chunk->get_channels();
    uint32_t sample_rate = p_chunk->get_sample_rate();

    uint32_t total = sample_count * channels;
    std::vector<float> input(total);
    std::vector<float> output(total);

    memcpy(input.data(), data, total * sizeof(float));

    if (m_ipc_client.SendAudioData(sample_rate, channels, sample_count,
                                    input.data(), output.data())) {
        memcpy(data, output.data(), total * sizeof(float));
    }

    return true;
}

void jdsp_dsp::on_endoftrack(abort_callback&) {}
void jdsp_dsp::on_endofplayback(abort_callback&) {}

void jdsp_dsp::show_config_popup(HWND parent, abort_callback&) {
    // TODO: Show configuration dialog
}

void jdsp_dsp::get_preset(dsp_preset& p_out) {
    p_out.guid = g_get_guid();
}

void jdsp_dsp::set_preset(const dsp_preset& p_in) {}

bool jdsp_dsp::is_preset_current(const dsp_preset& p_in) {
    return true;
}

static dsp_factory_t<jdsp_dsp> g_jdsp_dsp_factory;
