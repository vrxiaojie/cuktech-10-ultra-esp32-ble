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
