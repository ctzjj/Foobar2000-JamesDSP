#pragma once
#include <SDK/foobar2000.h>
#include "jdsp_host_manager.h"
#include "jdsp_ipc_client.h"

class jdsp_dsp : public dsp_impl_base {
public:
    jdsp_dsp(const dsp_preset& p_preset);
    ~jdsp_dsp();

    static void g_get_name(pfc::string_base& p_out);
    static void g_get_display_name(const dsp_preset& arg, pfc::string_base& out);

    virtual void on_endoftrack(abort_callback& p_abort);
    virtual void on_endofplayback(abort_callback& p_abort);
    virtual bool on_chunk(audio_chunk* p_chunk, abort_callback& p_abort);

    virtual void flush();
    virtual double get_latency();
    virtual bool need_track_change_mark();

    static bool g_have_config_popup() { return true; }
    static bool g_get_default_preset(dsp_preset& p_out);
    static void g_show_config_popup(const dsp_preset& p_data, fb2k::hwnd_t p_parent, dsp_preset_edit_callback& p_callback);
    static service_ptr g_show_config_popup_v3(fb2k::hwnd_t parent, dsp_preset_edit_callback_v2::ptr callback);

    virtual void get_preset(dsp_preset& p_out);
    virtual void set_preset(const dsp_preset& p_in);
    virtual bool is_preset_current(const dsp_preset& p_in);

    static const GUID& g_get_guid();

private:
    bool EnsureHostRunning();
    void ApplyPresetToHost();

    JdspHostManager m_host_manager;
    JdspIpcClient m_ipc_client;
    bool m_host_started = false;
    dsp_preset_impl m_preset;
};
