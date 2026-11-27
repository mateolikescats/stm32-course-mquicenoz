/*
 * blinky.h
 * Status LED on PH1 toggled by TIM9.
 * Author: Mateo Quiceno Zapata
 */

#ifndef BLINKY_H
#define BLINKY_H

#include <stdbool.h>
#include <stdint.h>

#define BLINKY_MIN_MS 50U
#define BLINKY_MAX_MS 5000U

void blinky_Init(uint32_t toggle_ms);
bool blinky_SetPeriod(uint32_t toggle_ms); // "blink" command
uint32_t blinky_GetPeriod(void);

#endif /* BLINKY_H */
