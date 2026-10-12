/**
 ****************************************************************************************************
 * @file        esp8266.h
 * @brief       ESP8266 WiFi 模块驱动(AT 指令模式, 走 USART2: PA2-TX / PA3-RX, 115200bps)
 *              提供 AT 指令交互、WiFi 连接、TCP 透传(用于上层 MQTT 客户端)功能
 ****************************************************************************************************
 * @attention
 *
 * 引脚:
 *   ESP8266 TXD  ->  STM32 PA2 (USART2_RX)
 *   ESP8266 RXD  ->  STM32 PA3 (USART2_TX)
 *   ESP8266 VCC  ->  3.3V (峰值电流约500mA, 建议外挂 100uF 电容或独立 3.3V 电源)
 *   ESP8266 GND  ->  GND
 *
 * 说明:
 *   本驱动只使用 ESP8266 各版本 AT 固件都支持的基础指令
 *   (AT / CWMODE / CWJAP / CIFSR / CIPSTART / CIPSEND / +IPD / CLOSED),
 *   不依赖 AT+MQTT 系列指令, 因此新旧 AT 固件均兼容.
 *
 ****************************************************************************************************
 */
#ifndef __ESP8266_H
#define __ESP8266_H

#include "./SYSTEM/sys/sys.h"
#include <stdint.h>
#include <stddef.h>

/* AT 指令执行结果 */
#define ESP8266_OK        1     /* 收到 OK                      */
#define ESP8266_ERR       0     /* 收到 ERROR                   */
#define ESP8266_TIMEOUT   (-1)  /* 超时未收到应答               */

/* 驱动内部缓冲区大小 */
#define ESP8266_RESP_LEN  600   /* AT 应答缓冲区                */
#define ESP8266_DATA_LEN  1500  /* 透传数据/IPD 缓冲区          */

/**
 * @brief  初始化 ESP8266 驱动(配置 USART2 并与模块做 AT 握手)
 *         内部最多尝试多次 AT, 模块刚上电时可能未就绪
 * @retval 1-握手成功  0-失败
 */
int esp8266_init(void);

/**
 * @brief  发送一条 AT 指令并等待 OK / ERROR
 * @param  cmd         指令(不带 \r\n, 驱动自动补)
 * @param  timeout_ms  超时时间(毫秒)
 * @retval ESP8266_OK / ESP8266_ERR / ESP8266_TIMEOUT
 */
int esp8266_at(const char *cmd, uint32_t timeout_ms);

/**
 * @brief  发送一条 AT 指令并等待应答中出现指定字符串(如 "CONNECT")
 * @retval ESP8266_OK / ESP8266_ERR / ESP8266_TIMEOUT
 */
int esp8266_at_wait(const char *cmd, const char *expect, uint32_t timeout_ms);

/**
 * @brief  取最近一次 AT 应答的完整内容(未做结尾处理, 内含 \r\n)
 * @param  buf  存放缓冲区(可为 NULL, 仅取长度时)
 * @param  len  缓冲区大小
 * @retval 应答实际长度(字节)
 */
uint16_t esp8266_get_resp(char *buf, uint16_t len);

/**
 * @brief  设置 WiFi 工作模式为 STA(站点)
 * @retval ESP8266_OK / ESP8266_ERR / ESP8266_TIMEOUT
 */
int esp8266_wifi_set_sta(void);

/**
 * @brief  连接 WiFi 热点(AT+CWJAP). 返回 OK 表示连接流程已发起
 * @retval ESP8266_OK / ESP8266_ERR / ESP8266_TIMEOUT
 */
int esp8266_wifi_join(const char *ssid, const char *pwd, uint32_t timeout_ms);

/**
 * @brief  查询 STA 是否已获取 IP(AT+CIFSR, 应答中含 STAIP 即为成功)
 * @param  ip      获取到的 IP 字符串(成功时)
 * @param  ip_len  ip 缓冲区大小
 * @param  timeout_ms  超时时间
 * @retval ESP8266_OK-已获取IP  其它-未获取/失败
 */
int esp8266_wifi_get_ip(char *ip, uint16_t ip_len, uint32_t timeout_ms);

/**
 * @brief  建立 TCP 连接(AT+CIPSTART). 应答中含 "CONNECT" 即成功
 *         (已连接时模块返回 "ALREADY CONNECT", 同样视为成功)
 * @retval ESP8266_OK / ESP8266_ERR / ESP8266_TIMEOUT
 */
int esp8266_tcp_start(const char *host, uint16_t port, uint32_t timeout_ms);

/**
 * @brief  关闭 TCP 连接
 */
int esp8266_tcp_close(void);

/**
 * @brief  发送透传数据(AT+CIPSEND 后按长度发送原始字节)
 * @retval 1-数据已发出  0-失败
 */
int esp8266_send_raw(const uint8_t *data, uint16_t len);

/**
 * @brief  读取一段 +IPD 透传数据(阻塞, 直到读够或超时)
 *         内部自动解析 "+IPD,<len>:" 头; 扫描期间若检测到 "CLOSED" 置断开标志
 * @param  buf       数据缓冲区
 * @param  buf_size  缓冲区大小
 * @param  timeout_ms 超时时间
 * @retval 实际读到的数据长度(0=超时)
 */
uint16_t esp8266_read_ipd(uint8_t *buf, uint16_t buf_size, uint32_t timeout_ms);

/**
 * @brief  查询是否检测到 TCP 连接被断开(CLOSED 事件)
 * @retval 1-已断开 0-未断开
 */
int esp8266_is_closed(void);

/**
 * @brief  清除断开标志(重连调用)
 */
void esp8266_clear_closed(void);

/**
 * @brief  清空接收缓存(丢弃旧数据)
 */
void esp8266_rx_drain(void);

#endif
