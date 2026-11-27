/**
 ******************************************************************************
 * @file           : main.c
 * @author         : Mateo Quiceno Zapata
 * @brief          : Class HAL use
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2025 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */

#include <stdint.h>
#include "stm32f4xx_hal.h"
#include "stm32f4xx_hal_rcc_ex.h"
#include <stm32f4xx.h>

/* TIM3 handle — must be global so stm32f4xx_it.c can access it */
TIM_HandleTypeDef htim3;

/* Private function prototypes */
static void SystemClock_Config(void);
static void gpio_Init(void);
static void tim3_Init(void);

int main(void)
{
    HAL_Init();           // Initialize HAL
    SystemClock_Config(); // Principal timer for the program
    gpio_Init();          // Output for the blinky
    tim3_Init();          // Timer for blinky
    while (1)
    {
        /* application loop — LED toggling happens in the callback */
    }
}

static void SystemClock_Config(void) {
    RCC_OscInitTypeDef RCC_OscInitStruc = {0};
    RCC_ClkInitDefine 
}

static void gpio_Init(void) {
    // Encender señal de reloj
    __HAL_RCC_GPIO_CLK_ENABLE();

    //Configuracion del PIN
    GPIO_InitTypeDef gpio_pinA5 ={0};

    gpio_pinA5.Pin = GPIO_PIN_5;
    gpio_pinA5.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_pinA5.Pull = GPIO_NOPULL;
    gpio_pinA5.Speed = GPIO_SPEED_FREQ_HIGH;
    //gpio_pinA5.Alternate = GPIO_AF0_MCO

    HAL_GPIO_Init(GPIOA,&gpio_pinA5);
}

static void tim3_Init(void) {
    // Encendemos la señal de reloj
    __HAL_RCC_TIM3_CLK_ENABLE()

    
}