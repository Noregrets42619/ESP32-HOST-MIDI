# WT99P4C5 MIDI Piano 工程介绍

> 作者：Ecoli  
> 知识文档：[noregrets42619.github.io](https://noregrets42619.github.io/)  
> 开源地址：[Noregrets42619/ESP32-HOST-MIDI](https://github.com/Noregrets42619/ESP32-HOST-MIDI)

## 工程定位

这是一个面向 WT99P4C5-S1 开发板的 MIDI Piano 固件工程。主控为 ESP32-P4，WiFi 和蓝牙能力由板载 ESP32-C5 通过 ESP-Hosted 提供。

工程基于 ESP-IDF 构建，同时引入 Arduino component，用于复用 `ESP32_Host_MIDI`、AppleMIDI、Arduino WiFi 和 BLE MIDI 相关能力。整体目标不是重写上游 Arduino 工程，而是在 P4+C5 硬件上完成最小必要移植，并保持后续维护成本可控。

## 已实现功能

- USB Host MIDI：接入 USB MIDI 键盘并解析 NoteOn / NoteOff 等事件。
- SPI 屏幕显示：使用 ST7789 SPI 屏显示 piano UI 和当前按下键位。
- 本地音频输出：通过 ES8311 codec 和 I2S 输出合成音频。
- 外接功放控制：支持 NS4150B 功放使能，当前硬件确认为 GPIO53。
- WiFi AppleMIDI：通过 C5 hosted WiFi 连接热点，并提供 RTP-MIDI / AppleMIDI 服务。
- BLE MIDI：通过 C5 hosted Bluetooth controller 和 P4 NimBLE host 提供 BLE MIDI peripheral。
- MIDI 桥接：USB MIDI 可转发到 RTP-MIDI 和 BLE MIDI，网络或 BLE 输入也可进入本地显示与音频链路。
- 功能开关：WiFi MIDI 和 BLE MIDI 可按需独立开启，便于在资源紧张时切换模式。

## 工程目录说明

### 根目录

根目录保存工程级配置、分区表、SDK 配置、依赖锁定文件和项目说明文档。

- `CMakeLists.txt`：ESP-IDF 顶层工程入口。
- `partitions.csv`：Flash 分区表，保留了应用和数据分区。
- `sdkconfig` / `sdkconfig.defaults`：当前 P4 工程配置和默认配置。
- `dependencies.lock`：ESP-IDF Component Manager 依赖锁定结果。
- `README.md`：当前工程介绍文档。

### main

主入口目录，只负责启动最外层应用流程。

这里的入口文件保持尽量小，只做三件事：

- 初始化 NVS。
- 初始化 Arduino runtime。
- 调用 piano 应用层的初始化和循环函数。

SPIFFS 镜像生成已经改为可选：如果根目录存在 `spiffs` 文件夹，构建时会自动生成并烧录 SPIFFS 镜像；如果删除该文件夹，则跳过镜像生成，不再导致 reconfigure 或 build 报错。

### components/piano_app

当前 MIDI Piano 固件的核心应用组件。

它负责把 USB、WiFi、BLE、显示和音频串成完整业务链路：

- 接收 USB MIDI 键盘输入。
- 维护当前 128 个 MIDI note 的按下状态。
- 刷新 piano UI。
- 驱动音频合成器发声。
- 启动 WiFi RTP-MIDI。
- 启动 BLE MIDI peripheral。
- 在 USB、RTP-MIDI、BLE-MIDI 之间进行必要桥接。

该组件下的几个薄封装层分别承担不同职责：

- 显示层：负责 SPI 屏初始化、背光控制、framebuffer 绘制和 piano UI 渲染。
- 音频层：负责 ES8311/I2S 初始化、MIDI note 到音频合成的转换、音量和频段增益控制。
- WiFi 层：负责连接 hosted WiFi、启动 mDNS 和 RTP-MIDI 服务，并暴露 MIDI transport。
- BLE 层：负责启动 BLE MIDI peripheral、维护连接状态，并暴露 MIDI transport。
- 配置层：集中管理屏幕引脚、显示方向、WiFi 参数、RTP-MIDI 名称、BLE 名称和音频增益等选项。

### components/ESP32_Host_MIDI

这是从上游 Arduino MIDI 工程移植进来的核心 MIDI 库。

当前工程主要使用其中的：

- MIDI handler：统一解析和维护 MIDI 事件。
- USB MIDI transport：处理 USB Host MIDI 数据。
- BLE MIDI transport：提供 BLE MIDI peripheral 能力。
- RTP-MIDI transport：配合 AppleMIDI 实现网络 MIDI。

为了适配 ESP-IDF 混合编译，工程为该库补充了 IDF 组件构建入口，并按功能逐步纳入需要的源文件和依赖。

### components/AppleMIDI

AppleMIDI / RTP-MIDI 相关库，用于让设备在局域网内作为 AppleMIDI 设备被发现和连接。

它配合 Arduino WiFi、UDP 和 mDNS 使用。当前工程中，USB MIDI 键盘输入可以通过该层转发给手机、电脑或其他支持 RTP-MIDI 的软件。

### components/MIDI

Arduino MIDI 基础库，作为 AppleMIDI 和部分 MIDI transport 的底层依赖。

### components/wt99p4c5_s1_board

WT99P4C5-S1 的板级支持组件。

当前工程主要复用其中的硬件定义和音频相关能力，例如 ES8311、I2S、GPIO、SD/SPIFFS 等板级接口。虽然当前 MIDI Piano 主链路不依赖 SPIFFS 资源文件，但保留 BSP 中的 SPIFFS API 不影响编译。

### managed_components

ESP-IDF Component Manager 下载并管理的依赖组件目录。

这里包含 Arduino-ESP32、ESP-Hosted、WiFi remote、mDNS、codec、LVGL 等依赖。当前工程实际使用的重点是 Arduino-ESP32、ESP-Hosted/WiFi remote、mDNS 和音频 codec 相关组件。

### docs

项目文档目录，可用于放置 MkDocs 站点内容或本地开发记录。

当前公开知识文档地址为：

[https://noregrets42619.github.io/](https://noregrets42619.github.io/)

### firmware

固件产物或发布相关文件目录，可用于保存已验证的 bin、配置包或烧录说明。

### mp4

演示视频资源目录。当前构建系统不依赖该目录，删除后不影响编译。

### spiffs

可选 SPIFFS 资源目录。目录存在时会自动打包为 `storage` 分区镜像；目录不存在时构建系统会跳过 SPIFFS 镜像生成。

也就是说，如果当前固件不需要预置文件资源，可以删除该目录；如果后续需要在 Flash 内置音色、图片、配置或媒体资源，可以重新创建该目录并放入资源文件。

## 当前 MIDI 数据链路

```text
USB MIDI 键盘
    -> USB Host MIDI
    -> MIDI handler
    -> 屏幕显示
    -> ES8311 音频输出
    -> RTP-MIDI / BLE-MIDI 转发

AppleMIDI / RTP-MIDI 输入
    -> WiFi hosted
    -> MIDI handler
    -> 屏幕显示
    -> ES8311 音频输出

BLE MIDI 输入
    -> C5 Bluetooth controller
    -> ESP-Hosted VHCI
    -> P4 NimBLE host
    -> MIDI handler
    -> 屏幕显示
    -> ES8311 音频输出
```

## 构建说明

推荐使用 VSCode ESP-IDF 插件构建本工程。

常用流程：

1. `ESP-IDF: Reconfigure Project`
2. `ESP-IDF: Build Project`
3. `ESP-IDF: Flash Project`
4. `ESP-IDF: Monitor`

如果删除了 `spiffs` 文件夹，构建时会跳过 SPIFFS 镜像，不需要额外创建空目录。

如果要同时启用 WiFi MIDI 和 BLE MIDI，需要确认 P4 侧已经开启 hosted NimBLE VHCI，C5 侧 slave 固件也开启 BT sharing。

启动日志中应能看到类似：

```text
Host BT Support: Enabled
BT Transport Type: VHCI
```

## 维护原则

- 优先复用上游 Arduino MIDI 代码，减少不必要重写。
- 板级差异集中在配置和薄封装层处理。
- USB、WiFi、BLE、显示和音频模块保持相对独立。
- 如果 WiFi 与 BLE 同时开启后资源紧张，优先通过配置切换为单独 WiFi 或单独 BLE 模式。
- 如果屏幕或音频硬件更换，优先修改对应配置和封装层，而不是改 MIDI 核心逻辑。
