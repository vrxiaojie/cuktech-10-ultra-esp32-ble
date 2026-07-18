# BLE GATT 链路

## 角色与启动条件

ESP32-C3 固定作为 NimBLE Central / Observer / GATT Client，充电器作为 Peripheral / GATT Server。运行配置同时满足以下条件时才启动 BLE Host：

- `ble_enabled=true`
- 充电器 MAC 校验通过
- Token 已配置

本阶段只建立 GATT 链路，不执行 MiOT 登录。BLE Key 仍为可选预留字段，未参与连接、地址选择或任何密码学计算。

## 连接流程

BLE 应用 task 独占扫描、连接、GATT procedure 和后续协议顺序：

1. 被动扫描 5 秒，按配置 MAC 匹配可连接广播。
2. 找到广播时记录广播提供的 public/random 地址类型并连接。
3. 未找到广播时依次进行一次 public 和一次 random 直连，每次连接最长 10 秒。
4. 连接后交换 ATT MTU，首选值由 `sdkconfig.defaults` 固定为 247。
5. 通过 UUID 发现 `0xFE95` 服务，绝不使用固定 ATT Handle。
6. 在服务范围内发现全部特征，并确认固件版本、认证控制、认证数据、命令发送、命令接收和设备信息六个 UUID 均存在。
7. 根据完整特征列表计算每个特征的准确 descriptor 范围，发现 CCCD。
8. 为认证控制、认证数据、命令发送和命令接收四个必要特征写入 `0x0001`；遥测阶段另订阅设备信息特征，用于查询芯片型号。
9. 链路进入 `ready`；断开后从 1 秒开始指数退避，最大 300 秒。

配置 MAC 始终按正常显示顺序保存和显示，例如 `A1:B2:C3:D4:E5:F6`。NimBLE 的 `ble_addr_t.val` 使用低字节在前的内部布局，因此构造 API 参数时数组为 `F6 E5 D4 C3 B2 A1`；这只是库接口布局转换，不会反转配置 MAC，也不会产生第二种设备地址。

## 回调与队列

GAP/GATT 回调只复制事件并进行无等待入队：

- 控制事件队列固定 32 项，用于连接、断开、MTU、服务/特征/descriptor 发现和 CCCD 写入结果。
- Notify 队列固定 12 项，每项最多复制 244 字节，即 MTU 247 下的最大通知负载。
- 队列满或超长 Notify 会增加脱敏的丢弃计数，不打印负载。
- HKDF、HMAC、AES-CCM、JSON、HTTP 和 MQTT 均不在 NimBLE Host task 中执行。

`GET /api/status` 增加 `ble_state`、`ble_gatt_ready`、`ble_mtu` 和 `ble_notify_dropped`。兼容字段仍保留；阶段 07 起 `authenticated` 反映真实 MiOT 登录结果。

## 固件容量

首次链接真实 NimBLE Central 后，默认调试优化镜像达到 `0x10caa0`，超过 1 MiB factory 分区 `0xcaa0`。在不修改分区表的前提下，关闭未使用的 BLE SMP/绑定、连接自动重试、BLE 5 扩展扫描和本地 GAP 服务，并使用 `-Os` 后，镜像为 `0xec780`，1 MiB 分区剩余 `0x13880`（约 78 KiB）。

这些裁剪不影响 MiOT 应用层认证：项目不使用 BLE Pairing/Bonding，重连由应用状态机负责，扫描使用 legacy API。后续功能加入后仍需持续检查容量；若必须修改分区表，应先说明现有设备升级和数据布局影响并获得批准。

## 证据等级与待验证项

- GATT UUID、ESP32 Central 角色、扫描/直连策略：项目约束与上游源码确认。
- NimBLE API、Kconfig 名称和构建结果：ESP-IDF 5.5.2 源码与本机构建确认。
- MAC 解析、NimBLE 地址数组布局、1→300 秒退避：主机测试确认。
- 充电器实际广播地址类型、MTU 结果、Handle、CCCD 写入和 Notify：尚未真机确认。

硬件串口可见后，需要烧录并使用以下命令保留完整启动到 GATT `ready` 或失败阶段的日志：

```bash
idf.py -p "$PORT" flash
idf.py -p "$PORT" monitor
```

日志只能记录阶段、错误码、地址类型、MTU 和计数，不得记录 Token、BLE Key、派生密钥、HMAC、IV 或认证明文。
