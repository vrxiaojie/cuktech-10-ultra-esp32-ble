# CUKTECH 10 Ultra ESP32 BLE 网关

本项目使用 ESP32-C3 和 ESP-IDF 5.5.2 或更高版本，实现酷态科 10 号超级电能充 Ultra 的本地 BLE 到 MQTT/Home Assistant 网关。

ESP32 在 BLE 链路上是 Central / GATT Client，充电器是 Peripheral / GATT Server。项目不会把 ESP32 实现成等待充电器主动连接的 BLE Peripheral。

## 1、选择使用方式

本项目提供两种使用方式。选择其中一条完成固件烧录，不需要把两套步骤混在一起执行。

| 使用方式 | 适合用户 | 需要准备 | 入口 |
|---|---|---|---|
| 路径 A 下载 Release 固件 | 只想安装和使用网关，不修改源码 | ESP32-C3、数据线、Flash Download Tool 或 esptool | [直接下载 bin 并烧录](#3路径-a-直接下载-bin-并烧录) |
| 路径 B 从源码构建 | 需要修改代码、配置或参与开发 | ESP-IDF 5.5.2 或更高版本、CMake、编译环境 | [从源码构建并烧录](#4路径-b-从源码构建并烧录) |

路径 A 不需要克隆仓库，也不需要安装 ESP-IDF。路径 B 直接使用 `idf.py build` 和 `idf.py flash`，不需要从 Release 页面下载 `.bin` 文件。

## 2、两种方式都需要的设备信息

ESP32 固件不提供米家账号登录、设备绑定或云端密钥提取功能。开始配置前，需要按照[kairui1108/cuktech-ble-ha](https://github.com/kairui1108/cuktech-ble-ha/tree/main) 的方法，使用 [Xiaomi-cloud-tokens-extractor](https://github.com/PiotrMachowski/Xiaomi-cloud-tokens-extractor) 从自己的米家账号获取充电器信息。

在工具输出中选择自己的酷态科充电器，并确认以下字段：

| 字段 | 本项目要求 |
|---|---|
| BLE MAC | 正常显示顺序，例如 `AA:BB:CC:DD:EE:FF`，不要反转字节 |
| Token | 恰好 24 个十六进制字符，解码后为 12 字节 |
| BLE Key | 可选，非空时必须是 32 个十六进制字符，解码后为 16 字节 |

先保存这些信息，不要在 SoftAP 配网页面输入。两条路径完成固件烧录和 Wi-Fi 配网后，再通过 ESP32 的 STA 管理页面填写 BLE MAC、Token、可选 BLE Key 和 MQTT 参数。

## 3、路径 A 直接下载 bin 并烧录

### 3.1、下载 Release 文件

打开本项目的 [GitHub Releases](https://github.com/vrxiaojie/cuktech-10-ultra-esp32-ble/releases)，下载目标版本的固件和 `SHA256SUMS`。

首次安装可以下载 `cuktech_ble_gateway-<版本>-merged.bin`。它已经包含 Bootloader、分区表和应用程序，从 `0x0` 烧录。该文件覆盖的范围包含 NVS，烧录后需要重新配置 Wi-Fi、充电器和 MQTT。

需要保留已有 NVS 配置时，下载三个独立文件：

| 文件 | 烧录地址 |
|---|---:|
| `cuktech_ble_gateway-<版本>-bootloader.bin` | `0x0` |
| `cuktech_ble_gateway-<版本>-partition-table.bin` | `0x8000` |
| `cuktech_ble_gateway-<版本>-app.bin` | `0x10000` |

### 3.2、烧录 Release 固件

Windows 用户可以使用乐鑫 [Flash Download Tool](https://docs.espressif.com/projects/esp-test-tools/zh_CN/latest/esp32/production_stage/tools/flash_download_tool.html)。合并固件只配置一行并填写地址 `0x0`；分立固件按上表配置三行，不要执行整片擦除，也不要同时勾选合并固件和分立固件。

使用 esptool 烧录合并固件的示例：

```bash
esptool.py --chip esp32c3 -p "$PORT" write_flash \
  0x0 cuktech_ble_gateway-1.0.0-merged.bin
```

图形界面配置、SHA-256 校验、分立烧录命令和 NVS 数据影响见 [Release 固件烧录说明](docs/flash-download-tool.md)。烧录并复位设备后，继续执行 [首次配网和管理配置](#5首次配网和管理配置)。

## 4、路径 B 从源码构建并烧录

### 4.1、获取源码和构建环境

```bash
git clone https://github.com/vrxiaojie/cuktech-10-ultra-esp32-ble.git
cd cuktech-10-ultra-esp32-ble
. "$IDF_PATH/export.sh"
idf.py --version
idf.py set-target esp32c3
idf.py reconfigure
idf.py build
```

`idf.py --version` 必须显示 5.5.2 或更高版本。生成的 `sdkconfig` 只属于当前构建环境，跨环境默认配置以版本控制中的 `sdkconfig.defaults` 为准。

### 4.2、运行主机测试

```bash
cmake -S tests/host -B /tmp/cuktech-host-tests -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/cuktech-host-tests
ctest --test-dir /tmp/cuktech-host-tests --output-on-failure
```

### 4.3、烧录并查看日志

先确认实际串口，再分别执行烧录和监视命令：

```bash
PORT=/dev/ttyACM0
idf.py -p "$PORT" flash
idf.py -p "$PORT" monitor
```

不要猜测或硬编码不存在的串口。烧录完成后必须单独运行 monitor，不能仅凭 `flash` 成功声称程序已经正常启动。WSL USB 转发和完整部署步骤见 [部署与烧录](docs/deployment.md)。完成后继续执行 [首次配网和管理配置](#5首次配网和管理配置)。

## 5、首次配网和管理配置

### 5.1、完成 Wi-Fi 配网

没有有效 Wi-Fi 配置时，设备建立 `CUKTECH-BLE-<MAC后四位>` SoftAP，默认密码为 `cuktech10`，页面地址为 `http://192.168.4.1/`。

连接该 SoftAP，在浏览器打开 `http://192.168.4.1/`，只提交 Wi-Fi SSID 和密码。设备会先验证 STA 能获得 IP，成功后才保存并重启；验证失败时保持 SoftAP，不会覆盖上一份有效凭据。

### 5.2、填写充电器和 MQTT 配置

设备加入局域网后，打开 `http://<ESP32-IP>/`，填写第 2 节准备的 BLE MAC、Token、可选 BLE Key，以及 MQTT host、port、用户名、密码和 topic prefix。

默认 MQTT topic prefix 为 `cuktech/charger`。使用上游 Home Assistant 集成时必须保持该值。保存配置后等待设备重启，再访问 `http://<ESP32-IP>/api/status` 检查 Wi-Fi、BLE 和 MQTT 状态。

## 6、安装 Home Assistant 集成

Home Assistant 插件来自上游独立仓库 [kairui1108/cuktech-ble-ha-integration](https://github.com/kairui1108/cuktech-ble-ha-integration)，本仓库不复制或打包该 Python 自定义集成。安装前先在 Home Assistant 中配置 MQTT 集成，并确保 Home Assistant 与 ESP32 使用同一个 Broker。

通过 HACS 安装时：

1. 打开上游集成的 [HACS 添加入口](https://my.home-assistant.io/redirect/hacs_repository/?owner=kairui1108&repository=cuktech-ble-ha-integration&category=integration)。
2. 将仓库添加为自定义集成，搜索并安装 `CUKTECH Charger`。
3. 重启 Home Assistant。
4. 在“设置 → 设备与服务 → 添加集成”中搜索 `CUKTECH Charger`。
5. `Server URL` 填写 `http://<ESP32-IP>`，不要使用上游 Python 服务的默认地址 `http://localhost:8199`。

手动安装时有两个来源可选。独立集成仓库中的目录是 `custom_components/cuktech_charger`；原始 `cuktech-ble-ha` 仓库中的目录是 `ha_integration/custom_components/cuktech_charger`。把其中的 `cuktech_charger` 完整复制到 Home Assistant 配置目录的 `/config/custom_components/cuktech_charger`，然后重启 Home Assistant 并添加集成。

上游集成文档中的 BLE Server 在本项目中由 ESP32 网关替代，不需要再启动上游 Python BLE Server。MQTT topic prefix 必须保持默认 `cuktech/charger`。完整配置和检查步骤见 [Home Assistant 接入](docs/home-assistant.md)。

## 7、文档导航

- [部署与烧录](docs/deployment.md)
- [Release 固件烧录说明](docs/flash-download-tool.md)
- [Home Assistant 接入](docs/home-assistant.md)
- [HTTP API](docs/http-api.md)
- [MQTT 契约](docs/mqtt-bridge.md)
- [故障排查](docs/troubleshooting.md)
- [许可证与上游归属](NOTICE.md)
