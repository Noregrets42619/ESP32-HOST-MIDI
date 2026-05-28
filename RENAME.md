# WT99P4C5 MIDI Piano 完整移植日志

本文主要用来记录本次移植 `ESP32_Host_MIDI` piano 固件的完整过程：从旧工程审查、项目目标调整到编译报错、烧录验证，到最后打通 USB、屏幕、音频、WiFi 和 BLE 的主要节点。

## 1. 工程结构确认

最开始先审查了整个文件夹。该工程不是普通 Arduino 工程，而是一个 Arduino 的库文件工程。目录中包含 `example`、`src`、 和说明文档。

旧工程虽对P4支持，但主要原生功能均集中于esp32S3系列芯片，P4仅为二合一MIDI中转器。 旧工程中`example`中，主要功能为基于 `T-display S3`开发板的piano项目，`examples`基于`arduino`环境，`example`按照实现功能基于蓝牙+USB，基于ESP32_NOW+USB，以及屏幕显示内容等，分为多个项目，并未存在同时使用USB+WIFI+BLE项目。


## 2. 项目目标调整

基于P4C5开发板外设，复刻目标最终确定为：以 ESP-IDF 作为主工程环境，将USB，WIFI，BLE以及屏幕显示，集成于同一个项目，并复用 `ESP32_Host_MIDI` 的 piano 相关逻辑，实现功能集合与拓展。

参考ESPIDF官方指示文件`esp32_p4_function_ev_board`，下载`esp-hosted-mcu` 例程，并进入`esp-hosted-mcu\slave`目录，进行工程编译并烧录至C5。

基于 USB-TTL 对 C5 侧烧录 `hosted slave`固件，P4 侧 `sdkconfig` 也需要进行配置。 主要工作集中在 P4 工程本身：USB MIDI、SPI 屏、ES8311 音频、WiFi AppleMIDI 和 BLE MIDI。

## 3. ESP32_Host_MIDI 接入 IDF 构建

下载ESP32P4C5例程，剔除多余spiffs等文件，删除不需要的组件，保留BSP板级支持文件，用于后续音频开发。

`ESP32_Host_MIDI` 被拷贝进 `components` 目录。这个库本身是 Arduino 风格，直接放入后不能被 IDF 当作组件编译，因此补了 CMake 构建入口。

首先只让最小功能参与编译：MIDI handler、USB Host MIDI 和 UART MIDI。BLE、WIFI没有在一开始全部加入程序。

 `main` 被改成最小 USB MIDI 验证入口：初始化 NVS、初始化 Arduino runtime、注册 USB MIDI transport、启动 MIDI handler，并在日志中打印 NoteOn / NoteOff。

## 4. 编译报错

第一次在 VSCode 中 Reconfigure 时，工程报了 `usb/usb_host.h` 找不到。询问AI得知问题不在 IDF 环境，而是 USB 组件依赖作用域写窄了。`USBConnection.h` 公开包含 `usb/usb_host.h`，所以 `usb` 必须作为公共依赖传递。CMake 依赖改成公共依赖后，这个问题解决。

接着又遇到 Brookesia 的 `Unknown memory malloc`。这个错误来自旧 UI 框架继续被工程拉进来编译，而当前主应用已经不再使用 Brookesia。旧依赖从 main manifest 中移除后，工程主线回到 MIDI。

后来 Component Manager 还在下载 `lvgl/lvgl`，并且下载下来的 `component.zip` 不是合法 zip。判断最终项目不需要 LVGL，于是 BSP 切成无 LVGL 图形库模式，并移除 `lvgl/lvgl` 和 `esp_lvgl_port` 等旧显示栈依赖。

这些处理完成后，工程能够正常编译。

## 5. USB MIDI 烧录验证

编译通过后，先烧录最小固件并接入 USB MIDI 设备验证数据链路。串口能看到 NoteOn / NoteOff，说明 P4 的 USB Host 能识别 MIDI 设备。

当时日志中出现两个现象：

- 上电或者插入MIDI键盘后，有 `HUB: Root port reset failed`。
- 同一个 MIDI 事件会打印两次。

`Root port reset failed` 后续不影响 MIDI 收包，因此认定为 USB Host 初始化阶段的非致命提示。重复打印更影响调试，处理方式为加了一个很窄的 USB-MIDI packet 去重保护。

它只丢掉极短时间内完全相同的 USB-MIDI 4 字节包，并忽略 cable number。处理完成后，USB MIDI 输入变干净，piano 层可以继续向上接。

## 6. Piano 状态层与 SPI 屏

USB MIDI 正常后，开始搭建 piano 状态层。屏幕尺寸和 Host-MIDI 示例中的 piano display 接近，P4C5开发板的小屏幕，使用排针走 SPI 协议，8 针屏幕中的 BLK 单独使用一个 GPIO 控制。

最初尝试延续原工程，使用`Arduino LovyanGFX` 库，将 piano 状态和显示层拆开：一个部分维护 128 个 MIDI note 状态，另一个部分绘制 ST7789 170x320 的 piano UI。

默认 SPI 引脚先按当时引出的 IO 设定：SCLK=7、MOSI=8、CS=6、DC=4、RST=5、BLK=38。

LovyanGFX 也被拷贝进 components，用于尝试复用其屏幕驱动能力。

## 7. 显示层 panic

第一次烧录后，USB MIDI host 初始化成功，但随后发生 panic。询问GPT，栈中显示崩在 LovyanGFX 的 `fillScreen()`，`PianoDisplay::begin()` 访问屏幕宽高时出错，屏幕没有显示。

后续做过几次尝试：把 LovyanGFX 对象改成运行时创建，避免全局构造阶段初始化外设对象；在 `tft.init()` 失败时提前返回，不再继续 `fillScreen()`等。

这些处理完成后仍然 panic。新的日志依旧卡在 `fillScreen()` 和 `width()`，说明问题集中在 LovyanGFX 的 panel 状态，而不是钢琴绘制逻辑。


## 8. 改用 IDF esp_lcd 直接驱动 SPI

最终决定重写SPI方式，目标是：上层仍按 Host-MIDI 的 PianoDisplay 绘制思路组织，但物理推屏不再走 Arduino/LovyanGFX 显示库，而是改成 IDF 原生 `esp_lcd` 驱动 ST7789。

这一步没有大改业务逻辑，只替换底层显示后端。先用 `esp_lcd` 初始化 SPI ST7789，再用 `esp_lcd_panel_draw_bitmap()` 推整帧。

最开始还保留 LovyanGFX 的 `LGFX_Sprite` 当画布，但烧录后屏幕亮起并出现花屏，程序崩在 `LGFX_Sprite::createSprite()`。这个现象说明物理 SPI 已经打通，真正不稳定的是 LovyanGFX 相关对象。

随后 `LGFX_Sprite` 也被移除，改成本地 framebuffer 画布，只保留 piano 需要的矩形、线条和简单文字绘制。至此显示层完全避开 Arduino 显示库，底层走 IDF `esp_lcd`，上层继续保留 Host-MIDI 的 piano display 思路。 components 中 LovyanGFX 库被删除。

## 9. SPI 屏幕调通

换成 `esp_lcd` 后，屏幕能亮，日志也显示 ST7789 SPI panel 初始化成功。后面的问题变成屏参问题：一圈花屏，一圈白屏，但是按 MIDI 键时画面有变化。

这个现象说明 SPI 写屏链路已经工作，问题集中在窗口偏移、横竖屏和像素格式上。后续确认 `swap_xy(true)` 横屏后，原来 Host-MIDI 示例中的物理偏移轴需要调整。之前偏移加在 X 上，写入窗口会越界，因此出现一边花、一边白。

偏移最终改成 X=0、Y=35，并将 endian、mirror、swap、RGB 顺序等屏参集中成宏。再次烧录后，屏幕正常显示，接入 MIDI 键盘时也能显示按下的键位。至此SPI屏幕显示移植正常。

## 10. ES8311 音频接入

屏幕跑通后，开始处理音频。板级支持文件中已有 I2S 和 ES8311 的初始化函数，Host-MIDI 示例中也有 `SynthEngine.h`。因此音频部分沿用 Host-MIDI 的合成思路，将输出端换成 P4C5 板子上的 ES8311。

新增的音频合成层负责根据 MIDI NoteOn / NoteOff 管理复音、生成 PCM 数据，并把音频写到 ES8311。主应用中的 MIDI 事件同时进入显示层和音频层。

这里遇到一个硬件冲突：屏幕 SPI 一开始用了 GPIO7/8，而 BSP 中 ES8311 的 I2C 也在 GPIO7/8。为了保留已经调通的屏幕链路，先加了冲突保护；后续屏幕 SPI 挪到其他引脚后，音频链路能够正常初始化。

## 11. 声音小和功放使能脚处理

音频第一次能出声后，频率正确，但声音很小。 最后调整音量增强参数，增大声音增益。 重新烧录后声音恢复，并且比最开始更大。

后续又出现低频偏小、高频偏大的听感问题。处理方式不是继续单纯拉大总音量，而是加入按音高变化的增益补偿：低音区补一点，高音区压一点。这样听感比单纯提高主增益更自然。

## 12. WiFi 和 AppleMIDI 接入

音频之后开始接 WiFi。P4 的 WiFi 由 C5 hosted slave 提供，前期已成功烧录C5例程，并使用ESPIDF官方可用参考代码验证。不过 `ESP32_Host_MIDI` 的 RTP-MIDI 示例依赖 Arduino 的 `WiFiUDP`、`ESPmDNS` 和 AppleMIDI 接口，直接重写成纯 IDF UDP 会牵扯更多适配。

因此 AppleMIDI 和 MIDI 库被补上 IDF 组件构建，RTP-MIDI 作为新的 MIDI transport 接入现有 `midiHandler`，同时新增 WiFi 封装层负责连接热点、启动 mDNS 和 RTP-MIDI。

这一步编译时遇到 AppleMIDI 的位判断错误：库里把 `(flags & FLAG)` 写成 `== 1`，在当前编译器和 `-Werror` 下会被判成永远 false。相关判断改成 `!= 0` 后，AppleMIDI 编译通过。

## 13. Hosted WiFi 内存问题

WiFi 第一次运行时，`esp_hosted` 创建 SDIO mempool 时出现 `no mem`，意为DMA溢出，处理方式是先调整 hosted SDIO 队列和不必要的 hosted 功能，压低内部 DMA 内存占用。

调完后 P4 能连上热点，MIDI 软件也能搜索到设备。

当时弹琴仍然没有传到手机端。后续确认 RTP-MIDI transport 只是作为输入接进本地 synth，USB 键盘输入不会自动发到 AppleMIDI。也就是说设备能被发现，但 USB MIDI 没有桥接到网络端。

USB transport 随后从自动队列中拆出，改为手动处理：USB 收到 MIDI 后，一份喂给本地显示和音频，一份转发给 RTP-MIDI。这样手机端能够收到 MIDI 消息并演奏。

## 14. WiFi 与 BLE 功能开关

WiFi AppleMIDI 跑通后，日志中没有明显 mempool 或 SDIO DMA 失败，WiFi 单独运行已经可用。

为了防止DMA后续溢出，程序设置定义WIFI，BLE开启标志位，如遇到DMA可分配资源过小，可以修改标志位决定开关状态。 WiFi MIDI 和 BLE MIDI 都做成独立开关，用于按需要选择单独 WiFi、单独 BLE，或两者一起运行。

## 15. P4 + C5 Hosted BLE 开启

BLE 不能按普通 ESP32 本地蓝牙处理，因为 P4 本身没有蓝牙控制器。配置路线是打开 P4 侧 Bluetooth Host、关闭本地 Controller，并启用 NimBLE + Hosted VHCI。

配置完成后，启动日志中出现：

```text
Host BT Support: Enabled
BT Transport Type: VHCI
```

看到这两行后，可以确认 P4 到 C5 的蓝牙 HCI 通道已经启动。

## 16. BLE MIDI peripheral 接入

最后将 `ESP32_Host_MIDI` 中现有的 BLE transport 接进工程。这个过程没有额外下载 Arduino BLE 库，因为 Arduino-ESP32 component 中已经带有 BLE 相关实现。

BLE 源文件在 BT/NimBLE 开启时参与构建，并补上 BT 依赖，避免 BLE 头文件间接包含 NimBLE 头时找不到依赖。同时新增 BLE MIDI 封装层，用于初始化 BLE MIDI peripheral、维护连接状态，并把 BLE MIDI 数据送进统一的 MIDI handler。

BLE 接完后，USB MIDI 输入也能同时转发到 BLE MIDI，BLE 端发来的 MIDI 也能进入本地显示和音频链路。到这里，USB、显示、音频、WiFi AppleMIDI 和 BLE MIDI 都完成收尾。

## 17. 项目最终状态

最终，一个原本偏综合 demo 的 WT99P4C5-S1 工程被整理成能够实际运行的 MIDI Piano 工程。上游 Host-MIDI 没有被完全重写，能复用的部分尽量保留；真正重写的是在 P4+C5 这块硬件上不稳定或不适合的底层适配，例如 SPI 显示后端。

最终跑通的链路是：

```text
USB MIDI 键盘 -> 本地显示 -> ES8311 发声 -> RTP-MIDI / BLE-MIDI 转发
AppleMIDI 输入 -> 本地显示 -> ES8311 发声
BLE MIDI 输入 -> 本地显示 -> ES8311 发声
```
