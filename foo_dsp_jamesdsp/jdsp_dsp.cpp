#include "stdafx.h"
#include "jdsp_dsp.h"
#include "jdsp_config_dialog.h"
#include "jdsp_live_link.h"

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
        m_ipc_client.SendShutdown();
        m_host_manager.Stop();
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

    if (!m_host_manager.Start(dll_path)) return false;
    m_host_started = true;
    ApplyPresetToHost();
    JdspSetActiveClient(&m_ipc_client);
    return true;
}

// JamesDSP rebuilds its convolver, crossfeed and equaliser state whenever the
// block size it is fed changes (jdspController.c does a full refresh on every
// "blockSize != n"), which foobar2000's variable chunk lengths would trigger
// constantly. The pipeline therefore re-blocks the stream into fixed stereo
// frames: input is accumulated until a whole block is available, and the
// output of a block is emitted one block later. The initial out FIFO padding
// of one silent block keeps the invariant that there is always enough
// processed audio to cover an incoming chunk, at the cost of one block of
// latency (about 23 ms at 44.1 kHz).
void jdsp_dsp::ResetPipeline() {
    m_in_fifo.clear();
    m_out_fifo.clear();
    m_out_fifo.assign((size_t)kBlockFrames * 2, 0.0f);
}

void jdsp_dsp::ProcessFifo() {
    std::vector<float> block((size_t)kBlockFrames * 2);
    std::vector<float> out((size_t)kBlockFrames * 2);

    while (m_in_fifo.size() >= (size_t)kBlockFrames * 2) {
        std::copy(m_in_fifo.begin(), m_in_fifo.begin() + block.size(), block.begin());
        m_in_fifo.erase(m_in_fifo.begin(), m_in_fifo.begin() + block.size());

        bool ok = m_ipc_client.SendAudioData(m_fmt_rate, 2, kBlockFrames,
                                             block.data(), out.data());
        const float* src = ok ? out.data() : block.data();
        m_out_fifo.insert(m_out_fifo.end(), src, src + block.size());
    }
}

bool jdsp_dsp::on_chunk(audio_chunk* p_chunk, abort_callback&) {
    uint32_t sample_count = (uint32_t)p_chunk->get_sample_count();
    uint32_t channels = (uint32_t)p_chunk->get_channels();
    uint32_t sample_rate = (uint32_t)p_chunk->get_sample_rate();
    uint32_t channel_config = p_chunk->get_channel_config();
    if (sample_count == 0) return true;

    // The engine is stereo only. Mono is upmixed and folded back; anything
    // with more than two channels bypasses the DSP entirely.
    if (channels != 1 && channels != 2) return true;
    if (!EnsureHostRunning()) return true;

    if (sample_rate != m_fmt_rate || channels != m_fmt_channels) {
        m_fmt_rate = sample_rate;
        m_fmt_channels = channels;
        m_fmt_config = channel_config;
        ResetPipeline();
    }

    audio_sample* data = p_chunk->get_data();

    size_t frames = sample_count;
    m_in_fifo.reserve(m_in_fifo.size() + frames * 2);
    if (channels == 2) {
        for (size_t i = 0; i < frames; i++) {
            float l = (float)data[i * 2], r = (float)data[i * 2 + 1];
            m_in_fifo.push_back(l);
            m_in_fifo.push_back(r);
        }
    } else {
        for (size_t i = 0; i < frames; i++) {
            float m = (float)data[i];
            m_in_fifo.push_back(m);
            m_in_fifo.push_back(m);
        }
    }

    ProcessFifo();

    size_t out_frames = m_out_fifo.size() / 2;
    if (out_frames < frames) return true;  // cannot happen while the pad holds
    size_t take = frames * 2;
    const float* src = m_out_fifo.data();
    if (channels == 2) {
        for (size_t i = 0; i < frames; i++) {
            float l = src[i * 2], r = src[i * 2 + 1];
            data[i * 2] = (audio_sample)l;
            data[i * 2 + 1] = (audio_sample)r;
        }
    } else {
        for (size_t i = 0; i < frames; i++) {
            float l = src[i * 2], r = src[i * 2 + 1];
            float m = (l + r) * 0.5f;
            data[i] = (audio_sample)m;
        }
    }
    m_out_fifo.erase(m_out_fifo.begin(), m_out_fifo.begin() + take);

    return true;
}

void jdsp_dsp::on_endoftrack(abort_callback&) {}
void jdsp_dsp::on_endofplayback(abort_callback&) {
    if (!m_fmt_rate) return;

    // Return everything still buffered: pad the input FIFO to a whole block,
    // process it and hand the whole output FIFO back through insert_chunk.
    size_t block_samples = (size_t)kBlockFrames * 2;
    if (m_in_fifo.size() % block_samples) {
        size_t pad = block_samples - (m_in_fifo.size() % block_samples);
        m_in_fifo.resize(m_in_fifo.size() + pad, 0.0f);
    }
    ProcessFifo();

    size_t frames = m_out_fifo.size() / 2;
    if (frames) {
        audio_chunk* out = insert_chunk(frames * m_fmt_channels);
        std::vector<audio_sample> buf(frames * m_fmt_channels);
        const float* src = m_out_fifo.data();
        if (m_fmt_channels == 2) {
            for (size_t i = 0; i < frames * 2; i++) buf[i] = (audio_sample)src[i];
        } else {
            for (size_t i = 0; i < frames; i++) {
                buf[i] = (audio_sample)((src[i * 2] + src[i * 2 + 1]) * 0.5f);
            }
        }
        out->set_data(buf.data(), frames, m_fmt_channels, m_fmt_rate, m_fmt_config);
    }

    m_fmt_rate = 0;
    ResetPipeline();
}
void jdsp_dsp::flush() {
    m_fmt_rate = 0;
    ResetPipeline();
}
double jdsp_dsp::get_latency() {
    if (!m_fmt_rate) return 0;
    return (double)((m_in_fifo.size() + m_out_fifo.size()) / 2) / (double)m_fmt_rate;
}
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

    std::string orig;
    if (p_data.get_data() && p_data.get_data_size() > 0) {
        orig.assign(static_cast<const char*>(p_data.get_data()), (size_t)p_data.get_data_size());
        dlg.DeserializeSettings(orig);
    }

    if (dlg.Show(p_parent)) {
        std::string blob = dlg.SerializeSettings();
        if (blob == orig) {
            return;
        }
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
