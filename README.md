# AI Passport 固件（小智 + Codex 提醒 + 在线录音）

基于 [78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) 2.5.1 的分支，面向 [FoloToy AI Passport](https://github.com/FoloToy/ai-passport)（ESP32-C3，8 MB Flash，无 PSRAM）。在官方小智固件之上增加：

- **Muse 风格像素 UI**：原创像素女机器人“Mimi”，按对话状态切换动画。
- **Codex 完成提醒**：电脑上的 Codex 任务结束后，设备经自己的 MQTT/WSS 通道收到信号，自动发起一次对话播报结果。
- **在线录音**：长按 OK 键开始/结束，音频实时传到你的服务器，云端保存为 Ogg/Opus 并转写；全屏录音界面显示时长、音量和云端确认状态。
- **闲置熄屏**：60 秒变暗，6 分钟关屏降频（Wi-Fi 保持，Codex 提醒会唤醒），2 小时深度睡眠。
- **电量显示修复**：开机检查并写入 CW2017 电池参数。

配套服务端与 Mac 端：[ironmonkey98/xiaozhi-cloud-codex-bridge](https://github.com/ironmonkey98/xiaozhi-cloud-codex-bridge)。完整的复现顺序是：先按那边 README 搭好 Mac Bridge 和服务器，再回到这里编译烧录。

上游原始说明：[README_zh.md](README_zh.md)（[English](https://github.com/78/xiaozhi-esp32#readme)）。

## 分支

| 分支 | 内容 |
|---|---|
| `main`（默认） | 全部功能，推荐使用 |
| `muse-style-ui` | 只有 Muse UI（AI Passport 与 ESP-Hi 竖屏版）和自定义 MAC，无提醒和录音 |

## 不接服务器也能用

不创建私有头文件时，固件照常编译：保留 Muse UI、小智对话、熄屏和电量修复，只是没有 Codex 提醒和录音。适合只想换界面的人。

## 编译

### 1. 安装 ESP-IDF

实测 ESP-IDF **v6.0.1**（macOS）。按 [官方文档](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32c3/get-started/) 安装后：

```bash
. ~/esp/esp-idf/export.sh          # 每个新终端执行一次
idf.py --version
```

### 2. 获取代码

```bash
git clone https://github.com/ironmonkey98/ai-passport-firmware.git
cd ai-passport-firmware             # 默认分支 main
```

### 3. 设备凭据（需要提醒和录音时）

```bash
cp main/boards/folotoy/ai-passport/codex_notify_private.example.h \
   main/boards/folotoy/ai-passport/codex_notify_private.h
```

按 bridge 仓库 `config/notify.local.json` 填写：

| 宏 | 填什么 |
|---|---|
| `CODEX_NOTIFY_URL` | `wss://<你的域名>/codex-notify/mqtt` |
| `CODEX_NOTIFY_USERNAME` | 设备 ID，如 `passport-01`（与服务器 ACL、`recording.json` 的 `devices` 一致） |
| `CODEX_NOTIFY_PASSWORD` | 设备账号密码 |
| `CODEX_NOTIFY_CLIENT_ID` | 任意唯一值，如 `xiaozhi-passport-01` |
| `CODEX_NOTIFY_TOPIC_PREFIX` / `SUB_TOPIC` / `ACK_TOPIC` | 把示例里的 `passport-01` 换成你的设备 ID |

这个文件已被 `.gitignore` 忽略，**不要提交**。编译出的固件里含有密码，也不要公开发布。服务器证书必须是公网受信任的（固件用内置 CA 包校验）。

### 4. 编译

```bash
rm -f sdkconfig                     # 换板子或改 config.json 后需要
python scripts/build.py folotoy/ai-passport --name ai-passport --language zh-CN
```

产物在 `build/`：`xiaozhi.bin`（应用）和 `merged-binary.bin`（整片镜像）。

## 烧录

USB-C 连接设备，找到串口（macOS 上是 `/dev/cu.usbmodem*`）。如果找不到：换一根能传数据的线，或按住 BOOT 键再插线。

**先备份原厂固件**（8 MB，只需一次）：

```bash
esptool --chip esp32c3 -p /dev/cu.usbmodem101 read-flash 0 0x800000 backup-$(date +%Y%m%d).bin
```

**新设备或原厂固件**：写整片镜像。会清除 Wi-Fi 配置和小智激活状态，需要重新配网、绑定。

```bash
esptool --chip esp32c3 -p /dev/cu.usbmodem101 write-flash 0x0 build/merged-binary.bin
```

**已经刷过本固件，只更新应用**：保留配网和绑定。

```bash
esptool --chip esp32c3 -p /dev/cu.usbmodem101 write-flash 0x20000 build/xiaozhi.bin
```

`esptool` 随 ESP-IDF v6 安装（v5 版命令格式）。也可以直接 `idf.py -p /dev/cu.usbmodem101 flash`（写全部分区）。

查看日志：`idf.py -p /dev/cu.usbmodem101 monitor`。注意：打开串口会让 C3 重启一次。

## 首次使用

1. **配网**：首次开机设备进入配网模式，手机连接设备热点 `Xiaozhi-XXXX`，在弹出的页面（或浏览器打开 `http://192.168.4.1`）选择 2.4 GHz Wi-Fi 并输入密码。
2. **绑定**：设备进入“激活设备”并播报 6 位验证码，在 <https://xiaozhi.me/console/agents> 添加设备并输入验证码，绑定到装了 Codex MCP 的那个智能体。
3. **检查提醒通道**：开机约 15 秒后串口日志出现 `CodexNotify: notification channel connected`。

## 操作

| 操作 | 作用 |
|---|---|
| 单击 OK | 开始 / 结束对话 |
| 长按 OK | 开始录音；再长按结束 |
| 任意键 | 从变暗或熄屏中唤醒 |

录音界面说明：REC 点闪烁、计时、音量条、“云端已保存时长”，以及状态灯：绿色表示已确认，黄色表示积压，红色表示断网。结束后显示“已保存”、“部分保存”或“录音失败”；只有云端确认完整收到才显示“已保存”。录音时不能对话；Codex 提醒会排队到录音结束后。

## 省电策略

| 闲置时间 | 动作 |
|---|---|
| 60 秒 | 背光降到 10% |
| 6 分钟 | 关屏、关音频、CPU 降到 40 MHz；Wi-Fi 与提醒通道保持在线，收到 Codex 提醒自动亮屏 |
| 2 小时 | 深度睡眠，按键唤醒（重新开机） |

对话中和播报中不会进入睡眠。录音时只会变暗（60 秒无按键），不会关屏或中断录音。分级策略来自上游 `af55a78`（#2283），深度睡眠时间由 36 分钟改为 2 小时，并加了提醒唤醒和录音变暗。

## 其他可选项

- **ESP-Hi 竖屏 Muse UI**：编译 `espressif/esp-hi`，`CONFIG_ESP_HI_MUSE_STYLE_UI` 默认开启，适合拆掉机器狗身体单独使用。
- **自定义 MAC**：`idf.py menuconfig` → `CONFIG_CUSTOM_BASE_MAC`，填写本地管理地址（第一个字节为 `02`）。默认留空，使用芯片出厂 MAC。
- **关闭 Muse UI**：`CONFIG_AI_PASSPORT_MUSE_STYLE_UI=n`，恢复上游界面。

## 改动位置

| 路径 | 内容 |
|---|---|
| `main/display/muse_style/` | Muse 风格 UI 与像素机器人 |
| `main/boards/folotoy/ai-passport/` | 板级：按键、省电、电量、提醒（`codex_notify.h`）、录音（`passport_recorder.h`、`recorder_core.h`、`recording_overlay.h`） |
| `main/application.*`、`main/audio/`、`main/device_state*` | 录音状态与音频旁路 |

板级详细说明（英文）：[main/boards/folotoy/ai-passport/README.md](main/boards/folotoy/ai-passport/README.md)。录音协议：bridge 仓库 `docs/RECORDING_PROTOCOL.md`。

## 已知问题

- 串口打开即重启（ESP32-C3 USB Serial/JTAG 的行为），抓开机前的复位原因需要用不触发复位的方式读串口。
- 偶发“开机后自动重启一次”，原因还没定位。

## 许可证

MIT，与上游一致，见 [LICENSE](LICENSE)。电池参数取自 FoloToy `bsp_battery.c`（MIT）。
