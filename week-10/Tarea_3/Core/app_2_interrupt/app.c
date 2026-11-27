/**
 ******************************************************************************
 * @file           : app.c (Stage 2: interrupt)
 * @author         : Mateo Quiceno Zapata
 * @brief          : USART2 receives one byte per interrupt into a ring buffer.
 *                   The main loop assembles lines and runs the commands.
 ******************************************************************************
 *** DESCRIPTION

 HAL_UART_Receive_IT() asks for ONE byte and returns at once. When it arrives
 the CPU jumps to USART2_IRQHandler(), the HAL stores it in rx_byte and calls
 HAL_UART_RxCpltCallback(), which pushes it into the ring and asks for the
 next byte. app_loop() takes the bytes out whenever it has time, so pasted
 text is no longer lost.

 ***SOFTWARE STRUCTURE

 - Interrupt (producer) writes rx_head, app_loop (consumer) writes rx_tail.
   Each index has one writer only, so no interrupt lock is needed.
 - Line assembly: echo every key, Enter (\r, \n or \r\n) runs the line.
 - cmd_parser.c splits the line, commands.c answers (one line).
 - An overrun aborts the reception: HAL_UART_ErrorCallback() restarts it, or
   the UART would stay deaf forever.

 ***RING BUFFER (RX_RING_SIZE = 64, one slot always empty)

        rx_tail             rx_head
           v                   v
   +---+---+---+---+---+---+---+---+---
   |   | h | e | l | l | o |   |   | ...
   +---+---+---+---+---+---+---+---+---
   empty: rx_tail == rx_head     full: (rx_head + 1) & 63 == rx_tail

 ***INTERRUPT MAP

 +-------------+-----------------------+----------+---------------------------+
 | IRQ         | Handler               | Priority | Callback                  |
 +-------------+-----------------------+----------+---------------------------+
 | USART2_IRQn | USART2_IRQHandler     | 2        | RxCplt (byte), Error      |
 | TIM9 (blink)| TIM1_BRK_TIM9_IRQHand.| 3        | PeriodElapsed (blinky.c)  |
 +-------------+-----------------------+----------+---------------------------+
 USART2 is more urgent than the blinky: a byte read late is lost, a blink
 that starts a few microseconds late is invisible.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "stm32f4xx_hal.h"
#include "app.h"
#include "system.h"
#include "cmd_parser.h"
#include "commands.h"

//Constants
#define TX_TIMEOUT_MS 100U //timeout for transmitting
#define RX_RING_SIZE  64U //power of two, "& (size - 1)" wraps the index
#define LINE_MAX_LEN  63U //longest line accepted
#define REPLY_SIZE    96U //longest command answer

//Handles (static: only for this file)
static UART_HandleTypeDef huart2;

//Reception: the interrupt writes, app_loop reads
static uint8_t rx_byte; //HAL drops each byte here; only used inside the interrupt, so not volatile
static uint8_t rx_ring[RX_RING_SIZE]; //ring buffer between interrupt (producer) and app_loop (consumer)
static volatile uint16_t rx_head = 0U; //next free slot, written only by the interrupt
static volatile uint16_t rx_tail = 0U; //next byte to read, written only by app_loop
static volatile uint32_t rx_error = 0U; //HAL error code, set by the interrupt, cleared by app_loop

//Line being typed (only the main loop uses them)
static char line[LINE_MAX_LEN + 1U]; //+1 for the final '\0'
static uint16_t line_len = 0U; //characters stored in line[]
static bool line_too_long = false; //set when the user typed past LINE_MAX_LEN
static uint8_t previous_byte = 0U; //last byte seen, to detect the '\n' of "\r\n"

//Functions
static void uart2_init(void); //UART initialization
static void uart2_start_rx(void); //asks the HAL for the next byte by interrupt
static void uart2_send_bytes(const uint8_t *data, uint16_t size); //blocking send of raw bytes
static void uart2_send_text(const char *text); //sends a '\0'-terminated string
static void line_add_byte(uint8_t new_byte); //adds one byte to the line, echoes it, runs the line on Enter
static void line_execute(void); //parses and runs the finished line, then prints a new prompt

void app_init(void)
{
	uart2_init(); //clocks, pins, USART2 and its interrupt
	uart2_send_text("\r\nStage 2: interrupt reception.\r\n> "); //banner and first prompt
}

void app_loop(void)
{
	/*Part 1: empty the ring*/
	while (rx_tail != rx_head) //there are unread bytes
	{
		uint8_t b = rx_ring[rx_tail]; //take the oldest byte
		rx_tail = (uint16_t)((rx_tail + 1U) & (RX_RING_SIZE - 1U)); //advance tail, 63 -> 0
		line_add_byte(b); //build the line with it
	}

	/*Part 2: report reception errors*/
	if (rx_error != 0U)
	{
		char msg[48]; //line to send
		uint32_t code = rx_error; //copy error
		rx_error = 0U; //clear for next
		(void)snprintf(msg, sizeof msg, "\r\n[uart error 0x%02lX, reception restarted]\r\n", (unsigned long)code); //0x08 = overrun
		uart2_send_text(msg); //shows in terminal
	}
}

//Actions: line assembly
static void line_add_byte(uint8_t new_byte)
{
	if ((new_byte == '\n') && (previous_byte == '\r'))
	{
		//second half of \r\n: already handled
	}
	else if ((new_byte == '\r') || (new_byte == '\n')) //Enter, whatever the terminal sends
	{
		uart2_send_text("\r\n"); //move the cursor to a new line
		line_execute(); //run what was typed
	}
	else if (line_len < LINE_MAX_LEN) //still room in line[]
	{
		line[line_len] = (char)new_byte; //store the character
		line_len++; //next free position
		uart2_send_bytes(&new_byte, 1U); //shows what is typed (echo)
	}
	else
	{
		line_too_long = true; //line[] is full: byte dropped, reported on Enter
	}
	previous_byte = new_byte; //for the next "\r\n" test
}

static void line_execute(void)
{
	if (line_too_long)
	{
		uart2_send_text("error: line too long\r\n"); //shows in terminal
	}
	else
	{
		cmd_t cmd; //result of the parser
		line[line_len] = '\0'; //UART bytes have no end mark: add it before any string function
		if (cmd_parse(line, &cmd)) //false = empty line: only a new prompt
		{
			char reply[REPLY_SIZE]; //answer of the command
			commands_execute(&cmd, reply, sizeof reply); //commands.c writes the answer
			uart2_send_text(reply); //shows in terminal
			uart2_send_text("\r\n"); //new line
		}
	}

	uart2_send_text("> "); //new prompt
	line_len = 0U; //empty the line in every case
	line_too_long = false; //rearm for the next line
}

//Hardware
static void uart2_init(void)
{
	GPIO_InitTypeDef gpio = {0}; //pin settings

	/*1. Clocks*/
	__HAL_RCC_USART2_CLK_ENABLE(); //enable USART2 (APB1)
	__HAL_RCC_GPIOA_CLK_ENABLE(); //enable GPIOA, where PA2/PA3 are

	/*2. PA2 TX, PA3 RX*/
	gpio.Pin = GPIO_PIN_2 | GPIO_PIN_3; //PA2 TX, PA3 RX
	gpio.Mode = GPIO_MODE_AF_PP; //alternate function
	gpio.Pull = GPIO_PULLUP; //idle UART line is high
	gpio.Speed = GPIO_SPEED_FREQ_HIGH; //high speed
	gpio.Alternate = GPIO_AF7_USART2; //AF7: USART2
	HAL_GPIO_Init(GPIOA, &gpio); //Initialization

	/*3. USART2: 115200 8N1*/
	huart2.Instance = USART2; //select
	huart2.Init.BaudRate = 115200U; //bits per second, must match the terminal
	huart2.Init.WordLength = UART_WORDLENGTH_8B; //8 data bits per character
	huart2.Init.StopBits = UART_STOPBITS_1; //one stop bit closes each character
	huart2.Init.Parity = UART_PARITY_NONE; //not using parity
	huart2.Init.Mode = UART_MODE_TX_RX; //transmit and receive
	huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE; //no RTS/CTS (keeps PA1 free for PWM)
	huart2.Init.OverSampling = UART_OVERSAMPLING_16; //reads 16 times each bit
	if (HAL_UART_Init(&huart2) != HAL_OK) {
		Error_Handler(); //checks error
	}

	/*4. Interrupt: two switches must be on*/
	HAL_NVIC_SetPriority(USART2_IRQn, 2, 0); //more urgent than the blinky (3): a late byte is lost
	HAL_NVIC_EnableIRQ(USART2_IRQn); //switch 2: the NVIC accepts USART2
	uart2_start_rx(); //switch 1: USART2 may request the interrupt (RXNE + errors)
}

static void uart2_start_rx(void)
{
	(void)HAL_UART_Receive_IT(&huart2, &rx_byte, 1U); //returns at once; one call = one byte, so it is called again after each byte
}

static void uart2_send_bytes(const uint8_t *data, uint16_t size)
{
	(void)HAL_UART_Transmit(&huart2, data, size, TX_TIMEOUT_MS); //blocks until sent or timeout
}

static void uart2_send_text(const char *text)
{
	uart2_send_bytes((const uint8_t *)text, (uint16_t)strlen(text)); //strlen stops at '\0'
}

//Interrupt handlers: move data + flag, no decisions
void USART2_IRQHandler(void)
{
	HAL_UART_IRQHandler(&huart2); //HAL reads flags, stores the byte, calls the callbacks
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
	if (huart->Instance == USART2)
	{
		uint16_t next = (uint16_t)((rx_head + 1U) & (RX_RING_SIZE - 1U)); //where head goes after this byte
		if (next != rx_tail) //one slot stays empty: "full" and "empty" look different
		{
			rx_ring[rx_head] = rx_byte; //store the byte
			rx_head = next; //publish it to app_loop
		}
		//ring full: the byte is dropped
		uart2_start_rx(); //ask for the next byte, or reception stops after this one
	}
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
	if (huart->Instance == USART2)
	{
		rx_error = huart->ErrorCode; //app_loop prints it
		uart2_start_rx(); //an overrun aborts reception: restart it or the UART stays deaf
	}
}
