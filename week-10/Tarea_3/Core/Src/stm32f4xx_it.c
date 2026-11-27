/*
 * stm32f4xx_it.c
 * Core interrupt handlers (the others live in their modules).
 * Author: Mateo Quiceno Zapata
 */

#include "stm32f4xx_hal.h"

//Interrupt handlers
void SysTick_Handler(void)
{
	HAL_IncTick(); //+1 ms for HAL_GetTick()
}
