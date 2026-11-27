/*
 * blinky.h
 * System blinky: status LED on PH1 toggled by the TIM9 update interrupt.
 * No command ever turns it off: if it stops, the CPU is stuck.
 * Author: Mateo Quiceno Zapata
 */

#ifndef BLINKY_H
#define BLINKY_H

#include <stdbool.h>
#include <stdint.h>

#define BLINKY_MIN_MS 50U
#define BLINKY_MAX_MS 5000U

void blinky_Init(uint32_t toggle_ms);
bool blinky_SetPeriod(uint32_t toggle_ms); // used by the "blink" command in Stage 2
uint32_t blinky_GetPeriod(void);

#endif /* BLINKY_H */
