/*
 * system.c
 * Clock setup (HSI 16 MHz, no PLL) and error trap.
 * Author: Mateo Quiceno Zapata
 */

#include "stm32f4xx_hal.h"
#include "system.h"

//Hardware
void SystemClock_Config(void)
{
	RCC_OscInitTypeDef osc = {0}; //oscillator settings
	RCC_ClkInitTypeDef clk = {0}; //bus clock settings

	/*Oscillator: HSI, no PLL*/
	osc.OscillatorType      = RCC_OSCILLATORTYPE_HSI; //configure HSI
	osc.HSIState            = RCC_HSI_ON; //16 MHz internal RC
	osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT; //factory trimming
	osc.PLL.PLLState        = RCC_PLL_NONE; //no PLL
	if (HAL_RCC_OscConfig(&osc) != HAL_OK)
	{
		Error_Handler(); //checks error
	}

	/*Buses: all at 16 MHz*/
	clk.ClockType      = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
	                     RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2; //the four clocks
	clk.SYSCLKSource   = RCC_SYSCLKSOURCE_HSI; //SYSCLK = 16 MHz
	clk.AHBCLKDivider  = RCC_SYSCLK_DIV1; //HCLK  = 16 MHz
	clk.APB1CLKDivider = RCC_HCLK_DIV1; //PCLK1 = 16 MHz (USART2, TIM2)
	clk.APB2CLKDivider = RCC_HCLK_DIV1; //PCLK2 = 16 MHz (TIM9)
	if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_0) != HAL_OK) //0 wait states for 16 MHz
	{
		Error_Handler(); //checks error
	}
}

//Error trap
void Error_Handler(void)
{
	__disable_irq(); //interrupts off, blinky freezes
	while (1)
	{
		//stuck here
	}
}
