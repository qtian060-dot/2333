#ifndef __KEY_H
#define __KEY_H

#include "../SYSTEM/sys/sys.h"

/* IO?? */
#define KEY0        PEin(4)
#define KEY1        PEin(3)
#define WK_UP       PAin(0)

/* ??? */
#define KEY0_PRES   1
#define KEY1_PRES   2
#define WKUP_PRES   3

void KEY_Init(void);
uint8_t KEY_Scan(uint8_t mode);

#endif
