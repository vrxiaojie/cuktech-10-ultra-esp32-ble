# ESP32-C3 Release 固件烧录

## 1、选择需要的固件

[GitHub Releases](https://github.com/vrxiaojie/cuktech-10-ultra-esp32-ble/releases) 会提供以下文件，其中 `<版本>` 例如 `1.0.0`：

| 文件 | 烧录地址 | 用途 |
|---|---:|---|
| `cuktech_ble_gateway-<版本>-merged.bin` | `0x0` | 首次安装或需要完整重写固件时使用 |
| `cuktech_ble_gateway-<版本>-bootloader.bin` | `0x0` | 分立烧录的 Bootloader |
| `cuktech_ble_gateway-<版本>-partition-table.bin` | `0x8000` | 分立烧录的分区表 |
| `cuktech_ble_gateway-<版本>-app.bin` | `0x10000` | 分立烧录的应用程序 |
| `SHA256SUMS` | 不烧录 | 校验下载文件是否完整 |

合并固件已经包含 Bootloader、分区表和应用程序，只能配置一行并从 `0x0` 烧录。不要把合并固件与三个分立固件放在同一次下载任务中。

当前合并固件会覆盖 `0x0` 到应用末尾之间的区域，其中也包括 NVS 配置区。使用合并固件会清除已保存的 Wi-Fi、充电器和 MQTT 配置。需要保留配置时，应使用三个分立固件，或在分区布局未变化时只更新应用固件，并且不要点击整片擦除。

## 2、校验下载文件

Release 中的 `SHA256SUMS` 记录了所有 `.bin` 文件的 SHA-256。Linux、WSL 或 macOS 可以把文件放在同一目录后执行：

```bash
sha256sum -c SHA256SUMS
```

Windows PowerShell 可以逐个计算，例如：

```powershell
Get-FileHash .\cuktech_ble_gateway-1.0.0-merged.bin -Algorithm SHA256
```

输出值必须与 `SHA256SUMS` 中同名文件对应的值一致。校验不一致时不要继续烧录，应重新下载文件。

## 3、使用 Flash Download Tool

### 3.1、准备工具和串口

Flash Download Tool 是乐鑫提供的 Windows 图形化下载工具。请从以下官方页面下载并查看最新说明：

- [Flash Download Tool 用户指南](https://docs.espressif.com/projects/esp-test-tools/zh_CN/latest/esp32/production_stage/tools/flash_download_tool.html)
- [Flash Download Tool 下载地址](https://dl.espressif.com/public/flash_download_tool.zip)

连接 ESP32-C3 开发板后，在 Windows 设备管理器中确认实际 COM 端口。关闭串口监视器和其他占用该端口的软件。如果开发板不能自动进入下载模式，按开发板的 BOOT 和 RESET 按键说明手动进入下载模式；不同开发板的按键时序可能不同，应以开发板原理图或使用说明为准。

启动工具时选择：

| 选项 | 设置 |
|---|---|
| `ChipType` | `ESP32-C3` |
| `WorkMode` | `Develop` |
| `LoadMode` | `UART` |

进入 `SPIDownload` 页面后，选择实际 `COM`。`BAUD` 可先使用 `460800`，若出现连接失败、超时或校验失败，再降低为 `115200`。

建议勾选 `DoNotChgBin`，让工具按 Release 中 bin 文件的原始内容烧录。当前 Release 的 CI 构建参数为 `DIO`、`80 MHz`，镜像头声明的 Flash size 为 `2 MB`。Flash Download Tool 3.9.10 及更高版本会让 `SPI SPEED` 和 `SPI MODE` 与固件编译配置保持一致，不需要手工修改；`DETECTED INFO` 显示的是开发板实际检测结果，实际 Flash 容量大于镜像声明容量时不需要改写 bin。不要启用安全启动或 Flash 加密配置，本项目发布固件没有为这些不可逆安全功能提供量产配置。

### 3.2、烧录合并固件

在 `Download Path Config` 中只添加一行：

| 勾选 | 文件 | 地址 |
|---|---|---:|
| 是 | `cuktech_ble_gateway-1.0.0-merged.bin` | `0x0` |

确认其他固件行没有勾选，不需要点击 `CombineBin`。Release 中的 `merged.bin` 已由 ESP-IDF 5.5.2 合并完成。

选择正确的 `COM` 和 `BAUD` 后点击 `START`。工具显示 `FINISH` 后，按开发板的 RESET 键或重新上电。合并固件会覆盖 NVS 配置，启动后应重新完成 Wi-Fi、充电器和 MQTT 配置。

### 3.3、分立烧录并保留 NVS

需要更新 Bootloader、分区表和应用程序，同时保留现有 NVS 配置时，在 `Download Path Config` 中添加并勾选以下三行：

| 勾选 | 文件 | 地址 |
|---|---|---:|
| 是 | `cuktech_ble_gateway-1.0.0-bootloader.bin` | `0x0` |
| 是 | `cuktech_ble_gateway-1.0.0-partition-table.bin` | `0x8000` |
| 是 | `cuktech_ble_gateway-1.0.0-app.bin` | `0x10000` |

地址必须与表格完全一致。三个文件不能都填 `0x0`，也不要同时勾选 `merged.bin`。

不要点击 `ERASE`。直接点击 `START` 时，工具只写入所选文件对应的 Flash 区域，当前位于其他扇区的 NVS 配置不会被主动写入。若未来 Release 明确说明分区布局发生变化，应按该版本的升级说明处理，不能继续假设 NVS 地址兼容。

### 3.4、使用工具重新生成合并固件

Release 已提供由 ESP-IDF 生成的 `merged.bin`，通常不需要再次合并。如果确实要用 Flash Download Tool 的 `CombineBin` 生成合并文件，先按分立烧录表添加并勾选三个文件，地址保持为 `0x0`、`0x8000`、`0x10000`，同时勾选 `DoNotChgBin`，再点击 `CombineBin`。

根据乐鑫官方说明，工具会把选中固件之间的空白区域填充为 `0xFF`，输出到工具目录的 `combine/target.bin`。生成后重新配置下载列表，只保留 `target.bin` 一行并填入地址 `0x0`，然后点击 `START`。这个本地生成的合并文件同样会覆盖 NVS，不能用于保留配置升级。

### 3.5、只更新应用程序

确认新旧版本使用相同分区布局，并且发布说明没有要求更新 Bootloader 或分区表时，可以只添加：

| 勾选 | 文件 | 地址 |
|---|---|---:|
| 是 | `cuktech_ble_gateway-1.0.0-app.bin` | `0x10000` |

这种方式写入范围最小，也会保留 NVS。应用镜像接近当前 `0x100000` 字节的 factory app 分区上限，不能把应用文件写到其他地址，也不能使用其他项目的分区表。

## 4、使用 esptool 命令行

Windows 下的串口示例为 `COM5`，Linux 或 WSL 应替换成实际的 `/dev/ttyACM*` 或 `/dev/ttyUSB*`。

合并固件从 `0x0` 烧录：

```bash
esptool.py --chip esp32c3 --port COM5 --baud 460800 \
  write_flash 0x0 cuktech_ble_gateway-1.0.0-merged.bin
```

分立烧录三个文件：

```bash
esptool.py --chip esp32c3 --port COM5 --baud 460800 write_flash \
  0x0 cuktech_ble_gateway-1.0.0-bootloader.bin \
  0x8000 cuktech_ble_gateway-1.0.0-partition-table.bin \
  0x10000 cuktech_ble_gateway-1.0.0-app.bin
```

只更新应用程序：

```bash
esptool.py --chip esp32c3 --port COM5 --baud 460800 \
  write_flash 0x10000 cuktech_ble_gateway-1.0.0-app.bin
```

连接不稳定时把 `--baud 460800` 改为 `--baud 115200`。这些命令都不会主动执行整片擦除；合并固件仍会因为文件内容覆盖 NVS 所在范围而清除配置。

## 5、使用 ESP-IDF 工程烧录

从源码构建时，`idf.py flash` 会读取构建目录中的烧录参数，分别写入 Bootloader、分区表和应用程序：

```bash
. "$IDF_PATH/export.sh"
idf.py set-target esp32c3
idf.py build
idf.py -p "$PORT" flash
idf.py -p "$PORT" monitor
```

同一分区布局下，普通 `idf.py flash` 不会主动擦除 NVS。只有明确需要清空全部配置时，才执行 `idf.py -p "$PORT" erase-flash`。

## 6、烧录后的检查

Flash Download Tool 只能确认下载过程完成，不能证明应用已经正常运行。烧录后应复位设备，并通过串口工具或 `idf.py monitor` 检查启动日志。

首次启动或 NVS 被清除后，应看到设备建立 `CUKTECH-BLE-<MAC后四位>` 配网 AP。保留 NVS 升级后，设备应尝试连接原有 Wi-Fi，并继续加载原有充电器和 MQTT 配置。日志不得包含 Token、BLE Key、Wi-Fi 密码、MQTT 密码、派生密钥、IV 或 HMAC。

当前 Release 固件只完成了 CI 编译验证。没有真机烧录记录时，应明确标注为待真机验收，不能仅根据 Flash Download Tool 显示 `FINISH` 声称 BLE、MQTT 或 Home Assistant 功能已经验证通过。

## 7、常见问题

### 7.1、工具一直显示等待同步

确认 `ChipType` 为 `ESP32-C3`，COM 端口没有被其他软件占用，并按开发板说明进入下载模式。仍然失败时，将波特率降低到 `115200`，更换支持数据传输的 USB 线并重新插拔设备。

### 7.2、烧录后设备没有启动

先确认是否把三个分立固件都错误地写到了 `0x0`。正确地址依次为 `0x0`、`0x8000`、`0x10000`。使用合并固件时只能勾选一行，地址为 `0x0`。

### 7.3、升级后配置消失

如果使用了 `merged.bin`，或在 Flash Download Tool 中点击了 `ERASE`，NVS 配置被清除是预期结果。重新完成配网和管理配置即可。下次需要保留配置时使用分立固件，并避免整片擦除。
