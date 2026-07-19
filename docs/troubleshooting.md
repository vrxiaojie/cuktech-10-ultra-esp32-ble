# 故障排查

## WSL 看不到串口

症状：`ls /dev/ttyACM* /dev/ttyUSB*` 没有输出。

1. 更换确认支持数据传输的 USB 线。
2. Windows PowerShell 运行 `usbipd list`，确认设备存在。
3. 管理员 PowerShell 执行 `usbipd bind --busid <BUSID>`，再执行 `usbipd attach --wsl --busid <BUSID>`。
4. 关闭占用串口的 Windows 串口工具。
5. WSL 中重新检查设备节点，再把实际路径传给 `idf.py -p`。

不要在没有设备节点时猜测 `/dev/ttyACM0` 并宣称烧录完成。

## 烧录无法连接

- 按开发板要求进入下载模式，再重试 `idf.py -p "$PORT" flash`。
- 确认目标为 `esp32c3`，并检查 USB 权限和串口是否被 monitor 占用。
- 降低波特率可帮助诊断不稳定 USB 链路：`idf.py -p "$PORT" -b 115200 flash`。
- 不要用 `erase-flash` 作为普通连接错误的第一反应；它会清除 NVS 配置。
- 使用 Flash Download Tool 时，`ChipType` 必须选择 `ESP32-C3`，`LoadMode` 选择 `UART`，并关闭占用同一 COM 端口的串口软件。

## Release 固件地址配置错误

- 合并固件 `cuktech_ble_gateway-<版本>-merged.bin` 只配置一行，地址为 `0x0`。
- 分立固件地址为 Bootloader `0x0`、分区表 `0x8000`、应用程序 `0x10000`。
- 不要同时勾选合并固件和分立固件，也不要把三个分立固件都写入 `0x0`。
- 使用合并固件或点击 Flash Download Tool 的 `ERASE` 会清除 NVS；需要保留配置时按分立地址烧录且不要整片擦除。
- 详细操作见 [Release 固件烧录说明](flash-download-tool.md)。

## 找不到配网 AP

- 无有效 Wi-Fi 配置时才立即进入 `provisioning_ap`；已有配置时约 60 秒失败后进入 `fallback_ap`。
- 默认 SSID 为 `CUKTECH-BLE-<后四位>`，默认密码 `cuktech10`。
- MQTT、Token 或充电器故障不会触发配网 AP。
- 查看 monitor 中 `wifi_state` 和 SoftAP 日志；不要记录 Wi-Fi password。

## Wi-Fi 验证失败

- 只支持 2.4 GHz WPA2-Personal 第一阶段路径；检查 SSID、密码和 AP 隔离。
- 验证失败不会覆盖上一份有效配置，配网 AP 会保留。
- STA 恢复后回退 AP 自动关闭。

## BLE 一直扫描或连接失败

- 确认 MAC 是正常显示顺序，格式为 `AA:BB:CC:DD:EE:FF`。
- 充电器空闲时可能不广播；固件会在有界扫描后尝试 public/random 直连。
- 让充电器有负载、短暂重新上电，再观察 `ble_state` 和稳定错误名。
- ESP32 必须靠近充电器；不要把地址类型推测当作真机结论。

## 认证失败或锁定

- Token 必须是 24 个十六进制字符；BLE Key 可以留空，当前认证未使用它。
- 连续五次计数型失败进入 `auth_failed_locked`，避免快速轰击充电器。
- 确认 Token 后，可点击管理页重试按钮或 POST `/api/retry-ble`；必要时给充电器断电重启。
- 不要在 issue 或日志中粘贴 Token、随机数、HMAC、Key/IV 或认证帧。

## MQTT 不连接

- 检查 `/api/status` 的 `mqtt_state` 和 `last_error`，以及 Broker host/port/账号。
- 第一阶段只支持明文 TCP MQTT 3.1.1，不支持 TLS、WebSocket 或 MQTT 5。
- Broker 不可达不会中断 BLE，也不会打开 Wi-Fi 配网 AP。
- 确认 Broker ACL 允许发布/订阅 `cuktech/charger/#`。

## Home Assistant 没有实体或控制无效

- 上游集成 v1.0.5 硬编码 `cuktech/charger`，ESP32 topic prefix 必须一致。
- Server URL 应为 `http://<ESP32-IP>`，不是 `localhost:8199`。
- 从 HA 主机访问 `/api/status`，并在 MQTT 工具中监听 `cuktech/charger/#`。
- 控制失败查看 MQTT/BLE command 计数；未认证时命令会被拒绝。

## 镜像容量警告

默认 1 MiB app 分区只剩约 2%。当前构建仍通过分区尺寸检查，警告不等于溢出。不要擅自修改分区表；新增 TLS、OTA 或大型 Web 资源前必须单独评估并获得同意。
