# 协议证据与实现说明

上游基线为 `https://github.com/kairui1108/cuktech-ble-ha` 的提交 `89e5f78387812323528f5967421f65a689e803ef`。移植的协议估算逻辑保留了上游 MIT 许可证，见 `components/cuktech_protocol/UPSTREAM_LICENSE`。

## 源码确认

以下结论由固定提交的 `ble_server/src/cuktech_ble/controller.py` 当前调用链确认：

- Token 是 12 字节 HKDF IKM，不直接作为 AES Key，也不通过 BLE 发送。
- `salt = app_random || dev_random`。
- HKDF-SHA256 info 为 ASCII `mible-login-info`，输出长度 64 字节。
- `dev_key=derived[0:16]`、`app_key=derived[16:32]`、`dev_iv=derived[32:36]`、`app_iv=derived[36:40]`。
- 设备 HMAC 为 `HMAC-SHA256(dev_key, dev_random || app_random)`。
- 应用 HMAC 为 `HMAC-SHA256(app_key, app_random || dev_random)`。
- ESP32→充电器使用 `app_key`、4 字节 CCM Tag，以及 `app_iv || 00000000 || send_it_le32`。
- 发送包前缀只携带 `send_it` 的低 16 位，因此固件在计数器达到 `0xffff` 前要求重建会话。
- 充电器→ESP32使用 `dev_key`，Nonce 为 `dev_iv || 00000000 || it_le16 || 0000`。
- MiOT TLV 的总长度包含 2 字节 frame header；UINT8 的 `type_id=1`，UINT32 的 `type_id=5`。

BLE Key、反序 MAC 辅助值和 `PRODUCT_ID=0x660e` 均未进入当前上游认证调用链。本项目不会把这些值混入 HKDF、HMAC 或 AES。

认证传输状态机同样按该固定提交确认：`a4` 初始化、回写递增的协商帧、等长 `f2` 占位、`0x24` 登录、随机数与 HMAC 的 RCV_RDY/RCV_OK 握手，以及认证控制 `0x21/0x11` 成功、`0x23/0x12` 失败。固件同时实现上游当前 inline 与 multiframe 两种认证响应格式。

## 测试向量确认

仓库 `AGENTS.md` 中的合成黄金向量已由主机测试验证：

- 完整 `derived64`
- `dev_hmac` 与 `app_hmac`
- 计数器 0 的 `tx_packet` 与 `rx_packet`
- AES-CCM 篡改拒绝
- SET/GET TLV 字节序和长度
- C1 端口 20.1 V、2.5 A、50.2 W、PD 样例
- 完整认证写入/通知顺序、短初始化恢复、inline/multiframe 和 HMAC 拒绝

主机测试链接 OpenSSL，仅作为独立、无需 ESP32 的算法验证后端；固件编译使用 ESP-IDF 自带 mbedTLS。两者共享相同的会话、Nonce、计数器、TLV 和解析代码。

## 协议启发式

`port_decode.c` 移植上游 `state_protocol_v2.py` 的当前规则。MiOT Notify 没有稳定提供硬件协议号，`PD/PPS/QC/5V` 等结果由端口类型、原始 code、电压、可选 PDO 类型和 PIID21 开关共同估算。

这部分是“源码确认的启发式实现”，不是“真机确认的绝对协议号”。AFC、FCP、SCP 和 UFCS 名称已保留在稳定枚举中，但在没有可靠证据时不会仅凭猜测输出。

## 与上游的安全差异

上游当前源码包含会话 Key/IV 的 debug 日志。ESP32 实现不输出 Token、随机数、派生材料、HMAC、IV、完整认证明文或加密包；临时派生缓冲、失败输出和会话清理使用显式清零。
