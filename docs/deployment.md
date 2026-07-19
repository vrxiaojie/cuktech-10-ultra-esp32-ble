# 部署与烧录

## 前置条件

- ESP32-C3 开发板，无 PSRAM 要求。
- ESP-IDF 5.5.2 或更高版本；本项目验证路径为 WSL。
- 可传输数据的 USB 线和实际可见的 `/dev/ttyACM*` 或 `/dev/ttyUSB*`。
- 2.4 GHz WPA2-Personal Wi-Fi；第一阶段不启用 WPA3/SAE 或企业 Wi-Fi配置。
- 局域网 MQTT Broker，默认 TCP 1883、MQTT 3.1.1。
- 充电器正常显示顺序的 BLE MAC 和 24 位十六进制 Token。BLE Key 可留空。

不要把真实 Token、BLE Key 或密码写入源码、shell 历史、截图、issue 或串口日志。

## WSL 连接 USB 串口

在 Windows 管理员 PowerShell 中查看并转发 USB 设备：

```powershell
usbipd list
usbipd bind --busid <BUSID>
usbipd attach --wsl --busid <BUSID>
```

`bind` 通常只需执行一次；重新插拔或重启后可能需要再次 `attach`。回到 WSL 检查：

```bash
ls -l /dev/ttyACM* /dev/ttyUSB* 2>/dev/null
```

若没有任何输出，不要猜测串口名，也不要执行 flash。参见 [故障排查](troubleshooting.md)。

## 构建

```bash
cd /home/vrxiaojie/cuktech-10-ultra-esp32-ble
. /home/vrxiaojie/esp/v5.5.2/esp-idf/export.sh
idf.py --version
idf.py set-target esp32c3
idf.py reconfigure
idf.py build
idf.py size
```

`idf.py --version` 必须为 5.5.2 或更高。当前默认 1 MiB app 分区已接近容量上限；构建必须显示镜像仍小于 `0x100000`。本项目未引入自定义分区表。

## 烧录并单独查看日志

从 GitHub Release 下载 `.bin` 文件时，可以使用 Windows Flash Download Tool 或 esptool。合并固件从 `0x0` 烧录，但会覆盖 NVS 配置；需要保留配置时，应按 `0x0`、`0x8000`、`0x10000` 分别烧录 Bootloader、分区表和应用程序。完整界面配置和命令见 [Release 固件烧录说明](flash-download-tool.md)。

从本仓库源码构建时，把检测到的实际串口赋给 `PORT`：

```bash
PORT=/dev/ttyACM0
idf.py -p "$PORT" flash
idf.py -p "$PORT" monitor
```

第二条命令必须在 flash 成功后单独执行。退出 monitor 使用 `Ctrl+]`。首次启动至少应看到网关启动、NVS、Wi-Fi 状态和 HTTP server 就绪日志；日志不得出现 Token、Key、IV、HMAC 或密码。

只有在明确需要清除整片 Flash 且已理解会丢失所有 NVS 配置时，才手动执行：

```bash
idf.py -p "$PORT" erase-flash
```

普通升级、Wi-Fi 故障或 Token 错误都不需要擦除 Flash。

## 首次配网与管理配置

1. 手机或电脑连接 `CUKTECH-BLE-<后四位>`，默认 WPA2 密码 `cuktech10`。
2. 打开 `http://192.168.4.1/`，只提交 Wi-Fi SSID/password。
3. 页面确认 STA 获得 IP 后等待设备受控重启。
4. 浏览器访问 `http://<ESP32-IP>/`。
5. 填入充电器 MAC、Token、可选 BLE Key 和 MQTT 参数；默认 topic 保持 `cuktech/charger`。
6. 保存后等待重启，访问 `http://<ESP32-IP>/api/status` 检查 `wifi_state`、`ble_state` 和 `mqtt_state`。

Token 错误连续五次会进入 `auth_failed_locked`。修正 Token 需要保存配置并重启；只想对同一凭据重新尝试时，可在管理页点击“重新尝试 BLE 认证”。

## MQTT 快速验收

若系统已安装 Mosquitto 客户端，可在另一终端观察 retained 状态：

```bash
mosquitto_sub -h <BROKER> -t 'cuktech/charger/#' -v
```

认证成功后应出现 status、settings 和四个端口主题。控制测试示例：

```bash
mosquitto_pub -h <BROKER> -t cuktech/charger/port \
  -m '{"port":"c1","action":"off"}'
mosquitto_pub -h <BROKER> -t cuktech/charger/set \
  -m '{"piid":5,"value":3}'
```

端口断电会影响真实负载，执行前确认设备安全。协议开关 PIID21 必须发布完整 32 位值，不要用猜测位图测试。

## 升级

同一分区布局下可重新执行 build、flash、monitor。分立写入或普通 `idf.py flash` 不会主动覆盖 NVS；从 `0x0` 写入 Release 的合并固件会覆盖 NVS 所在范围并清除配置。配置 schema 当前为 v1，并支持从 v0 显式迁移；未知 schema 或 CRC 损坏会回退内存默认值而不自动擦除 NVS。任何未来分区表变化都必须先评估回滚和配置数据影响。
