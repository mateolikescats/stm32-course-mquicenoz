/*
 * main.c
 * Homework 3: UART commands that change the blinky period and a PWM LED.
 * NUCLEO-F411RE. PA2/PA3 UART, PH1 blinky, PA1 PWM.
 * The stage is chosen with -DAPP=... in .vscode/settings.json.
 * Author: Mateo Quiceno Zapata
 */

#include "stm32f4xx_hal.h"
#include "system.h"
#include "blinky.h"
#include "app.h"

int main(void)
{
	HAL_Init(); //init HAL, SysTick 1 ms
	SystemClock_Config(); //16 MHz clock
	blinky_Init(250U); //blinky at 250 ms
	app_init(); //stage init

	while (1)
	{
		app_loop(); //stage loop
	}
}
