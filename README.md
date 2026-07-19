# CUKTECH 10 Ultra ESP32 BLE 网关

本项目使用 ESP32-C3 和 ESP-IDF 5.5.2 或更高版本，实现酷态科 10 号超级电能充 Ultra 的本地 BLE 到 MQTT/Home Assistant 网关。

ESP32 在 BLE 链路上是 Central / GATT Client，充电器是 Peripheral / GATT Server。项目不会把 ESP32 实现成等待充电器主动连接的 BLE Peripheral。

## 构建

```bash
. "$IDF_PATH/export.sh"
idf.py --version
idf.py set-target esp32c3
idf.py reconfigure
idf.py build
```

无需硬件的配置测试：

```bash
cmake -S tests/host -B /tmp/cuktech-host-tests -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/cuktech-host-tests
ctest --test-dir /tmp/cuktech-host-tests --output-on-failure
```

ESP-IDF 版本必须为 5.5.2 或更高版本。生成的 `sdkconfig` 仅属于本机环境，跨环境默认配置以版本控制中的 `sdkconfig.defaults` 为准。

## 发布固件

仓库的 `Release ESP32-C3 firmware` 工作流用于正式发布。维护者从 `main` 分支手动运行该工作流并输入不带 `v` 前缀的版本号，例如 `1.0.0`。工作流会：

- 使用 ESP-IDF 5.5.2 构建 `esp32c3` 固件，并把版本号写入应用描述；
- 生成从 Flash 地址 `0x0` 烧录的合并固件，以及应用、Bootloader 和分区表的独立固件；
- 生成 SHA-256 校验文件，同时上传 Actions artifact；
- 创建 `v<版本号>` 标签和 GitHub Release，并附加全部固件文件。

发布页中的 `cuktech_ble_gateway-<版本>-merged.bin` 适合首次完整烧录，从地址 `0x0` 写入。该文件覆盖的范围包含 NVS，使用后需要重新配置 Wi-Fi、充电器和 MQTT：

```bash
esptool.py --chip esp32c3 -p "$PORT" write_flash 0x0 cuktech_ble_gateway-1.0.0-merged.bin
```

需要保留 NVS 配置时，应使用三个独立 `.bin`，地址依次为 Bootloader `0x0`、分区表 `0x8000`、应用程序 `0x10000`，并且不要执行整片擦除。也可以在分区布局未变化时只把应用程序写入 `0x10000`。三个文件不能都从 `0x0` 写入，也不能与合并固件同时烧录。

Windows 用户可使用乐鑫 [Flash Download Tool](https://docs.espressif.com/projects/esp-test-tools/zh_CN/latest/esp32/production_stage/tools/flash_download_tool.html)。图形界面的选项、合并固件配置、分立固件地址、esptool 命令和 NVS 数据影响见 [Release 固件烧录说明](docs/flash-download-tool.md)。

## 烧录与日志

先根据实际设备确认串口，再执行：

```bash
idf.py -p "$PORT" flash
idf.py -p "$PORT" monitor
```

不要把串口名硬编码进源码或脚本。烧录会写入目标设备 Flash；引入自定义分区表前必须单独评估并说明影响。

WSL USB 转发、完整部署顺序和发布检查见 [部署与烧录](docs/deployment.md)。烧录完成后必须单独运行 `idf.py -p "$PORT" monitor`，不能仅凭 flash 成功声称运行通过。

## 首次配网

没有有效 Wi-Fi 配置时，设备建立 `CUKTECH-BLE-<MAC后四位>` SoftAP，默认密码为 `cuktech10`，页面地址为 `http://192.168.4.1/`。默认密码可在 `idf.py menuconfig` 的 `CUKTECH BLE gateway` 菜单修改。

配网页面只允许输入 Wi-Fi SSID 和密码。设备会先在 APSTA 模式验证新凭据能获得 IP，成功后才保存并延迟重启；失败时保持 SoftAP，且不会覆盖上一份有效配置。已保存的 STA 连续约 60 秒无法获得 IP 时会开启同一回退 SoftAP，不会清除充电器或 MQTT 配置。

## 文档导航

- [部署与烧录](docs/deployment.md)
- [Release 固件烧录说明](docs/flash-download-tool.md)
- [Home Assistant 接入](docs/home-assistant.md)
- [HTTP API](docs/http-api.md)
- [MQTT 契约](docs/mqtt-bridge.md)
- [故障排查](docs/troubleshooting.md)
- [许可证与上游归属](NOTICE.md)
