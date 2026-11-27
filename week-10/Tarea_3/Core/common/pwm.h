/*
 * pwm.h
 * One PWM output: TIM2 channel 2 on PA1 (Arduino A1), 1 kHz.
 * Author: Mateo Quiceno Zapata
 */

#ifndef PWM_H
#define PWM_H

#include <stdbool.h>
#include <stdint.h>

#define PWM_MAX_PERCENT 100U

void pwm_init(void);                  /* starts the output at 0 % */
bool pwm_set_duty(uint32_t percent);  /* 0..100; false if out of range or not started */
uint32_t pwm_get_duty(void);          /* last accepted duty in % */
bool pwm_is_started(void);            /* true once pwm_init() ran in this application */

#endif /* PWM_H */
