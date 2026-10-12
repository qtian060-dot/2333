#include "../Drivers/SYSTEM/sys/sys.h"
#include "../Drivers/SYSTEM/delay/delay.h"
#include "../Drivers/BSP/LED/led.h"
#include "../Drivers/BSP/LCD/lcd.h"
#include "../Drivers/BSP/REMOTE/remote.h"
#include "../Drivers/BSP/key.h"

/* ??? PB8 */
#define BEEP_ON()   GPIOB->BRR  = (1 << 8)
#define BEEP_OFF()  GPIOB->BSRR = (1 << 8)

/* ????? */
#define ALARM_DEFAULT 20

/* WK_UP?? */
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

int main(void)
{
uint16_t num = 0;

/* ?????? */
uint8_t alarm_done = 0;

/* ?????? */
uint16_t alarm_value = ALARM_DEFAULT;

/* ???? */
uint16_t i;

/* ??? */
uint8_t key;

/* ????? */
delay_init(72);

led_init();

lcd_init();

KEY_Init();

LCD_BL(1);

Remote_Init();

/******** ?????? ********/
RCC->APB2ENR |= (1 << 3);

/* PB8 ???? */
GPIOB->CRH &= ~(0x0F << 0);
GPIOB->CRH |=  (0x03 << 0);

BEEP_OFF();

/******** WK_UP -> PA0 ********/
RCC->APB2ENR |= (1 << 2);

GPIOA->CRL &= ~(0x0F << 0);
GPIOA->CRL |=  (0x08 << 0);

/******** LCD?? ********/
lcd_clear(WHITE);

lcd_show_string(20,0,240,16,16,"IR COUNT SYSTEM",BLACK);

lcd_show_string(20,40,240,16,16,"COUNT:",BLUE);

lcd_show_string(20,80,240,16,16,"ALARM:",BLUE);

lcd_show_num(100,40,0,5,16,RED);

lcd_show_num(100,80,alarm_value,5,16,RED);

while(1)
{
    /******** ???? ********/
    key = KEY_Scan(0);

    /******** KEY0 ??+1 ********/
    if(key == KEY0_PRES)
    {
        alarm_value++;

        lcd_fill(100,80,220,100,WHITE);

        lcd_show_num(100,80,alarm_value,5,16,RED);
    }

    /******** KEY1 ??-1 ********/
    if(key == KEY1_PRES)
    {
        if(alarm_value > 1)
        {
            alarm_value--;
        }

        lcd_fill(100,80,220,100,WHITE);

        lcd_show_num(100,80,alarm_value,5,16,RED);
    }

    /******** ???? ********/
    if(Remote_Scan())
    {
        num = Remote_GetCount();

        /* ?????? */
        lcd_fill(100,40,220,60,WHITE);

        lcd_show_num(100,40,num,5,16,RED);

        /******** LED?? ********/
        LED0(0);

        /******** ???? ********/
        if(num < alarm_value)
        {
            BEEP_ON();

            delay_ms(10);

            BEEP_OFF();
        }

        LED0(1);

        /******** ????? ********/
        if(num == alarm_value && alarm_done == 0)
        {
            alarm_done = 1;

            for(i = 0; i < 500; i++)
            {
                /* WK_UP???? */
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

        /******** ????? ********/
        if(num > alarm_value)
        {
            BEEP_ON();

            delay_ms(10);

            BEEP_OFF();
        }
    }

    /******** WK_UP?? ********/
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
