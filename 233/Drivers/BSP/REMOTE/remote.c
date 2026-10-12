#include "./BSP/REMOTE/remote.h"
#include "../../SYSTEM/sys/sys.h"

volatile uint16_t ir_cnt = 0;

/* ????? */
static uint8_t last_state1 = 1;
static uint8_t last_state2 = 1;

/* ??? */
void Remote_Init(void)
{
    /* ??GPIOA?? */
    RCC->APB2ENR |= (1 << 2);

    /*
        PA2 -> OUT1
        PA3 -> OUT2
    */

    /******** PA2 ???? ********/
    GPIOA->CRL &= ~(0x0F << 8);
    GPIOA->CRL |=  (0x08 << 8);

    /******** PA3 ???? ********/
    GPIOA->CRL &= ~(0x0F << 12);
    GPIOA->CRL |=  (0x08 << 12);

    /* ?? */
    GPIOA->ODR |= (1 << 2);
    GPIOA->ODR |= (1 << 3);

    ir_cnt = 0;
}

/* ???? */
uint8_t Remote_Scan(void)
{
    uint8_t now_state1;
    uint8_t now_state2;

    /* ??PA2 */
    now_state1 = (GPIOA->IDR & (1 << 2)) ? 1 : 0;

    /* ??PA3 */
    now_state2 = (GPIOA->IDR & (1 << 3)) ? 1 : 0;

    /******** OUT1?? ********/
    if((last_state1 == 1) && (now_state1 == 0))
    {
        ir_cnt++;

        last_state1 = now_state1;
        last_state2 = now_state2;

        return 1;
    }

    /******** OUT2?? ********/
    if((last_state2 == 1) && (now_state2 == 0))
    {
        ir_cnt++;

        last_state1 = now_state1;
        last_state2 = now_state2;

        return 1;
    }

    last_state1 = now_state1;
    last_state2 = now_state2;

    return 0;
}

/* ???? */
uint16_t Remote_GetCount(void)
{
    return ir_cnt;
}

/* ?? */
void Remote_Clear(void)
{
    ir_cnt = 0;
}
