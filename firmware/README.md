# Magpie Screen 固件 0.2.0

目标板：Waveshare ESP32-S3-Touch-AMOLED-2.16（CO5300 480×480、QMI8658、AXP2101、16 MB Flash、8 MB OPI PSRAM）。

## 三页与按键

面向屏幕、USB 接口在下方时，顶部三个实体键从左到右对应：

| 按键 | 短按 | 其他行为 |
| --- | --- | --- |
| BOOT / GPIO0 | 1：供应商液体余量 | 上电时按住仍进入芯片下载模式 |
| PWR / AXP2101 | 2：实时路由 | 保留 PMU 原有长按关机行为 |
| GPIO18 | 3：今日概览 | — |

第一页接收 Mac 选定的供应商、账号与额度窗口；未知额度显示 `--`，不会填充模拟值。Codex 为灰黑液体，Claude 为橙色。板载 IMU 驱动重力、倾斜、倒置和惯性波动。65 节点波动与面积守恒算法保持原样。

第二页展示客户端 → Magpie → 实际供应商/模型，支持四个目的地与有限次请求/尝试/响应/完成动画。首连与重连建立基线，不重播历史。

第三页展示今日含缓存 Token、输入/输出/缓存拆分、请求、USD 估算、缓存命中率、输出速度和供应商三行汇总（超过三个时第三行合并其他）。文字采用原生精简排版，当前固定阅读方向；网页的文字自动翻正尚未移植。

三页都由固件本地绘制，不需要持续传送屏幕视频。Mac 停止发送状态 45 秒后清空数据并显示离线。亮度、黏性、最后选中的页面、Wi-Fi 凭据和配对密钥保存在 NVS；统计数据不写入 Flash。

## Mac 配置、配网与 OTA

1. 保持 USB 连接，运行仓库根目录的 `npm run build && npm start`。
2. 打开 [设备配置](http://127.0.0.1:4173/config)。修改供应商、账号、窗口、亮度或黏性，收到设备 ACK 后才保存 Mac 配置。
3. 输入 **2.4 GHz** Wi-Fi 的 SSID 和密码。凭据通过 USB 发送，只存入开发板，不存入 Mac 配置文件。
4. 出现设备 IP 后可拔除 USB 数据连接，使用其他方式供电；Mac 与开发板需要在同一局域网。默认使用已发现的 IP 或 `magpie-screen.local`；也可填写局域网 IP。
5. OTA 选择 `firmware/.pio/build/liquid/firmware.bin`。Mac 通过带配对密钥的 HTTP 上传，固件使用 Arduino Update 写入备用 OTA 应用分区，校验通过后切换并重启。不要选择 bootloader、partitions 或整片备份。

首次配网依赖 USB，不提供手机热点配网页。配对密钥只通过物理 USB 设置，Wi-Fi 命令和 OTA 均要求密钥。Mac 服务继续只监听 `127.0.0.1:4173`。首次烧录、分区变化和故障恢复使用 USB；普通应用更新使用 OTA。

## 构建与烧录

安装 PlatformIO Core 后：

```sh
pio run -d firmware
# 先停止 npm 服务，释放 USB 串口
pio run -d firmware -t upload --upload-port /dev/cu.usbmodem21101
```

本机 PlatformIO 位于 `/tmp/magpie-screen-flash-tools/bin/pio`。端口以实际连接为准；工具目录位于临时目录，重启后可能需要重新安装。原厂整片备份继续保留在被忽略的 `device-backups/original-2026-10-23/`，未执行回刷或擦除整片 Flash。

Mac USB 桥接使用 Python 3 + pyserial。优先读取 `MAGPIE_PYTHON` 环境变量，当前机器也识别上述工具虚拟环境，否则使用 `python3`。仅自动连接唯一的 Espressif USB Serial/JTAG 设备（VID 303A / PID 1001）。

```sh
python3 -m venv .local/python
.local/python/bin/pip install pyserial
MAGPIE_PYTHON="$PWD/.local/python/bin/python" npm start
```

纯算法测试：

```sh
c++ -std=c++17 -O2 firmware/test/liquid_test.cpp -o /tmp/magpie-liquid-test
/tmp/magpie-liquid-test
```

`labels.h` 是预生成的少量中文标签，不需要字体生成依赖即可构建。维护标签时可在 macOS 使用 Pillow 运行 `scripts/generate_labels.py`；日常构建不需要 Pillow。

## 性能与验证（2026-10-23）

- USB 写入与芯片校验通过；真实额度、三页软件切换和配置 ACK 已在此板验证，配置往返约 50–70 ms。
- 第一页在 Wi-Fi 已连接、真实 Codex 余量下连续采样约 20 秒，平均 **30.000 FPS**（2 秒窗口 29.78–30.21）；与前版约 21 FPS 相比，采用两个 **480×24 内部 RAM 缓冲**，绘制运行于主任务，专用显示任务执行 QSPI DMA 传输，两个阶段重叠。分辨率仍为 480×480。
- 修复 GFX 1.5.9 自定义字体在分块绘制时漏掉负坐标裁剪导致的内存越界；USB 接收队列扩展为 32 KB，防止完整状态 JSON 被截断。
- 网页构建、Node 测试和液体 C++ 算法测试通过。日志和帧率采样保存在被忽略的 `.local/`。
- Wi-Fi 凭据由使用者在配置页填写后，已验证局域网命令（正确密钥 200、错误密钥 401）、一次 OTA 写入重启，以及重启后自动连接 Wi-Fi、恢复数据。暂停 USB 桥接后，Mac 自动改用 Wi-Fi 下发配置，确认约 88 ms。路由器中断后的重连未单独模拟；实体键的手感、按键顺序和屏幕观感需要在板上人工确认。

## 驱动依据

引脚与显示初始化来自 [Waveshare 官方示例](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-2.16/tree/225a62bff11b5d0a0b607873860d39485a9a9685/examples/arduino)。依赖版本锁定在 `platformio.ini`；PlatformIO espressif32 6.12.0 对应 Arduino 2.0.17，保留兼容的 GFX 1.5.9。不包含原厂演示的定时重启逻辑。
