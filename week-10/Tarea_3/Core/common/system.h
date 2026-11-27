/*
 * system.h
 * Clock setup and error trap.
 * Author: Mateo Quiceno Zapata
 */

#ifndef SYSTEM_H
#define SYSTEM_H

void SystemClock_Config(void);
void Error_Handler(void); /* stops the CPU */

#endif /* SYSTEM_H */
