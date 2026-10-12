#include "../Drivers/SYSTEM/sys/sys.h"
#include "../Drivers/SYSTEM/delay/delay.h"
#include "../Drivers/BSP/LED/led.h"
#include "../Drivers/BSP/LCD/lcd.h"
#include "../Drivers/BSP/REMOTE/remote.h"
#include "../Drivers/BSP/key.h"
#include "cloud.h"
#include <stdio.h>
#include <string.h>

/* 蜂鸣器 PB8 (低电平有效) */
#define BEEP_ON()   GPIOB->BRR  = (1 << 8)
#define BEEP_OFF()  GPIOB->BSRR = (1 << 8)

/* 报警默认阈值 */
#define ALARM_DEFAULT 20

/* WK_UP按键扫描 */
uint8_t WKUP_Scan(void)
{
    if(GPIOA->IDR & (1 << 0))
    {
        delay_ms(10);

        if(GPIOA->IDR & (1 << 0))
        {
            while(GPIOA->IDR & (1 << 0));

            return 1;
        }
    }

    return 0;
}

/* 云端状态 -> 屏幕文字 */
static void Cloud_StatusText(cloud_state_t st, char *out)
{
    switch(st)
    {
        case CLOUD_ONLINE:    strcpy(out, "NET:ONLINE");  break;
        case CLOUD_WIFI:      strcpy(out, "NET:WIFI");    break;
        case CLOUD_TCP:       strcpy(out, "NET:TCP");     break;
        case CLOUD_MQTT:      strcpy(out, "NET:CONN");    break;
        case CLOUD_RECONNECT: strcpy(out, "NET:RETRY");   break;
        default:              strcpy(out, "NET:----");    break;
    }
}

int main(void)
{
    uint16_t num = 0;

    /* 报警是否已触发(同值只报一次) */
    uint8_t alarm_done = 0;

    /* 报警阈值 */
    uint16_t alarm_value = ALARM_DEFAULT;

    /* 循环变量 */
    uint16_t i;

    /* 按键值 */
    uint8_t key;

    /* 云端相关 */
    char     net_str[16];
    char     alm_str[16];
    cloud_state_t last_net  = CLOUD_OFF;
    uint8_t  last_alarm_en  = 1;
    uint16_t cmd_value;

    /* 延时初始化 */
    delay_init(72);

    led_init();

    lcd_init();

    KEY_Init();

    LCD_BL(1);

    Remote_Init();

    /******** 云端模块初始化(联网过程由主循环状态机驱动) ********/
    Cloud_Init();

    /******** 蜂鸣器 PB8 ********/
    RCC->APB2ENR |= (1 << 3);

    /* PB8 推挽输出 */
    GPIOB->CRH &= ~(0x0F << 0);
    GPIOB->CRH |=  (0x03 << 0);

    BEEP_OFF();

    /******** WK_UP -> PA0 ********/
    RCC->APB2ENR |= (1 << 2);

    GPIOA->CRL &= ~(0x0F << 0);
    GPIOA->CRL |=  (0x08 << 0);

    /******** LCD显示初始化 ********/
    lcd_clear(WHITE);

    lcd_show_string(20,0,240,16,16,"IR COUNT SYSTEM",BLACK);

    lcd_show_string(20,40,240,16,16,"COUNT:",BLUE);

    lcd_show_string(20,80,240,16,16,"ALARM:",BLUE);

    lcd_show_string(20,120,240,16,16,"NET:",BLUE);

    lcd_show_string(20,160,240,16,16,"ALM:",BLUE);

    lcd_show_num(100,40,0,5,16,RED);

    lcd_show_num(100,80,alarm_value,5,16,RED);

    Cloud_StatusText(CLOUD_OFF, net_str);
    lcd_show_string(64,120,240,16,16,net_str,RED);

    strcpy(alm_str, "ON");
    lcd_show_string(64,160,240,16,16,alm_str,RED);

    while(1)
    {
        /******** 云端: 上报数据 + 状态机服务 ********/
        Cloud_Update(num, alarm_value, (uint8_t)(num > alarm_value), (uint8_t)(num > alarm_value));

        Cloud_Service();

        /******** 云端指令处理 ********/
        {
            uint16_t cmd = Cloud_TakeCmd(&cmd_value);

            if(cmd == CLOUD_CMD_RESET)
            {
                Remote_Clear();

                num = 0;

                alarm_done = 0;

                BEEP_OFF();

                LED0(1);

                lcd_fill(100,40,220,60,WHITE);

                lcd_show_num(100,40,0,5,16,RED);

                printf("cloud: count reset\r\n");
            }
            else if(cmd == CLOUD_CMD_SET_VALUE)
            {
                alarm_value = cmd_value;

                lcd_fill(100,80,220,100,WHITE);

                lcd_show_num(100,80,alarm_value,5,16,RED);

                printf("cloud: alarm_value -> %d\r\n", (int)alarm_value);
            }
        }

        /******** 云端状态显示 ********/
        {
            cloud_state_t st = Cloud_GetState();

            if(st != last_net)
            {
                last_net = st;

                Cloud_StatusText(st, net_str);

                lcd_fill(64,120,240,136,WHITE);

                lcd_show_string(64,120,240,16,16,net_str,RED);
            }

            if(Cloud_AlarmEnabled() != last_alarm_en)
            {
                last_alarm_en = Cloud_AlarmEnabled();

                strcpy(alm_str, last_alarm_en ? "ON" : "OFF");

                lcd_fill(64,160,240,176,WHITE);

                lcd_show_string(64,160,240,16,16,alm_str,RED);
            }
        }

        /******** 按键扫描 ********/
        key = KEY_Scan(0);

        /******** KEY0 阈值+1 ********/
        if(key == KEY0_PRES)
        {
            alarm_value++;

            lcd_fill(100,80,220,100,WHITE);

            lcd_show_num(100,80,alarm_value,5,16,RED);
        }

        /******** KEY1 阈值-1 ********/
        if(key == KEY1_PRES)
        {
            if(alarm_value > 1)
            {
                alarm_value--;
            }

            lcd_fill(100,80,220,100,WHITE);

            lcd_show_num(100,80,alarm_value,5,16,RED);
        }

        /******** 红外检测 ********/
        if(Remote_Scan())
        {
            num = Remote_GetCount();

            /* 屏幕刷新 */
            lcd_fill(100,40,220,60,WHITE);

            lcd_show_num(100,40,num,5,16,RED);

            /******** LED闪烁 ********/
            LED0(0);

            /******** 未达阈值提示音(云端关闭声光报警时静音) ********/
            if((num < alarm_value) && Cloud_AlarmEnabled())
            {
                BEEP_ON();

                delay_ms(10);

                BEEP_OFF();
            }

            LED0(1);

            /******** 达到阈值报警(云端关闭声光报警时静音) ********/
            if((num == alarm_value) && (alarm_done == 0))
            {
                alarm_done = 1;

                if(Cloud_AlarmEnabled())
                {
                    for(i = 0; i < 500; i++)
                    {
                        /* WK_UP清零 */
                        if(GPIOA->IDR & (1 << 0))
                        {
                            Remote_Clear();

                            num = 0;

                            alarm_done = 0;

                            BEEP_OFF();

                            LED0(1);

                            lcd_fill(100,40,220,60,WHITE);

                            lcd_show_num(100,40,0,5,16,RED);

                            break;
                        }

                        BEEP_ON();

                        delay_us(500);

                        BEEP_OFF();

                        delay_us(500);
                    }
                }
                else
                {
                    BEEP_OFF();
                }
            }

            /******** 超过阈值提示音(云端关闭声光报警时静音) ********/
            if((num > alarm_value) && Cloud_AlarmEnabled())
            {
                BEEP_ON();

                delay_ms(10);

                BEEP_OFF();
            }
        }

        /******** WK_UP清零 ********/
        if(WKUP_Scan())
        {
            Remote_Clear();

            num = 0;

            alarm_done = 0;

            BEEP_OFF();

            LED0(1);

            lcd_fill(100,40,220,60,WHITE);

            lcd_show_num(100,40,0,5,16,RED);
        }

        delay_ms(10);
    }

}
