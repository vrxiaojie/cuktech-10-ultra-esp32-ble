# 韧性与安全

## 故障域隔离

- Wi-Fi STA 连接失败约 60 秒后开启回退 SoftAP，但保留原 Wi-Fi、充电器和 MQTT 配置，不形成重启循环。
- MQTT 使用 ESP-MQTT 自动重连；Broker 故障不停止 BLE 采集，也不触发 Wi-Fi 配网。
- BLE 普通错误使用 1～300 秒有界指数退避；认证错误至少等待 3 秒，连续 5 次后锁定。
- 连续 10 次 AES-CCM 解密失败视为会话过期，清理 CCCD、连接、会话密钥和队列后重连。
- 运行时禁用会唤醒各阶段等待，阻止新控制命令，尽力干净断开并清零端口状态；重新启用从新的 BLE 会话开始。

`POST /api/retry-ble` 只在 `auth_failed_locked` 时投递专用请求。BLE task 收到后清零认证失败计数并立即回到扫描，不会把 BLE Key 混入认证，也不会改写 Token。

## 长时间运行不变量

- 所有 GATT、加密、MiOT 序列和命令计数由单一 BLE task 拥有。
- 控制队列固定为 8 项，Notify 队列固定为 12 项，回调不做动态大对象处理。
- request ID 永不返回 0，到 `UINT32_MAX` 后回到 1。
- 发送计数器在低 16 位耗尽前要求新会话，不允许产生歧义 nonce。
- 指数退避饱和后保持 300 秒，不发生整数翻倍溢出。
- 状态 JSON 先复制线程安全快照；网络发送不长时间持有状态锁。

主机测试用 100000 次连续退避计算覆盖饱和稳定性，并覆盖 request ID 回绕、认证 5 次阈值和解密 10 次阈值。它们只能证明纯逻辑不变量，不能替代数日真机运行和堆内存趋势观察。

## Secret 审计

- Token、BLE Key、Wi-Fi password、MQTT password 不进入日志、`GET /api/config`、`GET /api/status` 或 MQTT 状态。
- 启动日志只显示 `token=yes/no`、`ble_key=yes/no`、`mqtt_password=yes/no`。
- 认证随机数、HMAC 和临时包在认证函数所有退出路径显式清零；协议会话清理使用密码学后端的 zeroize。
- Web 配置解析后清零请求体、cJSON Secret 字符串和 NVS 临时 Blob。
- 测试中的 Token/Key 均为 `AGENTS.md` 允许提交的合成黄金向量，不是真实设备凭据。

## 明确不提供的保护

第一阶段 HTTP、MQTT 和 NVS 默认配置不等同于端到端加密保护：

- 管理 HTTP 无 TLS 和登录认证，只能部署在可信局域网。
- MQTT 为明文 TCP，用户名/密码会受局域网窃听风险影响。
- 未启用 Flash encryption 或 NVS encryption 时，物理读取 Flash 可能恢复配置 Blob。
- 默认 SoftAP 密码是公开文档值，且配网 AP 只允许输入 Wi-Fi 凭据，不能输入 Token、BLE Key 或 MQTT password。

HTTPS、MQTT TLS、Flash/NVS encryption、Secure Boot 与 OTA 必须在后续独立功能中评估容量、部署和密钥生命周期，不能把当前固件描述为已经提供这些能力。

## 待真机压力测试

发现实际串口并烧录后至少验证：反复 Wi-Fi 断连与恢复、Broker 重启和 LWT、充电器断电、错误 Token 连续五次锁定、Web 手动重试、连续端口控制、运行时启停、24～72 小时堆内存趋势及串口是否出现 watchdog/reset。所有结果必须标为真机确认并记录固件提交。
