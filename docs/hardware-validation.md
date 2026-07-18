# 真机验收记录

## 当前记录

- 日期：2026-07-18
- 环境：WSL，ESP-IDF v5.5.2-dirty，目标 esp32c3
- 当前软件基线：阶段 11 合并提交 `d996d08`
- 串口探测：未发现 `/dev/ttyACM*` 或 `/dev/ttyUSB*`
- 烧录：未执行
- `idf.py monitor`：未执行
- 结论：仅完成编译、主机测试和消毒器测试，不能标记为真机确认

该记录必须在硬件可见后追加，不能把源码确认或测试向量确认改写成真机确认。

## 烧录与启动

记录实际端口、开发板型号、USB 桥、固件提交、完整构建尺寸，以及：

```bash
idf.py -p "$PORT" flash
idf.py -p "$PORT" monitor
```

验收：无启动循环、panic、watchdog；NVS/HTTP/Wi-Fi 状态机启动；日志无 Secret。

## Wi-Fi 与 Web

- 首次启动 SoftAP、WPA2 密码和 `192.168.4.1` 页面。
- 错误 Wi-Fi 不保存；正确 Wi-Fi 获得 IP 后重启。
- STA 管理页、`/api/status`、脱敏 `/api/config`。
- STA 断开约 60 秒后的回退 AP，以及恢复后关闭 AP。

## BLE 与认证

- 实际广播地址类型、扫描/直连路径、MTU、UUID/CCCD。
- MiOT 认证成功或稳定失败阶段；错误 Token 五次锁定和 Web 手动恢复。
- 断开后至少 3 秒重试、指数退避和干净会话重建。

## 遥测与控制

- 四端口电压/电流/功率与外部仪表或充电器屏幕对比。
- C1 样例、设置读取、PIID16/21、60 秒刷新。
- 四端口 on/off、all、设置 SET 和协议开关。
- 协议名称只记录为启发式结果，并与实际负载说明一起保存。

## MQTT 与 Home Assistant

- 首连/重连完整 retained 快照、LWT、Broker 重启。
- HA 实体、HTTP health、BLE enable、端口/设置/协议控制。
- MQTT 断线期间 BLE 继续采集，恢复后重发完整快照。

## 长时间运行

至少运行 24～72 小时，周期记录 `free_heap`、重连次数、Notify 丢弃、命令失败、reset reason 和 watchdog。重复 Wi-Fi、Broker、充电器断电恢复，并确认没有快速重启循环或持续内存下降。

## 结果格式

每条结果写明：固件提交、充电器固件、ESP32 板型、测试步骤、预期、实际、日志时间戳和证据等级。日志先脱敏再提交；真实 Token、BLE Key、密码、HMAC、Key/IV 与完整认证帧不得进入仓库。
