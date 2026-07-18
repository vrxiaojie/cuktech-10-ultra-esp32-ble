# CUKTECH 10 Ultra ESP32 BLE 网关

本项目使用 ESP32-C3 和 ESP-IDF 5.5.2 或更高版本，实现酷态科 10 号超级电能充 Ultra 的本地 BLE 到 MQTT/Home Assistant 网关。

ESP32 在 BLE 链路上是 Central / GATT Client，充电器是 Peripheral / GATT Server。项目不会把 ESP32 实现成等待充电器主动连接的 BLE Peripheral。

## 当前状态

项目正在按照 `AGENTS.md` 的阶段计划开发。当前工程可以为 `esp32c3` 构建，已实现版本化配置、首次启动 SoftAP 配网、APSTA 联网验证、STA 断线 60 秒回退和最小 HTTP 配网页面。STA 管理页面、MiOT 认证、遥测和 MQTT 功能将在后续功能分支实现。

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

## 首次配网

没有有效 Wi-Fi 配置时，设备建立 `CUKTECH-BLE-<MAC后四位>` SoftAP，默认密码为 `cuktech10`，页面地址为 `http://192.168.4.1/`。默认密码可在 `idf.py menuconfig` 的 `CUKTECH BLE gateway` 菜单修改。

配网页面只允许输入 Wi-Fi SSID 和密码。设备会先在 APSTA 模式验证新凭据能获得 IP，成功后才保存并延迟重启；失败时保持 SoftAP，且不会覆盖上一份有效配置。已保存的 STA 连续约 60 秒无法获得 IP 时会开启同一回退 SoftAP，不会清除充电器或 MQTT 配置。

## 安全边界

后续管理页面默认只适用于可信局域网。第一阶段不提供 HTTPS、MQTT TLS、OTA、云端账号登录或多充电器支持。Token、BLE Key、Wi-Fi 密码和 MQTT 密码不得写入日志、HTTP 响应或 MQTT 消息。
