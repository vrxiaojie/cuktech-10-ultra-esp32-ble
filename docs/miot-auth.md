# MiOT BLE 登录认证

## 实现边界

认证流程依据上游 `cuktech-ble-ha@89e5f78387812323528f5967421f65a689e803ef` 的 `controller.py` 当前调用链实现，并保留 MIT 许可证于 `components/cuktech_ble/UPSTREAM_LICENSE`。

ESP32-C3 与充电器之间不执行 BLE Pairing/Bonding。Token 是 12 字节应用层共享秘密，只作为 HKDF-SHA256 的 IKM；BLE Key、MAC 辅助反序值和 `PRODUCT_ID` 均不进入认证。

## 串行状态机

认证在唯一 BLE 应用 task 中执行，NimBLE 回调只复制 Notify。每个等待状态都有独立超时和稳定错误名：

1. 清空认证数据残留，向认证控制写 `a4`。
2. 等初始化响应，将 `byte[2]` 加一后回写。
3. 等 key-exchange 数据；短的重复初始化帧最多额外等待三次。
4. 回写相同长度的 `00 00 05 01 || f2...`，等待 600 ms 并清空残留。
5. 向认证控制写登录命令 `24 00 00 00`。
6. 使用 `esp_fill_random()` 生成 16 字节 `app_random`。
7. 写随机数发送头，有限次等待 `00 00 01 01`，再写 `01 00 || app_random` 并等待 `00 00 01 00`。
8. 分别接收 16 字节 `dev_random` 和 32 字节 `dev_hmac`。
9. 支持 inline `00 00 02 <id> <payload>`，并回 `00 00 03 00`。
10. 支持 multiframe `00 00 00 <id> <count_le16>`；帧数限制为 1～64，回 RCV_RDY 后按连续帧号接收，最后回 RCV_OK。
11. 调用协议组件派生 `dev_key/app_key/dev_iv/app_iv`，常量时间校验设备 HMAC。
12. 写 HMAC 发送头、`01 00 || app_hmac`，消费 RCV_OK。
13. 认证控制首字节 `0x21` 或 `0x11` 成功；`0x23` 或 `0x12` 明确拒绝。
14. 成功会话的发送计数器为 0，MiOT sequence 为 1。

所有认证特征写入使用 Write Without Response，与上游 `response=False` 一致；CCCD 配置仍使用带响应的 descriptor write。

## 失败与清理

- 任何失败都清零随机数、HMAC 临时缓冲和会话 Key/IV。
- 断开前尽力关闭所有已启用 CCCD（当前为四个认证/命令通道和一个设备信息通道），再终止连接并清空 Notify 队列。
- 认证失败重试间隔不少于 3 秒，随后沿用最大 300 秒的指数退避。
- 连续 5 次协议认证失败进入 `auth_failed_locked`，避免持续轰击充电器；当前可通过重启解除，后续 Web 手动重试接口将复用该状态。
- 纯链路断开或传输内部错误不累计为 Token 认证失败。

`GET /api/status` 的 `authenticated` 已改为真实运行状态。`last_error` 只返回例如 `auth:device_hmac_mismatch` 的阶段名，不返回通知内容或 Secret。

## 测试与证据等级

主机状态机测试使用 `AGENTS.md` 的合成 Token、随机数、派生 Key 和 HMAC：

- 完整成功路径验证 `dev_key/app_key`、计数器 0 和 sequence 1。
- 同一路径同时覆盖短重复初始化恢复、设备随机数 multiframe 和设备 HMAC inline。
- 错误 HMAC 必须被拒绝并清零会话。
- 登录结果 `0x23` 必须返回明确拒绝并清零会话。
- 非连续 multiframe 帧号必须被拒绝。

以上是“源码确认”和“测试向量确认”。真实充电器的初始化帧长度、通知时序、帧号、HMAC 和结果码仍待真机确认。硬件出现后必须烧录，并使用：

```bash
idf.py -p "$PORT" monitor
```

记录从 GATT ready 到 `authenticated` 或稳定错误阶段的日志；不得增加随机数、Token、Key、IV、HMAC 或完整认证帧日志。
