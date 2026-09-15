# foobar2000 JamesDSP Plugin Design

## Overview

A foobar2000 DSP component that integrates the open-source JamesDSP audio effects engine via an out-of-process architecture, providing full DSP functionality with crash isolation.

## Requirements

- **Target platforms**: foobar2000 v1.x (x86) and v2.x (x64)
- **Build toolchain**: Visual Studio 2019
- **GUI**: Win32 configuration dialog for parameter adjustment
- **Localization**: Chinese and English support, default English, with language setting in top toolbar
- **Features**: Full JamesDSP feature set (Analog Modelling, BS2B, DDC, Limiter, Compressor, Convolver, Reverb, Bass Boost, Stereo Widener, IIR Filters, Spectrum Extender, Dynamic System, EEL2 Scripting)
- **Preset support**: Save/load DSP presets via foobar2000's `dsp_preset` mechanism

## Architecture

### Component Diagram

```
foobar2000 Main Process                    Host Process
+------------------------+  stdin/stdout  +------------------------+
|  foo_dsp_jamesdsp.dll  | <------------> |    jdsp_host.exe       |
|  (DSP plugin DLL)      |  binary frames |  (libjamesdsp wrapper) |
+------------------------+                +------------------------+
```

### Component 1: DSP Plugin (`foo_dsp_jamesdsp.dll`)

Responsibilities:
- Implements `dsp` service interface (`dsp_v2`/`dsp_v3`) for foobar2000
- Manages host process lifecycle (spawn, monitor, restart, shutdown)
- Serializes/deserializes audio data and control messages via IPC
- Provides Win32 configuration dialog for parameter adjustment
- Handles preset save/load via `dsp_preset` blob

Key classes:
- `jdsp_dsp` : `dsp_impl_base_t<jdsp_dsp>` — main DSP implementation
- `jdsp_config_dialog` — Win32 dialog for parameter configuration
- `jdsp_ipc_client` — IPC communication layer
- `jdsp_host_manager` — subprocess lifecycle management

### Component 2: Host Process (`jdsp_host.exe`)

Responsibilities:
- Links libjamesdsp as static library
- Reads audio data from stdin, processes through JamesDSP, writes to stdout
- Receives control commands (parameter changes, preset load/save)
- Pure command-line process, no GUI

Key components:
- `jdsp_ipc_server` — IPC communication layer
- `jdsp_engine` — JamesDSP wrapper with all effect modules
- `main()` — entry point, IPC loop

### IPC Protocol

Binary frame protocol over stdin/stdout pipes:

```
Frame format:
[4 bytes: frame_type] [4 bytes: data_length] [N bytes: payload]

Frame types:
  AUDIO_DATA    = 0x01   // Audio data (f32 interleaved, stereo)
  SET_PARAM     = 0x10   // Set parameter (key=value string)
  LOAD_PRESET   = 0x20   // Load preset (binary blob)
  SAVE_PRESET   = 0x21   // Save preset request
  PRESET_DATA   = 0x22   // Preset data response
  STATUS        = 0x30   // Status query/response
  SHUTDOWN      = 0xFF   // Shutdown signal
```

Audio data flow:
1. foobar2000 calls `jdsp_dsp::on_chunk(audio_chunk*)`
2. Plugin serializes chunk to `AUDIO_DATA` frame, writes to host stdin
3. Host reads frame, runs JamesDSP processing, writes `AUDIO_DATA` frame to stdout
4. Plugin reads processed frame, writes back to `audio_chunk*`

Parameter flow:
1. User adjusts parameter in config dialog
2. Plugin sends `SET_PARAM` frame to host
3. Host applies parameter to JamesDSP engine

## JamesDSP Modules

| Module | Description | Parameters |
|--------|-------------|------------|
| Analog Modelling | 12AX7 tube simulation | tube_drive |
| BS2B | Crossfeed for headphones | feed, frequency |
| DDC | Dynamic Direct Control (ViPER) | multiple |
| Limiter | Peak limiter | threshold, release |
| Compressor | Dynamic range compression | threshold, ratio, attack, release |
| Convolver | Impulse response loading | ir_file_path |
| Reverb | Progenitor2 reverb | room_size, damping, wet_level |
| Bass Boost | Low frequency enhancement | boost_amount, frequency |
| Stereo Widener | M/S stereo enhancement | width |
| IIR Filters | 10-band parametric EQ | band frequencies, gains, Q |
| Spectrum Extender | VFX_RE spectrum extender | strength |
| Dynamic System | VFX_RE dynamic system | multiple |
| EEL2 Scripting | Custom script engine | script_text |

## Build Configuration

### Solution Structure

```
foobar2000-jamesdsp/
  sdk/                          # foobar2000 SDK (vendored)
  libjamesdsp/                  # libjamesdsp source (vendored/patched)
  foo_dsp_jamesdsp/             # DSP plugin project
    foo_dsp_jamesdsp.cpp        # Main DSP implementation
    jdsp_config_dialog.cpp      # Configuration dialog
    jdsp_ipc_client.cpp         # IPC client
    jdsp_host_manager.cpp       # Host process management
    resource.h                  # Dialog resources
    foo_dsp_jamesdsp.rc         # Dialog template
  jdsp_host/                    # Host process project
    jdsp_ipc_server.cpp         # IPC server
    jdsp_engine.cpp             # JamesDSP wrapper
    main.cpp                    # Entry point
```

### Build Targets

- `foo_dsp_jamesdsp.dll` — Debug/Release for x86 and x64
- `jdsp_host.exe` — Debug/Release for x86 and x64

### Dependencies

- foobar2000 SDK (2025-03-07 or newer)
- libjamesdsp (from JamesDSPManager repo)
- Windows SDK (included with VS 2019)
- No external runtime dependencies

## Localization

### Supported Languages
- English (default)
- Chinese (Simplified)

### Implementation
- String resources stored in separate header files (`strings_en.h`, `strings_zh.h`)
- Language preference stored in foobar2000 config object
- UI switches language dynamically via dialog redraw

### Dialog Layout

Main dialog with top toolbar and tab control:
```
+-----------------------------------------------------------+
| JamesDSP Settings                          [Language: EN] v|  <- Top toolbar
+-----------------------------------------------------------+
| [Modules] [EQ] [Dynamics] [Effects] [Convolver] [Script]  |  <- Tab control
|                                                         |
|  (Tab content area - see per-module layouts below)       |
|                                                         |
+-----------------------------------------------------------+
| [OK]  [Cancel]  [Apply]                                  |  <- Bottom buttons
+-----------------------------------------------------------+
```

Top toolbar contains:
- Window title "JamesDSP Settings" / "JamesDSP 设置"
- Language dropdown (EN/ZH) on the right side
- **Configuration Management** (overall config save/load):
  - [Save Config] — Save all settings to .jdsp file
  - [Load Config] — Load settings from .jdsp file
  - [Reset All] — Reset all modules to defaults

**Configuration File Format (.jdsp):**
- JSON format storing all module states and parameters
- Includes version info for forward compatibility
- File dialog filters: `JamesDSP Config (*.jdsp)|*.jdsp|All Files (*.*)|*.*`

#### Tab 1: Modules (模块开关)
```
+-----------------------------------------------------------+
| Module Enable/Disable                                     |
|                                                           |
| [x] Analog Modelling (电子管模拟)                          |
| [x] BS2B (跨馈环绕声)                                     |
| [x] DDC (动态直接控制)                                     |
| [ ] Limiter (限幅器)                                      |
| [x] Compressor (压缩器)                                   |
| [ ] Convolver (卷积)                                      |
| [x] Reverb (混响)                                        |
| [x] Bass Boost (低音增强)                                 |
| [x] Stereo Widener (立体声增强)                           |
| [x] IIR Filters (IIR 滤波器)                              |
| [ ] Spectrum Extender (频谱扩展)                           |
| [ ] Dynamic System (动态系统)                              |
| [ ] EEL2 Scripting (EEL2 脚本)                            |
+-----------------------------------------------------------+
```

#### Tab 2: EQ (10段参数均衡器)

```
+-----------------------------------------------------------+
| IIR Equalizer - 10 Band Parametric EQ                     |
|                                                           |
| +-------------------------------------------------------+ |
| |          EQ Frequency Response Curve                   | |
| |   +12dB |        .                                     | |
| |         |       / \                                    | |
| |    0dB -|------/--\--------/-------\--------           | |
| |         |     /    \      /         \                  | |
| |  -12dB  |    /      \____/           \___              | |
| |         +--+--+--+--+--+--+--+--+--+--+--+->          | |
| |         31 62 125 250 500 1k 2k 4k 8k 16k Hz          | |
| +-------------------------------------------------------+ |
|   ^   ^   ^   ^   ^   ^   ^   ^   ^   ^                   |
|   |   |   |   |   |   |   |   |   |   |                   |
|  Drag handles to adjust gain (vertical)                   |
|  Right-click handle to change freq/Q                      |
|                                                           |
| Selected Band: 3                                          |
|   Freq: [31   v] Hz   Gain: [+2.0 dB]   Q: [0.70]       |
|                                                           |
| [Reset Flat] [Band 1-10 toggle buttons]                   |
| [Copy EQ] [Paste EQ] [Load AutoEQ] [Save as Preset]      |
+-----------------------------------------------------------+
```

**EQ Curve Widget (自定义绘制):**
- X轴: 频率 (对数刻度, 20Hz - 20kHz)
- Y轴: 增益 (-12dB 到 +12dB)
- 每个频段显示为一个可拖拽的圆形手柄
- 拖拽手柄上下移动: 调整 Gain
- 拖拽手柄左右移动: 调整 Frequency
- 右键点击手柄: 弹出 Q 值调整菜单
- 实时绘制频率响应曲线 (贝塞尔曲线连接各频段)
- 背景显示频率网格线

**Band Toggle Buttons:**
- 每个频段有独立的 Enable/Disable 按钮
- 禁用时手柄变灰, 曲线不显示该频段

#### Tab 3: Dynamics (动态处理)
```
+-----------------------------------------------------------+
| Dynamics Processing                                       |
|                                                           |
| --- Compressor (压缩器) ---                                |
| Enable: [x]                                              |
| Threshold: [----o---------] -20 dB                        |
| Ratio:     [----o---------] 4:1                           |
| Attack:    [----o---------] 5 ms                          |
| Release:   [----o---------] 50 ms                         |
|                                                           |
| --- Limiter (限幅器) ---                                  |
| Enable: [ ]                                              |
| Threshold: [----o---------] -1 dB                         |
| Release:   [----o---------] 50 ms                         |
|                                                           |
| --- DDC (动态直接控制) ---                                 |
| Enable: [x]                                              |
| Strength: [----o---------] 50%                            |
|                                                           |
| DDC Profile:                                              |
| [C:\DDC\profile1.xml                  ] [Browse...]       |
| [Reload Profile]  [Reset to Default]                      |
+-----------------------------------------------------------+
```

#### Tab 4: Effects (音效处理)
```
+-----------------------------------------------------------+
| Effects                                                   |
|                                                           |
| --- Bass Boost (低音增强) ---                               |
| Enable: [x]                                              |
| Boost:  [----o---------] +6 dB                            |
| Freq:   [----o---------] 100 Hz                           |
|                                                           |
| --- Stereo Widener (立体声增强) ---                        |
| Enable: [x]                                              |
| Width:  [----o---------] 120%                             |
|                                                           |
| --- Reverb (混响) ---                                     |
| Enable: [x]                                              |
| Room Size:  [----o---------] 0.7                           |
| Damping:    [----o---------] 0.5                           |
| Wet Level:  [----o---------] 0.3                           |
| Dry Level:  [----o---------] 0.8                           |
|                                                           |
| --- Analog Modelling (电子管模拟) ---                       |
| Enable: [x]                                              |
| Tube Drive: [----o---------] 60%                          |
|                                                           |
| --- BS2B (跨馈环绕声) ---                                  |
| Enable: [x]                                              |
| Feed:      [----o---------] 70%                           |
| Frequency: [----o---------] 650 Hz                        |
|                                                           |
| --- Spectrum Extender (频谱扩展) ---                       |
| Enable: [ ]                                              |
| Strength: [----o---------] 50%                            |
| Profile:                                                  |
| [C:\Profiles\spectrum.xml            ] [Browse...]         |
|                                                           |
| --- Dynamic System (动态系统) ---                          |
| Enable: [ ]                                              |
| Mode: [Normal v]                                         |
| Config File:                                              |
| [C:\Profiles\dynamicsys.xml          ] [Browse...]         |
+-----------------------------------------------------------+
```

#### Tab 5: Convolver (卷积)
```
+-----------------------------------------------------------+
| Convolution Engine                                        |
|                                                           |
| Enable: [x]                                              |
|                                                           |
| Impulse Response File:                                    |
| [C:\IRs\small_room.wav                    ] [Browse...]   |
|                                                           |
| IR Info:                                                  |
|   Sample Rate: 44100 Hz                                   |
|   Channels: 2                                             |
|   Length: 44100 samples (1.0 sec)                         |
|                                                           |
| Gain: [----o---------] 0 dB                               |
|                                                           |
| [Reload IR]  [Test Convolution]                           |
+-----------------------------------------------------------+
```

#### Tab 6: Script (EEL2 脚本)
```
+-----------------------------------------------------------+
| EEL2 Scripting Engine                                     |
|                                                           |
| Enable: [ ]                                              |
|                                                           |
| Script:                                                   |
| +-------------------------------------------------------+ |
| | // Example: Simple lowpass filter                      | |
| | #include <jamesdsp/eel2.h>                             | |
| |                                                        | |
| | sp_rate = srate;                                       | |
| | ...                                                    | |
| +-------------------------------------------------------+ |
|                                                           |
| [Load Script...]  [Save Script...]  [Validate]            |
|                                                           |
| Status: Ready                                             |
+-----------------------------------------------------------+
```

## Error Handling

- Host process crash: Plugin detects pipe closure, attempts restart, notifies user
- IPC timeout: Plugin shows warning, continues with unprocessed audio
- Invalid preset: Falls back to default settings
- Missing host exe: Shows error message, DSP bypasses processing

## Testing Strategy

- Unit tests for IPC protocol serialization/deserialization
- Integration tests with mock JamesDSP engine
- Manual testing with foobar2000 (v1.x and v2.x)
- Audio quality verification (A/B comparison with known reference)
