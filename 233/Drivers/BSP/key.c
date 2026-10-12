#include "./key.h"

void KEY_Init(void)
{
/* 开启AFIO时钟 */
RCC->APB2ENR |= (1 << 0);

/* 关闭JTAG，保留SWD */
AFIO->MAPR &= ~(7 << 24);
AFIO->MAPR |=  (2 << 24);

/* GPIOA GPIOC时钟 */
RCC->APB2ENR |= (1 << 2);
RCC->APB2ENR |= (1 << 4);

/******** KEY0 -> PC5 ********/
GPIOC->CRL &= ~(0x0F << 20);
GPIOC->CRL |=  (0x08 << 20);

/* 上拉 */
GPIOC->ODR |= (1 << 5);

/******** KEY1 -> PA15 ********/
GPIOA->CRH &= ~(0xF << 28);
GPIOA->CRH |=  (0x8 << 28);

/* 上拉 */
GPIOA->ODR |= (1 << 15);

/******** WK_UP -> PA0 ********/
GPIOA->CRL &= ~(0x0F << 0);
GPIOA->CRL |=  (0x08 << 0);

}

uint8_t KEY_Scan(uint8_t mode)
{
static uint8_t key_up = 1;

if(mode)
{
    key_up = 1;
}

if(key_up &&
  (KEY0 == 0 || KEY1 == 0 || WK_UP == 1))
{
    delay_ms(10);

    key_up = 0;

    if(KEY0 == 0)
    {
        return KEY0_PRES;
    }

    if(KEY1 == 0)
    {
        return KEY1_PRES;
    }

    if(WK_UP == 1)
    {
        return WKUP_PRES;
    }
}
else if(KEY0 == 1 &&
        KEY1 == 1 &&
        WK_UP == 0)
{
    key_up = 1;
}

return 0;

}
