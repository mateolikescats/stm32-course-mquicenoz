/*
 * blinky.c
 * Status LED on PH1 toggled by TIM9 (1 tick = 0.1 ms).
 * Author: Mateo Quiceno Zapata
 */

#include "stm32f4xx_hal.h" //HAL and CMSIS
#include "blinky.h"
#include "system.h"

//Constants
#define BLINKY_TIM      TIM9 //timer of the blinky
#define BLINKY_TICK_HZ  10000U //1 tick = 0.1 ms
#define BLINKY_LED_PORT GPIOH //port of the led
#define BLINKY_LED_PIN  GPIO_PIN_1 //PH1

//Handles (only this file)
static TIM_HandleTypeDef htim_blinky;
static volatile uint32_t blinky_period_ms; //current toggle period in ms

//Hardware
void blinky_Init(uint32_t toggle_ms)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0}; //definition for initialization

	/*Clock*/
	__HAL_RCC_GPIOH_CLK_ENABLE(); //enable gpioh for PH1

	/*PH1 Blinky*/
	GPIO_InitStruct.Pin = BLINKY_LED_PIN; //select PH1
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP; //config as output
	GPIO_InitStruct.Pull = GPIO_NOPULL; //no pull-up, no pull-down
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW; //low speed
	HAL_GPIO_Init(BLINKY_LED_PORT, &GPIO_InitStruct); //Initialization

	/*TIM9 base*/
	__HAL_RCC_TIM9_CLK_ENABLE(); //enable TIM9 (APB2)

	htim_blinky.Instance = BLINKY_TIM; //select TIM9
	htim_blinky.Init.Prescaler = (HAL_RCC_GetPCLK2Freq() / BLINKY_TICK_HZ) - 1U; //16 MHz / 10 kHz - 1 = 1599
	htim_blinky.Init.CounterMode = TIM_COUNTERMODE_UP; //counter up
	htim_blinky.Init.Period = (toggle_ms * (BLINKY_TICK_HZ / 1000U)) - 1U; //10 ticks per ms, -1
	htim_blinky.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1; //not used
	htim_blinky.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE; //ARR preload
	if (HAL_TIM_Base_Init(&htim_blinky) != HAL_OK) {
		Error_Handler(); //checks error
	}

	blinky_period_ms = toggle_ms; //save period

	/*NVIC*/
	HAL_NVIC_SetPriority(TIM1_BRK_TIM9_IRQn, 3, 0); //priority 3, below USART2
	HAL_NVIC_EnableIRQ(TIM1_BRK_TIM9_IRQn); //enable TIM9 interrupt

	if (HAL_TIM_Base_Start_IT(&htim_blinky) != HAL_OK) { //start timer with interrupt
		Error_Handler(); //checks error
	}
}

//Actions
bool blinky_SetPeriod(uint32_t toggle_ms)
{
	if ((toggle_ms < BLINKY_MIN_MS) || (toggle_ms > BLINKY_MAX_MS)) {
		return false; //out of range
	}

	__HAL_TIM_SET_AUTORELOAD(&htim_blinky, (toggle_ms * (BLINKY_TICK_HZ / 1000U)) - 1U); //new ARR
	blinky_period_ms = toggle_ms; //save period

	return true;
}

uint32_t blinky_GetPeriod(void)
{
	return blinky_period_ms; //last accepted period
}

//Interrupt handlers
void TIM1_BRK_TIM9_IRQHandler(void)
{
	HAL_TIM_IRQHandler(&htim_blinky); //clears flag, calls callback
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) //called on update
{
	if (htim->Instance == BLINKY_TIM) {
		HAL_GPIO_TogglePin(BLINKY_LED_PORT, BLINKY_LED_PIN); //toggle led
	}
}
