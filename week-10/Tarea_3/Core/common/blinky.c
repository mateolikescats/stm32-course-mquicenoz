/**
 ******************************************************************************
 * @file           : blinky.c
 * @author         : Mateo Quiceno Zapata
 * @brief          : System blinky: status LED on PH1 toggled by the TIM9
 *                   update interrupt.
 ******************************************************************************
 *** DESCRIPTION

 The TIM9 handle, its interrupt handler and the period-elapsed callback all
 live here, so no other file touches TIM9. No command ever turns it off: if
 it stops, the CPU is stuck. The "blink <ms>" command changes the period
 through blinky_SetPeriod().

 ***TIMER (TIM9, APB2, 16-bit)

   PSC = 16 MHz / 10 kHz - 1 = 1599  -> 1 tick = 0.1 ms
   ARR = toggle_ms * 10 - 1          -> 250 ms = 2499, 5000 ms = 49999 (< 65535)
   toggle period = toggle_ms, blink period = 2 * toggle_ms
 */

#include "stm32f4xx_hal.h" //pulls in every enabled HAL module and the CMSIS device header
#include "blinky.h"
#include "system.h"

//Constants
#define BLINKY_TIM      TIM9 //timer of the blinky
#define BLINKY_TICK_HZ  10000U //1 tick = 0.1ms -> 5000 ms = 50000 ticks, fits in 16-bit ARR
#define BLINKY_LED_PORT GPIOH //port of the led
#define BLINKY_LED_PIN  GPIO_PIN_1 //PH1

//Handles (static: only this file can touch it)
static TIM_HandleTypeDef htim_blinky;
static volatile uint32_t blinky_period_ms; //current toggle period in ms

//ms -> ARR value. Used by Init and SetPeriod so both always agree
static uint32_t blinky_MsToArr(uint32_t toggle_ms)
{
	return (toggle_ms * (BLINKY_TICK_HZ / 1000U)) - 1U; //10 ticks per ms, -1: counts from 0
}

//Hardware
void blinky_Init(uint32_t toggle_ms)
{
	if ((toggle_ms < BLINKY_MIN_MS) || (toggle_ms > BLINKY_MAX_MS)) {
		Error_Handler(); //bad value written in main.c: programmer error
	}

	GPIO_InitTypeDef GPIO_InitStruct = {0}; //definition for initialization

	/*Clock*/
	__HAL_RCC_GPIOH_CLK_ENABLE(); //enable gpioh for PH1

	/*PH1 Blinky*/
	GPIO_InitStruct.Pin = BLINKY_LED_PIN; //select PH1
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP; //config as output
	GPIO_InitStruct.Pull = GPIO_NOPULL; //no pull-up, no pull-down
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW; //low speed
	HAL_GPIO_Init(BLINKY_LED_PORT, &GPIO_InitStruct); //constructor of initialization

	/*TIM9 base*/
	__HAL_RCC_TIM9_CLK_ENABLE(); //enable TIM9 clock (TIM9 is on APB2)

	htim_blinky.Instance = BLINKY_TIM; //select TIM9
	htim_blinky.Init.Prescaler = (HAL_RCC_GetPCLK2Freq() / BLINKY_TICK_HZ) - 1U; //APB2 divider = 1: 16 MHz / 10 kHz - 1 = 1599
	htim_blinky.Init.CounterMode = TIM_COUNTERMODE_UP; //counter up
	htim_blinky.Init.Period = blinky_MsToArr(toggle_ms); //ARR from the requested period
	htim_blinky.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1; //secondary clock (filters/dead-time), not used
	htim_blinky.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE; //new ARR applies at next update
	if (HAL_TIM_Base_Init(&htim_blinky) != HAL_OK) {
		Error_Handler(); //checks error
	}

	blinky_period_ms = toggle_ms; //store before the interrupt starts

	/*NVIC*/
	HAL_NVIC_SetPriority(TIM1_BRK_TIM9_IRQn, 3, 0); //less urgent than USART2 (2)
	HAL_NVIC_EnableIRQ(TIM1_BRK_TIM9_IRQn); //TIM9 shares its vector with TIM1 break

	if (HAL_TIM_Base_Start_IT(&htim_blinky) != HAL_OK) { //update interrupt enable + counter begins
		Error_Handler(); //checks error
	}
}

//Actions
bool blinky_SetPeriod(uint32_t toggle_ms)
{
	if ((toggle_ms < BLINKY_MIN_MS) || (toggle_ms > BLINKY_MAX_MS)) {
		return false; //blinky must stay visible
	}

	__HAL_TIM_SET_AUTORELOAD(&htim_blinky, blinky_MsToArr(toggle_ms)); //preload: applies at the end of this period
	blinky_period_ms = toggle_ms; //keep GetPeriod in sync with the hardware

	return true;
}

uint32_t blinky_GetPeriod(void)
{
	return blinky_period_ms; //last accepted period
}

//Interrupt handlers: hardware work, no decisions
void TIM1_BRK_TIM9_IRQHandler(void)
{
	HAL_TIM_IRQHandler(&htim_blinky); //clears UIF and calls the callback
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) //only one in the whole program
{
	if (htim->Instance == BLINKY_TIM) {
		HAL_GPIO_TogglePin(BLINKY_LED_PORT, BLINKY_LED_PIN); //toggle led
	}
}
