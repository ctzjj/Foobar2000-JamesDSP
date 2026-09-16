#include "stdafx.h"
#include "jdsp_dsp.h"
#include "jdsp_config_dialog.h"
#include "jdsp_live_link.h"
#include <cstdio>
#include <chrono>

extern void CfgLog(const char* msg);
static int g_chunk_count = 0;
static int g_chunk_ok = 0;
static long long g_oc_n = 0;
static double g_oc_total = 0.0;
static double g_oc_max = 0.0;
static int g_oc_fail = 0;

static const GUID g_jdsp_guid =
{ 0x12345678, 0x1234, 0x1234, { 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0 } };

const GUID& jdsp_dsp::g_get_guid() {
    return g_jdsp_guid;
}

void jdsp_dsp::g_get_name(pfc::string_base& p_out) {
    p_out = "JamesDSP";
}

void jdsp_dsp::g_get_display_name(const dsp_preset& arg, pfc::string_base& out) {
    out = "JamesDSP";
}

jdsp_dsp::jdsp_dsp(const dsp_preset& p_preset) : m_ipc_client(m_host_manager) {
    m_preset = p_preset;
    if (m_preset.get_owner() == pfc::guid_null) m_preset.set_owner(g_jdsp_guid);
    CfgLog("jdsp_dsp: constructed");
}

void jdsp_dsp::ApplyPresetToHost() {
    const void* data = m_preset.get_data();
    t_size size = m_preset.get_data_size();
    if (!data || size == 0) return;

    std::string blob(static_cast<const char*>(data), (size_t)size);
    m_ipc_client.SendSetParams(blob);
}

jdsp_dsp::~jdsp_dsp() {
    JdspClearActiveClient(&m_ipc_client);
    if (m_host_started) {
        CfgLog("jdsp_dsp: destroying -> shutdown host");
        m_ipc_client.SendShutdown();
        m_host_manager.Stop();
    } else {
        CfgLog("jdsp_dsp: destroyed (host never started)");
    }
}

bool jdsp_dsp::EnsureHostRunning() {
    if (m_host_manager.IsRunning()) return true;

    HMODULE hMod = NULL;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCWSTR)&jdsp_dsp::g_get_guid, &hMod);

    wchar_t dll_path[MAX_PATH];
    GetModuleFileNameW(hMod, dll_path, MAX_PATH);

    wchar_t* last_slash = wcsrchr(dll_path, L'\\');
    if (last_slash) {
        *(last_slash + 1) = L'\0';
        wcscat_s(dll_path, L"jdsp_host.exe");
    }

    if (!m_host_manager.Start(dll_path)) {
        CfgLog("EnsureHostRunning: Start FAILED");
        return false;
    }
    m_host_started = true;
    CfgLog("EnsureHostRunning: host STARTED");
    ApplyPresetToHost();
    JdspSetActiveClient(&m_ipc_client);
    return true;
}

bool jdsp_dsp::on_chunk(audio_chunk* p_chunk, abort_callback&) {
    if (!EnsureHostRunning()) {
        if (g_chunk_count < 3) CfgLog("on_chunk: host NOT running");
        return true;
    }

    audio_sample* data = p_chunk->get_data();
    uint32_t sample_count = (uint32_t)p_chunk->get_sample_count();
    uint32_t channels = (uint32_t)p_chunk->get_channels();
    uint32_t sample_rate = (uint32_t)p_chunk->get_sample_rate();

    uint32_t total = sample_count * channels;
    std::vector<float> input(total);
    std::vector<float> output(total);

    float peak_in = 0.0f;
    for (uint32_t i = 0; i < total; i++) {
        float v = (float)data[i];
        input[i] = v;
        float a = v < 0.0f ? -v : v;
        if (a > peak_in) peak_in = a;
    }

    auto t0 = std::chrono::steady_clock::now();
    bool ok = m_ipc_client.SendAudioData(sample_rate, channels, sample_count,
                                         input.data(), output.data());
    auto t1 = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    g_oc_total += ms;
    g_oc_n++;
    if (ms > g_oc_max) g_oc_max = ms;
    if (!ok) g_oc_fail++;

    if (ok) {
        for (uint32_t i = 0; i < total; i++) data[i] = (audio_sample)output[i];
        g_chunk_ok++;
    }

    if (g_chunk_count < 3) {
        char b[200];
        sprintf_s(b, "on_chunk: #%d sr=%u ch=%u n=%u ipc_ok=%d asize=%u pkin=%.4f", g_chunk_count,
                  (unsigned)sample_rate, (unsigned)channels, (unsigned)sample_count, (int)ok,
                  (unsigned)sizeof(audio_sample), (double)peak_in);
        CfgLog(b);
    }
    if (g_chunk_count < 20) {
        char b[200];
        sprintf_s(b, "chunk-in n=%d n=%u pkin=%.4f asize=%u", g_chunk_count,
                  (unsigned)sample_count, (double)peak_in, (unsigned)sizeof(audio_sample));
        CfgLog(b);
    }
    g_chunk_count++;

    if (g_oc_n <= 120 || g_oc_n % 10 == 0) {
        char b[224];
        sprintf_s(b, "chunk n=%lld sr=%u ch=%u n=%u ms=%.2f ok=%d fail=%d avg=%.2f max=%.2f",
                  g_oc_n, (unsigned)sample_rate, (unsigned)channels, (unsigned)sample_count,
                  ms, (int)ok, g_oc_fail,
                  g_oc_total / (double)g_oc_n, g_oc_max);
        CfgLog(b);
    }

    return true;
}

void jdsp_dsp::on_endoftrack(abort_callback&) {}
void jdsp_dsp::on_endofplayback(abort_callback&) {}
void jdsp_dsp::flush() {}
double jdsp_dsp::get_latency() { return 0; }
bool jdsp_dsp::need_track_change_mark() { return false; }

bool jdsp_dsp::g_get_default_preset(dsp_preset& p_out) {
    dsp_preset_builder builder;
    builder.finish(g_get_guid(), p_out);
    return true;
}

static void RunDSPConfigPopup(const dsp_preset& p_data, HWND p_parent, dsp_preset_edit_callback& p_callback) {
    JdspHostManager host_mgr;
    JdspIpcClient ipc(host_mgr);
    JdspConfigDialog dlg(ipc);

    if (p_data.get_data() && p_data.get_data_size() > 0) {
        dlg.DeserializeSettings(std::string(static_cast<const char*>(p_data.get_data()),
                                            (size_t)p_data.get_data_size()));
    }

    if (dlg.Show(p_parent)) {
        std::string blob = dlg.SerializeSettings();
        dsp_preset_impl new_preset;
        new_preset.set_owner(g_jdsp_guid);
        new_preset.set_data(blob.data(), blob.size());
        p_callback.on_preset_changed(new_preset);
    }
}

void jdsp_dsp::g_show_config_popup(const dsp_preset& p_data, fb2k::hwnd_t p_parent, dsp_preset_edit_callback& p_callback) {
    RunDSPConfigPopup(p_data, p_parent, p_callback);
}

service_ptr jdsp_dsp::g_show_config_popup_v3(fb2k::hwnd_t parent, dsp_preset_edit_callback_v2::ptr callback) {
    JdspHostManager host_mgr;
    JdspIpcClient ipc(host_mgr);
    JdspConfigDialog dlg(ipc);
    dlg.Show(parent);
    return nullptr;
}

void jdsp_dsp::get_preset(dsp_preset& p_out) {
    p_out.copy(m_preset);
    if (p_out.get_owner() == pfc::guid_null) p_out.set_owner(g_get_guid());
}

void jdsp_dsp::set_preset(const dsp_preset& p_in) {
    m_preset = p_in;
    if (m_preset.get_owner() == pfc::guid_null) m_preset.set_owner(g_get_guid());
    if (m_host_manager.IsRunning()) ApplyPresetToHost();
}

bool jdsp_dsp::is_preset_current(const dsp_preset& p_in) {
    return m_preset == p_in;
}

static dsp_factory_t<jdsp_dsp> g_jdsp_dsp_factory;
