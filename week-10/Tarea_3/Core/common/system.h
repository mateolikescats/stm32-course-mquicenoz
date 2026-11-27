/*
 * system.h
 * Clock tree setup and the common error trap.
 * Author: Mateo Quiceno Zapata
 */

#ifndef SYSTEM_H
#define SYSTEM_H

void SystemClock_Config(void);
void Error_Handler(void); /* disables interrupts and loops forever */

#endif /* SYSTEM_H */
