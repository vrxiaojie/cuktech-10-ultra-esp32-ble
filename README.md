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
- [Home Assistant 接入](docs/home-assistant.md)
- [HTTP API](docs/http-api.md)
- [MQTT 契约](docs/mqtt-bridge.md)
- [故障排查](docs/troubleshooting.md)
- [许可证与上游归属](NOTICE.md)
