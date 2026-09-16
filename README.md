# foobar2000 JamesDSP 插件

把 [JamesDSP](https://github.com/james34602/JamesDSPManager)（libjamesdsp）的音频效果链带到 foobar2000，
支持 **foobar2000 v1.x (x86)** 与 **v2.x (x64)**。

界面支持英文/中文（默认英文），所有参数调整**实时生效**，无需按"确定"。

## 功能

共 12 个模块，每个参数都直接对应库的真实 API（没有摆设控件）：

| 模块 | 参数 |
|---|---|
| Analog Modelling（电子管模拟） | 驱动 -3 ~ +12 dB |
| Crossfeed / BS2B | 模式 0–5（BS2B Lv1/Lv2、HRTF Crossfeed、HRTF Surround 1/2/3） |
| ViPER DDC | 配置文件（`.vdc` 文本） |
| Limiter | 阈值 ≤ -0.09 dB、释放 ≥ 0.15 ms |
| Compressor | 时间常数、Granularity 0–3、TF Resolution 0–3、7 段增益 |
| Convolver | 脉冲响应文件（WAV：8/16/24/32bit PCM、32bit float、Extensible） |
| Reverb | 预设 0–18（19 种）+ 湿声、干声、宽度、混响时间、阻尼、低音、预延迟、早期反射 |
| Bass Boost | 最大增益 0–15 dB（自适应低频增强，见下方"已知限制"） |
| Stereo Widener | 展宽 0–100% |
| Equalizer | 15 段（25 Hz – 16 kHz）、Filter type（FIR 最小相位 / IIR 4·6·8·10·12 阶）、插值（PCHIP / Makima）、可拖拽曲线 |
| Spectrum Extender | 响应文件（文本，每行 `频率 增益`） |
| EEL2 Script | EEL2 脚本（需含 `@init` 与 `@sample`，声道寄存器为 `spl0`/`spl1`） |

另有全局 **Output Gain**（-15 ~ +15 dB）。

## 架构

插件采用**进程外**设计（与官方 VST 适配器类似）：

```
foobar2000  ──►  foo_dsp_jamesdsp.dll   (DSP 插件，跑在 foobar2000 进程内)
                        │  二进制 IPC（stdin/stdout，帧协议）
                        ▼
                 jdsp_host.exe          (JamesDSP 引擎宿主，独立进程)
```

- `foo_dsp_jamesdsp.dll` 只负责音频格式转换与参数下发。
- `jdsp_host.exe` 承载 libjamesdsp，崩溃不会带走 foobar2000。
- 帧协议定义在 [`jdsp_ipc_protocol.h`](jdsp_ipc_protocol.h)。

## 安装

1. 从 [Releases](https://github.com/ctzjj/Foobar2000-JamesDSP/releases) 下载对应架构的压缩包：
   - foobar2000 **v2.x / x64** → `foo_dsp_jamesdsp-x64.zip`
   - foobar2000 **v1.x / x86** → `foo_dsp_jamesdsp-x86.zip`
2. 关闭 foobar2000。
3. 把压缩包里的 **两个文件** 解压到 foobar2000 的 `components` 目录：
   ```
   components\foo_dsp_jamesdsp.dll
   components\jdsp_host.exe
   ```
   > `jdsp_host.exe` 必须与 DLL 位于同一目录，DLL 会从自己的目录启动它。
4. 启动 foobar2000 → `File → Preferences → Playback → DSP Manager`，把 **JamesDSP** 加入 Active DSPs。

## 使用

在 DSP Manager 里选中 JamesDSP，点 **Configure** 打开配置窗口，共 7 个标签页：

`Modules`（模块开关）· `Equalizer`（均衡器）· `Dynamics`（动态）· `Effects`（效果）·
`Convolver` · `Spectrum` · `Script`

- **实时生效**：拖动滑块/曲线时立刻能听到变化（播放中打开即可）。
- **取消**：恢复打开窗口前的状态。
- **确定**：写入 DSP 预设并持久化（foobar2000 会保存到自己的配置里）。
- **Save Config / Load Config**：导出/导入 `.jdsp` 配置；**Reset All**：全部恢复默认。
- 各标签页的"启用"勾选框与 `Modules` 页联动，改哪个都行。

配置文件格式是纯文本的 `key=value` 行，`.jdsp` 文件即该文本本身。

## 从源码编译

需要 **Visual Studio 2019（v142）** 或 **Visual Studio 2022（v143）**，含 C++ 桌面开发组件。
foobar2000 SDK 已经随仓库提供（`sdk/`），无需额外下载。

```powershell
# x64
msbuild "foobar2000-jamesdsp.sln" /p:Configuration=Release /p:Platform=x64

# x86
msbuild "foobar2000-jamesdsp.sln" /p:Configuration=Release /p:Platform=Win32
```

（VS2022 若未安装 v142 工具集，加 `/p:PlatformToolset=v143`。）

产物：

| 平台 | 输出目录 | 文件 |
|---|---|---|
| x86 | `Release\` | `foo_dsp_jamesdsp.dll`, `jdsp_host.exe` |
| x64 | `x64\Release\` | `foo_dsp_jamesdsp.dll`, `jdsp_host.exe` |

仓库根目录的 `atl*.h` 是 ATL 的最小存根，用于在**没有安装 ATL** 的 Build Tools 下编译。

## 持续集成 / 发布

[`.github/workflows/build.yml`](.github/workflows/build.yml)：**发布 Release 时**自动在
`windows-2022` 上构建 x86 与 x64，打包为 `foo_dsp_jamesdsp-x86.zip` /
`foo_dsp_jamesdsp-x64.zip` 并附加到该 Release。也可用 `workflow_dispatch` 手动触发
（可填一个已有的 release tag 把产物挂上去）。

> 使用草稿（Draft）Release 时，产物会在**发布**该草稿时生成。

## 目录结构

```
foo_dsp_jamesdsp/        foobar2000 DSP 插件（对话框、EQ 曲线控件、IPC 客户端）
jdsp_host/               引擎宿主进程（libjamesdsp 封装、WAV 解析、IPC 服务端）
libjamesdsp/             JamesDSP 引擎源码（上游）
sdk/                     foobar2000 SDK + pfc + libPPUI（随仓库提供）
jdsp_ipc_protocol.h      DLL ↔ host 的二进制帧协议
jdsp_reverb_presets.h    混响预设参数表（DLL 与 host 共用）
docs/                    设计与实现文档
```

## 已知限制

- **Bass Boost 没有频率参数**：JamesDSP 用 16 点 FFT 自适应检测低频峰值再增强，
  只能设置最大增益。若要固定频率的低频提升，请用均衡器最低几段（25/40/63/100/160 Hz）。
- **均衡器没有 Q**：库中的 15 段 FIR/IIR 均衡器本身没有 Q 参数。
- **卷积器没有独立增益**：请用全局 `Output Gain`。
- **BS2B 的 feed / cutoff 未单独暴露**：库以 6 种模式提供（见 Crossfeed 模式）。
- 采样率变化、切换音轨时宿主会重启，可能有极短暂停顿。
- 配置键基于新参数面；旧版预设中的过时键会被忽略并回落到默认值。

## 致谢与许可

- 本仓库自身代码采用 [MIT 许可](LICENSE)。
- 音频引擎来自 **JamesDSP / libjamesdsp**，版权归其作者所有。
- 使用 **foobar2000 SDK** 开发，SDK 的使用受 [`sdk/sdk-license.txt`](sdk/sdk-license.txt) 约束
  （`sdk/pfc/pfc-license.txt`、`sdk/libPPUI/libPPUI-license.txt` 同理）。
