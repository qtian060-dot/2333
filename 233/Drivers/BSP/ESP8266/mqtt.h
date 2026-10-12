/**
 ****************************************************************************************************
 * @file        mqtt.h
 * @brief       极简 MQTT 3.1.1 客户端(基于 ESP8266 TCP 透传)
 *              只实现本项目需要的部分: CONNECT / SUBSCRIBE / PUBLISH(QoS0) / PINGREQ
 *              以及接收 PUBLISH / 处理 CONNACK / SUBACK / PINGRESP
 ****************************************************************************************************
 */
#ifndef __MQTT_H
#define __MQTT_H

#include "./SYSTEM/sys/sys.h"
#include <stdint.h>
#include <stddef.h>

/* 收到云端下发消息的回调: topic / payload(未加结尾0) / 长度 */
typedef void (*mqtt_msg_cb_t)(const char *topic, const uint8_t *payload, uint16_t len);

/**
 * @brief  发送 MQTT CONNECT 并等待 CONNACK(调用前 TCP 已建立)
 * @param  keepalive  心跳周期(秒), 建议 30
 * @param  timeout_ms 等待 CONNACK 超时
 * @retval 0-成功  -1-发送失败  -2-CONNACK 认证失败  -3-超时
 */
int mqtt_connect(const char *client_id, const char *username, const char *password,
                 uint16_t keepalive, uint32_t timeout_ms);

/**
 * @brief  订阅 topic(等待 SUBACK)
 * @retval 0-成功  -1-发送失败  -2-超时
 */
int mqtt_subscribe(const char *topic, uint8_t qos, uint32_t timeout_ms);

/**
 * @brief  发布消息(QoS0, 不等回执)
 * @retval 1-已发送  0-失败
 */
int mqtt_publish(const char *topic, const uint8_t *payload, uint16_t len);

/**
 * @brief  发送 PINGREQ 并等待 PINGRESP
 * @retval 0-成功  -1-发送失败  -2-超时
 */
int mqtt_ping(uint32_t timeout_ms);

/**
 * @brief  处理已到达的 MQTT 数据(非阻塞, 主循环中频繁调用)
 *         收到云端 PUBLISH 时调用 cb
 */
void mqtt_yield(mqtt_msg_cb_t cb);

/**
 * @brief  注册消息回调(也可用 mqtt_yield 每次传入)
 */
void mqtt_set_callback(mqtt_msg_cb_t cb);

/**
 * @brief  清空接收缓存(重连前调用)
 */
void mqtt_clear_rx(void);

#endif
