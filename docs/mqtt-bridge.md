# MQTT 状态桥接

## 兼容契约

默认前缀为 `cuktech/charger`。现有上游 Home Assistant 集成硬编码了该前缀；修改前缀后，未经同步修改的集成不会收到消息。

| Topic | QoS | Retain | 负载 |
|---|---:|---:|---|
| `<prefix>/port/c1` | 0 | 是 | `voltage/current/power/active/protocol` |
| `<prefix>/port/c2` | 0 | 是 | 同上 |
| `<prefix>/port/c3` | 0 | 是 | 同上 |
| `<prefix>/port/a` | 0 | 是 | 同上 |
| `<prefix>/settings` | 1 | 是 | 以字符串 PIID 为键的对象 |
| `<prefix>/status` | 1 | 是 | BLE 连接、认证、型号和充电器固件版本 |

断线遗嘱发布到 `<prefix>/status`，QoS 1、retain=true，负载为：

```json
{"connected":false}
```

MQTT Client 第一次连接或自动重连成功后，不等待下一次 BLE Notify，立即重新发布四端口、settings 和 status 的完整 retained 快照。正常运行时根据线程安全状态快照只发布发生变化的主题。发布入队失败会保留全量重发标志，在后续轮询重试。

阶段 09 只实现状态发布。`<prefix>/set`、`<prefix>/port` 和 `/api/enable` 属于阶段 10，不能把当前固件描述为已支持 HA 控制。

## 并发与 Secret

ESP-MQTT 事件回调只更新轻量状态并唤醒发布 task。JSON 生成、状态差异比较和 publish 调用均在独立 task 中执行；MQTT 断线不会中断 BLE 采集。

Broker host、port 和 topic 可以写入诊断日志。MQTT password 只复制到静态运行配置并交给 ESP-MQTT，不写入日志、HTTP 响应、状态 JSON、LWT 或崩溃诊断。第一阶段只支持可信局域网中的明文 MQTT，不能宣称密码在传输中受保护。

## 容量与网络范围

ESP32-C3 使用原有 1 MiB app 分区，未修改分区表。为容纳 ESP-MQTT，默认配置关闭第一阶段未使用的 MQTT TLS/WebSocket、MQTT 5、WPA3/SAE/OWE、GMAC、IPv6 和通用 WebSocket transport，保留 WPA2-Personal、IPv4、SoftAP、HTTP 和 MiOT HKDF/HMAC/AES-CCM。

## 证据等级

- 源码确认：主题、QoS、retain、LWT、自动重连、完整快照与增量发布调用链。
- 测试向量确认：主题和 JSON 格式、QoS/retain/LWT 契约、首次连接及重连后的全量发布掩码。
- 真机确认：待 ESP32-C3 串口和 MQTT Broker 可用后验证实际连接、LWT、retained 恢复与断线重连。
