# Wi-Fi 配网与回退

## 状态

| 状态 | 含义 |
|---|---|
| `provisioning_ap` | 没有有效 Wi-Fi 配置，只开启 SoftAP |
| `sta_connecting` | 使用已保存凭据连接 STA |
| `sta_connected` | STA 已获得 IPv4 地址 |
| `verifying_sta` | 保持 SoftAP，同时验证页面提交的新凭据 |
| `fallback_ap` | 已保存的 STA 长时间无法获得 IP，开启 APSTA 回退 |

MQTT Broker 不可达、充电器离线或 Token 错误都不会触发 Wi-Fi 配网回退。

## SoftAP

- SSID：`CUKTECH-BLE-<STA MAC 后四位>`
- IPv4：ESP-IDF 默认 `192.168.4.1`
- 默认安全：WPA2-PSK
- 默认密码：`cuktech10`
- 最大客户端：4

在 `idf.py menuconfig` → `CUKTECH BLE gateway` 中可修改：

- `Provisioning SoftAP password`：空值表示开放 AP；非空必须为 8～63 字符。
- `STA failure time before enabling fallback SoftAP`：默认 60 秒。
- `Provisioning credential verification timeout`：默认 30 秒。

不建议使用开放 AP。即使配置为开放 AP，配网页面也只提供 Wi-Fi SSID/密码字段，不提供 Token、BLE Key 或 MQTT 密码输入。

## HTTP 接口

### `GET /`

返回小型无框架配网页面。

### `POST /api/provision`

必须使用 JSON 请求体：

```json
{"ssid":"example","password":"example-password"}
```

请求体最大 256 字节。SSID 必须为 1～32 字节，密码最长 64 字节。空密码用于开放 Wi-Fi。

处理顺序：

1. 校验请求和字段长度。
2. 切换 APSTA，保持浏览器当前 SoftAP 连接。
3. 使用候选凭据连接 STA，最长等待 30 秒获得 IP。
4. 成功后才保存完整版本化配置 Blob。
5. 返回 STA IP，约 3 秒后受控重启。
6. 失败时恢复旧 STA 配置或首次启动 SoftAP，不写入候选凭据。

成功响应：

```json
{"ok":true,"ip":"192.168.1.23"}
```

失败响应只返回稳定错误名，例如 `invalid_input`、`connect_failed`、`save_failed` 或 `busy`，不包含 SSID、密码或底层认证数据。

`GET /generate_204` 与 `GET /hotspot-detect.html` 会重定向到 `/`，用于常见系统的 captive portal 探测。当前未实现 captive DNS。

## 回退行为

有已保存 Wi-Fi 配置时，设备先以 STA 启动。连续约 60 秒没有获得 IP 才开启回退 SoftAP；旧 Wi-Fi、充电器、Token、BLE Key 和 MQTT 配置全部保留。STA 恢复连接后关闭回退 AP。回退流程不执行自动重启，因此不会形成快速重启循环。
