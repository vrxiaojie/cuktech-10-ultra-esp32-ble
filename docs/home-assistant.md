# Home Assistant 接入

## 兼容基线

兼容性依据上游 `kairui1108/cuktech-ble-ha@89e5f78387812323528f5967421f65a689e803ef` 中的自定义集成 v1.0.5 源码确认。该集成：

- 硬编码订阅 `cuktech/charger/port/*`、`settings` 和 `status`。
- 向 `cuktech/charger/set` 与 `cuktech/charger/port` 发布控制命令。
- 使用配置的 Server URL 调用 `GET /api/status` 和 `POST /api/enable`。
- 依赖 Home Assistant 已配置并可用的 MQTT 集成。

因此 ESP32 的 MQTT topic prefix 必须保持默认 `cuktech/charger`。修改 prefix 后，上游 v1.0.5 不会自动跟随。

## 准备 Home Assistant

1. 在 Home Assistant 中先配置 MQTT 集成，并确认 HA 与 ESP32 连接同一个 Broker。
2. 从上游项目或其 HACS 仓库安装 `cuktech_charger` 自定义集成；本仓库不复制或打包该 Python 集成。
3. 重启 Home Assistant。
4. 添加“CUKTECH Charger”集成。
5. Server URL 填 `http://<ESP32-IP>`，不要沿用上游主机服务默认值 `http://localhost:8199`。

ESP32 HTTP server 使用默认端口 80，因此 URL 通常不带端口。HA 主机必须能直接访问该局域网 IP 的 `/api/status`。

## 预期实体和数据

认证且 MQTT 连接成功后，上游集成可创建：

- C1/C2/C3/A 的电压、电流、功率、active 和协议传感器。
- BLE 连接状态和运行时启停开关。
- 四端口开关。
- 上游定义的 PIID 设置、倒计时和协议开关实体。

协议名称由启发式规则估算，不是充电器硬件直接报告的绝对协议号。意外 BLE 断开时 ESP32 保留最后端口 retained 值，同时 status 变为 disconnected；这是与上游行为保持一致的设计。

## 手工检查主题

在 Home Assistant 的 MQTT“监听主题”中输入：

```text
cuktech/charger/#
```

应看到：

```text
cuktech/charger/status
cuktech/charger/settings
cuktech/charger/port/c1
cuktech/charger/port/c2
cuktech/charger/port/c3
cuktech/charger/port/a
```

如果 MQTT 有数据但集成显示不可用，确认 Server URL 的 `/api/status` 从 HA 主机可访问。若 HTTP 正常但完全没有 MQTT 数据，检查 Broker host/port/账号、ESP32 `mqtt_state` 和 topic prefix。

## 控制语义

- BLE 连接开关调用 `/api/enable`，会持久化 `ble_enabled`。
- 端口开关发布 PORT 命令，ESP32 在 BLE task 内 GET PIID16 后原子读改写。
- 设置实体发布完整 PIID/value，并在 ESP32 侧做范围校验。
- 协议开关由上游集成根据当前 PIID21 快照编码完整 32 位值，再发布 SET。

控制被接受只表示已进入有界队列；最终执行结果可在 `/api/status` 的 `ble_commands_completed`、`ble_commands_failed` 和 `ble_last_request_id` 中诊断。

## 不兼容范围

ESP32 第一阶段不提供上游 Python 服务的 SQLite 历史、图表、CSV、完整 Web UI 或 8199 端口。依赖这些服务端专有功能的上游页面不属于兼容目标；HA 所需 MQTT 主题和最低 HTTP API 才是兼容范围。
