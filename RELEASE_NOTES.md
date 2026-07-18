# 发布说明

## 第一阶段候选版本

本版本面向无 PSRAM 的 ESP32-C3 和 ESP-IDF 5.5.2 或更高版本，实现单台酷态科 10 号超级电能充 Ultra 的本地 BLE→MQTT/Home Assistant 网关。

### 已实现

- WPA2 SoftAP 首次配网、APSTA 凭据验证、STA 约 60 秒失败回退。
- NVS v1 配置、v0 迁移、CRC、完整校验和 Secret 脱敏。
- STA HTTP 管理页、status/config/enable/retry API。
- NimBLE Central 扫描、public/random 有界直连、UUID/CCCD 发现。
- MiOT Token 登录、HKDF/HMAC/AES-CCM、inline/multiframe、超时和清理。
- 设置读取、端口遥测、协议启发式、线程安全快照。
- MQTT 3.1.1 retained 状态、LWT、自动重连和完整快照。
- Home Assistant SET、端口控制、BLE 运行时启停和认证锁定恢复。

### 自动验证

- 10/10 主机测试通过。
- 10/10 ASan/UBSan 测试通过；当前执行环境不支持 LeakSanitizer。
- ESP-IDF v5.5.2-dirty、`esp32c3` 构建通过。
- 镜像 `0xfa610`，1 MiB app 分区剩余 `0x59f0`。

### 已知限制

- 当前环境无可见串口，尚未烧录或执行 `idf.py monitor`；所有 BLE/充电器运行结论待真机验收。
- HTTP、MQTT 和默认 NVS 不提供 TLS/传输或物理 Flash 加密保护，只适用于可信局域网。
- 单充电器；无 OTA、多设备、MQTT TLS、HTTPS、云端账号登录和设备绑定。
- 协议名称来自启发式规则，不能视为硬件协议号的绝对结果。
- 默认 1 MiB app 分区仅剩约 2%，新增大型功能前必须重新评估容量；不得未经批准修改分区表。

### 升级提示

同一分区布局可直接重新烧录并保留 NVS。任何未来 schema 或分区变更必须提供迁移、回滚和数据影响说明。完整操作和真机步骤见 [部署与烧录](docs/deployment.md)。
