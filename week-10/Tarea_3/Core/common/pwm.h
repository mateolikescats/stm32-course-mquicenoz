/*
 * pwm.h
 * PWM on PA1 (TIM2_CH2), 1 kHz.
 * Author: Mateo Quiceno Zapata
 */

#ifndef PWM_H
#define PWM_H

#include <stdbool.h>
#include <stdint.h>

#define PWM_MAX_PERCENT 100U

void pwm_init(void);                  /* starts the output at 0 % */
bool pwm_set_duty(uint32_t percent);  /* 0..100, false if invalid */
uint32_t pwm_get_duty(void);          /* last accepted duty in % */
bool pwm_is_started(void);            /* true after pwm_init() */

#endif /* PWM_H */
