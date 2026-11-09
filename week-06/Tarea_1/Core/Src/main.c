/**
 ******************************************************************************
 * @file           : main.c
 * @author         : Mateo Quiceno Zapata
 * @brief          : Main program body
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
 */

#include <stdint.h>
#include "stm32f411xe.h"

/*
Homework 1: Seven segments commun anode display counter +1 and -1

It is based in eight functions which are segmented in the code.
Four of them are for initialization of GPIO, TIM, interrupts and blinky led.
The other four functions are for the finite state machine (read_event and
sevenSegment_display), multiplexing and value to segments.
The logic of the counter is based in two photointerrupts which are connected to PB5 and PB2.

Interrupts used (4):
- TIM9   (TIM1_BRK_TIM9_IRQHandler): blinky LED on PH1, 2.5 Hz toggle.
- TIM2   (TIM2_IRQHandler): display refresh tick, 250 Hz -> 62.5 Hz per full display.
- EXTI5  (EXTI9_5_IRQHandler): photointerrupt PB5, falling edge -> counter +1.
- EXTI2  (EXTI2_IRQHandler): photointerrupt PB2, rising edge -> counter -1.
The interrupts only raise flags (flag_on, flag_up, flag_down); all the work is
done in the main loop by the finite state machine.

Finite state machine (sevenSegment_display):

 States:
   STATE_IDLE      : waiting for an event.
   STATE_INCREMENT : counter + 1 (9999 -> 0).
   STATE_DECREMENT : counter - 1 (0 -> 9999).
   STATE_REFRESH   : turn off all digits, write the segments of the next digit
                     and turn it on (POV multiplexing, no ghosting).

 Events (read_event, from the interrupt flags):
   EVENT_REFRESH : flag_on   set by TIM2  (highest priority, keeps the POV timing)
   EVENT_UP      : flag_up   set by EXTI5
   EVENT_DOWN    : flag_down set by EXTI2
   EVENT_NONE    : no flag set

 Transitions:
   current state    | event          | next state
   -----------------+----------------+-----------------
   STATE_IDLE       | EVENT_REFRESH  | STATE_REFRESH
   STATE_IDLE       | EVENT_UP       | STATE_INCREMENT
   STATE_IDLE       | EVENT_DOWN     | STATE_DECREMENT
   STATE_IDLE       | EVENT_NONE     | STATE_IDLE
   STATE_INCREMENT  | (always)       | STATE_IDLE
   STATE_DECREMENT  | (always)       | STATE_IDLE
   STATE_REFRESH    | (always)       | STATE_IDLE

Mapping GPIO:

11 a, 7 b, 4 c, 2 d, 1 e, 10 f, 5 g

a UPPER SEGMENT: PA12
b RIGHT UPPER SEGMENT: PC8
c RIGHT LOWER SEGMENT: PB8
d LOWER SEGMENT: PC9
e LEFT LOWER SEGMENT: PC10
f LEFT UPPER SEGMENT: PC5
g MIDDLE SEGMENT: PB9

DIGIT 1: PB12
DIGIT 2: PA11
DIGIT 3: PA5
DIGIT 4: PC12

Counter +1: PB5
Counter -1: PB2


          a
        ─────
     f |     | b
       |  g  |
        ─────
     e |     | c
       |     |
        ─────
          d

 número | a b c d e f g
 -------+--------------
   0    | 1 1 1 1 1 1 0
   1    | 0 1 1 0 0 0 0
   2    | 1 1 0 1 1 0 1
   3    | 1 1 1 1 0 0 1
   4    | 0 1 1 0 0 1 1
   5    | 1 0 1 1 0 1 1
   6    | 1 0 1 1 1 1 1
   7    | 1 1 1 0 0 0 0
   8    | 1 1 1 1 1 1 1
   9    | 1 1 1 1 0 1 1
*/

// Finite state machine states
typedef enum {
    STATE_IDLE = 0,   // Waiting for an event
    STATE_INCREMENT,  // Counter +1 (photointerrupt PB5)
    STATE_DECREMENT,  // Counter -1 (photointerrupt PB2)
    STATE_REFRESH     // Show the next digit (TIM2)
} fsm_state_t;

// Finite state machine events (generated from the interrupt flags)
typedef enum {
    EVENT_NONE = 0,   // No flag set
    EVENT_UP,         // flag_up set by EXTI5
    EVENT_DOWN,       // flag_down set by EXTI2
    EVENT_REFRESH     // flag_on set by TIM2
} fsm_event_t;

volatile uint16_t counter = 0; // Counter variable
volatile uint8_t flag_up = 0; // Flag for incrementing the counter
volatile uint8_t flag_down = 0; // Flag for decrementing the counter
volatile uint8_t flag_on =0; // Flag for multiplexing
volatile uint8_t number = 0;  // Current number to display (0-9)
volatile uint8_t digit = 0;   // Current digit to display (0-3)
fsm_state_t state = STATE_IDLE; // Current state of the finite state machine

static void init_GPIO(void);
static void init_blinky(void);
static void init_interrupts(void);
static void init_TIM(void);
static fsm_event_t read_event(void);
static void sevenSegment_display(void);
static void multiplex_display(uint8_t digit);
static void value_to_segments(uint8_t number, uint16_t counter);

int main(void)
{
    init_GPIO();         // Initialize GPIO pins
    init_blinky();      // Initialize blinky LED
    init_TIM();         // Initialize TIM2
    init_interrupts();   // Initialize interrupts for photointerrupts

    while (1)
    {
        sevenSegment_display(); // Run the finite state machine
    }
}

void init_GPIO(void)
{
    // Initialize GPIO pins for seven-segment display and photointerrupts
    // Implementation for GPIO initialization
    // Enable clock for GPIO ports
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN; // Enable GPIOA clock
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN; // Enable GPIOB clock
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN; // Enable GPIOC clock
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOHEN; // Enable GPIOH clock

    // GPIOs for segments (PA12, PC8, PB8, PC9, PC10, PC5, PB9)
    GPIOA->MODER |= GPIO_MODER_MODE12_0; // Set PA12 as output
    GPIOC->MODER |= GPIO_MODER_MODE8_0;  // Set PC8 as output
    GPIOB->MODER |= GPIO_MODER_MODE8_0;  // Set PB8 as output
    GPIOC->MODER |= GPIO_MODER_MODE9_0;  // Set PC9 as output
    GPIOC->MODER |= GPIO_MODER_MODE10_0; // Set PC10 as output
    GPIOC->MODER |= GPIO_MODER_MODE5_0;  // Set PC5 as output
    GPIOB->MODER |= GPIO_MODER_MODE9_0;  // Set PB9 as output

    // GPIOs for digits (PB12, PA11, PA5, PC12)
    GPIOB->MODER |= GPIO_MODER_MODE12_0; // Set PB12 as output
    GPIOA->MODER |= GPIO_MODER_MODE11_0; // Set PA11 as output
    GPIOA->MODER |= GPIO_MODER_MODE5_0;  // Set PA5 as output
    GPIOC->MODER |= GPIO_MODER_MODE12_0; // Set PC12 as output

    // GPIOs for photointerrupts (PB5, PB2)
    GPIOB->MODER &= ~GPIO_MODER_MODE5;  // Set PB5 as input
    GPIOB->MODER &= ~GPIO_MODER_MODE2;  // Set PB2 as input

    // Set output type to push-pull for all output pins
    GPIOA->OTYPER &= ~GPIO_OTYPER_OT12; // PA12
    GPIOC->OTYPER &= ~GPIO_OTYPER_OT8;  // PC8
    GPIOB->OTYPER &= ~GPIO_OTYPER_OT8;  // PB8
    GPIOC->OTYPER &= ~GPIO_OTYPER_OT9;  // PC9
    GPIOC->OTYPER &= ~GPIO_OTYPER_OT10; // PC10
    GPIOC->OTYPER &= ~GPIO_OTYPER_OT5;  // PC5
    GPIOB->OTYPER &= ~GPIO_OTYPER_OT9;  // PB9
    GPIOB->OTYPER &= ~GPIO_OTYPER_OT12; // PB12
    GPIOA->OTYPER &= ~GPIO_OTYPER_OT11; // PA11
    GPIOA->OTYPER &= ~GPIO_OTYPER_OT5;  // PA5
    GPIOC->OTYPER &= ~GPIO_OTYPER_OT12; // PC12

    // Set output speed to low for all output pins
    GPIOA->OSPEEDR &= ~GPIO_OSPEEDR_OSPEED12_1; // PA12
    GPIOC->OSPEEDR &= ~GPIO_OSPEEDR_OSPEED8_1;  // PC8
    GPIOB->OSPEEDR &= ~GPIO_OSPEEDR_OSPEED8_1;  // PB8
    GPIOC->OSPEEDR &= ~GPIO_OSPEEDR_OSPEED9_1;  // PC9
    GPIOC->OSPEEDR &= ~GPIO_OSPEEDR_OSPEED10_1; // PC10
    GPIOC->OSPEEDR &= ~GPIO_OSPEEDR_OSPEED5_1;  // PC5
    GPIOB->OSPEEDR &= ~GPIO_OSPEEDR_OSPEED9_1;  // PB9
    GPIOB->OSPEEDR &= ~GPIO_OSPEEDR_OSPEED12_1; // PB12
    GPIOA->OSPEEDR &= ~GPIO_OSPEEDR_OSPEED11_1; // PA11
    GPIOA->OSPEEDR &= ~GPIO_OSPEEDR_OSPEED5_1;  // PA5
    GPIOC->OSPEEDR &= ~GPIO_OSPEEDR_OSPEED12_1; // PC12

    // Initial State: Turn off all segments and digits
    GPIOA->BSRR = GPIO_BSRR_BS12; // Turn off PA12
    GPIOC->BSRR = GPIO_BSRR_BS8;  // Turn off PC8
    GPIOB->BSRR = GPIO_BSRR_BS8;  // Turn off PB8
    GPIOC->BSRR = GPIO_BSRR_BS9;  // Turn off PC9
    GPIOC->BSRR = GPIO_BSRR_BS10; // Turn off PC10
    GPIOC->BSRR = GPIO_BSRR_BS5;  // Turn off PC5
    GPIOB->BSRR = GPIO_BSRR_BS9;  // Turn off PB9
    GPIOB->BSRR = GPIO_BSRR_BS12; // Turn off PB12
    GPIOA->BSRR = GPIO_BSRR_BS11; // Turn off PA11
    GPIOA->BSRR = GPIO_BSRR_BS5;  // Turn off PA5
    GPIOC->BSRR = GPIO_BSRR_BS12; // Turn off PC12
}

void init_blinky(void)
{
    // Initialize blinky LED on PH1
    // Implementation for blinky LED initialization
    //GPIO for blinky LED (PH1)
    GPIOH->MODER |= GPIO_MODER_MODE1_0;  // Set PH1 as output

    // Set output type to push-pull for PH1
    GPIOH->OTYPER &= ~GPIO_OTYPER_OT1; // PH1
    GPIOH->OSPEEDR &= ~GPIO_OSPEEDR_OSPEED1_1; // Set output speed to low for PH1

    RCC->APB2ENR |= RCC_APB2ENR_TIM9EN; // Enable TIM9 clock
    TIM9->PSC = 16000 - 1; // Set prescaler to 16000 (assuming 16 MHz clock)
    TIM9->ARR = 400 - 1; // Set auto-reload value
    TIM9->DIER |= TIM_DIER_UIE; // Enable update interrupt

    NVIC_EnableIRQ(TIM1_BRK_TIM9_IRQn); // Enable TIM9 interrupt in NVIC
    TIM9->CR1 |= TIM_CR1_CEN; // Enable TIM9
}


void TIM1_BRK_TIM9_IRQHandler(void)
{
    if (TIM9->SR & TIM_SR_UIF) // Check if update interrupt flag is set
    {
        TIM9->SR &= ~TIM_SR_UIF; // Clear update interrupt flag
        GPIOH->ODR ^= GPIO_ODR_OD1; // Toggle blinky LED (PH1)
    }
}


void init_TIM(void)
{
    // Initialize TIM for multiplexing the seven-segment display
    // Implementation for TIM initialization
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN; // Enable TIM2 clock
    TIM2->PSC = 16000 - 1; // Set prescaler to 16000 (assuming 16 MHz clock)
    TIM2->ARR = 4 - 1; // 250 Hz interrupt -> 62.5 Hz per full display (4 digits)
    TIM2->DIER |= TIM_DIER_UIE; // Enable update interrupt
    NVIC_EnableIRQ(TIM2_IRQn);
    TIM2->CR1 |= TIM_CR1_CEN; // Enable TIM2
}

void TIM2_IRQHandler(void)
{
    if (TIM2->SR & TIM_SR_UIF) // Check if update interrupt flag is set
    {
        flag_on = 1;
        TIM2->SR &= ~TIM_SR_UIF; // Clear update interrupt flag
    }
}

fsm_event_t read_event(void)
{
    // Convert the interrupt flags into one event for the state machine
    // The refresh has the highest priority so the POV timing is not affected
    if (flag_on == 1) {
        flag_on = 0; // Reset the flag after reading it
        return EVENT_REFRESH;
    }
    if (flag_up == 1) {
        flag_up = 0; // Reset the flag after reading it
        return EVENT_UP;
    }
    if (flag_down == 1) {
        flag_down = 0; // Reset the flag after reading it
        return EVENT_DOWN;
    }
    return EVENT_NONE;
}

void sevenSegment_display(void)
{
    // Finite state machine for the counter and the seven-segment display
    switch (state)
    {
        case STATE_IDLE:
            // Wait for an event and go to the state that handles it
            switch (read_event())
            {
                case EVENT_REFRESH:
                    state = STATE_REFRESH;
                    break;
                case EVENT_UP:
                    state = STATE_INCREMENT;
                    break;
                case EVENT_DOWN:
                    state = STATE_DECREMENT;
                    break;
                default:
                    break; // No event: stay in STATE_IDLE
            }
            break;

        case STATE_INCREMENT:
            counter++; // Increment the counter
            state = STATE_IDLE; // Set for waiting
            break;

        case STATE_DECREMENT:
            counter--; // Decrement the counter
            state = STATE_IDLE; //Set for waiting 
            break;

        case STATE_REFRESH:
            multiplex_display(digit); // Show the current digit
            digit = (digit + 1) % 4;  // Move to the next digit
            state = STATE_IDLE; // Set for waiting
            break;

        default:
            state = STATE_IDLE; // Unknown state: go back to a safe state
            break;
    }
}

void init_interrupts(void)
{
    // Initialize interrupts for photointerrupts (PB5, PB2)
    // Implementation for interrupt initialization

    // Enable SYSCFG clock
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;

    // Configure EXTI lines for PB5 and PB2
    SYSCFG->EXTICR[1] |= SYSCFG_EXTICR2_EXTI5_PB; // Map EXTI5 to PB5
    SYSCFG->EXTICR[0] |= SYSCFG_EXTICR1_EXTI2_PB; // Map EXTI2 to PB2

    // Configure EXTI lines: one falling edge and one rising edge trigger
    EXTI->FTSR |= EXTI_FTSR_TR5; // Falling edge trigger for EXTI5 (PB5)
    EXTI->RTSR |= EXTI_RTSR_TR2; // Rising edge trigger for EXTI2 (PB2)

    // Enable EXTI lines
    EXTI->IMR |= EXTI_IMR_MR5; // Unmask EXTI5 (PB5)
    EXTI->IMR |= EXTI_IMR_MR2; // Unmask EXTI2 (PB2)

    // Enable NVIC interrupts for EXTI lines
    NVIC_EnableIRQ(EXTI9_5_IRQn);  // Enable interrupt for EXTI lines 9 to 5
    NVIC_EnableIRQ(EXTI2_IRQn);     // Enable interrupt for EXTI line 2
}

void EXTI9_5_IRQHandler(void)
{
    if (EXTI->PR & EXTI_PR_PR5) // Check if EXTI5 (PB5) triggered the interrupt
    {
        flag_up = 1; // Set flag to increment counter
        EXTI->PR = EXTI_PR_PR5; // Clear pending interrupt flag for EXTI5
    }
}

void EXTI2_IRQHandler(void)
{
    if (EXTI->PR & EXTI_PR_PR2) // Check if EXTI2 (PB2) triggered the interrupt
    {
        flag_down = 1; // Set flag to decrement counter
        EXTI->PR = EXTI_PR_PR2; // Clear pending interrupt flag for EXTI2
    }
}

void multiplex_display(uint8_t digit)
{
    // Multiplexing logic to display the current counter value on the seven-segment display
    // Implementation for multiplexing display

    // Turn off all digits
    GPIOB->BSRR = GPIO_BSRR_BS12; // Turn off PB12 (Digit 1)
    GPIOA->BSRR = GPIO_BSRR_BS11; // Turn off PA11 (Digit 2)
    GPIOA->BSRR = GPIO_BSRR_BS5;  // Turn off PA5 (Digit 3)
    GPIOC->BSRR = GPIO_BSRR_BS12; // Turn off PC12 (Digit 4)

    // Determine which digit to display based on the current digit variable
    switch (digit)
    {
        case 0:
            number = (counter / 1000) % 10; // Get the most significant digit
            value_to_segments(number, counter);
            GPIOB->BSRR = GPIO_BSRR_BR12; // Turn on PB12 (Digit 1)
            break;
        case 1:
            number = (counter / 100) % 10; // Get the third least significant digit
            value_to_segments(number, counter);
            GPIOA->BSRR = GPIO_BSRR_BR11; // Turn on PA11 (Digit 2)
            break;
        case 2:
            number = (counter / 10) % 10; // Get the second least significant digit
            value_to_segments(number, counter);
            GPIOA->BSRR = GPIO_BSRR_BR5;  // Turn on PA5 (Digit 3)
            break;
        case 3:
            number = counter % 10; // Get the least significant digit
            value_to_segments(number, counter);
            GPIOC->BSRR = GPIO_BSRR_BR12; // Turn on PC12 (Digit 4)
            break;
        default:
            break;
    }
}

void value_to_segments(uint8_t number, uint16_t counter){
    // Convert the counter value to the corresponding seven-segment display pattern
    // Implementation for converting value to segments
    GPIOA->BSRR = GPIO_BSRR_BR12; // Turn on a
    GPIOC->BSRR = GPIO_BSRR_BR8;  // Turn on b
    GPIOB->BSRR = GPIO_BSRR_BR8;  // Turn on c
    GPIOC->BSRR = GPIO_BSRR_BR9;  // Turn on d
    GPIOC->BSRR = GPIO_BSRR_BR10; // Turn on e
    GPIOC->BSRR = GPIO_BSRR_BR5;  // Turn on f
    GPIOB->BSRR = GPIO_BSRR_BR9;  // Turn on g

    switch (number)
    {
        case 0:
            GPIOB->BSRR = GPIO_BSRR_BS9; // Turn off g
            break;
        case 1:
            GPIOA->BSRR = GPIO_BSRR_BS12; // Turn off a
            GPIOC->BSRR = GPIO_BSRR_BS9;  // Turn off d
            GPIOC->BSRR = GPIO_BSRR_BS10; // Turn off e
            GPIOC->BSRR = GPIO_BSRR_BS5;  // Turn off f
            GPIOB->BSRR = GPIO_BSRR_BS9;  // Turn off g
            break;
        case 2:
            GPIOB->BSRR = GPIO_BSRR_BS8; // Turn off c
            GPIOC->BSRR = GPIO_BSRR_BS5; // Turn off f
            break;
        case 3:
            GPIOC->BSRR = GPIO_BSRR_BS10;  // Turn off e
            GPIOC->BSRR = GPIO_BSRR_BS5;  // Turn off f
            break;
        case 4:
            GPIOA->BSRR = GPIO_BSRR_BS12;  // Turn off a
            GPIOC->BSRR = GPIO_BSRR_BS9;   // Turn off d
            GPIOC->BSRR = GPIO_BSRR_BS10;  // Turn off e
            break;
        case 5:
            GPIOC->BSRR = GPIO_BSRR_BS8;  // Turn off b
            GPIOC->BSRR = GPIO_BSRR_BS10; // Turn off e
            break;
        case 6:
            GPIOC->BSRR = GPIO_BSRR_BS8;  // Turn off b
            break;
        case 7:
            GPIOC->BSRR = GPIO_BSRR_BS9;  // Turn off d
            GPIOC->BSRR = GPIO_BSRR_BS10; // Turn off e
            GPIOC->BSRR = GPIO_BSRR_BS5;  // Turn off f
            GPIOB->BSRR = GPIO_BSRR_BS9;  // Turn off g
            break;
        case 8:
            break;
        case 9:
            GPIOC->BSRR = GPIO_BSRR_BS10; // Turn off e
            break;
    }

}
