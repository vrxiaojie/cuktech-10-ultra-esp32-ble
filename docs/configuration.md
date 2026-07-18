# 配置模型

配置存放在 NVS 命名空间 `app_cfg` 的 `cfg_blob` 键中。Blob 由魔数、schema 版本、负载长度、CRC32 和固定版本负载组成。当前 schema 为 v1；读取 v0 时显式迁移到 v1，未知版本或 CRC 错误只在内存中回退默认值，不自动擦除 NVS。

## 字段与上限

| 字段 | 规则 |
|---|---|
| Wi-Fi SSID | 最长 32 字节；`wifi_configured=true` 时不能为空 |
| Wi-Fi password | 最长 64 字节 |
| 充电器 MAC | 可为空；非空时接受冒号或短横线，保存为大写冒号格式 |
| Token | 24 个十六进制字符，解码为 12 字节 |
| BLE Key | 可为空；非空时为 32 个十六进制字符，解码为 16 字节 |
| MQTT host | 最长 128 字节；为空表示未配置 Broker |
| MQTT port | 1～65535；默认 1883 |
| MQTT username/password | 各最长 64 字节 |
| MQTT topic prefix | 最长 128 字节；默认 `cuktech/charger`；去除首尾 `/`；禁止 `+` 和 `#` |
| MQTT keepalive | 非零；默认 60 秒 |
| BLE enabled | 默认启用；MAC 或 Token 缺失时后续 BLE 服务不会启动 |

## Secret 更新语义

- Token、BLE Key 和 MQTT password 不进入公开配置结构。
- Web API 后续只能返回 `*_configured` 布尔值，不能返回 Secret 内容。
- 空白 Secret 输入表示保留已有值。
- 清除必须通过独立布尔动作完成；清除时同时把内存中的旧字节归零。
- 配置 Blob 临时缓冲区在 NVS 操作结束后显式清零。
- 日志只允许输出 `configured=yes/no`，不得输出凭据、派生材料或认证帧。

完整配置在规范化和校验成功后才调用 `nvs_set_blob` 与 `nvs_commit`。非法输入不会生成新 Blob，也不会覆盖上一份有效配置。
