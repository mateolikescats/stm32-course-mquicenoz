/*
 * pwm.c
 * PWM on PA1 (TIM2_CH2), 1 kHz: CCR2 = percent * 10.
 * Author: Mateo Quiceno Zapata
 */

#include "stm32f4xx_hal.h"
#include "pwm.h"
#include "system.h"

//Constants
#define PWM_TIM          TIM2 //timer of the PWM
#define PWM_CHANNEL      TIM_CHANNEL_2 //CH2 -> PA1
#define PWM_COUNTER_HZ   1000000U //1 tick = 1 us
#define PWM_PERIOD_TICKS 1000U //1000 steps, 1 kHz
#define PWM_PORT         GPIOA //port of the led
#define PWM_PIN          GPIO_PIN_1 //PA1

//Handles (only this file)
static TIM_HandleTypeDef htim_pwm;
static uint32_t pwm_duty = 0U; //last accepted duty in %
static bool pwm_started = false; //pwm_init() done

//Hardware
void pwm_init(void)
{
	GPIO_InitTypeDef gpio = {0}; //pin settings
	TIM_OC_InitTypeDef oc = {0}; //channel settings

	/*1. Clocks*/
	__HAL_RCC_GPIOA_CLK_ENABLE(); //enable GPIOA
	__HAL_RCC_TIM2_CLK_ENABLE(); //enable TIM2 (APB1)

	/*2. PA1 as TIM2_CH2*/
	gpio.Pin = PWM_PIN; //select PA1
	gpio.Mode = GPIO_MODE_AF_PP; //alternate function, push-pull
	gpio.Pull = GPIO_NOPULL; //no pull-up, no pull-down
	gpio.Speed = GPIO_SPEED_FREQ_LOW; //low speed
	gpio.Alternate = GPIO_AF1_TIM2; //AF1: TIM2
	HAL_GPIO_Init(PWM_PORT, &gpio); //Initialization

	/*3. Time base*/
	htim_pwm.Instance = PWM_TIM; //select TIM2
	htim_pwm.Init.Prescaler = (HAL_RCC_GetPCLK1Freq() / PWM_COUNTER_HZ) - 1U; //16MHz / 1MHz - 1 = 15
	htim_pwm.Init.CounterMode = TIM_COUNTERMODE_UP; //counter up
	htim_pwm.Init.Period = PWM_PERIOD_TICKS - 1U; //ARR = 999 -> 1 kHz
	htim_pwm.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1; //not used
	htim_pwm.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE; //ARR preload
	if (HAL_TIM_PWM_Init(&htim_pwm) != HAL_OK)
	{
		Error_Handler(); //checks error
	}

	/*4. Channel 2: PWM mode 1*/
	oc.OCMode = TIM_OCMODE_PWM1; //high while CNT < CCR2
	oc.Pulse = 0U; //CCR2 = 0: led off
	oc.OCPolarity = TIM_OCPOLARITY_HIGH; //active high
	oc.OCFastMode = TIM_OCFAST_DISABLE; //not used
	if (HAL_TIM_PWM_ConfigChannel(&htim_pwm, &oc, PWM_CHANNEL) != HAL_OK) //config channel
	{
		Error_Handler(); //checks error
	}

	/*5. Start, no interrupt*/
	if (HAL_TIM_PWM_Start(&htim_pwm, PWM_CHANNEL) != HAL_OK) //start PWM
	{
		Error_Handler(); //checks error
	}

	pwm_duty = 0U; //starts at 0 %
	pwm_started = true; //ready
}

//Actions
bool pwm_set_duty(uint32_t percent)
{
	if (!pwm_started)
	{
		return false; //not started in this app
	}

	__HAL_TIM_SET_COMPARE(&htim_pwm, PWM_CHANNEL, percent * (PWM_PERIOD_TICKS / 100U)); //CCR2 = percent * 10
	pwm_duty = percent; //save duty
	return true;
}

uint32_t pwm_get_duty(void)
{
	return pwm_duty; //last accepted duty
}
