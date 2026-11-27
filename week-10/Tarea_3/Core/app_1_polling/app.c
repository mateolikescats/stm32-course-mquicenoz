/*
 * app.c (Stage 1: polling)
 * USART2 with blocking calls: tick every second and echo of each key.
 * Author: Mateo Quiceno Zapata
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

//Handles
static UART_HandleTypeDef huart2;

//Functions
static void uart2_init(void); //Initialize USART2
static void uart2_send_text(const char *text); //sends a string, blocking

void app_init(void)
{
	uart2_init(); //init UART
	uart2_send_text("\r\nStage 1: polling. Type something.\r\n"); //create first msg
}

void app_loop(void)
{
	static uint32_t last_tick_ms = 0U; //last tick time
	static uint32_t tick_count = 0U; //tick counter
	char msg[48]; //line to send
	uint8_t byte = 0U; //received key

	/*Tick line*/
	uint32_t now = HAL_GetTick(); //current time
	if ((now - last_tick_ms) >= TICK_PERIOD_MS) //one second passed
	{
		last_tick_ms = now; //refresh
		tick_count++; //count
		(void)snprintf(msg, sizeof msg, "tick %lu at %lu ms\r\n", (unsigned long)tick_count, (unsigned long)now); //build into msg
		uart2_send_text(msg); //shows in terminal
	}

	/*Key*/
	if (HAL_UART_Receive(&huart2, &byte, 1U, RX_TIMEOUT_MS) == HAL_OK) //key received
	{
		char shown = ((byte >= 0x20U) && (byte < 0x7FU)) ? (char)byte : '.'; //printable ASCII or '.'
		(void)snprintf(msg, sizeof msg, "rx: '%c' 0x%02X\r\n", shown, (unsigned int)byte); //build into msg
		uart2_send_text(msg); //shows in terminal
	}
}

//Hardware
static void uart2_init(void)
{
	GPIO_InitTypeDef gpio = {0}; //pin settings

	/*Clocks*/
	__HAL_RCC_USART2_CLK_ENABLE(); //enable USART2 (APB1)
	__HAL_RCC_GPIOA_CLK_ENABLE(); //enable GPIOA

	/*PA2 TX, PA3 RX*/
	gpio.Pin = GPIO_PIN_2 | GPIO_PIN_3; //PA2 TX, PA3 RX
	gpio.Mode = GPIO_MODE_AF_PP; //alternate function
	gpio.Pull = GPIO_PULLUP; //idle UART line is high
	gpio.Speed = GPIO_SPEED_FREQ_HIGH; //high speed
	gpio.Alternate = GPIO_AF7_USART2; //AF7: USART2
	HAL_GPIO_Init(GPIOA, &gpio); //Initialization

	/*USART2: 115200 8N1*/
	huart2.Instance = USART2; //select
	huart2.Init.BaudRate = 115200U; //115200 baud
	huart2.Init.WordLength = UART_WORDLENGTH_8B; //8 data bits
	huart2.Init.StopBits = UART_STOPBITS_1; //1 stop bit
	huart2.Init.Parity = UART_PARITY_NONE; //not using parity
	huart2.Init.Mode = UART_MODE_TX_RX; //transmit and receive
	huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE; //no flow control
	huart2.Init.OverSampling = UART_OVERSAMPLING_16; //oversampling 16
	if (HAL_UART_Init(&huart2) != HAL_OK) {
		Error_Handler(); //checks error
	}
}

static void uart2_send_text(const char *text) {
	(void)HAL_UART_Transmit(&huart2, (const uint8_t *)text, (uint16_t)strlen(text), TX_TIMEOUT_MS); //blocking send
}
