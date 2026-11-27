/*
 * app.c (Stage 3: DMA + IDLE)
 * DMA receives USART2 bytes until the line is idle, each block is printed.
 * Author: Mateo Quiceno Zapata
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "stm32f4xx_hal.h"
#include "app.h"
#include "system.h"

//Constants
#define TX_TIMEOUT_MS  100U //timeout for transmitting
#define RX_BUF_SIZE    32U //DMA buffer size
#define EVENT_NAME_LEN 8U //event name size

//Handles
static UART_HandleTypeDef huart2;
static DMA_HandleTypeDef hdma_usart2_rx; //RX DMA

//Reception
static uint8_t rx_buf[RX_BUF_SIZE]; //DMA buffer
static volatile uint16_t rx_event_size = 0U; //bytes received
static volatile HAL_UART_RxEventTypeTypeDef rx_event_type = HAL_UART_RXEVENT_IDLE; //IDLE, TC or HT
static volatile bool rx_event_ready = false; //block ready
static volatile uint32_t rx_error = 0U; //error code

//Functions
static void uart2_init(void); //UART settings
static void uart2_start_rx(void); //start DMA reception
static void uart2_send_text(const char *text); //sends a string, blocking
static void event_name(HAL_UART_RxEventTypeTypeDef type, char *name, size_t size); //event name
static void print_block(HAL_UART_RxEventTypeTypeDef type, uint16_t size); //print block

void app_init(void)
{
	uart2_init(); //init UART
	uart2_send_text("\r\nStage 3: DMA reception until idle.\r\n"); //banner
	uart2_start_rx(); //first reception
}

void app_loop(void)
{
	/*Part 1: claim the event*/
	if (rx_event_ready)
	{
		__disable_irq(); //interrupts off
		uint16_t size_aux = rx_event_size; //copy size
		HAL_UART_RxEventTypeTypeDef type_aux = rx_event_type; //copy type
		rx_event_ready = false; //event taken
		__enable_irq(); //interrupts on
		print_block(type_aux, size_aux); //shows in terminal
	}

	/*Part 2: report reception errors*/
	if (rx_error != 0U)
	{
		char msg[48]; //line to send
		uint32_t code = rx_error; //copy error
		rx_error = 0U; //clear for next
		(void)snprintf(msg, sizeof msg, "[uart error 0x%02lX, reception restarted]\r\n",
		               (unsigned long)code); //0x04 framing, 0x02 noise, 0x08 overrun
		uart2_send_text(msg); //shows in terminal
	}
}

//Actions: printing
static void event_name(HAL_UART_RxEventTypeTypeDef type, char *name, size_t size)
{
	const char *text = "?"; //unknown event

	if (type == HAL_UART_RXEVENT_IDLE)
	{
		text = "IDLE"; //standby
	}
	else if (type == HAL_UART_RXEVENT_TC)
	{
		text = "TC"; //buffer full
	}
	else if (type == HAL_UART_RXEVENT_HT)
	{
		text = "HT"; //half buffer
	}
	(void)snprintf(name, size, "%s", text); //copy name
}

static void print_block(HAL_UART_RxEventTypeTypeDef type, uint16_t size)
{
	char name[EVENT_NAME_LEN]; //event name
	char msg[24U + RX_BUF_SIZE]; //line to send
	size_t used = 0U; //characters already in msg

	if (size > RX_BUF_SIZE)
	{
		size = RX_BUF_SIZE; //limit size
	}

	event_name(type, name, sizeof name); //"IDLE", "TC" or "HT"
	int n = snprintf(msg, sizeof msg, "[%s %2u bytes] '", name, (unsigned int)size); //header
	if ((n < 0) || ((size_t)n >= sizeof msg))
	{
		return; //header did not fit
	}
	used = (size_t)n; //after header

	for (uint16_t i = 0U; i < size; i++)
	{
		uint8_t b = rx_buf[i]; //received byte
		msg[used] = ((b >= 0x20U) && (b < 0x7FU)) ? (char)b : '.'; //printable ASCII (char) or '.'
		used++;
	}

	(void)snprintf(&msg[used], sizeof msg - used, "'\r\n"); //end of line
	uart2_send_text(msg); //shows in terminal
}

//Hardware
static void uart2_init(void)
{
	/*USART2: 115200 8N1*/
	huart2.Instance = USART2; //select
	huart2.Init.BaudRate = 115200U; //115200 baud
	huart2.Init.WordLength = UART_WORDLENGTH_8B; //8 data bits
	huart2.Init.StopBits = UART_STOPBITS_1; //1 stop bit
	huart2.Init.Parity = UART_PARITY_NONE; //not using parity
	huart2.Init.Mode = UART_MODE_TX_RX; //transmit and receive
	huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE; //no flow control
	huart2.Init.OverSampling = UART_OVERSAMPLING_16; //oversampling 16 times
	if (HAL_UART_Init(&huart2) != HAL_OK) //calls HAL_UART_MspInit()
	{
		Error_Handler(); //checks error
	}
}

void HAL_UART_MspInit(UART_HandleTypeDef *huart) //UART hardware
{
	GPIO_InitTypeDef gpio = {0}; //pin settings

	if (huart->Instance != USART2)
	{
		return; //only USART2
	}

	/*1. Clocks*/
	__HAL_RCC_USART2_CLK_ENABLE(); //enable USART2 (APB1)
	__HAL_RCC_GPIOA_CLK_ENABLE(); //enable GPIOA
	__HAL_RCC_DMA1_CLK_ENABLE(); //enable DMA1 (AHB1)

	/*2. PA2 TX, PA3 RX*/
	gpio.Pin = GPIO_PIN_2 | GPIO_PIN_3; //PA2 transmit, PA3 receive
	gpio.Mode = GPIO_MODE_AF_PP; //alternate function
	gpio.Pull = GPIO_PULLUP; //idle UART line is high
	gpio.Speed = GPIO_SPEED_FREQ_HIGH; //high speed
	gpio.Alternate = GPIO_AF7_USART2; //alternate function for USART2
	HAL_GPIO_Init(GPIOA, &gpio); //Initialization

	/*3. DMA1 stream 5 channel 4: USART2_RX*/
	hdma_usart2_rx.Instance = DMA1_Stream5; //stream 5
	hdma_usart2_rx.Init.Channel = DMA_CHANNEL_4; //channel 4
	hdma_usart2_rx.Init.Direction = DMA_PERIPH_TO_MEMORY; //USART2->DR -> rx_buf
	hdma_usart2_rx.Init.PeriphInc = DMA_PINC_DISABLE; //fixed address
	hdma_usart2_rx.Init.MemInc = DMA_MINC_ENABLE; //memory increments
	hdma_usart2_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE; //8-bit reads
	hdma_usart2_rx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE; //8-bit writes
	hdma_usart2_rx.Init.Mode = DMA_NORMAL; //not circular
	hdma_usart2_rx.Init.Priority = DMA_PRIORITY_LOW; //low priority
	hdma_usart2_rx.Init.FIFOMode = DMA_FIFOMODE_DISABLE; //no FIFO
	if (HAL_DMA_Init(&hdma_usart2_rx) != HAL_OK)
	{
		Error_Handler(); //checks error
	}
	__HAL_LINKDMA(huart, hdmarx, hdma_usart2_rx); //link DMA to UART

	/*4. Interrupts*/
	HAL_NVIC_SetPriority(DMA1_Stream5_IRQn, 2, 0); //TC and DMA errors
	HAL_NVIC_EnableIRQ(DMA1_Stream5_IRQn); //enable DMA RX interrupt
	HAL_NVIC_SetPriority(USART2_IRQn, 2, 0); //IDLE and UART errors
	HAL_NVIC_EnableIRQ(USART2_IRQn); //enable USART2 interrupt
}

static void uart2_start_rx(void)
{
	if (HAL_UARTEx_ReceiveToIdle_DMA(&huart2, rx_buf, RX_BUF_SIZE) == HAL_OK) //ends at IDLE or when full
	{
		__HAL_DMA_DISABLE_IT(&hdma_usart2_rx, DMA_IT_HT); //HT interrupt off
	}
}

static void uart2_send_text(const char *text)
{
	(void)HAL_UART_Transmit(&huart2, (const uint8_t *)text, (uint16_t)strlen(text), TX_TIMEOUT_MS); //blocks until sent
}

//Interrupt handlers
void USART2_IRQHandler(void)
{
	HAL_UART_IRQHandler(&huart2); //IDLE and UART errors
}

void DMA1_Stream5_IRQHandler(void)
{
	HAL_DMA_IRQHandler(&hdma_usart2_rx); //buffer full (TC)
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
	if (huart->Instance == USART2)
	{
		rx_event_size = Size; //bytes received
		rx_event_type = HAL_UARTEx_GetRxEventType(huart); //IDLE, TC or HT
		rx_event_ready = true; //app_loop prints it
		uart2_start_rx(); //restart reception
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
