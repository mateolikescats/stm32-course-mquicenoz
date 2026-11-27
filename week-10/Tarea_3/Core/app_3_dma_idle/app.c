/**
 ******************************************************************************
 * @file           : app.c (Stage 3: DMA + IDLE)
 * @author         : Mateo Quiceno Zapata
 * @brief          : DMA copies USART2 bytes into rx_buf without the CPU. The
 *                   CPU gets one event per burst and prints it.
 ******************************************************************************
 *** DESCRIPTION

 HAL_UARTEx_ReceiveToIdle_DMA() starts DMA1 stream 5 and the IDLE interrupt.
 The reception ends at the first of two events, and the HAL reports it in
 HAL_UARTEx_RxEventCallback(huart, Size):

   +-----------------------+---------------------------------+------------+
   | Event                 | Meaning                         | Stops RX?  |
   +-----------------------+---------------------------------+------------+
   | HAL_UART_RXEVENT_IDLE | line quiet for one frame        | yes        |
   |                       | (86.8 us at 115200)             |            |
   | HAL_UART_RXEVENT_TC   | buffer full (32 bytes)          | yes        |
   | HAL_UART_RXEVENT_HT   | buffer half full -> switched off| no         |
   +-----------------------+---------------------------------+------------+

 No commands in this stage: it only prints each block. They come back in
 Stage 4.

 ***WEAK POINTS (seen on the board)

 - One buffer: while app_loop is slow, the DMA writes again from position 0
   ("hello" + "x" -> 'xello').
 - A 40-char paste is split: [TC 32 bytes] + [IDLE  8 bytes].

 ***SOFTWARE STRUCTURE

 - HAL_UART_MspInit() holds all the hardware around USART2, in order:
   clocks -> pins -> DMA stream -> interrupts. HAL_UART_Init() calls it.
 - uart2_init() keeps only the handle settings.
 - The callback writes size, type and ready; app_loop claims them with the
   interrupts off, so a new event can't change one between the two reads.

 ***INTERRUPT / DMA MAP

 +-------------------+-------------------------+----------+----------------------+
 | IRQ               | Handler                 | Priority | Brings               |
 +-------------------+-------------------------+----------+----------------------+
 | USART2_IRQn       | USART2_IRQHandler       | 2        | IDLE, UART errors    |
 | DMA1_Stream5_IRQn | DMA1_Stream5_IRQHandler | 2        | TC (buffer full)     |
 +-------------------+-------------------------+----------+----------------------+
 USART2_RX = DMA1, stream 5, channel 4 (RM0383, DMA1 request mapping).
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
#define RX_BUF_SIZE    32U //DMA buffer: a longer burst ends with TC
#define EVENT_NAME_LEN 8U //"IDLE" + '\0' fits

//Handles (static at file level: the HAL keeps their addresses after init)
static UART_HandleTypeDef huart2;
static DMA_HandleTypeDef hdma_usart2_rx; //linked to huart2 by __HAL_LINKDMA

//Reception: the DMA and the callback write, app_loop reads
static uint8_t rx_buf[RX_BUF_SIZE]; //the DMA writes here, static: alive for the whole program
static volatile uint16_t rx_event_size = 0U; //bytes in rx_buf at the event
static volatile HAL_UART_RxEventTypeTypeDef rx_event_type = HAL_UART_RXEVENT_IDLE; //IDLE, TC or HT
static volatile bool rx_event_ready = false; //a block is waiting to be printed
static volatile uint32_t rx_error = 0U; //HAL error code, set by the interrupt, cleared by app_loop

//Functions
static void uart2_init(void); //USART2 handle settings (hardware goes in HAL_UART_MspInit)
static void uart2_start_rx(void); //starts DMA until idle, switches HT off
static void uart2_send_text(const char *text); //sends a string, blocking
static void event_name(HAL_UART_RxEventTypeTypeDef type, char *name, size_t size); //writes "IDLE"/"TC"/"HT" into name
static void print_block(HAL_UART_RxEventTypeTypeDef type, uint16_t size); //prints [TYPE N bytes] 'text'

void app_init(void)
{
	uart2_init(); //handle + HAL_UART_MspInit (clocks, pins, DMA, IRQs)
	uart2_send_text("\r\nStage 3: DMA reception until idle.\r\n"); //banner
	uart2_start_rx(); //first reception
}

void app_loop(void)
{
	/*Part 1: claim the event*/
	if (rx_event_ready)
	{
		__disable_irq(); //a new event can't change size/type between the two reads
		uint16_t size_aux = rx_event_size; //copy size
		HAL_UART_RxEventTypeTypeDef type_aux = rx_event_type; //copy type
		rx_event_ready = false; //event taken
		__enable_irq(); //only a few instructions with interrupts off
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
		text = "IDLE"; //line went quiet
	}
	else if (type == HAL_UART_RXEVENT_TC)
	{
		text = "TC"; //buffer full
	}
	else if (type == HAL_UART_RXEVENT_HT)
	{
		text = "HT"; //half buffer (only if HT was left on)
	}
	(void)snprintf(name, size, "%s", text); //written into the caller's array
}

static void print_block(HAL_UART_RxEventTypeTypeDef type, uint16_t size)
{
	char name[EVENT_NAME_LEN]; //event name, filled by event_name()
	char msg[24U + RX_BUF_SIZE]; //header + up to 32 characters + "'\r\n"
	size_t used = 0U; //characters already in msg

	if (size > RX_BUF_SIZE)
	{
		size = RX_BUF_SIZE; //never read past rx_buf
	}

	event_name(type, name, sizeof name); //"IDLE", "TC" or "HT"
	int n = snprintf(msg, sizeof msg, "[%s %2u bytes] '", name, (unsigned int)size); //header, %2u = 2 columns
	if ((n < 0) || ((size_t)n >= sizeof msg))
	{
		return; //header did not fit
	}
	used = (size_t)n; //continue after the header

	for (uint16_t i = 0U; i < size; i++)
	{
		uint8_t b = rx_buf[i]; //byte copied by the DMA
		msg[used] = ((b >= 0x20U) && (b < 0x7FU)) ? (char)b : '.'; //printable ASCII or '.'
		used++;
	}

	(void)snprintf(&msg[used], sizeof msg - used, "'\r\n"); //close the quote, adds '\0'
	uart2_send_text(msg); //shows in terminal
}

//Hardware
static void uart2_init(void)
{
	/*USART2: 115200 8N1*/
	huart2.Instance = USART2; //select
	huart2.Init.BaudRate = 115200U; //bits per second, must match the terminal
	huart2.Init.WordLength = UART_WORDLENGTH_8B; //8 data bits per character
	huart2.Init.StopBits = UART_STOPBITS_1; //one stop bit closes each character
	huart2.Init.Parity = UART_PARITY_NONE; //not using parity
	huart2.Init.Mode = UART_MODE_TX_RX; //transmit and receive
	huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE; //no RTS/CTS (keeps PA1 free for PWM)
	huart2.Init.OverSampling = UART_OVERSAMPLING_16; //reads 16 times each bit
	if (HAL_UART_Init(&huart2) != HAL_OK) //calls our HAL_UART_MspInit() before writing the registers
	{
		Error_Handler(); //checks error
	}
}

void HAL_UART_MspInit(UART_HandleTypeDef *huart) //replaces the HAL's weak empty version
{
	GPIO_InitTypeDef gpio = {0}; //pin settings

	if (huart->Instance != USART2)
	{
		return; //only USART2 is ours
	}

	/*1. Clocks (DMA clock before HAL_DMA_Init, or the settings are lost)*/
	__HAL_RCC_USART2_CLK_ENABLE(); //enable USART2 (APB1)
	__HAL_RCC_GPIOA_CLK_ENABLE(); //enable GPIOA, where PA2/PA3 are
	__HAL_RCC_DMA1_CLK_ENABLE(); //enable DMA1 (AHB1)

	/*2. PA2 TX, PA3 RX*/
	gpio.Pin = GPIO_PIN_2 | GPIO_PIN_3; //PA2 TX, PA3 RX
	gpio.Mode = GPIO_MODE_AF_PP; //alternate function
	gpio.Pull = GPIO_PULLUP; //idle UART line is high
	gpio.Speed = GPIO_SPEED_FREQ_HIGH; //high speed
	gpio.Alternate = GPIO_AF7_USART2; //AF7: USART2
	HAL_GPIO_Init(GPIOA, &gpio); //Initialization

	/*3. DMA1 stream 5 channel 4: USART2_RX*/
	hdma_usart2_rx.Instance = DMA1_Stream5; //fixed pair in RM0383
	hdma_usart2_rx.Init.Channel = DMA_CHANNEL_4; //request channel of USART2_RX
	hdma_usart2_rx.Init.Direction = DMA_PERIPH_TO_MEMORY; //USART2->DR -> rx_buf
	hdma_usart2_rx.Init.PeriphInc = DMA_PINC_DISABLE; //always the same data register
	hdma_usart2_rx.Init.MemInc = DMA_MINC_ENABLE; //one position further in rx_buf each byte
	hdma_usart2_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE; //8-bit reads
	hdma_usart2_rx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE; //8-bit writes
	hdma_usart2_rx.Init.Mode = DMA_NORMAL; //stops when the buffer is full (no circular)
	hdma_usart2_rx.Init.Priority = DMA_PRIORITY_LOW; //only stream in use
	hdma_usart2_rx.Init.FIFOMode = DMA_FIFOMODE_DISABLE; //direct mode, byte by byte
	if (HAL_DMA_Init(&hdma_usart2_rx) != HAL_OK)
	{
		Error_Handler(); //checks error
	}
	__HAL_LINKDMA(huart, hdmarx, hdma_usart2_rx); //UART handle <-> DMA handle, both ways

	/*4. Interrupts*/
	HAL_NVIC_SetPriority(DMA1_Stream5_IRQn, 2, 0); //TC and DMA errors
	HAL_NVIC_EnableIRQ(DMA1_Stream5_IRQn); //the NVIC accepts DMA1 stream 5
	HAL_NVIC_SetPriority(USART2_IRQn, 2, 0); //IDLE and UART errors
	HAL_NVIC_EnableIRQ(USART2_IRQn); //the NVIC accepts USART2
}

static void uart2_start_rx(void)
{
	if (HAL_UARTEx_ReceiveToIdle_DMA(&huart2, rx_buf, RX_BUF_SIZE) == HAL_OK) //ends at IDLE or when full
	{
		__HAL_DMA_DISABLE_IT(&hdma_usart2_rx, DMA_IT_HT); //HAL turns HT on at every start: switch it off every time
	}
}

static void uart2_send_text(const char *text)
{
	(void)HAL_UART_Transmit(&huart2, (const uint8_t *)text, (uint16_t)strlen(text), TX_TIMEOUT_MS); //blocks until sent
}

//Interrupt handlers: move data + flag, no decisions
void USART2_IRQHandler(void)
{
	HAL_UART_IRQHandler(&huart2); //IDLE and UART errors
}

void DMA1_Stream5_IRQHandler(void)
{
	HAL_DMA_IRQHandler(&hdma_usart2_rx); //buffer full (TC) and DMA errors, name must match the vector table
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
	if (huart->Instance == USART2)
	{
		rx_event_size = Size; //bytes now in rx_buf
		rx_event_type = HAL_UARTEx_GetRxEventType(huart); //IDLE, TC or HT
		rx_event_ready = true; //app_loop prints it
		uart2_start_rx(); //IDLE and TC stop the reception: start a new one
	}
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
	if (huart->Instance == USART2)
	{
		rx_error = huart->ErrorCode; //app_loop prints it
		uart2_start_rx(); //in DMA mode every error stops the reception: restart it
	}
}
