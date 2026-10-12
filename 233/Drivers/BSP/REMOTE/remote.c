#include "./BSP/REMOTE/remote.h"
#include "../../SYSTEM/sys/sys.h"

volatile uint16_t ir_cnt = 0;

/* 上一次电平 */
static uint8_t last_state1 = 1;
static uint8_t last_state2 = 1;

/*
 * 红外传感器初始化
 *
 * 引脚说明:
 *   PB5 -> OUT1 (红外通道1)
 *   PB6 -> OUT2 (红外通道2)
 *
 * 注意: 原 PA2/PA3 已让位给 USART2(ESP8266 云平台通信),
 *       红外传感器信号线请改接到 PB5 / PB6
 */
void Remote_Init(void)
{
    /* 使能GPIOB时钟 */
    RCC->APB2ENR |= (1 << 3);

    /*
        PB5 -> OUT1
        PB6 -> OUT2
    */

    /******** PB5 浮空输入 ********/
    GPIOB->CRL &= ~(0x0F << 20);
    GPIOB->CRL |=  (0x08 << 20);

    /******** PB6 浮空输入 ********/
    GPIOB->CRL &= ~(0x0F << 24);
    GPIOB->CRL |=  (0x08 << 24);

    /* 默认电平(输入模式下 ODR=1 表示内部上拉, 保持与原 PA2/PA3 配置一致) */
    GPIOB->ODR |= (1 << 5);
    GPIOB->ODR |= (1 << 6);

    ir_cnt = 0;
}

/* 红外扫描 */
uint8_t Remote_Scan(void)
{
    uint8_t now_state1;
    uint8_t now_state2;

    /* 读PB5 */
    now_state1 = (GPIOB->IDR & (1 << 5)) ? 1 : 0;

    /* 读PB6 */
    now_state2 = (GPIOB->IDR & (1 << 6)) ? 1 : 0;

    /******** OUT1 下降沿 ********/
    if((last_state1 == 1) && (now_state1 == 0))
    {
        ir_cnt++;

        last_state1 = now_state1;
        last_state2 = now_state2;

        return 1;
    }

    /******** OUT2 下降沿 ********/
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

/* 获取计数 */
uint16_t Remote_GetCount(void)
{
    return ir_cnt;
}

/* 清零 */
void Remote_Clear(void)
{
    ir_cnt = 0;
}
