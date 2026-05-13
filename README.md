# ESP32-S3 Audio Reactive LED Controller 🎵✨

基于 ESP32/ESP32-S3 的 I2S 音频拾音分析与 WS2812 灯带拾音灯项目。通过 I2S 麦克风实时采集音频，使用 `arduinoFFT` 分析音频频谱，提供分频段的动态音乐灯效，并内置 Web 服务器供手机实时期配置灯光色彩与特效。

## ✨ 特性 (Features)

*   **双核调度 (Dual-Core Processing)**：音频采样与 FFT 计算在 Core 1 执行，WiFi 网络与 Web 服务保留在 Core 0，保证高负载 FFT 及大量 LED 刷新操作不干扰网络通讯。
*   **原生 AP 修复 (Native AP Fix)**：巧妙使用 `ESP-IDF` 原生 API 配置 WiFi 广播，完美绕过 ESP32-S3 在部分 Arduino Core 版本下 `WiFi.softAP()` 省电休眠导致无广播的玄学 Bug。
*   **DSP 噪声过滤**：内建高通滤波器 (HPF)，自动消除 I2S 麦克风的直流漂移和环境 50Hz/低频沉闷底噪。
*   **Web 实时控制 UI**：通过手机连接 WiFi 即可调整：
    *   全局亮度调节
    *   多区域 (Zones) 独立控制
    *   全彩取色器 (RGB)
    *   调试信息输出开关
*   **7种独立灯效 (Effects)**：`Solid(纯色)`, `Breathe(呼吸)`, `Rainbow(彩虹)`, `Chase(追逐)`, `Off(关闭)`, `Audio Blink(随音量闪烁)`, `Audio VU(音频多段音量柱)`。

## 🧰 硬件需求 (Hardware Requirements)

*   **主控**：ESP32-S3 / ESP32 开发板
*   **麦克风**：INMP441 等 I2S 数字麦克风模块
*   **灯带**：WS2812 / WS2812B RGB 灯带 (默认配置为 48 颗 LED)
*   *建议为灯带提供独立 5V 供电，切勿直接使用开发板 5V 引脚以防烧毁*

## 🔌 引脚接线 (Pinout)

*可以在代码开头的 `#define` 中自定义*

| I2S 麦克风 | ESP32-S3 引脚 | 说明 |
| :--- | :--- | :--- |
| **WS (L/R)** | GPIO 5 | 字选择 (Word Select) |
| **SCK (BCLK)** | GPIO 6 | 串行时钟 (Serial Clock) |
| **SD (DOUT)** | GPIO 4 | 串行数据输出 |
| **L/R** | GND | 左右声道选择 (接GND选择左声道) |
| **VDD/GND** | 3.3V / GND | 电源 |

| WS2812 灯带 | ESP32-S3 引脚 | 说明 |
| :--- | :--- | :--- |
| **DIN (数据)** | GPIO 3 | 灯带数据输入 |
| **VCC/GND** | 5V / GND | 请使用外部电源独立供电 |

## 📦 依赖库 (Dependencies)

请在 Arduino IDE 的“库管理器”中安装以下库：
1.  **arduinoFFT** (版本建议 2.x) - 用于频谱分析计算
2.  **Freenove WS2812 Lib for ESP32** - 专为 ESP32 优化的轻量级免中断 RMT LED 驱动

*(其余如 `WiFi`, `WebServer`, `driver/i2s.h`, `esp_wifi.h` 均为 ESP32 Arduino Core 自带无需额外安装)*

## 🚀 快速上手 (Quick Start)

1.  在 Arduino IDE 中选择您的 ESP32 / ESP32-S3 开发板，编译并烧录。
2.  板子启动后，将释放一个无需外网的 WiFi 热点：
    *   **SSID (WiFi名称)**：`ESP32_Audio_LED`
    *   **Password (密码)**：`12345678`
3.  手机或电脑连接该热点。
4.  打开浏览器，访问控制面板：**`http://192.168.4.1`**
5.  在网页面板中开启你想玩的音频特效，播放音乐看灯带跳动吧！

## 🛠️ 参数调优 (Tuning)

如果环境嘈杂，或麦克风灵敏度不同，可以在代码中修改以下参数：

```cpp
#define NOISE_FLOOR     0.8  // 底噪门限：安静时灯还会亮就调大它 (0.5~3.0)
#define PEAK_SIGNAL     5.0  // 满载峰值：灯太容易全亮就调大它，不容易亮就调小 (10.0~30.0)
```

## 📜 许可 (License)
MIT License. Feel free to use it and modify it for your own projects!
