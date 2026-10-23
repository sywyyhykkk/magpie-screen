# Magpie Screen 独立固件 0.3.9

适用硬件：Waveshare ESP32-S3-Touch-AMOLED-2.16，CO5300 480×480 AMOLED、QMI8658、CST92xx、AXP2101、16 MB Flash、8 MB OPI PSRAM。固件直接通过 Wi-Fi 读取 Magpie；日常无需运行网页预览或 USB 桥接。

## 首次构建与安装

安装 PlatformIO Core，然后在仓库根目录运行：

```sh
pio run -d firmware
pio run -d firmware -t upload
```

烧录前备份原厂整片 Flash。备份保存在本地被忽略的 `device-backups/`，不要提交含 Wi-Fi 或其他凭据的镜像。若串口被旧桥接或监视器占用，先停止相应进程。应用固件输出为 `firmware/.pio/build/liquid/firmware.bin`。

后续更新可在设备配置页上传该 `firmware.bin`；成功后自动重启。不要上传 bootloader、分区表或整片备份。分区变动、首次安装或恢复设备时使用 USB。

## 配网与配置

1. 首次无 Wi-Fi 凭据时自动开启临时热点；随时长按右侧 GPIO18 键 3 秒也可开启。屏幕显示热点名、密码、配置访问码和地址。
2. 连接热点后访问 [192.168.4.1/config](http://192.168.4.1/config)；已联网时访问 `http://设备IP/config` 或 [magpie-screen.local/config](http://magpie-screen.local/config)。热点持续 10 分钟。
3. 输入配置访问码。成功认证后，同一浏览器、同一访问地址会记住访问码；点击“忘记此浏览器的访问码”可清除。已联网时页面显示当前 Wi-Fi，点击“切换 Wi-Fi”可修改网络。
4. 在 Magpie 开启局域网共享，配置 `http://Magpie主机的局域网IP:3425`，并提供允许读取额度、用量和路由的 API Key。当前支持局域网 HTTP，不支持 HTTPS、回环地址或带路径的服务地址。
5. 保存连接后等待采集，点击“刷新数据”取得账号和额度窗口。指定选择不存在时显示未知，不替换成其他账号。

API Key 和 Wi-Fi 密码保存在设备 NVS，不通过配置接口回显；API Key 留空保留原密钥。设备页面还可设置液体黏性、后台路由过滤、真实账号显示、USB/电池亮度与熄屏时间。页面选择由实体按键控制，保存配置不会切换当前页面。

“显示真实账号”默认关闭。开启后第二页使用接口提供的账号名称；没有可靠账号数据时保留编号。会话兼容模式仅为同一会话、请求 ID、供应商的已完成请求最后一次尝试标注账号。账号名称只缓存在内存中，切换设置会清空路由缓存并重新采集。

## 三页与按键

面向屏幕、USB 在下方时，顶部按键从左到右：

| 按键 | 短按 | 长按 |
| --- | --- | --- |
| BOOT / GPIO0 | 液体余量 | 上电按住进入下载模式 |
| PWR / AXP2101 | 实时路由 | 保留 PMU 关机行为 |
| GPIO18 | 今日概览 | 3 秒开启配置提示和临时热点 |

熄屏后的首次按键只唤醒。触摸和明显移动也会唤醒。内容在旋转停稳后按 90° 翻正，使用 420 ms 过渡；液面独立跟随重力。

- **液体余量**：供应商图标、剩余比例、额度窗口。灰度抗锯齿文字与同源图片素材；窗口说明保持单行，分隔点两侧等距。
- **实时路由**：来源、喜鹊、最多四个实际尝试目的地。只在新请求、尝试、响应和完成时播放一次动画；首次连接和重连不重播历史。所有请求完成且连续 10 秒无新路由活动后清空目的地，返回等待状态；未完成请求继续显示，重复快照不会重置计时或恢复已清空记录。
- **今日概览**：含缓存 Token 总量、输入/输出/缓存、请求数和成功率、USD 费用估算、缓存命中率、输出速度及供应商分布。超过三家时保留前两家，其余合并。

清空路由图仅影响显示，不删除 Magpie 的会话或请求历史。断网时未超时的数据保留；请求尚未完成时不会仅因轮询失败被判定完成。

## 采集与兼容范围

| 状态 | 额度 | 今日用量 | 路由 |
| --- | --- | --- | --- |
| USB | 10 秒 | 15 秒 | 1 秒 |
| 电池亮屏 | 30 秒 | 30 秒 | 30 秒 |
| 电池熄屏 | 60 秒 | 60 秒 | 60 秒 |

网络请求在独立任务串行执行，上游响应慢时实际间隔会延长。优先读取 trace；若接口返回 403，回退到会话模式。会话模式约每 10 秒从当日最近 100 条请求发现最多四个会话，读取每个会话最新路由。新会话可能在首次请求完成后才被发现；短请求可能跨过中间状态，因此动画频率不等于实际调用频率。

## 电源与绘制

USB 默认亮度 70%，无活动 5 分钟熄屏、提前 30 秒调暗；电池默认亮度 40%，无活动 30 秒调暗、2 分钟熄屏。亮度范围 10–255，熄屏时间范围 30–3600 秒。亮度预览持续 5 秒，保存后长期生效。

USB 可开启常亮或新业务活动唤醒；电池模式的数据更新不唤醒屏幕。重复数据、心跳和液体余波不算用户活动。OLED 长期高亮显示固定内容有残影风险，配置页有常亮提示。

无电池时不显示虚假电量。电池低于等于 10% 时限制亮度；电池供电且电压持续 30 秒低于 3300 mV 时关机。固件保留 PMU 现有充电电流与电压配置。

静止时停止重绘，FPS 为 0 属于正常行为。UI 覆盖层使用约 675 KiB PSRAM，传输使用双 480×24 DMA 缓冲；液面沿用整数查表路径。实机运动帧率应按当前固件和硬件测量。

## 开发与验证

```sh
npm test
npm run build
pio run -d firmware
python3 firmware/test/quota_string_test.py
python3 firmware/test/route_session_test.py
c++ -std=c++17 firmware/test/liquid_test.cpp -o /tmp/magpie-liquid-test
/tmp/magpie-liquid-test
c++ -std=c++17 firmware/test/power_policy_test.cpp -o /tmp/magpie-power-test
/tmp/magpie-power-test
c++ -std=c++17 firmware/test/orientation_test.cpp -o /tmp/magpie-orientation-test
/tmp/magpie-orientation-test
c++ -std=c++17 firmware/test/route_flights_test.cpp -o /tmp/magpie-route-flights-test
/tmp/magpie-route-flights-test
```

Python 回归测试使用 C++17 编译器及 PlatformIO 已下载的 ArduinoJson，支持 `CXX` 和 `SDKROOT` 环境变量。覆盖额度字符串生命周期、64 位请求 ID、路由事件、真实账号匹配、闲置清空、旋转和电源策略。

`standalone_device_test.py` 是会修改设备配置的专用板上集成测试，只适用于尚未配置真实 API Key 的测试板。必须设置 `DEVICE_HOST`；认证使用 `DEVICE_KEY` 或本地 `.local/device-config.json` 中的配对密钥。测试结束会清除测试密钥，不用于日常已配置设备。

界面资产已预生成，普通构建无需图像工具。重新生成需 Pillow、resvg-py 和 macOS STHeiti Medium 字体：`python3 scripts/generate_ui_assets.py`。

旧桥接的 `state`/`config` 命令不适用于独立固件。维护接口 `/api/config`、`/api/status`、`/api/wifi`、`/api/preview`、`/api/wake`、`/api/refresh` 和 `/update` 均要求 `X-Magpie-Key`，可使用设备访问码或已有配对密钥。

## 驱动与许可

硬件初始化依据 [Waveshare 官方示例](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-2.16/tree/225a62bff11b5d0a0b607873860d39485a9a9685/examples/arduino)。依赖版本见 `platformio.ini`，第三方说明见 [THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md)。项目原创部分个人非商业使用免费，商业使用需书面授权，详见 [LICENSE](../LICENSE)。
