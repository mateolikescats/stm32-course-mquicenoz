/**
 ******************************************************************************
 * @file           : pwm.c
 * @author         : Mateo Quiceno Zapata
 * @brief          : One PWM output: TIM2 channel 2 on PA1 (Arduino A1), 1 kHz.
 ******************************************************************************
 *** DESCRIPTION

 commands.c decides what "pwm 25" means; this file is the only one that
 touches TIM2. pwm_set_duty() refuses to work if pwm_init() was never called
 in the running application (Stages 2 and 3 share commands.c but never start
 the PWM).

 ***TIMER (TIM2, APB1, 32-bit)

   PSC = 16 MHz / 1 MHz - 1 = 15   -> 1 tick = 1 us
   ARR = 1000 - 1 = 999            -> period 1 ms = 1 kHz
   CCR2 = percent * 10             -> 25 % = 250, 100 % = 1000 (> ARR: always high)

   PWM mode 1, active high: PA1 is HIGH while CNT < CCR2

        CCR2 = 250
   PA1  ___                ___
       |   |______________|   |______________   25 %
       0  250            999

 ***PIN

   PA1 -> AF1 (TIM2_CH2). AF7 on PA1 would be USART2_RTS: not used.
   No interrupt: the timer drives PA1 by itself, UART work never disturbs it.
 */

#include "stm32f4xx_hal.h"
#include "pwm.h"
#include "system.h"

//Constants
#define PWM_TIM          TIM2 //timer of the PWM
#define PWM_CHANNEL      TIM_CHANNEL_2 //CH2 -> PA1
#define PWM_COUNTER_HZ   1000000U //1 tick = 1 us
#define PWM_PERIOD_TICKS 1000U //ARR + 1: 1000 duty steps, 1 kHz
#define PWM_PORT         GPIOA //port of the led
#define PWM_PIN          GPIO_PIN_1 //PA1

//Handles (static: only this file can touch it)
static TIM_HandleTypeDef htim_pwm;
static uint32_t pwm_duty = 0U; //last accepted duty in %
static bool pwm_started = false; //pwm_init() ran in this application

//Hardware
void pwm_init(void)
{
	GPIO_InitTypeDef gpio = {0}; //pin settings
	TIM_OC_InitTypeDef oc = {0}; //channel settings

	/*1. Clocks*/
	__HAL_RCC_GPIOA_CLK_ENABLE(); //enable GPIOA (already on for USART2: harmless)
	__HAL_RCC_TIM2_CLK_ENABLE(); //enable TIM2 (APB1)

	/*2. PA1 as TIM2_CH2*/
	gpio.Pin = PWM_PIN; //select PA1
	gpio.Mode = GPIO_MODE_AF_PP; //alternate function, push-pull
	gpio.Pull = GPIO_NOPULL; //no pull-up, no pull-down
	gpio.Speed = GPIO_SPEED_FREQ_LOW; //1 kHz needs no speed
	gpio.Alternate = GPIO_AF1_TIM2; //AF1: TIM2 (not AF7 = USART2_RTS)
	HAL_GPIO_Init(PWM_PORT, &gpio); //only touches PA1, PA2/PA3 stay as they are

	/*3. Time base*/
	htim_pwm.Instance = PWM_TIM; //select TIM2
	htim_pwm.Init.Prescaler = (HAL_RCC_GetPCLK1Freq() / PWM_COUNTER_HZ) - 1U; //APB1 divider = 1: 16 MHz / 1 MHz - 1 = 15
	htim_pwm.Init.CounterMode = TIM_COUNTERMODE_UP; //counter up
	htim_pwm.Init.Period = PWM_PERIOD_TICKS - 1U; //ARR = 999 -> 1 kHz
	htim_pwm.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1; //secondary clock, not used
	htim_pwm.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE; //ARR preload
	if (HAL_TIM_PWM_Init(&htim_pwm) != HAL_OK)
	{
		Error_Handler(); //checks error
	}

	/*4. Channel 2: PWM mode 1, active high, starts at 0 %*/
	oc.OCMode = TIM_OCMODE_PWM1; //110: high while CNT < CCR2
	oc.Pulse = 0U; //CCR2 = 0: led off
	oc.OCPolarity = TIM_OCPOLARITY_HIGH; //active high: led on when the pin is HIGH
	oc.OCFastMode = TIM_OCFAST_DISABLE; //not needed for PWM
	if (HAL_TIM_PWM_ConfigChannel(&htim_pwm, &oc, PWM_CHANNEL) != HAL_OK) //also turns on the CCR2 preload
	{
		Error_Handler(); //checks error
	}

	/*5. Start: no interrupt, the timer drives PA1 by itself*/
	if (HAL_TIM_PWM_Start(&htim_pwm, PWM_CHANNEL) != HAL_OK) //CC2E + counter begins
	{
		Error_Handler(); //checks error
	}

	pwm_duty = 0U; //starts at 0 %
	pwm_started = true; //pwm_set_duty() may work now
}

//Actions
bool pwm_set_duty(uint32_t percent)
{
	if (!pwm_started || (percent > PWM_MAX_PERCENT))
	{
		return false; //not started in this app, or out of 0..100
	}

	__HAL_TIM_SET_COMPARE(&htim_pwm, PWM_CHANNEL,
	                      (percent * PWM_PERIOD_TICKS) / PWM_MAX_PERCENT); //CCR2 = percent * 10, applies at the end of this period
	pwm_duty = percent; //keep GetDuty in sync with the hardware
	return true;
}

uint32_t pwm_get_duty(void)
{
	return pwm_duty; //last accepted duty
}

bool pwm_is_started(void)
{
	return pwm_started; //true once pwm_init() ran
}
