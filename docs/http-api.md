# HTTP 管理 API

HTTP 管理页没有 TLS 或登录认证，只适用于可信局域网。首次配网和 Wi-Fi 回退 SoftAP 只开放 Wi-Fi 配网页面；充电器、Token、BLE Key 和 MQTT 配置接口仅在 STA 已获得 IP 时开放。

## `GET /api/status`

该接口在所有网络状态下可读取，不包含 Secret。当前阶段返回：

```json
{
  "connected": false,
  "authenticated": false,
  "mqtt_connected": false,
  "device_model": "njcuk.fitting.ad1204_...",
  "firmware_version": "1.0.0",
  "ports": {
    "c1": {
      "voltage": 20.1,
      "current": 2.5,
      "power": 50.2,
      "active": true,
      "protocol": "PD",
      "enabled": true
    },
    "c2": {"voltage": 0, "current": 0, "power": 0, "active": false, "protocol": "idle", "enabled": true},
    "c3": {"voltage": 0, "current": 0, "power": 0, "active": false, "protocol": "idle", "enabled": true},
    "a": {"voltage": 0, "current": 0, "power": 0, "active": false, "protocol": "idle", "enabled": true}
  },
  "settings": {"5": 3, "16": 15, "21": 50532111},
  "protocol_extend": 50532111,
  "protocol_switches": {
    "c1": {"pd": true, "pps": true, "ufcs": true},
    "c2": {"pd": true, "pps": true, "ufcs": true},
    "c3": {"scp": true, "ufcs": true},
    "a": {"scp": true, "ufcs": true}
  },
  "gateway_firmware_version": "...",
  "wifi_state": "sta_connected",
  "ble_state": "authenticated",
  "mqtt_state": "connected",
  "ble_gatt_ready": true,
  "ble_mtu": 247,
  "ble_notify_dropped": 0,
  "mqtt_reconnects": 1,
  "mqtt_publish_failures": 0,
  "last_error": "",
  "free_heap": 123456,
  "state_revision": 20
}
```

`connected` 和 `authenticated` 表示 BLE/MiOT 登录状态，不表示 Wi-Fi 状态。认证进行中 `connected=true`、`authenticated=false`；只有设备 HMAC 校验和认证控制成功结果均通过后，`authenticated` 才为 `true`。`ble_state` 可能为 `scanning`、`connecting`、`authenticating`、`authenticated`、`auth_failed_locked` 或错误/退避状态。

`mqtt_connected` 和 `mqtt_state` 来自 MQTT Client 实际状态。`mqtt_state` 可能为 `waiting_config`、`connecting`、`connected`、`disconnected` 或 `error`；Broker 不可达不会触发 Wi-Fi 配网回退，也不会停止 BLE 数据采集。

`firmware_version` 是从充电器 GATT 特征读取的版本；`gateway_firmware_version` 是 ESP32 固件版本。读取失败时充电器型号或版本为空字符串，但字段仍保留。`settings` 使用字符串 PIID 键；`ports` 只包含稳定遥测字段。意外断线会保留最后端口和设置快照，同时把连接状态置为 false。

`last_error` 只包含稳定阶段名和错误码，例如 `auth:device_hmac_mismatch`，不会包含 Token、随机数、Key、IV、HMAC 或完整认证帧。

## `GET /api/config`

只在 `sta_connected` 状态开放。响应示例：

```json
{
  "schema_version": 1,
  "wifi_configured": true,
  "wifi_ssid": "example",
  "charger_mac": "AA:BB:CC:DD:EE:FF",
  "token_configured": true,
  "ble_key_configured": false,
  "mqtt_host": "mqtt.lan",
  "mqtt_port": 1883,
  "mqtt_username": "gateway",
  "mqtt_password_configured": true,
  "mqtt_topic_prefix": "cuktech/charger",
  "mqtt_keepalive": 60,
  "ble_enabled": true
}
```

响应永远不包含 Wi-Fi password、Token、BLE Key 或 MQTT password。

## `POST /api/config`

只在 `sta_connected` 状态开放，请求体最大 1024 字节。字段均支持部分更新，但空对象会返回 `missing_fields`。

```json
{
  "charger_mac": "AA:BB:CC:DD:EE:FF",
  "token": "000102030405060708090a0b",
  "clear_token": false,
  "ble_key": "",
  "clear_ble_key": false,
  "mqtt_host": "mqtt.lan",
  "mqtt_port": 1883,
  "mqtt_username": "gateway",
  "mqtt_password": "",
  "clear_mqtt_password": false,
  "mqtt_topic_prefix": "cuktech/charger",
  "mqtt_keepalive": 60,
  "ble_enabled": true
}
```

Secret 规则：

- `token`、`ble_key`、`mqtt_password` 为空表示保持旧值。
- 只有对应 `clear_*` 为 `true` 才清除旧值。
- Token 非空时必须为 24 个十六进制字符。
- BLE Key 非空时必须为 32 个十六进制字符；当前认证流程不使用它。
- 请求解析缓冲和 cJSON 中的 Secret 字符串在使用后显式清零。

其他校验：

- MAC 接受冒号或短横线，保存为大写冒号形式。
- MQTT port 和 keepalive 为 1～65535 的整数。
- topic prefix 去除首尾 `/`，禁止 `+` 和 `#`。
- 所有字段完整校验通过后才保存 NVS；失败不会修改旧配置。

成功返回 `{"ok":true}`，并约 3 秒后受控重启，使后续 BLE/MQTT 服务使用新配置。常见错误为 `body_too_large`、`invalid_json`、`missing_fields`、`invalid_field`、`invalid_config`、`config_load_failed` 和 `config_save_failed`。
