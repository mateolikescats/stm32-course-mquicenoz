/**
 ******************************************************************************
 * @file           : main.c
 * @author         : Mateo Quiceno Zapata
 * @brief          : Third homework: UART terminal commands that change the
 *                   blink period of a status LED and the PWM duty of a LED.
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
 *** DESCRIPTION

 This is the third assignment for the Workshop V course (STM32 HAL, no
 CubeMX), running on a NUCLEO-F411RE.

 The board talks with a serial terminal through USART2 (ST-LINK virtual COM
 port, 115200 8N1, no flow control). The user types a command and presses
 Enter; the board answers with one line:

   help          -> lists the commands
   info          -> uptime, blink period and PWM duty
   blink <ms>    -> changes the blinky toggle period (50 to 5000 ms)
   blink         -> shows the current blink period
   pwm <0..100>  -> changes the PWM duty of the LED on PA1 (%)
   pwm           -> shows the current duty
   echo <text>   -> answers the same words back

 The project is built in stages. main.c is the same for all of them; the APP
 selector in CMakeLists.txt (-DAPP=... in .vscode/settings.json) decides
 which Core/app_N_<name>/app.c is compiled:

   blinky    -> Stage 0: only the blinky, no UART
   polling   -> Stage 1: blocking transmit/receive, tick line every second
   interrupt -> Stage 2: one interrupt per byte, ring buffer, first commands
   dma_idle  -> Stage 3: DMA reception until the line goes idle (no commands)
   pingpong  -> Stage 4/5/6: two DMA blocks, DMA transmission, all commands

 ***HARDWARE USED

 - NUCLEO-F411RE board (on-board ST-LINK/V2-1 for flashing, debugging and the
   virtual COM port, RESET button B2 available at any time).
 - Status LED + resistor (330 ohm - 1 kohm) on PH1: the blinky.
 - LED + resistor (330 ohm - 1 kohm) on PA1 (Arduino A1): the PWM output.
 - USB cable: power, flashing and serial terminal.

 ***SOFTWARE STRUCTURE

 - Calls only go down, events from the hardware come up through the vector
   table and the HAL callbacks:

   +----------------+-------------------------------------------------+
   | Layer          | Job                                             |
   +----------------+-------------------------------------------------+
   | main.c         | fixed start-up, then app_init() + app_loop()    |
   | app.c          | transport (UART, DMA, IRQs) + line assembly     |
   | cmd_parser.c   | splits a line into a command and its arguments  |
   | commands.c     | decides what each command does                  |
   | blinky.c       | TIM9 toggles PH1                                |
   | pwm.c          | TIM2_CH2 drives PA1                             |
   | system.c       | clock tree, Error_Handler                       |
   | ST HAL / CMSIS | drivers, register names, vector table           |
   +----------------+-------------------------------------------------+

 - ISRs and HAL callbacks only move data and raise volatile flags. The
   decisions are taken in app_loop(), never inside an interrupt.
 - Every module keeps its handle static: only its own file touches its
   peripheral.
 - Blinky rule: no command can stop the blinky. If PH1 stops toggling the CPU
   is stuck (Error_Handler or a fault); if it blinks but the terminal is
   silent, only the UART path is broken.

 ***PINS MAP

 +-----------+-----------+-------------+
 |   UART    |  BLINKY   |    PWM      |
 +-----------+-----------+-------------+
 | PA2 (TX)  | PH1       | PA1 (A1)    |
 | PA3 (RX)  |           |             |
 +-----------+-----------+-------------+

 VCC = 3.3 V
 LEDs active high (LED on when the pin is HIGH), cathodes to GND

 ***PERIPHERAL MAP (STM32F411RE datasheet DS10314, RM0383)

 +---------+---------+-------------+-----+--------+------+----------+----------------------+
 | Pin     | Use     | Peripheral  | AF  | Width  | Bus  | IRQ prio | Clock enable         |
 +---------+---------+-------------+-----+--------+------+----------+----------------------+
 | PH1     | Blinky  | TIM9 (base) | -   | 16-bit | APB2 | 3        | __HAL_RCC_TIM9_CLK   |
 | PA1     | PWM LED | TIM2_CH2    | AF1 | 32-bit | APB1 | none     | __HAL_RCC_TIM2_CLK   |
 | PA2     | UART TX | USART2      | AF7 | -      | APB1 | 2        | __HAL_RCC_USART2_CLK |
 | PA3     | UART RX | USART2      | AF7 | -      | APB1 | 2        | __HAL_RCC_USART2_CLK |
 | -       | RX DMA  | DMA1 S5 C4  | -   | byte   | AHB1 | 2        | __HAL_RCC_DMA1_CLK   |
 | -       | TX DMA  | DMA1 S6 C4  | -   | byte   | AHB1 | 2        | __HAL_RCC_DMA1_CLK   |
 +---------+---------+-------------+-----+--------+------+----------+----------------------+
 Clock: HSI 16 MHz, no PLL, every bus divider at 1 -> every timer counts at 16 MHz.
 PA1 is also USART2_RTS (AF7): flow control stays off and PA1 uses AF1.
 */

#include "stm32f4xx_hal.h"
#include "system.h"
#include "blinky.h"
#include "app.h"

int main(void)
{
	HAL_Init(); //SysTick at 1 ms, NVIC priority grouping, flash prefetch
	SystemClock_Config(); //HSI 16 MHz, all dividers at 1
	blinky_Init(250U); //toggle every 250 ms = 2 Hz blink, starts before the app
	app_init(); //provided by the selected stage

	while (1)
	{
		app_loop(); //provided by the selected stage
	}
}
