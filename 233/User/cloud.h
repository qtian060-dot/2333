/**
 ****************************************************************************************************
 * @file        cloud.h
 * @brief       OneNET 云平台接入层(物模型协议)
 *
 *  数据上行: 设备 -> $sys/{产品ID}/{设备名}/thing/property/post
 *  指令下行: 设备 <- $sys/{产品ID}/{设备名}/thing/property/set
 *
 *  物模型属性(与 OneNET 控制台配置保持一致):
 *    count        int32  只读  当前红外检测计数
 *    alarm_value  int32  读写  报警阈值
 *    alarm_state  int32  只读  报警状态 0=正常 1=报警
 *    alarm_enable int32  读写  声光报警开关 1=启用 0=远程关闭
 *    count_reset  int32  只写  清零命令 写1清零
 *    led_state    int32  只读  LED状态 0=灭 1=亮
 *
 ****************************************************************************************************
 */
#ifndef __CLOUD_H
#define __CLOUD_H

#include <stdint.h>

/* 云端连接状态 */
typedef enum
{
    CLOUD_OFF = 0,      /* 未启动                          */
    CLOUD_WIFI,         /* 正在连接 WiFi                   */
    CLOUD_TCP,          /* 正在连接 MQTT Broker(TCP)       */
    CLOUD_MQTT,         /* MQTT 连接/订阅中                */
    CLOUD_ONLINE,       /* 在线, 数据收发正常              */
    CLOUD_RECONNECT     /* 连接异常, 等待重连              */
} cloud_state_t;

/* 云端下发指令码(Cloud_TakeCmd 返回) */
#define CLOUD_CMD_NONE        0     /* 无指令       */
#define CLOUD_CMD_RESET       1     /* 清零计数     */
#define CLOUD_CMD_SET_VALUE   2     /* 修改报警阈值, *value 为新值 */

/**
 * @brief  云端模块初始化(轻量, 实际联网过程由 Cloud_Service 状态机驱动)
 */
void Cloud_Init(void);

/**
 * @brief  云端服务(主循环中每次调用, 约10ms一次)
 *         内部为联网状态机: WiFi -> TCP -> MQTT -> 在线(上行/下行/心跳/重连)
 */
void Cloud_Service(void);

/**
 * @brief  上报当前数据(主循环中每次调用, 有变化或心跳到点时自动上传)
 * @param  count        当前检测计数
 * @param  alarm_value  当前报警阈值
 * @param  alarm_state  报警状态 0/1
 * @param  led_state    LED 状态 0/1
 */
void Cloud_Update(uint16_t count, uint16_t alarm_value, uint8_t alarm_state, uint8_t led_state);

/**
 * @brief  获取当前云端连接状态
 */
cloud_state_t Cloud_GetState(void);

/**
 * @brief  获取声光报警开关状态(云端可远程关闭)
 * @retval 1-声光报警启用  0-云端已关闭声光报警
 */
uint8_t Cloud_AlarmEnabled(void);

/**
 * @brief  取走一条待处理的云端指令
 * @param  value  CLOUD_CMD_SET_VALUE 时输出新阈值
 * @retval CLOUD_CMD_NONE / CLOUD_CMD_RESET / CLOUD_CMD_SET_VALUE
 */
uint16_t Cloud_TakeCmd(uint16_t *value);

#endif
