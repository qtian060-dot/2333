#include "./key.h"
#include "../SYSTEM/delay/delay.h"

/* ????? */
void KEY_Init(void)
{
RCC->APB2ENR |= 1 << 6;
RCC->APB2ENR |= 1 << 2;

```
/* PE3 PE4 ???? */
GPIOE->CRL &= ~(0XFF << 12);
GPIOE->CRL |=  (0X88 << 12);

GPIOE->ODR |= 3 << 3;

/* PA0 ???? */
GPIOA->CRL &= ~(0X0F << 0);
GPIOA->CRL |=  (0X08 << 0);
```

}

/* ???? */
uint8_t KEY_Scan(uint8_t mode)
{
static uint8_t key_up = 1;

```
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
```

}
