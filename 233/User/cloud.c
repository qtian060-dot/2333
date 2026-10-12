/**
 ****************************************************************************************************
 * @file        cloud.c
 * @brief       OneNET 云平台接入层(物模型协议)
 *
 * 联网流程(状态机, 由主循环 Cloud_Service 驱动):
 *   UART握手 -> CWMODE=1 -> CWJAP(连WiFi) -> 等待IP -> CIPSTART(TCP)
 *   -> MQTT CONNECT(CONNACK) -> SUBSCRIBE(SUBACK) -> 在线
 *   在线时: 数据有变化/心跳到点 -> 发布物模型属性; 周期 PINGREQ 保活;
 *           收到 property/set 指令 -> 解析并交给主循环处理;
 *           TCP 断开/CLOSED -> 延时 5s 后整体重连.
 *
 ****************************************************************************************************
 */
#include "../Drivers/SYSTEM/sys/sys.h"
#include "../Drivers/SYSTEM/delay/delay.h"
#include "../Drivers/BSP/ESP8266/esp8266.h"
#include "../Drivers/BSP/ESP8266/mqtt.h"
#include "cloud_cfg.h"
#include "cloud.h"
#include <stdio.h>
#include <string.h>

/*================================================================================================*/
/* 时序参数                                                                                        */
/*================================================================================================*/
#define CLD_START_DELAY_MS    2000     /* 上电 2s 后开始联网, 先让本地功能可用   */
#define CLD_CWJAP_TIMEOUT     30000    /* WiFi 连接超时                          */
#define CLD_TCP_TIMEOUT       20000    /* TCP 建连超时                           */
#define CLD_MQTT_TIMEOUT      10000    /* MQTT CONNECT/SUBSCRIBE 等待超时        */
#define CLD_PING_INTERVAL     20000    /* 心跳 PINGREQ 周期(小于 keepalive=30s)  */
#define CLD_PING_TIMEOUT      10000    /* PINGRESP 等待超时                      */
#define CLD_HEARTBEAT_MS      3000     /* 数据心跳上传周期                       */
#define CLD_RETRY_WAIT_MS     5000     /* 断线后等待重连时间                     */
#define CLD_MQTT_KEEPALIVE    30       /* MQTT 连接保活周期(秒)                  */
#define CLD_WIFI_IP_TRIES     20       /* 等待 IP 最大轮询次数(约20s)            */

/* OneNET 物模型 topic */
#define CLD_TOPIC_POST       "$sys/" ONENET_PRODUCT_ID "/" ONENET_DEVICE_NAME "/thing/property/post"
#define CLD_TOPIC_SET        "$sys/" ONENET_PRODUCT_ID "/" ONENET_DEVICE_NAME "/thing/property/set"
#define CLD_TOPIC_SET_REPLY  "$sys/" ONENET_PRODUCT_ID "/" ONENET_DEVICE_NAME "/thing/property/set_reply"

/*================================================================================================*/
/* 内部状态                                                                                        */
/*================================================================================================*/
enum
{
    CS_IDLE = 0,      /* 未开始                */
    CS_UART,          /* ESP8266 AT 握手       */
    CS_WIFI_MODE,     /* AT+CWMODE=1           */
    CS_WIFI_JOIN,     /* AT+CWJAP 连热点       */
    CS_WIFI_IP,       /* 轮询 AT+CIFSR 等 IP   */
    CS_TCP,           /* AT+CIPSTART 连 Broker */
    CS_MQTT_CONN,     /* MQTT CONNECT          */
    CS_MQTT_SUB,      /* MQTT SUBSCRIBE        */
    CS_RUN,           /* 在线                  */
    CS_RETRY          /* 等待重连              */
};

static uint8_t  s_state = CS_IDLE;

/* 简易软件时钟: 采样 SysTick(72MHz, 24位), 调用间隔小于233ms时精确 */
static uint32_t s_tick_ms       = 0;
static uint32_t s_systick_last  = 0;

static uint32_t s_state_enter   = 0;
static uint32_t s_last_publish  = 0;
static uint32_t s_last_ping     = 0;
static uint32_t s_wifi_ip_tries = 0;
static uint32_t s_post_id       = 0;

/* 当前数据(主循环通过 Cloud_Update 刷新) */
static uint16_t s_count        = 0;
static uint16_t s_alarm_value  = 0;
static uint8_t  s_alarm_state  = 0;
static uint8_t  s_led_state    = 0;

/* 上一次已上传的快照(用于变化检测) */
static uint16_t s_prev_count        = 0;
static uint16_t s_prev_alarm_value  = 0;
static uint8_t  s_prev_alarm_state  = 0;
static uint8_t  s_prev_led_state    = 0;
static uint8_t  s_prev_alarm_enable = 1;

static uint8_t  s_alarm_enable = 1;       /* 声光报警开关(云端可远程关闭) */

/* 待主循环处理的云端指令 */
static volatile uint16_t s_cmd       = CLOUD_CMD_NONE;
static volatile uint16_t s_cmd_value = 0;

/*================================================================================================*/
/* 简易 JSON 工具(适配物模型报文, 不依赖第三方库)                                                  */
/*================================================================================================*/
/**
 * @brief  从 JSON 中提取 key 对应的整数值
 *         兼容两种写法: "key":123  与  "key":{"value":123}
 * @retval 1-成功 0-未找到
 */
static int json_get_int(const char *s, const char *key, long *out)
{
    char pat[64];
    const char *p;
    const char *num;

    if(s == NULL || key == NULL || out == NULL)
    {
        return 0;
    }
    if(strlen(key) >= sizeof(pat) - 1)
    {
        return 0;
    }

    sprintf(pat, "\"%s\"", key);
    p = strstr(s, pat);
    if(p == NULL)
    {
        return 0;
    }
    p += strlen(pat);

    while(*p == ' ' || *p == '\t' || *p == ':')
    {
        p++;
    }

    if(*p == '{')
    {
        const char *obj_end = strchr(p, '}');
        const char *v       = strstr(p, "\"value\"");

        if(v == NULL || (obj_end != NULL && v >= obj_end))
        {
            return 0;
        }
        v += 7;
        while(*v == ' ' || *v == '\t' || *v == ':')
        {
            v++;
        }
        num = v;
    }
    else
    {
        num = p;
    }

    {
        long v = 0;
        int  neg = 0;

        if(*num == '-')
        {
            neg = 1;
            num++;
        }
        if(*num < '0' || *num > '9')
        {
            return 0;
        }
        while(*num >= '0' && *num <= '9')
        {
            v = v * 10 + (long)(*num - '0');
            num++;
        }
        *out = neg ? -v : v;
        return 1;
    }
}

/**
 * @brief  从 JSON 中提取 key 对应的字符串值
 * @retval 1-成功 0-未找到
 */
static int json_get_str(const char *s, const char *key, char *out, int out_len)
{
    char pat[64];
    const char *p;
    int n = 0;

    if(s == NULL || key == NULL || out == NULL || out_len <= 0)
    {
        return 0;
    }
    if(strlen(key) >= sizeof(pat) - 1)
    {
        return 0;
    }

    sprintf(pat, "\"%s\"", key);
    p = strstr(s, pat);
    if(p == NULL)
    {
        return 0;
    }
    p += strlen(pat);

    while(*p == ' ' || *p == '\t' || *p == ':')
    {
        p++;
    }
    if(*p != '"')
    {
        return 0;
    }
    p++;

    while(*p != '\0' && *p != '"' && n < out_len - 1)
    {
        out[n++] = (char)*p++;
    }
    out[n] = '\0';
    return n > 0;
}

/*================================================================================================*/
/* 时钟与状态管理                                                                                  */
/*================================================================================================*/
static void cloud_tick(void)
{
    uint32_t now   = (uint32_t)(SysTick->VAL & 0x00FFFFFFUL);
    uint32_t delta = s_systick_last - now;      /* mod 2^24 */

    /* SysTick 24位@72MHz 约 233ms 回绕一次;
     * 若两次调用间隔过长(被阻塞), 计数封顶, 时钟只慢不超 */
    if(delta > 200UL * 72UL)
    {
        delta = 200UL * 72UL;
    }
    s_tick_ms += delta / 72UL;
    s_systick_last = now;
}

static void cloud_set_state(uint8_t st)
{
    s_state       = st;
    s_state_enter = s_tick_ms;

    switch(st)
    {
        case CS_WIFI_MODE:
            s_wifi_ip_tries = 0;
            break;
        default:
            break;
    }
}

static void cloud_fail(const char *why)
{
    printf("[CLOUD] %s failed, retry in %d ms\r\n", why, CLD_RETRY_WAIT_MS);
    cloud_set_state(CS_RETRY);
}

/*================================================================================================*/
/* 云端指令处理                                                                                    */
/*================================================================================================*/
static void cloud_on_msg(const char *topic, const uint8_t *payload, uint16_t len)
{
    char buf[256];
    long v;

    if(topic == NULL || payload == NULL || len == 0 || len >= sizeof(buf))
    {
        return;
    }
    memcpy(buf, payload, len);
    buf[len] = '\0';

    if(strcmp(topic, CLD_TOPIC_SET) != 0)
    {
        return;
    }

    printf("[CLOUD] recv: %s\r\n", buf);

    /* 声光报警开关 */
    if(json_get_int(buf, "alarm_enable", &v))
    {
        s_alarm_enable = (v != 0) ? 1 : 0;
        printf("[CLOUD] alarm_enable -> %d\r\n", (int)s_alarm_enable);
    }

    /* 清零指令 */
    if(json_get_int(buf, "count_reset", &v) && v == 1)
    {
        s_cmd = CLOUD_CMD_RESET;
        printf("[CLOUD] count_reset -> 1\r\n");
    }

    /* 修改报警阈值 */
    if(json_get_int(buf, "alarm_value", &v) && v >= 1 && v <= 9999)
    {
        s_cmd_value = (uint16_t)v;
        s_cmd       = CLOUD_CMD_SET_VALUE;
        printf("[CLOUD] alarm_value -> %ld\r\n", v);
    }

    /* 回复 set_reply(便于控制台确认指令已执行) */
    {
        char id[64];

        id[0] = '\0';
        if(json_get_str(buf, "id", id, sizeof(id)))
        {
            char reply[160];
            int  r;

            r = sprintf(reply, "{\"id\":\"%s\",\"code\":200,\"message\":\"success\",\"data\":{}}", id);
            (void)mqtt_publish(CLD_TOPIC_SET_REPLY, (uint8_t *)reply, (uint16_t)r);
        }
    }
}

/*================================================================================================*/
/* 在线状态处理                                                                                    */
/*================================================================================================*/
static void cloud_publish_data(void)
{
    char payload[220];
    int  n;

    s_post_id++;
    n = sprintf(payload,
                "{\"id\":\"%08lX\",\"version\":\"1.0\",\"params\":{"
                "\"count\":{\"value\":%u},"
                "\"alarm_value\":{\"value\":%u},"
                "\"alarm_state\":{\"value\":%u},"
                "\"alarm_enable\":{\"value\":%u},"
                "\"led_state\":{\"value\":%u}}}",
                (unsigned long)s_post_id,
                (unsigned)s_count,
                (unsigned)s_alarm_value,
                (unsigned)s_alarm_state,
                (unsigned)s_alarm_enable,
                (unsigned)s_led_state);

    if(mqtt_publish(CLD_TOPIC_POST, (uint8_t *)payload, (uint16_t)n) != 0)
    {
        printf("[CLOUD] post: %s\r\n", payload);
        s_prev_count        = s_count;
        s_prev_alarm_value  = s_alarm_value;
        s_prev_alarm_state  = s_alarm_state;
        s_prev_led_state    = s_led_state;
        s_prev_alarm_enable = s_alarm_enable;
        s_last_publish      = s_tick_ms;
    }
    else
    {
        cloud_fail("publish");
    }
}

static void cloud_run(void)
{
    uint32_t now     = s_tick_ms;
    uint8_t  changed = 0;

    /* 1. 处理已到达的云端消息 */
    mqtt_yield(cloud_on_msg);

    /* 2. TCP 断开检测 */
    if(esp8266_is_closed())
    {
        printf("[CLOUD] tcp closed by peer\r\n");
        esp8266_tcp_close();
        cloud_fail("tcp-closed");
        return;
    }

    /* 3. 心跳保活 */
    if(now - s_last_ping >= CLD_PING_INTERVAL)
    {
        if(mqtt_ping(CLD_PING_TIMEOUT) == 0)
        {
            s_last_ping = now;
        }
        else
        {
            esp8266_tcp_close();
            cloud_fail("ping");
            return;
        }
    }

    /* 4. 数据上传: 数据变化或心跳到点 */
    changed = (s_count != s_prev_count) ||
              (s_alarm_value != s_prev_alarm_value) ||
              (s_alarm_state != s_prev_alarm_state) ||
              (s_led_state != s_prev_led_state) ||
              (s_alarm_enable != s_prev_alarm_enable);

    if(changed || now - s_last_publish >= CLD_HEARTBEAT_MS)
    {
        cloud_publish_data();
    }
}

/*================================================================================================*/
/* 对外接口                                                                                        */
/*================================================================================================*/
void Cloud_Init(void)
{
    s_state             = CS_IDLE;
    s_tick_ms           = 0;
    s_systick_last      = (uint32_t)(SysTick->VAL & 0x00FFFFFFUL);
    s_alarm_enable      = 1;
    s_prev_alarm_enable = 1;
    s_cmd               = CLOUD_CMD_NONE;
    s_cmd_value         = 0;

    printf("[CLOUD] init done\r\n");
}

void Cloud_Service(void)
{
    uint32_t now;

    cloud_tick();
    now = s_tick_ms;

    switch(s_state)
    {
        case CS_IDLE:
            if(now >= CLD_START_DELAY_MS)
            {
                cloud_set_state(CS_UART);
            }
            break;

        case CS_UART:
            if(esp8266_init() != 0)
            {
                printf("[CLOUD] esp8266 ready\r\n");
                cloud_set_state(CS_WIFI_MODE);
            }
            else
            {
                cloud_fail("uart");
            }
            break;

        case CS_WIFI_MODE:
            if(esp8266_wifi_set_sta() == ESP8266_OK)
            {
                cloud_set_state(CS_WIFI_JOIN);
            }
            else
            {
                cloud_fail("cwm");
            }
            break;

        case CS_WIFI_JOIN:
            if(esp8266_wifi_join(CLOUD_WIFI_SSID, CLOUD_WIFI_PWD, CLD_CWJAP_TIMEOUT) == ESP8266_OK)
            {
                printf("[CLOUD] wifi joined\r\n");
                cloud_set_state(CS_WIFI_IP);
            }
            else
            {
                cloud_fail("cwjap");
            }
            break;

        case CS_WIFI_IP:
            {
                char ip[32];

                if(esp8266_wifi_get_ip(ip, sizeof(ip), 1000) == ESP8266_OK)
                {
                    printf("[CLOUD] wifi ip: %s\r\n", ip);
                    cloud_set_state(CS_TCP);
                }
                else if(++s_wifi_ip_tries >= CLD_WIFI_IP_TRIES)
                {
                    cloud_fail("wifi-ip");
                }
                else
                {
                    delay_ms(1000);
                }
            }
            break;

        case CS_TCP:
            if(esp8266_tcp_start(ONENET_MQTT_HOST, ONENET_MQTT_PORT, CLD_TCP_TIMEOUT) == ESP8266_OK)
            {
                printf("[CLOUD] tcp: %s:%d\r\n", ONENET_MQTT_HOST, (int)ONENET_MQTT_PORT);
                mqtt_clear_rx();
                esp8266_clear_closed();
                cloud_set_state(CS_MQTT_CONN);
            }
            else
            {
                cloud_fail("tcp");
            }
            break;

        case CS_MQTT_CONN:
            {
                int r = mqtt_connect(ONENET_CLIENT_ID, ONENET_DEVICE_NAME, ONENET_ACCESS_KEY,
                                     CLD_MQTT_KEEPALIVE, CLD_MQTT_TIMEOUT);

                if(r == 0)
                {
                    printf("[CLOUD] mqtt connected\r\n");
                    cloud_set_state(CS_MQTT_SUB);
                }
                else
                {
                    printf("[CLOUD] mqtt connect err %d\r\n", r);
                    esp8266_tcp_close();
                    cloud_fail("mqtt");
                }
            }
            break;

        case CS_MQTT_SUB:
            {
                int r = mqtt_subscribe(CLD_TOPIC_SET, 0, CLD_MQTT_TIMEOUT);

                if(r == 0)
                {
                    printf("[CLOUD] online\r\n");
                    s_last_publish = 0;    /* 上线立即上传一次 */
                    s_last_ping    = now;
                    cloud_set_state(CS_RUN);
                }
                else
                {
                    esp8266_tcp_close();
                    cloud_fail("sub");
                }
            }
            break;

        case CS_RUN:
            cloud_run();
            break;

        case CS_RETRY:
            if(now - s_state_enter >= CLD_RETRY_WAIT_MS)
            {
                cloud_set_state(CS_WIFI_MODE);
            }
            break;

        default:
            s_state = CS_IDLE;
            break;
    }
}

void Cloud_Update(uint16_t count, uint16_t alarm_value, uint8_t alarm_state, uint8_t led_state)
{
    s_count       = count;
    s_alarm_value = alarm_value;
    s_alarm_state = alarm_state;
    s_led_state   = led_state;
}

cloud_state_t Cloud_GetState(void)
{
    switch(s_state)
    {
        case CS_RUN:        return CLOUD_ONLINE;
        case CS_RETRY:      return CLOUD_RECONNECT;
        case CS_MQTT_CONN:
        case CS_MQTT_SUB:   return CLOUD_MQTT;
        case CS_TCP:        return CLOUD_TCP;
        case CS_IDLE:       return CLOUD_OFF;
        default:            return CLOUD_WIFI;
    }
}

uint8_t Cloud_AlarmEnabled(void)
{
    return s_alarm_enable;
}

uint16_t Cloud_TakeCmd(uint16_t *value)
{
    uint16_t c = s_cmd;

    if(value != NULL)
    {
        *value = s_cmd_value;
    }
    s_cmd = CLOUD_CMD_NONE;
    return c;
}
