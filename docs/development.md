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
