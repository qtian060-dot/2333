/**
 ****************************************************************************************************
 * @file        mqtt.c
 * @brief       极简 MQTT 3.1.1 客户端(基于 ESP8266 TCP 透传)
 *
 * 报文格式说明(MQTT 3.1.1):
 *   固定头 = 1字节类型(高4位) + 剩余长度(1~4字节变长编码)
 *   CONNECT   : 0x10 ... 协议名"MQTT"(0x00 0x04 M Q T T) 级别0x04 标志字 心跳(2B)
 *               ClientID(长度+内容) 用户名(长度+内容) 密码(长度+内容)
 *   CONNACK   : 0x20 0x02 <标志> <返回码0=成功>
 *   SUBSCRIBE : 0x82 ... 报文ID(2B) topic(长度+内容) QoS(1B)
 *   SUBACK    : 0x90 ... 报文ID(2B) 授权QoS
 *   PUBLISH   : 0x30 ... topic(长度+内容) [报文ID(2B), QoS>0时] 负载
 *   PINGREQ   : 0xC0 0x00    PINGRESP: 0xD0 0x00
 *
 ****************************************************************************************************
 */
#include "./BSP/ESP8266/mqtt.h"
#include "./BSP/ESP8266/esp8266.h"
#include "./SYSTEM/delay/delay.h"
#include <string.h>
#include <stdio.h>

#define MQTT_TX_BUF_SIZE    600
#define MQTT_RX_BUF_SIZE    1024

static uint8_t s_txbuf[MQTT_TX_BUF_SIZE];

/* 接收重组缓冲(一次 IPD 数据可能包含多个报文, 也可能截断) */
static uint8_t  s_rxbuf[MQTT_RX_BUF_SIZE];
static uint16_t s_rxlen = 0;

static uint16_t s_next_pkt_id = 1;

static int      s_connack_rc = -1;    /* CONNACK 返回码, -1 未收到 */
static int      s_suback_ok  = 0;     /* 收到 SUBACK 标志 */
static int      s_ping_ok    = 0;     /* 收到 PINGRESP 标志 */

static mqtt_msg_cb_t s_msg_cb = 0;

/*================================================================================================*/
/* 报文构造                                                                                        */
/*================================================================================================*/
/**
 * @brief  写入变长剩余长度
 * @retval 新位置
 */
static uint16_t put_remlen(uint8_t *buf, uint16_t pos, uint32_t len)
{
    do
    {
        uint8_t d = (uint8_t)(len % 128);

        len /= 128;
        if(len > 0)
        {
            d |= 0x80;
        }
        buf[pos++] = d;
    } while(len > 0);

    return pos;
}

static uint16_t build_connect(const char *client_id, const char *username, const char *password,
                              uint16_t keepalive)
{
    uint16_t p = 0;
    uint16_t cid_len = (uint16_t)strlen(client_id);
    uint16_t u_len   = (uint16_t)strlen(username);
    uint16_t p_len   = (uint16_t)strlen(password);
    uint16_t payload = (uint16_t)(10 + 2 + cid_len + 2 + u_len + 2 + p_len);

    if((uint32_t)(2 + payload) > sizeof(s_txbuf))
    {
        return 0;
    }

    s_txbuf[p++] = 0x10;                          /* CONNECT */
    p = put_remlen(s_txbuf, p, payload);

    s_txbuf[p++] = 0x00; s_txbuf[p++] = 0x04;     /* 协议名长度 4 */
    s_txbuf[p++] = 'M'; s_txbuf[p++] = 'Q';
    s_txbuf[p++] = 'T'; s_txbuf[p++] = 'T';
    s_txbuf[p++] = 0x04;                          /* 协议级别 3.1.1 */
    s_txbuf[p++] = 0xC2;                          /* 干净会话|用户名|密码 */
    s_txbuf[p++] = (uint8_t)(keepalive >> 8);
    s_txbuf[p++] = (uint8_t)(keepalive & 0xFF);

    s_txbuf[p++] = (uint8_t)(cid_len >> 8); s_txbuf[p++] = (uint8_t)(cid_len & 0xFF);
    memcpy(&s_txbuf[p], client_id, cid_len);
    p = (uint16_t)(p + cid_len);

    s_txbuf[p++] = (uint8_t)(u_len >> 8); s_txbuf[p++] = (uint8_t)(u_len & 0xFF);
    memcpy(&s_txbuf[p], username, u_len);
    p = (uint16_t)(p + u_len);

    s_txbuf[p++] = (uint8_t)(p_len >> 8); s_txbuf[p++] = (uint8_t)(p_len & 0xFF);
    memcpy(&s_txbuf[p], password, p_len);
    p = (uint16_t)(p + p_len);

    return p;
}

static uint16_t build_subscribe(const char *topic, uint8_t qos)
{
    uint16_t p = 0;
    uint16_t tlen = (uint16_t)strlen(topic);
    uint16_t payload = (uint16_t)(2 + 2 + tlen + 1);   /* 报文ID(2) + topic长度(2) + topic + QoS(1) */

    if((uint32_t)(2 + payload) > sizeof(s_txbuf))
    {
        return 0;
    }

    s_txbuf[p++] = 0x82;                          /* SUBSCRIBE, 固定标志 0x02 */
    p = put_remlen(s_txbuf, p, payload);

    s_txbuf[p++] = (uint8_t)(s_next_pkt_id >> 8); /* 报文 ID */
    s_txbuf[p++] = (uint8_t)(s_next_pkt_id & 0xFF);
    s_next_pkt_id = (uint16_t)(((s_next_pkt_id + 1) & 0xFFFE) | 1);  /* 跳过 0 */

    s_txbuf[p++] = (uint8_t)(tlen >> 8);
    s_txbuf[p++] = (uint8_t)(tlen & 0xFF);
    memcpy(&s_txbuf[p], topic, tlen);
    p = (uint16_t)(p + tlen);

    s_txbuf[p++] = qos;

    return p;
}

static uint16_t build_publish(const char *topic, const uint8_t *payload, uint16_t len)
{
    uint16_t p = 0;
    uint16_t tlen = (uint16_t)strlen(topic);
    uint16_t remlen = (uint16_t)(2 + tlen + len);       /* QoS0: 无报文ID */

    if((uint32_t)(2 + 1 + remlen) > sizeof(s_txbuf))
    {
        return 0;
    }

    s_txbuf[p++] = 0x30;                          /* PUBLISH QoS0, 非保留 */
    p = put_remlen(s_txbuf, p, remlen);

    s_txbuf[p++] = (uint8_t)(tlen >> 8);
    s_txbuf[p++] = (uint8_t)(tlen & 0xFF);
    memcpy(&s_txbuf[p], topic, tlen);
    p = (uint16_t)(p + tlen);

    memcpy(&s_txbuf[p], payload, len);
    p = (uint16_t)(p + len);

    return p;
}

/*================================================================================================*/
/* 接收解析                                                                                        */
/*================================================================================================*/
/**
 * @brief  解析重组缓冲中所有完整报文
 */
static void rx_parse(void)
{
    for(;;)
    {
        uint8_t  type;
        uint8_t  flags;
        uint32_t remlen = 0;
        uint32_t mult;
        uint16_t pos;
        uint16_t consumed;

        if(s_rxlen < 2)
        {
            return;
        }

        type  = (uint8_t)(s_rxbuf[0] >> 4);
        flags = (uint8_t)(s_rxbuf[0] & 0x0F);
        (void)flags;

        /* 解码剩余长度(最多4字节) */
        pos    = 1;
        mult   = 1;
        remlen = 0;
        for(;;)
        {
            uint8_t d;

            if(pos >= s_rxlen)
            {
                return;                 /* 头部未收全 */
            }
            if(pos >= 4)
            {
                s_rxlen = 0;            /* 非法报文, 丢弃 */
                return;
            }
            d    = s_rxbuf[pos++];
            remlen += (uint32_t)(d & 0x7F) * mult;
            if(!(d & 0x80))
            {
                break;
            }
            mult *= 128;
        }

        if((uint32_t)pos + remlen > s_rxlen)
        {
            return;                     /* 报文未收全, 等更多数据 */
        }

        consumed = (uint16_t)(pos + remlen);

        switch(type)
        {
            case 2:                     /* CONNACK */
                if(remlen >= 2)
                {
                    s_connack_rc = s_rxbuf[pos + 1];
                }
                break;

            case 9:                     /* SUBACK */
                s_suback_ok = 1;
                break;

            case 13:                    /* PINGRESP */
                s_ping_ok = 1;
                break;

            case 3:                     /* 云端下发的 PUBLISH */
            {
                uint16_t tp;
                uint16_t topic_len;
                uint8_t  qos;
                char     topic[160];

                tp = pos;
                if(remlen < 2)
                {
                    break;
                }
                topic_len = (uint16_t)((s_rxbuf[tp] << 8) | s_rxbuf[tp + 1]);
                tp = (uint16_t)(tp + 2);
                qos = (uint8_t)((s_rxbuf[0] >> 1) & 0x03);

                if((uint32_t)tp + topic_len + (qos > 0 ? 2 : 0) > (uint32_t)(pos + remlen))
                {
                    break;              /* 异常报文 */
                }

                if(topic_len >= sizeof(topic))
                {
                    topic_len = sizeof(topic) - 1;
                }
                memcpy(topic, &s_rxbuf[tp], topic_len);
                topic[topic_len] = '\0';
                tp = (uint16_t)(tp + topic_len);

                if(qos > 0)
                {
                    tp = (uint16_t)(tp + 2);    /* 跳过报文ID(QoS1/2) */
                }

                if(s_msg_cb != 0)
                {
                    s_msg_cb(topic, &s_rxbuf[tp], (uint16_t)((pos + remlen) - tp));
                }
                break;
            }

            default:
                break;                  /* 其它报文忽略 */
        }

        /* 移除已处理报文 */
        s_rxlen = (uint16_t)(s_rxlen - consumed);
        if(s_rxlen > 0)
        {
            memmove(s_rxbuf, s_rxbuf + consumed, s_rxlen);
        }
    }
}

/**
 * @brief  将 IPD 数据送入重组缓冲并解析
 */
static void rx_feed(const uint8_t *data, uint16_t len)
{
    uint16_t i;

    for(i = 0; i < len; i++)
    {
        if(s_rxlen < sizeof(s_rxbuf))
        {
            s_rxbuf[s_rxlen++] = data[i];
        }
        else
        {
            s_rxlen = 0;                /* 溢出保护 */
        }
    }
    rx_parse();
}

void mqtt_clear_rx(void)
{
    s_rxlen     = 0;
    s_connack_rc = -1;
    s_suback_ok = 0;
    s_ping_ok   = 0;
}

void mqtt_set_callback(mqtt_msg_cb_t cb)
{
    s_msg_cb = cb;
}

/*================================================================================================*/
/* 对外接口                                                                                        */
/*================================================================================================*/
int mqtt_connect(const char *client_id, const char *username, const char *password,
                 uint16_t keepalive, uint32_t timeout_ms)
{
    uint16_t len = build_connect(client_id, username, password, keepalive);
    uint8_t  chunk[256];
    uint32_t t50 = 0;
    uint32_t limit = timeout_ms / 50;

    if(len == 0)
    {
        return -1;
    }

    s_connack_rc = -1;

    if(esp8266_send_raw(s_txbuf, len) == 0)
    {
        return -1;
    }

    while(t50 < limit)
    {
        uint16_t n = esp8266_read_ipd(chunk, sizeof(chunk), 50);

        if(n > 0)
        {
            rx_feed(chunk, n);
            if(s_connack_rc >= 0)
            {
                return (s_connack_rc == 0) ? 0 : -2;
            }
        }
        t50++;
    }
    return -3;
}

int mqtt_subscribe(const char *topic, uint8_t qos, uint32_t timeout_ms)
{
    uint16_t len = build_subscribe(topic, qos);
    uint8_t  chunk[256];
    uint32_t t50 = 0;
    uint32_t limit = timeout_ms / 50;

    if(len == 0)
    {
        return -1;
    }

    s_suback_ok = 0;

    if(esp8266_send_raw(s_txbuf, len) == 0)
    {
        return -1;
    }

    while(t50 < limit)
    {
        uint16_t n = esp8266_read_ipd(chunk, sizeof(chunk), 50);

        if(n > 0)
        {
            rx_feed(chunk, n);
            if(s_suback_ok)
            {
                return 0;
            }
        }
        t50++;
    }
    return -2;
}

int mqtt_publish(const char *topic, const uint8_t *payload, uint16_t len)
{
    uint16_t blen = build_publish(topic, payload, len);

    if(blen == 0)
    {
        return 0;
    }
    return esp8266_send_raw(s_txbuf, blen);
}

int mqtt_ping(uint32_t timeout_ms)
{
    static const uint8_t pingreq[2] = {0xC0, 0x00};
    uint8_t  chunk[256];
    uint32_t t50 = 0;
    uint32_t limit = timeout_ms / 50;

    s_ping_ok = 0;

    if(esp8266_send_raw(pingreq, 2) == 0)
    {
        return -1;
    }

    while(t50 < limit)
    {
        uint16_t n = esp8266_read_ipd(chunk, sizeof(chunk), 50);

        if(n > 0)
        {
            rx_feed(chunk, n);
            if(s_ping_ok)
            {
                return 0;
            }
        }
        t50++;
    }
    return -2;
}

void mqtt_yield(mqtt_msg_cb_t cb)
{
    uint8_t chunk[256];
    uint16_t n;

    if(cb != 0)
    {
        s_msg_cb = cb;
    }

    while((n = esp8266_read_ipd(chunk, sizeof(chunk), 5)) > 0)
    {
        rx_feed(chunk, n);
    }
}
