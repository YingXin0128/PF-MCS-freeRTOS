#ifndef BSP_H
#define BSP_H

#include "main.h"

typedef enum
{
    BSP_KEY_B1 = 0,
    BSP_KEY_B2,
    BSP_KEY_B3,
    BSP_KEY_B4,
    BSP_KEY_COUNT
} BSP_Key;

void BSP_Init(void);
void BSP_Start(void);
uint16_t BSP_Pressure_ReadMillivolts(void);
uint32_t BSP_Flow_ReadHertz(void);
uint8_t BSP_Key_IsPressed(BSP_Key key);
void BSP_PWM_SetDuty(uint8_t duty_percent);
void BSP_LED_Write(uint8_t on_mask);

#endif
