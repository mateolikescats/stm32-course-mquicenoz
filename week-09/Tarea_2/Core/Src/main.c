/**
 ******************************************************************************
 * @file           : main.c
 * @author         : Mateo Quiceno Zapata
 * @brief          : Second homework: encoder-controlled PWM (RGB LED + DAC),
 *                   debounced mode switch and frequency capture.
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

 This is the second assignment for the Workshop V course (bare metal, CMSIS
 registers only), running on a NUCLEO-F411RE.

 A rotary encoder controls the duty cycle of four PWM signals. Three of them
 set the brightness of a common-anode RGB LED, the fourth goes to a simple DAC
 made of two RC low-pass filters in cascade plus an op-amp voltage buffer,
 giving 0 V to 3.3 V. Pressing the encoder switch (SW1) cycles through modes:

   MODE_RED   -> encoder changes only the red duty
   MODE_GREEN -> encoder changes only the green duty
   MODE_BLUE  -> encoder changes only the blue duty
   MODE_ALL   -> encoder changes the three colours at the same time
   MODE_DAC   -> encoder changes the 4th PWM channel (DAC output voltage)

 A status LED blinks every 250 ms to show that the MCU and the FSM are alive.
 An input-capture channel measures the frequency of a signal on PA0; the
 result is in capture_freq_hz (watch it in the debugger).

 ***HARDWARE USED

 - NUCLEO-F411RE board (on-board ST-LINK/V2-1 for flashing/debugging, RESET
   button B2 available at any time).
 - KY-040 rotary encoder (CLK, DT, SW).
 - Common-anode RGB LED with one current-limiting resistor per colour.
 - Status LED + resistor.
 - DAC 2 x RC low-pass in cascade + op-amp buffer on the 4th PWM channel.
 - Signal source for the capture input (function generator, or a jumper
   from one of the PWM outputs).

 ***SOFTWARE STRUCTURE

 - ISRs only do hardware work (clear flags, mask a line) and raise volatile
   flags. They never decide what the system does.
 - get_next_event() reads the flags and the encoder counter and returns ONE
   event per pass, by priority: SW press, SW confirmed, encoder, blink, capture.
   The encoder (TIM3->CNT) and the capture (TIM5 CC1IF) are hardware flags
   polled directly, so they need no ISR.
 - STATE_IDLE is the only state that reads events. Every other state runs its
   action once (state_entry_flag) and returns to IDLE.

 ***FINITE STATE MACHINE

 Event sources (ISR -> flag -> event in get_next_event, highest priority first):

   EXTI12 falling edge on PB12 ... sw_edge_flag ...... EV_SW_EDGE
   TIM10 update (20 ms later) .... sw_flag ........... EV_SWITCH_CASE
   |TIM3->CNT| >= 4 (polled) ..... (no flag) ......... EV_CCR_CHANGE
   TIM9 update (every 250 ms) .... blink_flag ........ EV_BLINK
   TIM5 CC1IF set (polled) ....... (no flag) ......... EV_CAPTURE
   nothing pending ............................................. EV_NONE

 Transition table:

 +------------------+----------------+------------------+----------------------------------+
 | Current state    | Event          | Next state       | Action (entry, runs once)        |
 +------------------+----------------+------------------+----------------------------------+
 | IDLE             | EV_SW_EDGE     | DEBOUNCE         | -                                |
 | IDLE             | EV_SWITCH_CASE | UPDATE_SW        | -                                |
 | IDLE             | EV_CCR_CHANGE  | UPDATE_CCR       | -                                |
 | IDLE             | EV_CAPTURE     | CAPTURE          | -                                |
 | IDLE             | EV_BLINK       | BLINK            | -                                |
 | IDLE             | EV_NONE        | IDLE             | -                                |
 | DEBOUNCE         | (always)       | IDLE             | debounce_Start()                 |
 | UPDATE_SW        | (always)       | IDLE             | sw_IsPressed()? sw_ChangeMode(); |
 |                  |                |                  | sw_Rearm()                       |
 | UPDATE_CCR       | (always)       | IDLE             | encoder_TakeSteps() +            |
 |                  |                |                  | dutty_ApplySteps() per mode      |
 | CAPTURE          | (always)       | IDLE             | capture_Read()                   |
 | BLINK            | (always)       | IDLE             | led_Toggle(),                    |
 |                  |                |                  | capture_CheckTimeout()           |
 | ERROR / unknown  | (always)       | IDLE             | -                                |
 +------------------+----------------+------------------+----------------------------------+

 Modes (current_mode, changed by sw_ChangeMode(), used by STATE_UPDATE_CCR):

   MODE_RED_CCR -> MODE_GREEN_CCR -> MODE_BLUE_CCR -> MODE_ALL_CCR -> MODE_DAC_CCR --+
        ^                                                                            |
        +----------------------------------------------------------------------------+

   +----------------+--------------------------+--------------------+
   | Mode           | Encoder changes          | Register(s)        |
   +----------------+--------------------------+--------------------+
   | MODE_RED_CCR   | red only                 | TIM4->CCR4         |
   | MODE_GREEN_CCR | green only               | TIM4->CCR3         |
   | MODE_BLUE_CCR  | blue only                | TIM4->CCR1         |
   | MODE_ALL_CCR   | red, green and blue      | TIM4->CCR4/3/1     |
   | MODE_DAC_CCR   | DAC voltage (0 - 3.3 V)  | TIM4->CCR2         |
   +----------------+--------------------------+--------------------+
   Duty: 0..1000 (0 % - 100 %), 50 per encoder detent, saturated at both ends.

 ***PINS MAP

 +-----------+-------------+----------+----------+
 |  ENCODER  |     LEDs    |   DAC    | CAPTURE  |
 +-----------+-------------+----------+----------+
 | PB12 (SW) | PB6 (blue)  | PB7      | PA0      |
 | PC6 (DT)  | PB8 (green) |          |          |
 | PC7 (CLK) | PB9 (red)   |          |          |
 |           | PH1 (status)|          |          |
 +-----------+-------------+----------+----------+

 VCC = 3.3 V
 RGB LED in common anode configuration (LED on when the pin is LOW)

 ***TIMER MAP (STM32F411RE datasheet DS10314, Table 9)

 +--------+---------+-----------+-------------+-----+--------+------+---------------------+
 | Role   | Pin     | Use       | Timer/Chan. | AF  | Width  | Bus  | Clock enable        |
 +--------+---------+-----------+-------------+-----+--------+------+---------------------+
 | A      | PH1     | Blinky    | TIM9 (base) | -   | 16-bit | APB2 | RCC_APB2ENR_TIM9EN  |
 | B      | PB6     | LED blue  | TIM4_CH1    | AF2 | 16-bit | APB1 | RCC_APB1ENR_TIM4EN  |
 | B      | PB7     | DAC       | TIM4_CH2    | AF2 | 16-bit | APB1 | RCC_APB1ENR_TIM4EN  |
 | B      | PB8     | LED green | TIM4_CH3    | AF2 | 16-bit | APB1 | RCC_APB1ENR_TIM4EN  |
 | B      | PB9     | LED red   | TIM4_CH4    | AF2 | 16-bit | APB1 | RCC_APB1ENR_TIM4EN  |
 | C      | PA0     | Capture   | TIM5_CH1    | AF2 | 32-bit | APB1 | RCC_APB1ENR_TIM5EN  |
 | D      | PC6     | Enc. DT   | TIM3_CH1    | AF2 | 16-bit | APB1 | RCC_APB1ENR_TIM3EN  |
 | D      | PC7     | Enc. CLK  | TIM3_CH2    | AF2 | 16-bit | APB1 | RCC_APB1ENR_TIM3EN  |
 | E      | PB12    | Enc. SW   | TIM10(base) | -   | 16-bit | APB2 | RCC_APB2ENR_TIM10EN |
 +--------+---------+-----------+-------------+-----+--------+------+---------------------+
 TIM9 and TIM10 only count time (no pin). PH1 is toggled by ODR, PB12 is a GPIO input on EXTI12.
 */

#include <stdint.h>
#include "stm32f4xx.h"

//Constants
#define TIM_CLK_HZ 16000000 //HSI, APB1/APB2 prescaler = 1 -> timer clock 16 MHz
#define PWM_MAX 1000 //ARR + 1: duty goes from 0 to 1000 (0 % to 100 %)
#define PWM_STEP 50 //duty change per encoder detent (20 detents = full range)
#define COUNTS_PER_STEP 4 //encoder mode 3 counts 4 edges per detent
#define CAPTURE_TIMEOUT 4 //blink ticks without a capture (4 x 250 ms = 1 s) -> 0 Hz

//FSM states
typedef enum {
     STATE_IDLE, //Standby state, the only one that reads events
     STATE_UPDATE_CCR, //Applies the encoder steps to the CCRx of the current mode
     STATE_DEBOUNCE, //SW edge seen: starts the debounce timer
     STATE_UPDATE_SW, //Debounce finished: confirms the press and changes mode
     STATE_CAPTURE, //Converts the captured period into frequency
     STATE_BLINK, //Toggles the status LED
     STATE_ERROR, //Recovery state
     NUM_STATES, //Number of states
} State_t;

//FSM modes
typedef enum {
     MODE_RED_CCR, //Controls TIM4 CCR4
     MODE_GREEN_CCR, //Controls TIM4 CCR3
     MODE_BLUE_CCR, //Controls TIM4 CCR1
     MODE_ALL_CCR, //Controls TIM4 CCR1, CCR3 and CCR4
     MODE_DAC_CCR, //Controls TIM4 CCR2
     NUM_MODES, //Number of modes
} Mode_t;

//FSM events
typedef enum {
     EV_NONE, //Nothing happened
     EV_CCR_CHANGE, //Encoder turned at least one detent
     EV_SW_EDGE, //First falling edge on SW (may be a bounce)
     EV_SWITCH_CASE, //Debounce time is over, check SW again
     EV_CAPTURE, //A new period was captured
     EV_BLINK, //Blink time
} Event_t;

State_t current_state = STATE_IDLE; //Initialization in standby
Mode_t current_mode = MODE_RED_CCR; //Initialization in mode 1

//flags (written by ISRs, read by the FSM)
volatile uint8_t state_entry_flag = 1; //initialization of fsm
volatile uint8_t sw_edge_flag = 0; //EXTI12 saw a falling edge
volatile uint8_t sw_flag = 0; //debounce time is over
volatile uint8_t blink_flag = 0; //blink time

//FSM data (only the main loop uses them)
uint32_t ccr_value_red = 0; //duty value for red
uint32_t ccr_value_blue = 0; //duty value for blue
uint32_t ccr_value_green = 0; //duty value for green
uint32_t ccr_value_dac = 0; //duty value for dac
uint32_t capture_freq_hz = 0; //measured frequency, watch it in the debugger
uint8_t capture_age = 0; //blink ticks since the last capture

Event_t get_next_event(void);
//actions
int32_t encoder_TakeSteps(void); //detents turned since last call (+CW / -CCW)
uint32_t dutty_ApplySteps(uint32_t ccr, int32_t steps); //adds steps to a CCR, saturated to 0..PWM_MAX
void debounce_Start(void); //starts the 20 ms one-pulse timer
uint8_t sw_IsPressed(void); //reads PB12 (pressed = low)
void sw_Rearm(void); //listens to EXTI12 again
void sw_ChangeMode(void); //change mode to the next
void capture_Read(void); //reads CCR1, computes the period and converts it into Hz
void capture_CheckTimeout(void); //sets 0 Hz when the signal disappears
void led_Toggle(void); //blinks the led

//Initialization
void init_GPIO(void); //Initialize GPIOs
void init_TIM9_Blinky(void); //Initialize blinky timer
void init_TIM4_PWM(void); //Initialize all PWMs
void init_TIM3_encoder(void); //Initialize encoder timer
void init_TIM5_capture(void); //Initialize capture timer
void init_TIM10_debounce(void); //Initialize debounce timer
void init_EXTI_SW(void); //Initialize EXTI for SW

int main(void)
{
     Event_t current_event = EV_NONE; //Event evaluation
     init_GPIO(); //calls for GPIOs
     init_TIM4_PWM(); //calls for PWM timer
     init_TIM3_encoder(); //calls for encoder timer
     init_TIM5_capture(); //calls for capture timer
     init_TIM10_debounce(); //calls for debounce timer
     init_EXTI_SW(); //calls for interrupts due encoder clicks
     init_TIM9_Blinky(); //Blinky begins when everything is alright

     while(1){

        //Safeguard
        if (current_state >= NUM_STATES) {
          current_state = STATE_ERROR; //error when current state floods num states
          state_entry_flag = 1;
        }

        //FSM
        switch (current_state) {
          case STATE_IDLE:
               if (state_entry_flag) {
                    state_entry_flag = 0; //Entry, nothing to do
               }

               current_event = get_next_event(); //looking for next event

               if (current_event == EV_SW_EDGE) {
                    current_state = STATE_DEBOUNCE; //state to prevent unexpected change mode
                    state_entry_flag = 1;
               }
               else if (current_event == EV_SWITCH_CASE) {
                    current_state = STATE_UPDATE_SW; //state to update mode
                    state_entry_flag = 1;
               }
               else if (current_event == EV_CCR_CHANGE) {
                    current_state = STATE_UPDATE_CCR; //state to update duty
                    state_entry_flag = 1;
               }
               else if (current_event == EV_CAPTURE) {
                    current_state = STATE_CAPTURE; //state to capture frequency
                    state_entry_flag = 1;
               }
               else if (current_event == EV_BLINK) {
                    current_state = STATE_BLINK; //state to toggle blinky led
                    state_entry_flag = 1;
               }
               break;

          case STATE_UPDATE_CCR:
               if (state_entry_flag) {
                    int32_t steps = encoder_TakeSteps(); //read ONCE: it resets the counter

                    if (current_mode == MODE_RED_CCR) {
                         ccr_value_red = dutty_ApplySteps(ccr_value_red, steps); //get the duty for red 
                         TIM4->CCR4 = ccr_value_red; //apply duty for red
                    }
                    else if (current_mode == MODE_GREEN_CCR) {
                         ccr_value_green = dutty_ApplySteps(ccr_value_green, steps); //get the duty for green 
                         TIM4->CCR3 = ccr_value_green; //apply the duty for green
                    }
                    else if (current_mode == MODE_BLUE_CCR) {
                         ccr_value_blue = dutty_ApplySteps(ccr_value_blue, steps); //get the duty for blue
                         TIM4->CCR1 = ccr_value_blue; //apply the duty for blue
                    }
                    else if (current_mode == MODE_ALL_CCR) {
                         ccr_value_red   = dutty_ApplySteps(ccr_value_red, steps); //get the duty for red 
                         ccr_value_green = dutty_ApplySteps(ccr_value_green, steps); //get the duty for green 
                         ccr_value_blue  = dutty_ApplySteps(ccr_value_blue, steps); //get the duty for blue
                         TIM4->CCR4 = ccr_value_red; //apply duty for red
                         TIM4->CCR3 = ccr_value_green; //apply the duty for green
                         TIM4->CCR1 = ccr_value_blue; //apply the duty for blue
                    }
                    else if (current_mode == MODE_DAC_CCR) {
                         ccr_value_dac = dutty_ApplySteps(ccr_value_dac, steps); //get the duty for dac signal
                         TIM4->CCR2 = ccr_value_dac; //apply duty for dac signal
                    }
                    state_entry_flag = 0;
               }
               current_state = STATE_IDLE; //back to idle
               state_entry_flag = 1;
               break;

          case STATE_DEBOUNCE:
               if (state_entry_flag) {
                    debounce_Start(); //wait 20 ms before trusting SW
                    state_entry_flag = 0;
               }
               current_state = STATE_IDLE; //back to idle
               state_entry_flag = 1;
               break;

          case STATE_UPDATE_SW:
               if (state_entry_flag) {
                    if (sw_IsPressed()) { //still pressed after 20 ms -> real press
                         sw_ChangeMode(); //change the mode when real press is detected
                    }
                    sw_Rearm(); //listen for the next press
                    state_entry_flag = 0;
               }
               current_state = STATE_IDLE; //back to idle
               state_entry_flag = 1;
               break;

          case STATE_CAPTURE:
               if (state_entry_flag) {
                    capture_Read(); //action for input frequency
                    state_entry_flag = 0;
               }
               current_state = STATE_IDLE;
               state_entry_flag = 1;
               break;

          case STATE_BLINK:
               if (state_entry_flag) {
                    led_Toggle(); //action call for toggle LED
                    capture_CheckTimeout(); //uses the 250 ms tick as a clock
                    state_entry_flag = 0;
               }
               current_state = STATE_IDLE;
               state_entry_flag = 1;
               break;

          case STATE_ERROR: //get the info into default for recuperation
          default: //Every unknown state gets into standby
               current_state = STATE_IDLE;
               state_entry_flag = 1;
               break;
        }
     }
}

//Drivers
Event_t get_next_event(void){

     if (sw_edge_flag == 1) { //first edge of a click
          sw_edge_flag = 0; //down the flag
          return EV_SW_EDGE;
     }

     if (sw_flag == 1) { //debounce time is over
          sw_flag = 0; //down the flag
          return EV_SWITCH_CASE;
     }

     int16_t counts = (int16_t) TIM3->CNT; //16-bit timer: CCW from 0 -> 0xFFFF = -1
     if ((counts >= COUNTS_PER_STEP) || (counts <= -COUNTS_PER_STEP)) { //1 detent or more
          return EV_CCR_CHANGE;
     }

     if (blink_flag == 1) { //ISR left pending event (before capture: a fast input can't starve the LED)
          blink_flag = 0; //down the flag
          return EV_BLINK;
     }

     if (TIM5->SR & TIM_SR_CC1IF) { //hardware latched a new edge time in CCR1 (polled, like the encoder)
          return EV_CAPTURE; //no flag to clear: capture_Read() reads CCR1, which clears CC1IF
     }

     return EV_NONE;
}

//Actions
int32_t encoder_TakeSteps(void){
     int16_t counts = (int16_t) TIM3->CNT; //signed: + CW, - CCW
     int32_t steps = counts / COUNTS_PER_STEP; //4 counts = 1 detent
     TIM3->CNT = 0; //CNT reboot
     return steps;
}

uint32_t dutty_ApplySteps(uint32_t ccr, int32_t steps){
     int32_t value = (int32_t) ccr + steps * (int32_t) PWM_STEP; //signed: can go below 0
     if (value < 0) {
          value = 0; //saturate at 0 % (off)
     }
     else if (value > (int32_t) PWM_MAX) {
          value = PWM_MAX; //saturate at 100 %
     }
     return (uint32_t) value;
}

void debounce_Start(void){
     TIM10->CNT = 0; //counts from 0
     TIM10->CR1 |= TIM_CR1_CEN; //one pulse: stops by itself after 20 ms
}

uint8_t sw_IsPressed(void){
     return (GPIOB->IDR & GPIO_IDR_ID12) ? 0U : 1U; //pull-up: released = 1, pressed = 0
}

void sw_Rearm(void){
     EXTI->PR = EXTI_PR_PR12; //drop edges seen while masked (write 1 to clear)
     EXTI->IMR |= EXTI_IMR_MR12; //listen again
}

void sw_ChangeMode(void){
     current_mode = (Mode_t) ((current_mode + 1) % NUM_MODES); //RED->GREEN->BLUE->ALL->DAC->RED
}

void capture_Read(void){
     static uint32_t last_capture = 0; //time of the previous rising edge
     static uint8_t first_edge = 1; //no previous edge yet

     uint32_t now = TIM5->CCR1; //time of this rising edge; reading CCR1 clears CC1IF
     if (TIM5->SR & TIM_SR_CC1OF) { //an edge came before we read the last one -> not neighbours
          TIM5->SR = ~TIM_SR_CC1OF; //clear overcapture (rc_w0: write 0 only to this bit)
          first_edge = 1; //discard this measurement
     }
     if (!first_edge) {
          uint32_t period = now - last_capture; //unsigned subtraction survives the wrap
          if (period != 0) {
               capture_freq_hz = TIM_CLK_HZ / period; //ticks of 1/16 MHz -> Hz
          }
          capture_age = 0; //signal is alive
     }
     first_edge = 0;
     last_capture = now;
}

void capture_CheckTimeout(void){
     if (capture_age < CAPTURE_TIMEOUT) {
          capture_age++;
     }
     else {
          capture_freq_hz = 0; //no edges for 1 s -> no signal
     }
}

void led_Toggle(void) {
     GPIOH->ODR ^= GPIO_ODR_OD1; //toggle led
}

//Hardware
void init_GPIO(void) {
     /*Enabling all GPIOx*/
     RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN; //GPIOA enable
     RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN; //GPIOB enable
     RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN; //GPIOC enable
     RCC->AHB1ENR |= RCC_AHB1ENR_GPIOHEN; //GPIOH enable 

     /*PH1 Blinky*/
     GPIOH->MODER &= ~GPIO_MODER_MODE1; //Clear the register
     GPIOH->MODER |= GPIO_MODER_MODE1_0; //01: general purpose output
     GPIOH->OTYPER &= ~GPIO_OTYPER_OT1; //Push-pull output
     GPIOH->OSPEEDR &= ~GPIO_OSPEEDR_OSPEED1; //Low speed
     GPIOH->PUPDR &= ~GPIO_PUPDR_PUPD1; //No pull up/down

     /*PB6 blue LED, TIM4_CH1*/
     GPIOB->MODER &= ~GPIO_MODER_MODE6; //clear
     GPIOB->MODER |= GPIO_MODER_MODE6_1; //10: alternate function
     GPIOB->AFR[0] &= ~GPIO_AFRL_AFSEL6; //clear
     GPIOB->AFR[0] |= GPIO_AFRL_AFSEL6_1; //0010: AF2

     /*PB7 DAC signal, TIM4_CH2*/
     GPIOB->MODER &= ~GPIO_MODER_MODE7; //clear
     GPIOB->MODER |= GPIO_MODER_MODE7_1; //10: alternate function
     GPIOB->AFR[0] &= ~GPIO_AFRL_AFSEL7; //clear
     GPIOB->AFR[0] |= GPIO_AFRL_AFSEL7_1; //0010: AF2

     /*PB8 green LED, TIM4_CH3 (pins 8-15 use AFR[1])*/
     GPIOB->MODER &= ~GPIO_MODER_MODE8; //clear
     GPIOB->MODER |= GPIO_MODER_MODE8_1; //10: alternate function
     GPIOB->AFR[1] &= ~GPIO_AFRH_AFSEL8; //clear
     GPIOB->AFR[1] |= GPIO_AFRH_AFSEL8_1; //0010: AF2

     /*PB9 red LED, TIM4_CH4*/
     GPIOB->MODER &= ~GPIO_MODER_MODE9; //clear
     GPIOB->MODER |= GPIO_MODER_MODE9_1; //10: alternate function
     GPIOB->AFR[1] &= ~GPIO_AFRH_AFSEL9; //clear
     GPIOB->AFR[1] |= GPIO_AFRH_AFSEL9_1; //0010: AF2

     /*PC6 encoder DT, TIM3_CH1*/
     GPIOC->MODER &= ~GPIO_MODER_MODE6; //clear
     GPIOC->MODER |= GPIO_MODER_MODE6_1; //10: alternate function (timer input)
     GPIOC->AFR[0] &= ~GPIO_AFRL_AFSEL6; //clear
     GPIOC->AFR[0] |= GPIO_AFRL_AFSEL6_1; //0010: AF2
     GPIOC->PUPDR &= ~GPIO_PUPDR_PUPD6; //clear
     GPIOC->PUPDR |= GPIO_PUPDR_PUPD6_0; //01: pull-up, no floating input

     /*PC7 encoder CLK, TIM3_CH2*/
     GPIOC->MODER &= ~GPIO_MODER_MODE7; //clear
     GPIOC->MODER |= GPIO_MODER_MODE7_1; //10: alternate function (timer input)
     GPIOC->AFR[0] &= ~GPIO_AFRL_AFSEL7; //clear
     GPIOC->AFR[0] |= GPIO_AFRL_AFSEL7_1; //0010: AF2 -> TIM3_CH2
     GPIOC->PUPDR &= ~GPIO_PUPDR_PUPD7; //clear
     GPIOC->PUPDR |= GPIO_PUPDR_PUPD7_0; //01: pull-up

     /*PB12 encoder SW, plain input on EXTI12*/
     GPIOB->MODER &= ~GPIO_MODER_MODE12; //00: input
     GPIOB->PUPDR &= ~GPIO_PUPDR_PUPD12; //clear
     GPIOB->PUPDR |= GPIO_PUPDR_PUPD12_0; //01: pull-up, released = 1, pressed = 0

     /*PA0 capture, TIM5_CH1*/
     GPIOA->MODER &= ~GPIO_MODER_MODE0; //clear
     GPIOA->MODER |= GPIO_MODER_MODE0_1; //10: alternate function (timer input)
     GPIOA->AFR[0] &= ~GPIO_AFRL_AFSEL0; //clear
     GPIOA->AFR[0] |= GPIO_AFRL_AFSEL0_1; //0010: AF2
}

void init_TIM9_Blinky(void) {
     RCC->APB2ENR |= RCC_APB2ENR_TIM9EN; //TIM9 clock (APB2)
     TIM9->PSC = 16000 - 1; //16 MHz / 16000 = 1 kHz -> 1 tick = 1 ms
     TIM9->ARR = 250 - 1; //250 ticks = 250 ms
     TIM9->CR1 |= TIM_CR1_ARPE; //ARR preload
     TIM9->EGR = TIM_EGR_UG; //load PSC and ARR now
     TIM9->SR = ~TIM_SR_UIF; //clear the flag that UG just set
     TIM9->DIER |= TIM_DIER_UIE; //update interrupt enable
     NVIC_EnableIRQ(TIM1_BRK_TIM9_IRQn); //TIM9 shares its vector with TIM1 break

     TIM9->CR1 |= TIM_CR1_CEN; //begins counter
}

void init_TIM3_encoder(void) {
     RCC->APB1ENR |= RCC_APB1ENR_TIM3EN; //TIM3 clock (APB1)

     TIM3->ARR = 0xFFFF; //full 16-bit range, wraps 0 <-> 0xFFFF
     TIM3->CCMR1 &= ~(TIM_CCMR1_CC1S | TIM_CCMR1_CC2S); //clear
     TIM3->CCMR1 |= TIM_CCMR1_CC1S_0; //01: CC1 input mapped on TI1 (PC6)
     TIM3->CCMR1 |= TIM_CCMR1_CC2S_0; //01: CC2 input mapped on TI2 (PC7)
     TIM3->CCMR1 |= TIM_CCMR1_IC1F; //1111: strongest input filter (contact bounce)
     TIM3->CCMR1 |= TIM_CCMR1_IC2F; //1111: strongest input filter
     TIM3->CCER &= ~(TIM_CCER_CC1P | TIM_CCER_CC1NP | TIM_CCER_CC2P | TIM_CCER_CC2NP); //non-inverted
     TIM3->SMCR &= ~TIM_SMCR_SMS; //clear
     TIM3->SMCR |= TIM_SMCR_SMS_0 | TIM_SMCR_SMS_1; //011: encoder mode 3, counts edges of TI1 and TI2
     TIM3->CNT = 0; //start at 0
     TIM3->CR1 |= TIM_CR1_CEN; //counter begins
}

void init_TIM4_PWM(void) {
     RCC->APB1ENR |= RCC_APB1ENR_TIM4EN; //TIM4 clock (APB1)

     TIM4->PSC = 0; //16 MHz, no division
     TIM4->ARR = PWM_MAX - 1; //16 MHz / 1000 = 16 kHz PWM, 1000 duty steps

     /*CH1 blue LED and CH2 DAC -> CCMR1*/
     TIM4->CCMR1 &= ~(TIM_CCMR1_CC1S | TIM_CCMR1_OC1M | TIM_CCMR1_CC2S | TIM_CCMR1_OC2M); //outputs, clear mode
     TIM4->CCMR1 |= TIM_CCMR1_OC1M_1 | TIM_CCMR1_OC1M_2; //110: PWM mode 1
     TIM4->CCMR1 |= TIM_CCMR1_OC1PE; //CCR1 preload
     TIM4->CCMR1 |= TIM_CCMR1_OC2M_1 | TIM_CCMR1_OC2M_2; //110: PWM mode 1
     TIM4->CCMR1 |= TIM_CCMR1_OC2PE; //CCR2 preload

     /*CH3 green LED and CH4 red LED -> CCMR2*/
     TIM4->CCMR2 &= ~(TIM_CCMR2_CC3S | TIM_CCMR2_OC3M | TIM_CCMR2_CC4S | TIM_CCMR2_OC4M); //outputs, clear mode
     TIM4->CCMR2 |= TIM_CCMR2_OC3M_1 | TIM_CCMR2_OC3M_2; //110: PWM mode 1
     TIM4->CCMR2 |= TIM_CCMR2_OC3PE; //CCR3 preload
     TIM4->CCMR2 |= TIM_CCMR2_OC4M_1 | TIM_CCMR2_OC4M_2; //110: PWM mode 1
     TIM4->CCMR2 |= TIM_CCMR2_OC4PE; //CCR4 preload

     /*Common anode: LED is on when the pin is LOW -> active low on the 3 LED channels*/
     TIM4->CCER |= TIM_CCER_CC1P; //active low CH1 blue
     TIM4->CCER |= TIM_CCER_CC3P; //active low CH3 green
     TIM4->CCER |= TIM_CCER_CC4P; //active low CH4 red
     TIM4->CCER |= TIM_CCER_CC1E | TIM_CCER_CC2E | TIM_CCER_CC3E | TIM_CCER_CC4E; //enable the 4 outputs

     TIM4->CCR1 = ccr_value_blue; //blue duty
     TIM4->CCR2 = ccr_value_dac; //DAC duty
     TIM4->CCR3 = ccr_value_green; //green duty
     TIM4->CCR4 = ccr_value_red; //red duty

     TIM4->CR1 |= TIM_CR1_ARPE; //ARR preload
     TIM4->EGR = TIM_EGR_UG; //preload PSC, ARR and CCRx now
     TIM4->CR1 |= TIM_CR1_CEN; //counter begins
}

void init_TIM5_capture(void) {
     RCC->APB1ENR |= RCC_APB1ENR_TIM5EN; //TIM5 clock (APB1)

     TIM5->PSC = 0; //1 tick = 1/16 MHz = 62.5 ns
     TIM5->ARR = 0xFFFFFFFF; //32-bit: wraps after ~268 s
     TIM5->CCMR1 &= ~(TIM_CCMR1_CC1S | TIM_CCMR1_IC1F | TIM_CCMR1_IC1PSC); //clear
     TIM5->CCMR1 |= TIM_CCMR1_CC1S_0; //01: CC1 input mapped on TI1 (PA0)
     TIM5->CCMR1 |= TIM_CCMR1_IC1F_0 | TIM_CCMR1_IC1F_1; //0011: small noise filter
     TIM5->CCER &= ~(TIM_CCER_CC1P | TIM_CCER_CC1NP); //00: rising edge
     TIM5->CCER |= TIM_CCER_CC1E; //capture enabled
     TIM5->EGR = TIM_EGR_UG; //load PSC and ARR now
     TIM5->SR = 0; //clear all flags
     //no interrupt: the FSM polls CC1IF in get_next_event()

     TIM5->CR1 |= TIM_CR1_CEN; //counter begins
}

void init_TIM10_debounce(void) {
     RCC->APB2ENR |= RCC_APB2ENR_TIM10EN; //TIM10 clock (APB2)
     TIM10->PSC = 16000 - 1; //1 tick = 1 ms
     TIM10->ARR = 20 - 1; //20 ms
     TIM10->CR1 |= TIM_CR1_OPM; //one-pulse: stops by itself
     TIM10->EGR = TIM_EGR_UG; //load PSC now
     TIM10->SR = ~TIM_SR_UIF; //clear the flag UG just set
     TIM10->DIER |= TIM_DIER_UIE; //interrupt enable
     NVIC_EnableIRQ(TIM1_UP_TIM10_IRQn); //TIM10 shares its vector with TIM1 update
}

void init_EXTI_SW(void) {
     RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN; //SYSCFG clock, needed to route PB12 to EXTI12
     SYSCFG->EXTICR[3] &= ~SYSCFG_EXTICR4_EXTI12; //clear
     SYSCFG->EXTICR[3] |= SYSCFG_EXTICR4_EXTI12_PB; //EXTI12 <- port B
     EXTI->FTSR |= EXTI_FTSR_TR12; //falling edge = press
     EXTI->PR = EXTI_PR_PR12; //clear any old pending edge
     EXTI->IMR |= EXTI_IMR_MR12; //unmask line 12
     NVIC_EnableIRQ(EXTI15_10_IRQn);
}

//Interrupt handlers: hardware work + flag, no decisions
void EXTI15_10_IRQHandler(void) {
     if (EXTI->PR & EXTI_PR_PR12) {
          EXTI->PR = EXTI_PR_PR12; //clear (write 1)
          EXTI->IMR &= ~EXTI_IMR_MR12; //mask now, so the bounces don't interrupt again
          sw_edge_flag = 1; //FSM starts the debounce
     }
}

void TIM1_UP_TIM10_IRQHandler(void) {
     TIM10->SR = ~TIM_SR_UIF; //flag down
     sw_flag = 1; //FSM checks SW again
}

void TIM1_BRK_TIM9_IRQHandler(void) {
     TIM9->SR = ~TIM_SR_UIF; //flag down
     blink_flag = 1; //FSM calls EV_BLINK
}

