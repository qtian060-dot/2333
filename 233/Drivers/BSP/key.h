#ifndef __KEY_H
#define __KEY_H

#include "../SYSTEM/sys/sys.h"
#include "../SYSTEM/delay/delay.h"
#include <stdint.h>

#define KEY0        ((GPIOC->IDR & (1 << 5)) ? 1 : 0)
#define KEY1        ((GPIOA->IDR & (1 << 15)) ? 1 : 0)
#define WK_UP       ((GPIOA->IDR & (1 << 0)) ? 1 : 0)

#define KEY0_PRES   1
#define KEY1_PRES   2
#define WKUP_PRES   3

void KEY_Init(void);
uint8_t KEY_Scan(uint8_t mode);

#endif
