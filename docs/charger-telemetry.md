# 充电器设置与端口遥测

## 实现边界

本阶段实现认证后的 MiOT 命令通道、初始/周期设置读取、端口 Notify 解析和线程安全状态快照，不包含 MQTT 发布或 HA 控制命令。

协议调用链依据上游 `cuktech-ble-ha@89e5f78387812323528f5967421f65a689e803ef` 的 `controller.py`、`ble_manager.py`、`state.py` 和 `state_protocol_v2.py`。相关移植代码沿用 `components/cuktech_ble/UPSTREAM_LICENSE` 与 `components/cuktech_protocol/UPSTREAM_LICENSE` 中的 MIT 许可证归属。

## 命令通道

认证成功后，唯一 BLE 应用 task 继续独占会话密钥、MiOT sequence、发送计数器和所有 GATT 写入：

1. 先处理认证期间积压的命令接收 Notify，并发送必要 ACK。
2. 使用 `cuktech_miot_build_get()` 构造 SIID 2 的 GET 命令。
3. 用 `app_key/app_iv` 和当前发送计数器执行 AES-CCM。
4. 向命令发送特征写 `00 00 00 00 01 00`，等待 RCV_RDY。
5. 写 `01 00 || encrypted_packet`，等待 RCV_OK。
6. 命令接收 inline 帧立即回 `00 00 03 00` 后解密。
7. multiframe header 回 RCV_RDY，按连续的 1 起始帧号拼接每帧去掉前两字节后的数据，最后回 RCV_OK 并解密。

单条发送帧限制在 247 ATT MTU 可承载范围内。发送计数器达到 `0xffff` 时不再发送，要求断开并建立新会话。连续 10 次 AES-CCM 解密失败将状态标记为 `session_stale_decrypt` 并触发干净重连。

## 设置和 PDO

认证后的首次读取以及约每 60 秒的刷新覆盖：

```text
5, 6, 8, 9, 10, 11, 12, 13, 15, 16, 17, 18, 19, 20, 21
```

GET result 只在 opcode、SIID 和完整 16 位 PIID 均匹配时写入状态。PIID 17/18 按上游映射拆成两个 16 位 PDO 描述：

- PIID 17：高半字对应 C1，低半字对应 C2。
- PIID 18：高半字对应 C3，低半字对应 A。
- 半字低 8 位为 capability；高 8 位 `0x07` 为 PD Fixed，`0x08` 为 PD PPS，其他值保留为 unknown。

PIID 21 保留 32 位原值，并派生 C1/C2 的 PD、PPS、UFCS 开关以及 C3/A 的 SCP、UFCS 开关。PIID 16 用于 `/api/status` 中各端口的 `enabled` 字段；尚未读到 PIID 16 时默认四端口启用，与上游状态模型一致。

## 端口状态

opcode `0x04`、SIID 2、PIID 1～4 的解密负载交给协议组件解析。线程安全快照只向外提供稳定字段：

```json
{
  "voltage": 20.1,
  "current": 2.5,
  "power": 50.2,
  "active": true,
  "protocol": "PD",
  "enabled": true
}
```

协议名称仍是上游启发式估算结果，不是硬件直接报告的绝对协议号。意外 BLE 断线只把 `connected/authenticated` 置为 false，保留最后端口读数和设置，以便后续 MQTT retained 行为与上游一致。

共享状态由 FreeRTOS mutex 保护。HTTP 或后续 MQTT 代码先复制完整快照，再在锁外生成 JSON 或执行网络发送。每次状态变化增加 `state_revision`，后续发布模块可据此判断快照是否更新。

## 设备信息

GATT 链路就绪后，固件通过动态发现的设备信息 UUID 写入 `0x03` 查询芯片名，并读取固件版本 UUID。成功时设备型号格式与上游保持为 `njcuk.fitting.ad1204_<chip>`；失败只保留空字符串，不影响 Token 登录或遥测。

## 验证与证据等级

主机测试已确认：

- 黄金向量的加密发送帧与 inline 解密。
- 同一黄金向量拆成两个连续 multiframe 后可以重组并解密。
- inline、multiframe 的 ACK 顺序。
- `0xffff` 计数器拒绝发送。
- UINT8/UINT32 GET result 的值位置与完整 PIID 匹配。
- PIID 17/18 PDO 拆分和 PIID 21 开关位。
- 端口状态快照只接受 PIID 1～4。

以上属于“源码确认”和“测试向量确认”。真实充电器的认证后自动推送数量、命令通道时序、设备信息响应、设置值和长期 60 秒刷新仍待真机确认。有串口设备后必须烧录并运行：

```bash
idf.py -p "$PORT" monitor
```

日志只记录阶段、PIID、稳定遥测字段和错误名，不输出 Token、Key/IV、HMAC、完整认证帧或加密包。
