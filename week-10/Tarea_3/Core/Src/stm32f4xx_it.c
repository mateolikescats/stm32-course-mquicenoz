/**
 ******************************************************************************
 * @file           : stm32f4xx_it.c
 * @author         : Mateo Quiceno Zapata
 * @brief          : System interrupt handlers.
 ******************************************************************************
 *** DESCRIPTION

 Only the core handlers live here. Peripheral handlers live with the module
 that owns the peripheral, because every handler name can be defined only
 once in the whole program:

   +----------------------------+----------------------------------+
   | Handler                    | File                             |
   +----------------------------+----------------------------------+
   | SysTick_Handler            | here                             |
   | TIM1_BRK_TIM9_IRQHandler   | Core/common/blinky.c             |
   | USART2_IRQHandler          | each stage's app.c (Stage 2 on)  |
   | DMA1_Stream5_IRQHandler    | app.c of Stages 3 and 4 (RX)     |
   | DMA1_Stream6_IRQHandler    | app.c of Stage 4 (TX)            |
   +----------------------------+----------------------------------+
 */

#include "stm32f4xx_hal.h"

//Interrupt handlers
void SysTick_Handler(void)
{
	HAL_IncTick(); //+1 ms: HAL_GetTick(), HAL_Delay() and every HAL timeout use it
}
