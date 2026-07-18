# 开发与验证记录

## 开发环境

- 目标芯片：ESP32-C3，无 PSRAM 假设
- 最低 ESP-IDF：5.5.2
- BLE Host：NimBLE
- BLE 角色：Central、Observer、GATT Client
- 首选 ATT MTU：247
- MQTT 协议：3.1.1

## 本地构建流程

```bash
. /home/vrxiaojie/esp/v5.5.2/esp-idf/export.sh
idf.py --version
idf.py set-target esp32c3
idf.py reconfigure
idf.py build
idf.py size
```

本机首次验证使用 `ESP-IDF v5.5.2-dirty`。版本满足要求，但 `dirty` 表示本机 ESP-IDF 安装树存在本地修改，发布前还需要在干净的 ESP-IDF v5.5.2 环境和 CI 中复核。

## 阶段 01 验证

- `idf.py build`：通过
- 固件目标：`esp32c3`
- 固件镜像：约 185.6 KiB（组件骨架阶段）
- `git diff --check`：通过
- 真机烧录：尚未执行，当前环境未发现 `/dev/ttyACM*` 或 `/dev/ttyUSB*`
- 真机串口日志：待硬件可见后使用 `idf.py -p "$PORT" monitor` 验证

阶段 01 只验证工程骨架和依赖配置，不代表 BLE、Wi-Fi、HTTP 或 MQTT 运行流程已经实现。

## 阶段 02 验证

- `idf.py build`：通过
- 主机配置测试：1/1 通过
- 覆盖：空配置、MAC 规范化、topic 规范化和通配符拒绝
- 覆盖：Token 24 位十六进制、BLE Key 32 位十六进制和空值保持/显式清除
- 覆盖：Secret 脱敏、版本化 Blob 往返、CRC 损坏拒绝、未知版本拒绝
- 覆盖：v0→v1 显式迁移、非法配置编码前拒绝且不修改目标缓冲区
- `git diff --check`：通过
- NVS 真机读写与掉电一致性：待硬件烧录后验证

主机测试命令：

```bash
cmake -S tests/host -B /tmp/cuktech-host-tests -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/cuktech-host-tests
ctest --test-dir /tmp/cuktech-host-tests --output-on-failure
```

## 阶段 03 验证

- `idf.py build`：通过
- SoftAP 名称：`CUKTECH-BLE-<STA MAC 后四位>`
- SoftAP 默认安全：WPA2-PSK，默认密码 `cuktech10`，可通过 Kconfig 修改
- 首次启动：无 Wi-Fi 配置时进入 `provisioning_ap`
- 正常启动：有 Wi-Fi 配置时进入 `sta_connecting`，约 60 秒未获得 IP 后进入 `fallback_ap`
- 配网写入：APSTA 获得 IP 后才提交 NVS；超时、无效输入或保存失败时恢复旧配置
- HTTP：请求体上限 256 字节；非法 JSON、缺字段和并发请求返回明确错误
- Secret：Wi-Fi 密码不进入日志，HTTP 和 cJSON 临时缓冲在使用后显式清零
- 固件镜像：约 847 KiB，默认 1 MiB 应用分区剩余约 17%
- 真机 SoftAP、DHCP、HTTP、APSTA 验证、重启和 60 秒回退：待串口设备可见后验证

本阶段未加入 mDNS。ESP-IDF 5.5.2 源码树不含高层 `mdns` 组件；若采用 Espressif Component Registry 的托管组件，需要先评估依赖、许可证和固件成本。

## 阶段 04 验证

- `idf.py build`：通过
- 主机测试：2/2 通过（配置模型与 Web 配置模型）
- ASan/UBSan：通过；当前受控环境不支持 LeakSanitizer，测试时关闭 leak 检测
- `GET /api/status`：包含现有 HA 健康检查要求的兼容字段
- `GET /api/config`：只包含公开字段和 `*_configured`，不包含任何 Secret 值
- `POST /api/config`：覆盖请求上限、非法 JSON、空对象、错误类型、非法十六进制、非法 topic 和 Secret 保持/清除
- SoftAP 限制：管理配置 GET/POST 只在 `sta_connected` 状态开放
- 固件镜像：约 869 KiB，默认 1 MiB 应用分区剩余约 15%
- 真机 HTTP 页面、NVS 保存后重启和局域网访问：待烧录验证

## 阶段 05 验证

- 上游基线：`kairui1108/cuktech-ble-ha@89e5f78387812323528f5967421f65a689e803ef`
- `idf.py build`：通过，固件侧使用 ESP-IDF mbedTLS
- 主机测试：3/3 通过，协议测试侧使用系统 OpenSSL 作为独立密码学后端
- ASan/UBSan：3/3 通过
- HKDF-SHA256：完整 64 字节黄金向量通过
- HMAC-SHA256：设备方向与应用方向黄金向量通过
- AES-CCM：Tag=4，TX/RX 计数器 0 黄金向量通过，篡改 Tag 拒绝
- 计数器：`send_counter >= 0xffff` 返回重建会话要求
- MiOT TLV：SET PIID5、GET PIID5、SET PIID21 三组向量通过
- PIID 范围：与上游 `state.py` 当前可写范围一致
- 端口解析：20.1 V、2.5 A、50.2 W、active、PD 样例通过
- 协议启发式：移植当前 `state_protocol_v2.py` 规则，结果不宣称绝对准确
- 上游 pytest：本机 Python 环境未安装 pytest，因此未执行；已直接审阅固定提交源码和测试文件
- 真机密码学认证与 Notify 解析：待 BLE 链路和硬件阶段验证

## 阶段 06 验证

- `idf.py build`：通过，ESP-IDF 5.5.2，目标 `esp32c3`
- 主机测试：4/4 通过，新增 MAC 解析、NimBLE 地址布局和有界指数退避测试
- BLE 角色：Central / Observer / GATT Client；未启用 Peripheral 或 GATT Server
- 连接：5 秒扫描匹配 MAC，未广播时有限次 public/random 直连，每次 10 秒超时
- GATT：交换 MTU，按 UUID 发现 FE95 服务、六个特征和四个 CCCD，不使用固定 Handle
- 并发：GAP/GATT 回调只复制入队，BLE 应用 task 串行拥有 GATT 顺序
- Notify：12 项固定队列，单项最大 244 字节，溢出只计数不打印负载
- 重连：普通错误指数退避 1 秒到 300 秒
- 固件容量：`0xec780`，默认 1 MiB app 分区剩余 `0x13880`（约 8%）
- 分区表：未修改；通过关闭未使用的 NimBLE 功能和 `-Os` 解决容量溢出
- 真机扫描、地址类型、MTU、UUID/CCCD 和 Notify：待串口设备可见后烧录验证

详细设计和证据等级见 [BLE GATT 链路](ble-gatt-link.md)。

## 阶段 07 验证

- `idf.py build`：通过，认证代码实际链接 mbedTLS HKDF/HMAC，镜像 `0xee120`
- 默认 1 MiB app 分区剩余 `0x11ee0`（约 7%），未修改分区表
- 主机测试：5/5 通过，新增完整 MiOT 认证状态机测试
- 覆盖：短重复初始化恢复、inline、multiframe、连续帧号、双向 HMAC 和成功结果
- 覆盖：错误设备 HMAC、`0x23` 登录拒绝、异常帧号和失败会话清零
- 随机数：固件使用 `esp_fill_random()`；主机测试使用固定合成向量
- 清理：失败后尽力关闭四个 CCCD，断开并清除会话与队列
- 重试：认证失败至少等待 3 秒，连续 5 次进入 `auth_failed_locked`
- Secret：日志和 HTTP 只暴露认证阶段名，不暴露 Token、随机数、Key、IV 或 HMAC
- 真机认证：当前环境未发现串口，尚未完成，不能标记为真机确认

详细状态机见 [MiOT BLE 登录认证](miot-auth.md)。
