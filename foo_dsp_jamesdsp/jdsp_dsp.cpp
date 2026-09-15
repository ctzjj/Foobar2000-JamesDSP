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

jdsp_dsp::jdsp_dsp(const dsp_preset& p_preset) : m_ipc_client(m_host_manager) {
    // Load settings from preset if available
}

jdsp_dsp::~jdsp_dsp() {
    if (m_host_started) {
        m_ipc_client.SendShutdown();
        m_host_manager.Stop();
    }
}

bool jdsp_dsp::EnsureHostRunning() {
    if (m_host_manager.IsRunning()) return true;

    // Get the directory of this DLL
    HMODULE hMod = NULL;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCWSTR)&jdsp_dsp::g_get_guid, &hMod);

    wchar_t dll_path[MAX_PATH];
    GetModuleFileNameW(hMod, dll_path, MAX_PATH);

    // Replace filename with jdsp_host.exe
    wchar_t* last_slash = wcsrchr(dll_path, L'\\');
    if (last_slash) {
        *(last_slash + 1) = L'\0';
        wcscat_s(dll_path, L"jdsp_host.exe");
    }

    if (!m_host_manager.Start(dll_path)) return false;

    m_host_started = true;
    return true;
}

bool jdsp_dsp::on_chunk(audio_chunk* p_chunk, abort_callback&) {
    if (!EnsureHostRunning()) return false;

    audio_sample* data = p_chunk->get_data();
    uint32_t sample_count = (uint32_t)p_chunk->get_sample_count();
    uint32_t channels = (uint32_t)p_chunk->get_channels();
    uint32_t sample_rate = (uint32_t)p_chunk->get_sample_rate();

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

void jdsp_dsp::flush() {
    // Reset any buffered data
}

double jdsp_dsp::get_latency() {
    // No buffering, return 0
    return 0;
}

bool jdsp_dsp::need_track_change_mark() {
    return false;
}

bool jdsp_dsp::g_get_default_preset(dsp_preset& p_out) {
    dsp_preset_builder builder;
    builder.finish(g_get_guid(), p_out);
    return true;
}

static void RunDSPConfigPopup(const dsp_preset& p_data, HWND p_parent, dsp_preset_edit_callback& p_callback) {
    // TODO: Show configuration dialog
}

void jdsp_dsp::g_show_config_popup(const dsp_preset& p_data, fb2k::hwnd_t p_parent, dsp_preset_edit_callback& p_callback) {
    RunDSPConfigPopup(p_data, p_parent, p_callback);
}

void jdsp_dsp::get_preset(dsp_preset& p_out) {
    p_out.set_owner(g_get_guid());
}

void jdsp_dsp::set_preset(const dsp_preset& p_in) {
    // TODO: Load settings from preset
}

bool jdsp_dsp::is_preset_current(const dsp_preset& p_in) {
    return true;
}

static dsp_factory_t<jdsp_dsp> g_jdsp_dsp_factory;
