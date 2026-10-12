# OneNET 云平台接入指南（设备连接云平台）

本指南完成下位机（STM32F103 + 红外检测）与 OneNET 云平台的对接：

- **数据上行**：红外检测计数、报警阈值、报警状态、LED 状态 → 每 3 秒或有变化时自动上传
- **指令下行**：远程开/关声光报警、远程清零、远程修改报警阈值
- **通信方式**：ESP8266（WiFi，AT 指令）→ TCP → MQTT 3.1.1 → OneNET 物模型

```
[红外传感器] → STM32F103 ──USART2(PA2/PA3)──> [ESP8266] ──WiFi──> [OneNET 云平台]
                 ▲                                    │
                 └────── 云端指令（MQTT 订阅下发）◄─────┘
                          [网页控制台 / OneNET 控制台 / MQTT.fx]
```

---

## 1. 硬件接线（重要！）

### 1.1 红外传感器改线

原来红外信号接在 **PA2/PA3**，这两个引脚现在让位给 ESP8266 的 USART2。
请把红外传感器信号线改接如下（电源/地不变）：

| 信号   | 原引脚 | 新引脚 |
| ------ | ------ | ------ |
| OUT1   | PA2    | **PB5** |
| OUT2   | PA3    | **PB6** |

代码 `Drivers/BSP/REMOTE/remote.c` 已同步修改，无需改程序。
（PB6 同时也是开发板 IIC_SDA 引出脚，本项目未使用 I2C，不影响。）

### 1.2 ESP8266 接线

| ESP8266 引脚 | 接开发板引脚（排针丝印） | 说明 |
| ------------ | ------------------------ | ---- |
| TXD          | **USART2_RX**（PA3）     | 交叉连接 |
| RXD          | **USART2_TX**（PA2）     | 交叉连接 |
| VCC          | **3V3**                  | 3.3V 供电 |
| GND          | **GND**                  | 共地 |

> 常见 ESP-01S / ESP-12F 模块板自带 EN/RST 电路，接好 VCC/GND/TX/RX 四线即可。
> 请勿将 RST/EN 接到 MCU 的 IO 上做控制。

### 1.3 电源注意事项

- ESP8266 WiFi 发射瞬间电流可达 **300~500mA**，建议在模块 VCC 与 GND 之间加一颗 **100uF** 电解/钽电容（尽量靠近模块）。
- 若用开发板 USB 取电且模块频繁复位（表现为 `AT` 无应答、反复重启），请给 ESP8266 单独接一个 **3.3V/1A 以上** 的电源（仍要与开发板共地）。
- WiFi 热点必须是 **2.4GHz** 频段（ESP8266 不支持 5GHz）。

---

## 2. OneNET 平台配置（网页操作，约 10 分钟）

登录 <https://open.iot.10086.cn>（OneNET 物联网平台）：

### 2.1 创建应用与产品

1. 注册/登录后，**应用 → 创建应用**（选择就近地域，如北京）。
2. **产品 → 创建产品**：名称如"红外检测云控系统"，设备类型选 **其他**（自定义）。
3. 进入产品详情页 → **物模型** 选项卡 → 新增属性，按下表配置：

| 属性标识符    | 属性名称       | 数据类型 | 读写权限 | 说明                     |
| ------------- | -------------- | -------- | -------- | ------------------------ |
| `count`       | 检测计数       | int32    | 只读     | 当前红外检测计数         |
| `alarm_value` | 报警阈值       | int32    | 读写     | 报警上限值               |
| `alarm_state` | 报警状态       | int32    | 只读     | 0=正常，1=报警中         |
| `alarm_enable`| 声光报警开关   | int32    | 读写     | 1=启用，0=远程关闭报警   |
| `count_reset` | 清零命令       | int32    | 只写     | 写入 1 表示清零          |
| `led_state`   | LED状态        | int32    | 只读     | 0=灭，1=亮               |

> 数据单位、取值范围可按默认（int32 全范围）。属性名必须与上表**完全一致**（区分大小写）。

### 2.2 创建设备

1. **设备管理 → 创建设备**，设备名如 `IRDevice01`（记住它）。
2. 进入设备详情页，记录三个关键信息：

| 信息           | 位置                     |
| -------------- | ------------------------ |
| 产品ID         | 产品详情页顶部           |
| 设备名         | 设备详情页               |
| 设备接入密钥   | 设备详情页的 **accessKey** |

### 2.3 连接参数

| 参数     | 值                          |
| -------- | --------------------------- |
| Broker   | `mqtts.heclouds.com`        |
| 端口     | `1883`（TCP，非 TLS）       |
| 用户名   | 设备名                      |
| 密码     | 设备接入密钥 accessKey      |
| ClientID | 任意唯一字符串（固件已设 `STM32IR01`） |

### 2.4 物模型 Topic

| 方向     | Topic                                                |
| -------- | ---------------------------------------------------- |
| 数据上行 | `$sys/{产品ID}/{设备名}/thing/property/post`         |
| 指令下行 | `$sys/{产品ID}/{设备名}/thing/property/set`          |
| 指令回复 | `$sys/{产品ID}/{设备名}/thing/property/set_reply`    |
| 上行回复 | `$sys/{产品ID}/{设备名}/thing/property/post_reply`   |

---

## 3. 固件配置

打开 `233/User/cloud_cfg.h`，填入实际信息：

```c
#define CLOUD_WIFI_SSID       "你的WiFi名"
#define CLOUD_WIFI_PWD        "你的WiFi密码"

#define ONENET_PRODUCT_ID     "你的产品ID"
#define ONENET_DEVICE_NAME    "你的设备名"
#define ONENET_ACCESS_KEY     "你的设备接入密钥accessKey"
```

其余（broker/端口/topic）已按 OneNET 物模型协议配置好，无需修改。

### 3.1 工程说明

`Projects/MDK-ARM/atk_f103.uvprojx` 已加入以下新源文件（打开工程直接编译即可，无需手动添加）：

| 文件                                   | 功能                                 |
| -------------------------------------- | ------------------------------------ |
| `Drivers/BSP/ESP8266/esp8266.c`        | ESP8266 驱动（AT 指令 + TCP 透传）   |
| `Drivers/BSP/ESP8266/mqtt.c`           | 极简 MQTT 3.1.1 客户端               |
| `User/cloud.c`                         | OneNET 物模型接入层（状态机/上下行） |

LCD 屏幕新增两行显示：

- `NET:ONLINE`（WIFI/TCP/CONN/RETRY 为连接过程状态）
- `ALM:ON/OFF`（声光报警开关状态，云端可关闭）

---

## 4. 运行与验证

### 4.1 上电串口日志（115200bps，USB 转串口）

```
[CLOUD] init done
[CLOUD] esp8266 ready
[CLOUD] wifi joined
[CLOUD] wifi ip: 192.168.x.x
[CLOUD] tcp: mqtts.heclouds.com:1883
[CLOUD] mqtt connected
[CLOUD] online
[CLOUD] post: {"id":"00000001","version":"1.0","params":{"count":{"value":0},...}}
```

看到 `[CLOUD] online` 后，数据每 3 秒（或数值变化时）自动上报一次。

### 4.2 OneNET 控制台验证

设备详情页 → 在线状态为 **在线**；**物模型 → 实时数据/历史数据** 中能看到
`count`、`alarm_value`、`alarm_state`、`alarm_enable`、`led_state`。

在设备详情页 **属性设置** 中：

- 将 `alarm_enable` 设为 `0` → 下位机蜂鸣器/LED 报警功能被远程关闭（屏幕 `ALM:OFF`）
- 将 `count_reset` 设为 `1` → 计数清零
- 将 `alarm_value` 设为新值 → 报警阈值被远程修改

### 4.3 PC 端 MQTT.fx 验证（答辩演示推荐）

Windows 下安装 MQTT.fx（或 MQTT X）：

| 项       | 值                                |
| -------- | --------------------------------- |
| Host     | `mqtts.heclouds.com`              |
| Port     | `1883`                            |
| ClientID | `PC_TEST_01`（不能与设备相同）     |
| Username | 设备名                            |
| Password | accessKey                         |

1. 连接后 Subscribe：`$sys/{产品ID}/{设备名}/thing/property/post` → 每 3 秒收到一帧 JSON。
2. Publish 到 `$sys/{产品ID}/{设备名}/thing/property/set`：

```json
{"id":"demo1","version":"1.0","params":{"alarm_enable":{"value":0}}}
```

设备收到后关闭声光报警，并回复 `set_reply`。

---

## 5. 网页控制台

见 `233/cloud/web/` 目录（实时显示 + 远程开关报警 + 清零 + 改阈值）。
配置与启动方法在该目录的 `README.md` 中。

---

## 6. 故障排查

| 现象                              | 原因与处理                                                                 |
| --------------------------------- | -------------------------------------------------------------------------- |
| 串口完全无输出                    | 开发板供电/USB 转串口问题；确认串口 115200bps                              |
| `[CLOUD] uart failed` 循环重试    | ESP8266 未收到 `AT`：查 TX/RX 是否接反、VCC/GND、波特率（模块默认 115200） |
| `cwjap failed` 循环重试           | WiFi 名称/密码错误；热点不是 2.4GHz；信号过弱                              |
| `wifi-ip failed`                  | 连上热点但没分到 IP：路由器 DHCP 问题，换热点试试                          |
| `tcp failed`                      | 网络不通（无法访问 mqtts.heclouds.com:1883）；检查路由/防火墙              |
| `mqtt failed`（认证失败）         | 用户名/密码填错；密码必须是 **accessKey**（不是平台登录密码）              |
| 平台收不到数据                    | 产品ID/设备名与 `cloud_cfg.h` 不一致；物模型属性名拼写不一致               |
| 平台设置属性设备无反应            | 该属性在物模型中必须配置为 **读写** 或 **只写**（只读属性平台不下发）      |
| 掉线后不重连                      | 正常情况 5 秒自动重连；若 ESP8266 供电不足导致反复复位，先解决电源问题     |

> 说明：驱动只使用 ESP8266 各版本 AT 固件通用的基础指令
> （AT / CWMODE / CWJAP / CIFSR / CIPSTART / CIPSEND / +IPD），
> 旧版（AT2.2.1）与新版（esp-at 2.x）固件均兼容，无需升级固件。

---

## 7. 设计要点（可写入报告）

1. **分层结构**：`esp8266.c`（驱动层）→ `mqtt.c`（协议层）→ `cloud.c`（应用层）→ `main.c`（业务），
   换云平台只需改 `cloud.c`/`cloud_cfg.h`。
2. **自研 MQTT 客户端**：不依赖模块端 AT+MQTT，固件内直接构造
   CONNECT/SUBSCRIBE/PUBLISH/PINGREQ 报文，兼容所有 AT 固件，且 JSON 载荷无转义问题。
3. **联网状态机**：上电 2 秒后开始联网（不阻塞本地按键/检测功能），
   WiFi → TCP → MQTT → 在线，任一步失败 5 秒后整体重连；在线时 20 秒 PINGREQ 保活。
4. **可靠上下行**：上行"变化即报 + 3 秒心跳"；下行指令解析后回复 `set_reply` 便于确认。
5. **软件时钟**：采样 SysTick 24 位计数器，免定时器占用即得毫秒计时（阻塞段自动钳位，只慢不超）。
