# Home Assistant 控制

## 入口与命令模型

阶段 10 提供三个兼容入口：

- MQTT `<prefix>/set`：`{"piid":5,"value":3}`。
- MQTT `<prefix>/port`：`{"port":"c1|c2|c3|a|all","action":"on|off"}`。
- HTTP `POST /api/enable`：`{"enabled":true|false}`。

MQTT callback 和 HTTP handler 不调用加密或 GATT API，只校验输入并投递到 8 项有界 BLE 请求队列。每个已接受请求分配非零递增 request ID；`/api/status` 暴露 accepted/completed/failed 计数和最后完成的 ID，便于定位队列满、会话失效或写入失败。

## BLE task 串行执行

BLE task 是会话密钥、MiOT 序列号、发送计数器和 GATT 写入顺序的唯一拥有者。直接 SET 命令先按上游范围表验证 PIID/value，再构造 MiOT SET 并通过 AES-CCM 命令通道发送。发送成功后才更新线程安全设置快照。

端口命令严格执行：

1. GET PIID16 并等待当前值。
2. 在同一个 BLE task 内修改位图。
3. 值发生变化时 SET PIID16。
4. 关闭端口后清零相应 retained 遥测；开启端口等待后续 Notify 更新实测值。

源码确认的 PIID16 位图为：C1=`bit0`、C2=`bit1`、C3=`bit2`、A=`bit3`、all on=`0x0f`、all off=`0x00`。该结论来自固定上游提交的端口控制调用链，并已用纯函数测试确认；尚未真机确认。

## 运行时启停

`/api/enable` 的空白与 Secret 保持规则无关，只接受一个布尔值。配置保存成功后，启停请求唤醒 BLE task：

- 禁用会阻止新控制命令，打断扫描、连接、GATT/认证/命令等待和指数退避，尽力关闭通知并断开，然后清除会话和四端口状态。
- 启用会从 disabled 状态重新进入有界扫描/直连和认证流程。
- 连续五次认证失败进入 `auth_failed_locked` 后，禁用仍可退出锁定；重新启用会重新开始认证。
- 也可在锁定状态调用 `POST /api/retry-ble`，无需改动持久配置即可清零失败计数并立即重试。

## 证据等级

- 源码确认：上游 MQTT 负载、PIID16 先 GET 后 RMW/SET、四端口 bit 映射、PIID21 完整值提交。
- 测试向量确认：所有端口位图、SET/PORT JSON、PIID 范围、PIID21 32 位值、非法字段和非法动作。
- 真机确认：待串口和充电器可用后验证实际 PIID 回包、端口供电变化、HA 控件和运行时启停。
