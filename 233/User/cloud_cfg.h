/**
 ****************************************************************************************************
 * @file        cloud_cfg.h
 * @brief       云平台(OneNET)接入配置 —— 使用前请修改为本机实际信息!
 ****************************************************************************************************
 * 获取方法(详见 doc/OneNET云平台接入指南.md):
 *   1. WiFi 信息: 本机路由器 SSID 与密码(必须为 2.4GHz 频段)
 *   2. OneNET 控制台(https://open.iot.10086.cn):
 *      - 产品ID       : 应用 -> 产品 -> 产品详情页
 *      - 设备名       : 产品 -> 设备管理 -> 设备详情页
 *      - 设备接入密钥 : 设备详情页中的 "accessKey"
 ****************************************************************************************************
 */
#ifndef __CLOUD_CFG_H
#define __CLOUD_CFG_H

/* ---------- WiFi 配置 ---------- */
#define CLOUD_WIFI_SSID       "YourWiFiSSID"
#define CLOUD_WIFI_PWD        "YourWiFiPassword"

/* ---------- OneNET 设备配置 ---------- */
#define ONENET_PRODUCT_ID     "YourProductID"          /* 产品ID */
#define ONENET_DEVICE_NAME    "YourDeviceName"         /* 设备名 */
#define ONENET_ACCESS_KEY     "YourDeviceAccessKey"    /* 设备接入密钥 accessKey */

/* ---------- MQTT Broker 配置(OneNET 标准值, 一般不用改) ---------- */
#define ONENET_MQTT_HOST      "mqtts.heclouds.com"
#define ONENET_MQTT_PORT      1883
#define ONENET_CLIENT_ID      "STM32IR01"              /* 客户端ID(任意唯一字符串) */

#endif
