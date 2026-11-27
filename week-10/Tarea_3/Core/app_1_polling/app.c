/**
 ******************************************************************************
 * @file           : app.c (Stage 1: polling)
 * @author         : Mateo Quiceno Zapata
 * @brief          : USART2 with blocking calls: banner, a tick line every
 *                   second and a description of every key received.
 ******************************************************************************
 *** DESCRIPTION

 HAL_UART_Transmit() and HAL_UART_Receive() block: the CPU waits inside them.
 app_loop() waits at most RX_TIMEOUT_MS for a key, so the tick line keeps
 coming every second.

 ***WEAK POINTS (seen on the board)

 - RX_TIMEOUT_MS = HAL_MAX_DELAY -> the loop freezes waiting for a key, the
   tick lines stop (the blinky keeps going: its own interrupt).
 - Pasting "0123456789" -> most characters are lost: while one rx: line is
   being sent (~1.2 ms) new bytes overrun the single receive register.

 ***TERMINAL OUTPUT

   tick N at T ms      -> every second, T grows by ~1000
   rx: 'a' 0x61        -> one line per key
   rx: '.' 0x0D        -> Enter (non printable bytes shown as '.')
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "stm32f4xx_hal.h"
#include "app.h"
#include "system.h"

//Constants
#define TX_TIMEOUT_MS 100U //timeout for transmitting
#define RX_TIMEOUT_MS 10U  //timeout for receiving
#define TICK_PERIOD_MS 1000U //one line per second

//Handles (static: only for this app)
static UART_HandleTypeDef huart2;

//Functions
static void uart2_init(void); //Initialize USART2
static void uart2_send_text(const char *text); //sends a string, blocking

void app_init(void)
{
	uart2_init(); //Initialize gpios, clocks and instance for uart
	uart2_send_text("\r\nStage 1: polling. Type something.\r\n"); //create first msg
}

void app_loop(void)
{
	static uint32_t last_tick_ms = 0U; //begins in 0, keeps its value between calls
	static uint32_t tick_count = 0U; //begins in 0, keeps its value between calls
	char msg[48]; //line to send
	uint8_t byte = 0U; //received key

	/*Tick line*/
	uint32_t now = HAL_GetTick(); //looks at current tick
	if ((now - last_tick_ms) >= TICK_PERIOD_MS) //unsigned subtraction survives the wrap
	{
		last_tick_ms = now; //refresh
		tick_count++; //increment per second
		(void)snprintf(msg, sizeof msg, "tick %lu at %lu ms\r\n", (unsigned long)tick_count, (unsigned long)now); //build into msg
		uart2_send_text(msg); //shows in terminal
	}

	/*Key*/
	if (HAL_UART_Receive(&huart2, &byte, 1U, RX_TIMEOUT_MS) == HAL_OK) //HAL_OK = key arrived, HAL_TIMEOUT = nothing in 10 ms
	{
		char shown = ((byte >= 0x20U) && (byte < 0x7FU)) ? (char)byte : '.'; //printable ASCII or '.'
		(void)snprintf(msg, sizeof msg, "rx: '%c' 0x%02X\r\n", shown, (unsigned int)byte); //build into msg, %02X = 2 hex digits
		uart2_send_text(msg); //shows in terminal
	}
}

//Hardware
static void uart2_init(void)
{
	GPIO_InitTypeDef gpio = {0}; //pin settings

	/*Clocks*/
	__HAL_RCC_USART2_CLK_ENABLE(); //enable USART2 (APB1)
	__HAL_RCC_GPIOA_CLK_ENABLE(); //enable GPIOA, where PA2/PA3 are

	/*PA2 TX, PA3 RX*/
	gpio.Pin = GPIO_PIN_2 | GPIO_PIN_3; //PA2 TX, PA3 RX
	gpio.Mode = GPIO_MODE_AF_PP; //alternate function
	gpio.Pull = GPIO_PULLUP; //idle UART line is high
	gpio.Speed = GPIO_SPEED_FREQ_HIGH; //high speed
	gpio.Alternate = GPIO_AF7_USART2; //AF7: USART2
	HAL_GPIO_Init(GPIOA, &gpio); //Initialization

	/*USART2: 115200 8N1*/
	huart2.Instance = USART2; //select
	huart2.Init.BaudRate = 115200U; //bits per second, must match the terminal
	huart2.Init.WordLength = UART_WORDLENGTH_8B; //8 data bits per character
	huart2.Init.StopBits = UART_STOPBITS_1; //one stop bit closes each character
	huart2.Init.Parity = UART_PARITY_NONE; //not using parity
	huart2.Init.Mode = UART_MODE_TX_RX; //transmit and receive
	huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE; //no flow control
	huart2.Init.OverSampling = UART_OVERSAMPLING_16; //reads 16 times each bit
	if (HAL_UART_Init(&huart2) != HAL_OK) { //calls the weak empty HAL_UART_MspInit: clocks and pins must be ready
		Error_Handler(); //checks error
	}
}

static void uart2_send_text(const char *text) {
	(void)HAL_UART_Transmit(&huart2, (const uint8_t *)text, (uint16_t)strlen(text), TX_TIMEOUT_MS); //blocks until sent, (void): result ignored on purpose
}
