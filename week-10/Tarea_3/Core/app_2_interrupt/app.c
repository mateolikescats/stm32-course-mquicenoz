/*
 * app.c (Stage 2: interrupt)
 * USART2 receives one byte per interrupt into a ring buffer.
 * The main loop builds lines and runs the commands.
 * Author: Mateo Quiceno Zapata
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
#define RX_RING_SIZE  64U //power of two
#define LINE_MAX_LEN  63U //longest line accepted
#define REPLY_SIZE    96U //longest command answer

//Handles
static UART_HandleTypeDef huart2;

//Reception
static uint8_t rx_byte; //received byte
static uint8_t rx_ring[RX_RING_SIZE]; //ring buffer
static volatile uint16_t rx_head = 0U; //write index (interrupt)
static volatile uint16_t rx_tail = 0U; //read index (app_loop)
static volatile uint32_t rx_error = 0U; //error code

//Line being typed
static char line[LINE_MAX_LEN + 1U]; //+1 for '\0'
static uint16_t line_len = 0U; //length
static bool line_too_long = false; //line overflow
static uint8_t previous_byte = 0U; //for "\r\n"

//Functions
static void uart2_init(void); //UART initialization
static void uart2_start_rx(void); //receive next byte
static void uart2_send_bytes(const uint8_t *data, uint16_t size); //send bytes
static void uart2_send_text(const char *text); //send string
static void line_add_byte(uint8_t new_byte); //add byte to line
static void line_execute(void); //run line

void app_init(void)
{
	uart2_init(); //init UART
	uart2_send_text("\r\nStage 2: interrupt reception.\r\n> "); //banner and first prompt
}

void app_loop(void)
{
	/*Part 1: empty the ring*/
	while (rx_tail != rx_head) //unread bytes
	{
		uint8_t b = rx_ring[rx_tail]; //read byte
		rx_tail = (uint16_t)((rx_tail + 1U) & (RX_RING_SIZE - 1U)); //next index
		line_add_byte(b); //add to line
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
		//ignore \n after \r
	}
	else if ((new_byte == '\r') || (new_byte == '\n')) //Enter
	{
		uart2_send_text("\r\n"); //new line
		line_execute(); //run line
	}
	else if (line_len < LINE_MAX_LEN) //space left in line
	{
		line[line_len] = (char)new_byte; //save character
		line_len++; //looks for next
		uart2_send_bytes(&new_byte, 1U); //shows typed char
	}
	else
	{
		line_too_long = true; //line full
	}
	previous_byte = new_byte; //save last byte
}

static void line_execute(void)
{
	if (line_too_long)
	{
		uart2_send_text("error: line too long\r\n"); //shows in terminal
	}
	else
	{
		cmd_t cmd; //parsed command
		line[line_len] = '\0'; //end string
		if (cmd_parse(line, &cmd)) //not empty
		{
			char reply[REPLY_SIZE]; //answer of the command
			commands_execute(&cmd, reply, sizeof reply); //run command
			uart2_send_text(reply); //shows in terminal
			uart2_send_text("\r\n"); //new line
		}
	}

	uart2_send_text("> "); //new prompt
	line_len = 0U; //clear line
	line_too_long = false; //reset flag
}

//Hardware
static void uart2_init(void)
{
	GPIO_InitTypeDef gpio = {0}; //pin settings

	/*1. Clocks*/
	__HAL_RCC_USART2_CLK_ENABLE(); //enable USART2 (APB1)
	__HAL_RCC_GPIOA_CLK_ENABLE(); //enable GPIOA

	/*2. PA2 TX, PA3 RX*/
	gpio.Pin = GPIO_PIN_2 | GPIO_PIN_3; //PA2 TX, PA3 RX
	gpio.Mode = GPIO_MODE_AF_PP; //alternate function
	gpio.Pull = GPIO_PULLUP; //idle UART line is high
	gpio.Speed = GPIO_SPEED_FREQ_HIGH; //high speed
	gpio.Alternate = GPIO_AF7_USART2; //alternate function USART2
	HAL_GPIO_Init(GPIOA, &gpio); //Initialization

	/*3. USART2: 115200 8N1*/
	huart2.Instance = USART2; //select
	huart2.Init.BaudRate = 115200U; //115200 baud
	huart2.Init.WordLength = UART_WORDLENGTH_8B; //8 data bits
	huart2.Init.StopBits = UART_STOPBITS_1; //1 stop bit
	huart2.Init.Parity = UART_PARITY_NONE; //not using parity
	huart2.Init.Mode = UART_MODE_TX_RX; //transmit and receive
	huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE; //no flow control
	huart2.Init.OverSampling = UART_OVERSAMPLING_16; //oversampling 16 times
	if (HAL_UART_Init(&huart2) != HAL_OK) {
		Error_Handler(); //checks error
	}

	/*4. Interrupt*/
	HAL_NVIC_SetPriority(USART2_IRQn, 2, 0); //priority 2, above blinky
	HAL_NVIC_EnableIRQ(USART2_IRQn); //enable in NVIC
	uart2_start_rx(); //start reception
}

static void uart2_start_rx(void)
{
	(void)HAL_UART_Receive_IT(&huart2, &rx_byte, 1U); //one byte by interrupt
}

static void uart2_send_bytes(const uint8_t *data, uint16_t size)
{
	(void)HAL_UART_Transmit(&huart2, data, size, TX_TIMEOUT_MS); //blocking send
}

static void uart2_send_text(const char *text)
{
	uart2_send_bytes((const uint8_t *)text, (uint16_t)strlen(text)); //strlen stops at null char
}

//Interrupt handlers
void USART2_IRQHandler(void)
{
	HAL_UART_IRQHandler(&huart2); //HAL handles it, calls callbacks
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
	if (huart->Instance == USART2)
	{
		uint16_t next = (uint16_t)((rx_head + 1U) & (RX_RING_SIZE - 1U)); //next write index
		if (next != rx_tail) //ring not full
		{
			rx_ring[rx_head] = rx_byte; //store the byte
			rx_head = next; //advance head
		}
		//full: byte lost
		uart2_start_rx(); //next byte
	}
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
	if (huart->Instance == USART2)
	{
		rx_error = huart->ErrorCode; //app_loop prints it
		uart2_start_rx(); //restart reception
	}
}
