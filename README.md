# stm32-embedded-course - Short description
AI-assisted STM32F4xx embedded systems course. Weekly configuration files for a Copilot CLI learning companion (Haiku 4.5 model - from Anthropic) that guides students through bare-metal ARM Cortex-M4 programming without replacing their thinking.

# STM32F4xx Embedded Systems Course — AI Configuration

This repository contains the AI companion configuration files for a 15-week university embedded systems course based on the STM32F4xx (ARM Cortex-M4) microcontroller.

## Philosophy

The AI is not here to write your code. It is here to help you think. Each week's configuration teaches the AI what you already know, what you are learning now, and how to guide you without doing the work for you.

This is not magic. It is engineering — and now you can see exactly how it works.

## How it works

Each week lives in its own branch (`week-01` through `week-15`).
The `AGENTS.md` file at the root is read automatically by Copilot CLI.
The `ai-config/` folder contains the full configuration — read it, understand it, learn from it.

## Getting started

1. Fork this repository
2. Clone your fork locally
3. Add upstream: `git remote add upstream <professor repo URL>`
4. Each week: `git fetch upstream week-XX && git checkout week-XX`

## Rules
- Do not modify `AGENTS.md` or `ai-config/`
- Save your work in `my-work/`
- Push only to your own fork

---

# My work in this course

Projects and homework for an **STM32F411RE (Nucleo‑64, ARM Cortex‑M4)**. They go from C
fundamentals to **bare‑metal register programming** and then to the **STM32 HAL**. Every
project is a standalone CMake project (`arm-none-eabi-gcc` toolchain, CMake presets, VS Code
with the STM32 extension and clangd).

| Week | Project | What it covers |
|---|---|---|
| 00 | `homework_exercises_week00` | C for embedded: integer types, representation, overflow and wrap‑around (`int8_t` …) |
| 01 | `taller_CMake`, `homework_exercises_week01` | Building firmware with CMake. Bitwise operations, shifts and overflow |
| 02 | `taller_registros` | First register access: `RCC` clock enable, `GPIOx->MODER`/`ODR` to drive an LED |
| 04 | `bare-metal-stm32`, `taller_semaforo` | **TIM3 update interrupt** (`PSC`/`ARR`, `TIM3_IRQHandler`) and **SysTick** timing. A traffic light on GPIOB/GPIOC |
| 05 | `Core`, `taller_EXTI_semaforo` | **External interrupts**: `SYSCFG` EXTI routing, `IMR`/`RTSR`/`FTSR`, `NVIC`. A pedestrian button on the traffic light, timed by TIM2 |
| 06 | `PWM_clase`, `Tarea_1` | **Homework 1:** an interrupt‑driven **finite state machine** that counts events from photointerrupters on EXTI and shows them on a **multiplexed 4‑digit seven‑segment display**. TIM2 and TIM9 handle multiplexing and timing, and `BSRR` does atomic pin writes |
| 07 | `taller_PWM` | **PWM** with output compare (`CCMR1`, `CCR1`, alternate functions in `AFR`) and a rotary **encoder in timer encoder mode** (`SMCR`) |
| 08 | `class_HAL` | First **HAL** project written without CubeMX: `HAL_Init`, clock config, `HAL_GPIO_Init`, a TIM3‑driven blinky on PA5. Includes a step‑by‑step tutorial (`HAL_BLINKY_TUTORIAL_week08_EN_vsCode.md`) |
| 09 | `taller_PWM_Servo` | A rotary encoder (TIM2) positions a **servomotor** with a 50 Hz PWM (TIM3, 1–2 ms pulse) |
| 09 | `Tarea_2` | **Homework 2:** an encoder sets the duty cycle of four PWM channels across TIM3/TIM4/TIM5/TIM9/TIM10. They drive an **RGB LED** and an RC‑filtered **PWM DAC** (0–3.3 V). The encoder switch on EXTI cycles through modes |
| 10 | `Tarea_3` | **Homework 3, UART + DMA + HAL**, built in stages, each one a selectable app: (0) blinky, (1) blocking USART2, (2) RX interrupt with a **ring buffer** and line assembler, (3) **DMA reception with IDLE‑line detection**, (4) **ping‑pong double buffering** with DMA TX. It ends in a serial command shell (`blink`, `pwm`, backspace editing) that controls a TIM2 PWM output |

## Skills and approaches

- **Register‑level programming** straight from the RM0383 reference manual: clock gating,
  GPIO modes, alternate functions, timer prescaler/auto‑reload math, interrupt flags.
- **Interrupt‑driven design:** short ISRs that set flags, and a main loop that does the work.
  NVIC configuration and EXTI line routing.
- **Finite state machines** whose events come from interrupt flags.
- **Timers in every mode:** periodic interrupts, PWM generation, encoder interface,
  servo control and PWM used as a DAC.
- **From bare metal to HAL:** the same peripherals written both ways, to understand what
  the HAL wraps.
- **Serial communication patterns:** blocking calls, then interrupt with a ring buffer,
  then DMA with IDLE detection, then ping‑pong buffers, each step lowering CPU load.
- **Toolchain:** CMake/CMakePresets, GCC ARM, linker scripts, startup code and VS Code
  debugging, with no IDE‑generated code.

Author: Mateo Quiceno Zapata
