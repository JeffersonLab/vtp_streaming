#ifndef __SI5341_CFG_H
#define __SI5341_CFG_H

#define SI5341_IN_SEL_VXS         0
#define SI5341_IN_SEL_VXS_250     1
#define SI5341_IN_SEL_VXS_125     2
#define SI5341_IN_SEL_LOCAL       3

void si5341_softReset();
void si5341_hardReset();
void si5341_sync();
void si5341_selectClockSource(int src);
int si5341_configure(int src);
int si5341_Setup();
int si5341_Init(int src);
int si5341_Test();

#endif
