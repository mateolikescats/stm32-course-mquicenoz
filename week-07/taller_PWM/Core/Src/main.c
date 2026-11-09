/**
 ******************************************************************************
 * @file           : main.c
 * @author         : Mateo Quiceno Zapata
 * @brief          : Workshop week 7 - PWM and Encoder
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


#include <stdint.h> //Tipos de ancho fijo (uint8_t, int32_t...) para que el tamaño de cada variable sea conocido
#include <stm32f4xx.h> 


/* Constantes */
#define COUNTS_PER_STEP	4 //El modo TI12 cuenta 4 flancos por cada paso mecánico del encoder
#define ARR_INIT		1999 //ARR inicial: (1999 + 1) x 1 us = 2000 us -> 500 Hz
#define CCR_INIT		0 //CCR1 inicial = 0
#define ARR_STEP		20 //Lo que suma o resta al ARR cada paso del encoder en MODE_ARR
#define ARR_MIN			99 //ARR mínimo: (99 + 1) x 1 us = 100 us -> 10 kHz (con ARR = 0 el contador queda bloqueado)
#define ARR_MAX			65534 //ARR máximo: periodo de unos 65 ms. TIM3 es de 16 bit y ARR + 1 (el CCR1 máximo) debe caber en 16 bit: con 65535 daría 65536, que se trunca a 0
#define CCR_STEP		10 //Lo que suma o resta al CCR1 cada paso del encoder en MODE_CCR
#define BLINK_CCR_MS	250 //PH1 cambia cada 250 ms en MODE_CCR
#define BLINK_ARR_MS	500 //PH1 cambia cada 500 ms en MODE_ARR



//Inicialización del hardware
static void gpio_Init(void); //GPIOH PH1 como salida (blinky)
static void tim4_Init(void); //TIM4 con interrupción cada 250 ms o 500 ms (tiempo del blinky)
static void tim3_pwm_Init(void); //GPIOA PA6 + TIM3 CH1 en modo PWM 1
static void tim2_encoder_Init(void); //GPIOA PA0, PA1 + TIM2 en modo encoder
static void exti0_Init(void); //GPIOB PB0 + EXTI0 (flanco de bajada)

int main(void){




    while(1){



    }
    return 0;

}

void gpio_Init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOHEN; //Reloj del GPIOH
    GPIOH->MODER |= GPIO_MODER_MODER1_0; //01: salida
    GPIOH->OSPEEDR |= GPIO_OSPEEDER_OSPEEDR1; //11: alta velocidad
    GPIOH->OTYPER &= ~GPIO_OTYPER_OT1; //0: push-pull
    GPIOH->PUPDR &= ~GPIO_PUPDR_PUPDR1; //00: sin pull-up ni pull-down


}

static void tim4_Init(void)
{
	RCC->APB1ENR |= RCC_APB1ENR_TIM4EN; //Reloj del TIM4

	TIM4->PSC = 15999; //16 MHz / 16000 = 1 kHz
	TIM4->ARR = BLINK_CCR_MS - 1; //Cuenta de 0 a 249: 250 ms (inicia en MODE_CCR)
	TIM4->CR1 |= TIM_CR1_ARPE; //ARR con preload: al cambiar de modo, el nuevo ARR entra al terminar el periodo actual

	TIM4->DIER |= TIM_DIER_UIE; //Habilita la interrupción por actualización (primer interruptor)
	__NVIC_EnableIRQ(TIM4_IRQn); //Habilita la interrupción en el NVIC (segundo interruptor)

	TIM4->CR1 |= TIM_CR1_CEN; //Arranca el contador, siempre de último
}

void tim3_pwm_Init(void){
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN; //Reloj del GPIOA
    GPIOA->MODER &= ~(GPIO_MODER_MODER6); //Limpia los bits de moder
    GPIOA->MODER |= (GPIO_MODER_MODER6_1); //10: Función alternativa

    GPIOA->AFR[0] |= GPIO_AFRL_AFSEL6_1; //AF2 (los pines 0 a 7 están en AFR[0])

    RCC->APB1ENR |= RCC_APB1ENR_TIM3EN; //Reloj del TIM3

    TIM3->PSC = 15; //Tick de 1 us
	TIM3->ARR = ARR_INIT; //Periodo: (1999 + 1) us
	TIM3->CCR1 = CCR_INIT; //Ancho del pulso
	TIM3->CR1 |= TIM_CR1_ARPE; //ARR con preload: el cambio de periodo no corta pulsos

    TIM3->CCMR1 |= (TIM_CCMR1_OC1M_1 | TIM_CCMR1_OC1M_2); //110: PWM modo 1
    TIM3->CCMR1 |= TIM_CCMR1_OC1PE; //CCR1 con preload
    TIM3->CCER |= TIM_CCER_CC1E; //Habilita la salida del canal 1 al pin

    TIM3->CR1 |= TIM_CR1_CEN; //Arranca el contador, siempre de último
} 

void tim2_encoder_Init(void){

    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN; //Reloj del GPIOA

    GPIOA->MODER |= (GPIO_MODER_MODER0_1 | GPIO_MODER_MODER1_1); //10: función alterna
    GPIOA->AFR[0] |= (GPIO_AFRL_AFSEL0_0 | GPIO_AFRL_AFSEL1_0); //AF1

    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN; //Reloj del TIM2

    TIM2->ARR = 0xFFFF; //ARR máximo: 16 bit
    TIM2->CCMR1 |= (TIM_CCMR1_CC1S_0 | TIM_CCMR1_CC2S_0); //01: modo entrada, mapeo en TI1 y TI2
    TIM2->SMCR |= TIM_SMCR_SMS_0 | TIM_SMCR_SMS_1; //011: modo encoder, cuenta flancos de ambos canales
    TIM2->CR1 |= TIM_CR1_CEN; //Arranca el contador, siempre de último

    TIM2->SMCR |= TIM_SMCR_SMS_0 | TIM_SMCR_SMS_1; //011: modo encoder, cuenta flancos de ambos canales
    TIM2->CR1 |= TIM_CR1_CEN; //Arranca el contador
}
    