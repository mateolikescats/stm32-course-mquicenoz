/**
 ******************************************************************************
 * @file           : app.c (Stage 0: blinky)
 * @author         : Mateo Quiceno Zapata
 * @brief          : Only the blinky, no UART yet.
 ******************************************************************************
 *** DESCRIPTION

 main.c already started the blinky (TIM9 toggles PH1 every 250 ms from its
 interrupt), so this stage has nothing to do. It checks that the structure
 (fixed main.c + APP selector + shared modules) builds and runs.

 ***HOW TO CHECK

 - PH1 blinks at 2 Hz.
 - Error_Handler() called in app_init() freezes the LED: "CPU stuck".
 */

#include "stm32f4xx_hal.h"
#include "app.h"
#include "system.h"
#include "blinky.h"

void app_init(void)
{
	//check blinky
}

void app_loop(void)
{
	//nothing: the blinky runs from the TIM9 interrupt
}
