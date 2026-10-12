# 网页控制台（OneNET）

实时显示下位机上传的红外检测数据，并支持远程下发指令：

- 实时显示：检测计数、报警阈值、报警状态、LED 状态、设备在线状态（每 3 秒刷新）
- 远程指令：**声光报警 开/关**、**一键清零**、**修改报警阈值**

## 目录结构

```
cloud/web/
├── server.js    后端代理(零第三方依赖, Node.js >= 14)
├── index.html   控制台页面
└── README.md    本文件
```

> 为什么要一个 Node 代理？OneNET 开放 API 不向浏览器直接开放跨域(CORS)，
> 浏览器无法直接调用，因此由 `server.js` 代理转发并附带 `api-key`。

## 配置

打开 `server.js`，修改文件顶部的 `ONENET_CONFIG`：

```js
apiKey:     "你的OneNET开放API-KEY",   // 控制台 -> 账户 -> 开放平台 -> 账号Token
productID:  "你的产品ID",               // 与控制台产品ID一致
deviceName: "你的设备名",               // 与控制台设备名一致
```

也可用环境变量：`ONENET_API_KEY` / `ONENET_PRODUCT_ID` / `ONENET_DEVICE_NAME`。

## 获取 API-KEY

OneNET 控制台 → 右上角 **账户** → **开放平台** → **账号 Token / API-KEY** → 新建。
（这是开放 API 的访问令牌，不是设备 accessKey，注意区分。）

## 启动

```bash
# 需要 Node.js >= 14（node -v 检查），无需 npm install
node server.js
```

浏览器打开 **http://127.0.0.1:8080** 即可。

## 说明

- 页面通过 `GET /api/props` 查询物模型属性与设备在线状态，
  通过 `POST /api/props` 下发 `alarm_enable` / `count_reset` / `alarm_value`。
- 若后端日志出现 `OneNET errCode=...`，请检查 API-KEY / 产品ID / 设备名
  是否与 OneNET 控制台一致，以及该属性在物模型中是否配置为可读写/可写。
- 控制台与设备都走 OneNET，设备离线时页面显示"离线"，指令下发会返回错误码。
