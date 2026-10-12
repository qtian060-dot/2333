#ifndef __REMOTE_H
#define __REMOTE_H

#include "stm32f10x.h"

void Remote_Init(void);

uint8_t Remote_Scan(void);

uint16_t Remote_GetCount(void);

void Remote_Clear(void);

#endif