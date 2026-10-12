/**
 ****************************************************************************************************
 * @file        esp8266.c
 * @brief       ESP8266 WiFi 模块驱动(AT 指令模式, USART2: PA2-TX / PA3-RX, 115200bps)
 *
 * 设计说明:
 *   - 采用轮询方式收发(本项目无 RTOS, 主循环周期约 10ms)
 *   - AT 指令交互: 发送后循环读串口, 以 "OK\r\n" / "ERROR\r\n" 为应答结束标志
 *   - 透传数据: 解析 "+IPD,<len>:" 头, 再按长度收取数据
 *   - 检测 "CLOSED" 事件, 通知上层 TCP 连接已断开
 *
 ****************************************************************************************************
 */
#include "./BSP/ESP8266/esp8266.h"
#include "./SYSTEM/delay/delay.h"
#include <string.h>
#include <stdio.h>

/*================================================================================================*/
/* 底层串口访问. 主机仿真测试(ESP8266_HOST_TEST)下重定向到模拟串口                                  */
/*================================================================================================*/
#ifdef ESP8266_HOST_TEST
#include "host_uart.h"
#define ESP_UART_RXNE()   host_uart_rxne()
#define ESP_UART_READ()   (uint8_t)host_uart_read()
#define ESP_UART_WRITE(c) host_uart_write((uint8_t)(c))
#else
#define ESP_UART_RXNE()   ((USART2->SR & (1 << 5)) != 0)     /* RXNE: 接收数据寄存器非空       */
#define ESP_UART_READ()   ((uint8_t)(USART2->DR & 0xFF))     /* 读 DR = 接收数据              */
#define ESP_UART_WRITE(c) do{ while((USART2->SR & (1 << 7)) == 0); USART2->DR = (uint8_t)(c); }while(0)
#endif

/*================================================================================================*/
/* 内部变量                                                                                        */
/*================================================================================================*/
static uint8_t  s_resp[ESP8266_RESP_LEN];         /* 最近一次 AT 应答缓存             */
static uint16_t s_resp_len = 0;                   /* 应答缓存长度                     */

static uint8_t  s_ipd_win[ESP8266_DATA_LEN + 16]; /* +IPD 扫描窗口                    */
static uint16_t s_ipd_win_len = 0;                /* 窗口内字节数                     */
static int      s_tcp_closed = 0;                 /* 检测到 CLOSED 事件               */

/*================================================================================================*/
/* 底层收发                                                                                        */
/*================================================================================================*/
static void esp8266_tx_str(const char *str)
{
    while(*str)
    {
        ESP_UART_WRITE(*str);
        str++;
    }
}

/**
 * @brief  从串口 FIFO 非阻塞读数据
 * @retval 本次读到的字节数
 */
static uint16_t esp8266_rx_fill(uint8_t *buf, uint16_t buf_size)
{
    uint16_t n = 0;

    while(n < buf_size)
    {
        if(ESP_UART_RXNE())
        {
            buf[n++] = ESP_UART_READ();
        }
        else
        {
            break;
        }
    }
    return n;
}

void esp8266_rx_drain(void)
{
    uint8_t tmp[256];

    while(esp8266_rx_fill(tmp, sizeof(tmp)) > 0);
}

/*================================================================================================*/
/* AT 指令交互                                                                                     */
/*================================================================================================*/
/**
 * @brief  等待 "OK\r\n" 或 "ERROR\r\n"
 * @retval ESP8266_OK / ESP8266_ERR / ESP8266_TIMEOUT
 */
static int esp8266_wait_ok(uint32_t timeout_ms)
{
    uint8_t  ch;
    uint32_t t100us = 0;
    uint32_t limit  = timeout_ms * 10;   /* 每轮循环约 100~150us, 按 100us 估算 */

    while(t100us < limit)
    {
        while(esp8266_rx_fill(&ch, 1) > 0)
        {
            if(s_resp_len < sizeof(s_resp) - 1)
            {
                s_resp[s_resp_len++] = ch;
                s_resp[s_resp_len]   = '\0';
            }

            /* 应答结束标志检查(行尾) */
            if(s_resp_len >= 4 &&
               s_resp[s_resp_len - 4] == 'O' && s_resp[s_resp_len - 3] == 'K' &&
               s_resp[s_resp_len - 2] == '\r' && s_resp[s_resp_len - 1] == '\n')
            {
                delay_us(500);            /* 排空剩余 FIFO */
                esp8266_rx_drain();
                return ESP8266_OK;
            }
            if(s_resp_len >= 7 &&
               strcmp((const char *)(s_resp + s_resp_len - 7), "ERROR\r\n") == 0)
            {
                delay_us(500);
                esp8266_rx_drain();
                return ESP8266_ERR;
            }
        }

        delay_us(100);
        t100us++;
    }

    if(s_resp_len < sizeof(s_resp))
    {
        s_resp[s_resp_len] = '\0';
    }
    return ESP8266_TIMEOUT;
}

/**
 * @brief  等待应答中出现指定字符串(expect), 同时监测 ERROR
 * @retval ESP8266_OK(出现 expect) / ESP8266_ERR / ESP8266_TIMEOUT
 */
static int esp8266_wait_expect(const char *expect, uint32_t timeout_ms)
{
    uint8_t  ch;
    uint32_t t100us = 0;
    uint32_t limit  = timeout_ms * 10;

    while(t100us < limit)
    {
        while(esp8266_rx_fill(&ch, 1) > 0)
        {
            if(s_resp_len < sizeof(s_resp) - 1)
            {
                s_resp[s_resp_len++] = ch;
                s_resp[s_resp_len]   = '\0';
            }

            if(strstr((const char *)s_resp, expect) != NULL)
            {
                delay_us(2000);           /* 收全后续可能的 OK 行 */
                esp8266_rx_drain();
                return ESP8266_OK;
            }
            if(s_resp_len >= 7 &&
               strcmp((const char *)(s_resp + s_resp_len - 7), "ERROR\r\n") == 0)
            {
                delay_us(500);
                esp8266_rx_drain();
                return ESP8266_ERR;
            }
        }

        delay_us(100);
        t100us++;
    }

    if(s_resp_len < sizeof(s_resp))
    {
        s_resp[s_resp_len] = '\0';
    }
    return ESP8266_TIMEOUT;
}

int esp8266_at(const char *cmd, uint32_t timeout_ms)
{
    esp8266_rx_drain();

    s_resp_len = 0;
    s_resp[0]  = '\0';

    esp8266_tx_str(cmd);
    esp8266_tx_str("\r\n");

    return esp8266_wait_ok(timeout_ms);
}

int esp8266_at_wait(const char *cmd, const char *expect, uint32_t timeout_ms)
{
    esp8266_rx_drain();

    s_resp_len = 0;
    s_resp[0]  = '\0';

    esp8266_tx_str(cmd);
    esp8266_tx_str("\r\n");

    return esp8266_wait_expect(expect, timeout_ms);
}

uint16_t esp8266_get_resp(char *buf, uint16_t len)
{
    uint16_t n = s_resp_len;

    if(buf != NULL && len > 0)
    {
        if(n >= len)
        {
            n = len - 1;
        }
        memcpy(buf, s_resp, n);
        buf[n] = '\0';
    }
    return n;
}

/*================================================================================================*/
/* WiFi 功能                                                                                       */
/*================================================================================================*/
int esp8266_init(void)
{
    int i;
    int ok = 0;

#ifndef ESP8266_HOST_TEST
    /* 时钟使能: GPIOA + USART2(APB1) */
    RCC->APB2ENR |= (1 << 2);      /* GPIOA 时钟   */
    RCC->APB1ENR |= (1 << 17);     /* USART2 时钟  */

    /* PA2 = USART2_TX: 复用推挽输出, 50MHz */
    GPIOA->CRL &= ~(0x0FUL << 8);
    GPIOA->CRL |=  (0x0BL  << 8);

    /* PA3 = USART2_RX: 浮空输入 */
    GPIOA->CRL &= ~(0x0FUL << 12);
    GPIOA->CRL |=  (0x08L  << 12);

    /* 波特率: PCLK1 = 36MHz, 115200 -> BRR = (36000000+57600)/115200 = 313 */
    USART2->BRR = (36000000UL + 115200UL / 2) / 115200UL;

    USART2->CR1 = 0;
    USART2->CR1 |= (1 << 3);       /* TE  发送使能  */
    USART2->CR1 |= (1 << 2);       /* RE  接收使能  */
    USART2->CR1 |= (1 << 13);      /* UE  外设使能  */
#endif

    /* 与模块握手: 上电后模块需要时间就绪, 多次尝试 */
    for(i = 0; i < 8; i++)
    {
        if(esp8266_at("AT", 500) == ESP8266_OK)
        {
            ok = 1;
            break;
        }
        delay_ms(300);
    }

    return ok;
}

int esp8266_wifi_set_sta(void)
{
    return esp8266_at("AT+CWMODE=1", 1000);
}

int esp8266_wifi_join(const char *ssid, const char *pwd, uint32_t timeout_ms)
{
    char cmd[128];

    /* 注意: SSID/密码若含 " 或 \ 需自行转义, 一般家庭 WiFi 不需要 */
    sprintf(cmd, "AT+CWJAP=\"%s\",\"%s\"", ssid, pwd);
    return esp8266_at(cmd, timeout_ms);
}

int esp8266_wifi_get_ip(char *ip, uint16_t ip_len, uint32_t timeout_ms)
{
    char resp[ESP8266_RESP_LEN];
    char *p;

    (void)timeout_ms;

    if(esp8266_at("AT+CIFSR", 1000) != ESP8266_OK)
    {
        return ESP8266_ERR;
    }

    esp8266_get_resp(resp, sizeof(resp));

    p = strstr(resp, "STAIP");
    if(p == NULL)
    {
        return ESP8266_ERR;         /* 尚未获取到 IP */
    }

    p = strchr(p, '"');
    if(p != NULL)
    {
        uint16_t n = 0;

        p++;
        while(*p != '\0' && *p != '"' && n < ip_len - 1)
        {
            ip[n++] = *p++;
        }
        ip[n] = '\0';
        return ESP8266_OK;
    }
    return ESP8266_ERR;
}

/*================================================================================================*/
/* TCP 透传                                                                                        */
/*================================================================================================*/
int esp8266_tcp_start(const char *host, uint16_t port, uint32_t timeout_ms)
{
    char cmd[96];

    /* 关闭多连接与透传模式, 保证 "+IPD,len:" 数据格式 */
    esp8266_at("AT+CIPMUX=0", 500);
    esp8266_at("AT+CIPMODE=0", 500);

    sprintf(cmd, "AT+CIPSTART=\"TCP\",\"%s\",%d", host, port);
    return esp8266_at_wait(cmd, "CONNECT", timeout_ms);
}

int esp8266_tcp_close(void)
{
    int r = esp8266_at("AT+CIPCLOSE", 1000);

    if(r == ESP8266_ERR)
    {
        r = esp8266_at("AT+CIPCLOSE=0", 1000);
    }
    return (r == ESP8266_OK) ? 1 : 0;
}

int esp8266_send_raw(const uint8_t *data, uint16_t len)
{
    char cmd[32];
    int  r;

    if(data == NULL || len == 0)
    {
        return 0;
    }

    /* 模块接收数据上限保护 */
    if(len > 1024)
    {
        return 0;
    }

    sprintf(cmd, "AT+CIPSEND=%d", len);

    /* CIPSEND 应答 "> " 提示符后进入数据发送状态 */
    r = esp8266_at_wait(cmd, "> ", 2000);
    if(r != ESP8266_OK)
    {
        return 0;
    }

    delay_ms(10);                   /* 等模块准备好接收数据 */

    for(; len > 0; len--)
    {
        ESP_UART_WRITE(*data++);
    }

    /* 给模块一点时间处理并应答 OK */
    delay_ms(20);
    esp8266_rx_drain();

    return 1;
}

/*================================================================================================*/
/* +IPD 数据接收                                                                                   */
/*================================================================================================*/
int esp8266_is_closed(void)
{
    return s_tcp_closed;
}

void esp8266_clear_closed(void)
{
    s_tcp_closed = 0;
    s_ipd_win_len = 0;
}

/**
 * @brief  向扫描窗口追加数据, 并检测 CLOSED 事件
 */
static void ipd_win_append(const uint8_t *data, uint16_t len)
{
    uint16_t i;

    for(i = 0; i < len; i++)
    {
        if(s_ipd_win_len < sizeof(s_ipd_win))
        {
            s_ipd_win[s_ipd_win_len++] = data[i];
        }
        else
        {
            s_ipd_win_len = 0;      /* 窗口溢出, 重置 */
        }
    }

    if(strstr((const char *)s_ipd_win, "CLOSED") != NULL)
    {
        s_tcp_closed  = 1;
        s_ipd_win_len = 0;
    }
}

/**
 * @brief  保留窗口尾部"可能是 +IPD, 前缀"的最长后缀(防止切断正在接收的头部)
 */
static uint16_t ipd_keep_prefix(void)
{
    static const char pat[6] = "+IPD,";
    int k;
    int j;

    for(k = 4; k >= 0; k--)
    {
        int ok = 1;

        if(s_ipd_win_len < (uint16_t)k)
        {
            continue;
        }
        for(j = 0; j < k; j++)
        {
            if(s_ipd_win[s_ipd_win_len - k + j] != pat[j])
            {
                ok = 0;
                break;
            }
        }
        if(ok)
        {
            return (uint16_t)k;
        }
    }
    return 0;
}

/**
 * @brief  在窗口中查找完整的 "+IPD,<len>:" 头
 * @retval 1-找到(输出 len 与数据起始偏移)  0-头部数字未收全  -1-无头(需丢弃窗口)
 */
static int ipd_find_head(uint16_t *out_len, uint16_t *out_off)
{
    char *p = strstr((const char *)s_ipd_win, "+IPD,");
    char *c;
    uint16_t dlen = 0;

    if(p == NULL)
    {
        return -1;
    }

    p += 5;                          /* 跳过 "+IPD," */
    while(*p >= '0' && *p <= '9')
    {
        dlen = (uint16_t)(dlen * 10 + (uint16_t)(*p - '0'));
        p++;
    }

    c = p;                           /* 此时应指向 ':' */
    if(*c != ':')
    {
        return 0;                    /* 长度数字未收全 */
    }
    c++;

    *out_len = dlen;
    *out_off = (uint16_t)(c - (const char *)s_ipd_win);
    return 1;
}

uint16_t esp8266_read_ipd(uint8_t *buf, uint16_t buf_size, uint32_t timeout_ms)
{
    uint16_t need = 0;               /* 还需收到的 IPD 数据字节数 */
    uint16_t base = 0;               /* 本次 IPD 数据在窗口中的起始偏移 */
    uint32_t t100us = 0;
    uint32_t limit  = timeout_ms * 10;
    uint8_t  tmp[256];

    if(buf == NULL || buf_size == 0)
    {
        return 0;
    }

    while(t100us < limit)
    {
        /* 本次 IPD 数据已收全? */
        if(need > 0 && s_ipd_win_len >= base + need)
        {
            uint16_t rest;

            memcpy(buf, s_ipd_win + base, need);
            rest = s_ipd_win_len - (base + need);
            if(rest > 0)
            {
                memmove(s_ipd_win, s_ipd_win + base + need, rest);
            }
            s_ipd_win_len = rest;
            return need;
        }

        {
            uint16_t n = esp8266_rx_fill(tmp, sizeof(tmp));

            if(n > 0)
            {
                t100us = 0;          /* 有数据到达, 重置空闲计时 */
                ipd_win_append(tmp, n);

                if(need == 0)
                {
                    /* 尝试解析 IPD 头 */
                    int r = ipd_find_head(&need, &base);

                    if(r == 1)
                    {
                        if(need == 0)
                        {
                            /* 空数据, 丢弃头部 */
                            {
                                uint16_t rest = s_ipd_win_len - base;
                                if(rest > 0)
                                {
                                    memmove(s_ipd_win, s_ipd_win + base, rest);
                                }
                                s_ipd_win_len = rest;
                            }
                        }
                        else if(need > buf_size)
                        {
                            /* 超过缓冲区, 丢弃整包 */
                            need = 0;
                            s_ipd_win_len = 0;
                        }
                        /* 否则继续收数据 */
                    }
                    else if(r == -1)
                    {
                        /* 无完整头: 丢弃窗口, 但保留可能的部分头部前缀 */
                        uint16_t keep = ipd_keep_prefix();

                        if(keep < s_ipd_win_len)
                        {
                            memmove(s_ipd_win, s_ipd_win + (s_ipd_win_len - keep), keep);
                        }
                        s_ipd_win_len = keep;
                    }
                    /* r == 0: 头部不完整, 等待更多数据 */
                }
            }
        }

        delay_us(100);
        t100us++;
    }

    return 0;
}
