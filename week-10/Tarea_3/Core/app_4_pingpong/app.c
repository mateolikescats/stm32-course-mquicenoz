/*
 * app.c (Stage 4/5/6: ping-pong)
 * Two DMA RX blocks (A/B), DMA TX, line assembly and all commands.
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
#include "pwm.h"

//Constants
#define RX_BLOCK_SIZE    32U //bytes per reception block
#define TX_BUF_SIZE      256U //text waiting to be sent
#define TX_WAIT_MS       50U //max transmit wait
#define LINE_MAX_LEN     63U //longest line accepted
#define REPLY_SIZE       96U //longest command answer
#define KEY_BACKSPACE    0x08U //Backspace
#define KEY_DELETE       0x7FU //Delete (Backspace)

//One reception block
typedef struct
{
	uint8_t data[RX_BLOCK_SIZE]; //the DMA writes here
	volatile uint16_t size; //bytes received
	volatile bool ready; //true: ready to read
} rx_block_t;

//Handles
static UART_HandleTypeDef huart2;
static DMA_HandleTypeDef hdma_usart2_rx; //receiving DMA
static DMA_HandleTypeDef hdma_usart2_tx; //transmitting DMA

//Reception
static rx_block_t rx_block[2]; //block A = 0, block B = 1
static volatile uint8_t rx_dma_index = 0U; //block the DMA is filling
static uint8_t read_index = 0U; //block to read
static volatile bool rx_paused = false; //reception paused
static volatile uint32_t rx_pause_count = 0U; //pause count
static volatile uint32_t rx_error = 0U; //error code
static uint32_t rx_pause_reported = 0U; //pauses already printed

//Transmission
static uint8_t tx_fill[TX_BUF_SIZE]; //text to send
static uint16_t tx_fill_len = 0U; //bytes in tx_fill
static uint8_t tx_dma_buf[TX_BUF_SIZE]; //DMA transmit buffer
static volatile bool tx_busy = false; //transmit running

//Line being typed
static char line[LINE_MAX_LEN + 1U]; //+1 for '\0'
static uint16_t line_len = 0U; //length
static bool line_too_long = false; //line overflow
static uint8_t previous_byte = 0U; //for "\r\n"

//Functions
static void uart2_init(void); //UART settings
static void uart2_start_rx(void); //start DMA reception
static void uart2_send_bytes(const uint8_t *data, uint16_t size); //queue bytes
static void uart2_send_text(const char *text); //queue string
static void uart2_flush(void); //start DMA transmitting
static void line_add_byte(uint8_t new_byte); //add byte to line
static void line_execute(void); //run line

void app_init(void)
{
	uart2_init(); //init UART
	pwm_init(); //PWM on PA1
	uart2_send_text("\r\nStage 4: ping-pong reception.\r\n> "); //banner and first prompt
	uart2_start_rx(); //first reception
}

void app_loop(void)
{
	/*Part 1: read full block*/
	if (rx_block[read_index].ready) //block ready
	{
		bool restart_aux; //restart flag
		uint16_t size = rx_block[read_index].size; //bytes in the block

		for (uint16_t i = 0U; i < size; i++)
		{
			line_add_byte(rx_block[read_index].data[i]); //add to line
		}

		__disable_irq(); //interrupts off
		rx_block[read_index].ready = false; //block free
		restart_aux = rx_paused; //was paused?
		if (restart_aux)
		{
			rx_paused = false; //pause over
			rx_dma_index = read_index; //use freed block
		}
		__enable_irq(); //interrupts on

		if (restart_aux)
		{
			uart2_start_rx(); //restart reception
		}
		read_index = (uint8_t)(1U - read_index); //next block
	}

	/*Part 2: report pauses and errors*/
	if (rx_pause_count != rx_pause_reported) //new pause
	{
		rx_pause_reported = rx_pause_count; //reported
		uart2_send_text("\r\n[reception paused: both blocks busy, data lost]\r\n"); //shows in terminal
	}
	if (rx_error != 0U)
	{
		char msg[48]; //line to send
		uint32_t code = rx_error; //copy error
		rx_error = 0U; //clear for next
		(void)snprintf(msg, sizeof msg, "\r\n[uart error 0x%02lX, reception restarted]\r\n",
		               (unsigned long)code); //0x04 framing, 0x02 noise, 0x08 overrun
		uart2_send_text(msg); //shows in terminal
	}

	/*Part 3: send text*/
	uart2_flush();
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
	else if ((new_byte == KEY_BACKSPACE) || (new_byte == KEY_DELETE)) //Backspace
	{
		if (line_len > 0U) //something to erase
		{
			line_len--; //remove char
			uart2_send_text("\b \b"); //erase on screen
		}
	}
	else if (line_len < LINE_MAX_LEN) //room left
	{
		line[line_len] = (char)new_byte; //save char
		line_len++; //length + 1
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
	gpio.Pin = GPIO_PIN_2 | GPIO_PIN_3; //PA2 TX, PA3 RX
	gpio.Mode = GPIO_MODE_AF_PP; //alternate function
	gpio.Pull = GPIO_PULLUP; //idle UART line is high
	gpio.Speed = GPIO_SPEED_FREQ_HIGH; //high speed
	gpio.Alternate = GPIO_AF7_USART2; //alternate function for USART2
	HAL_GPIO_Init(GPIOA, &gpio); //Initialization

	/*3a. DMA1 stream 5 channel 4: USART2_RX*/
	hdma_usart2_rx.Instance = DMA1_Stream5; //stream 5
	hdma_usart2_rx.Init.Channel = DMA_CHANNEL_4; //channel 4
	hdma_usart2_rx.Init.Direction = DMA_PERIPH_TO_MEMORY; //USART2->DR -> rx_block
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
	__HAL_LINKDMA(huart, hdmarx, hdma_usart2_rx); //link RX DMA to UART

	/*3b. DMA1 stream 6 channel 4: USART2_TX*/
	hdma_usart2_tx.Instance = DMA1_Stream6; //stream 6
	hdma_usart2_tx.Init.Channel = DMA_CHANNEL_4; //channel 4
	hdma_usart2_tx.Init.Direction = DMA_MEMORY_TO_PERIPH; //tx_dma_buf -> USART2->DR
	hdma_usart2_tx.Init.PeriphInc = DMA_PINC_DISABLE; //fixed address
	hdma_usart2_tx.Init.MemInc = DMA_MINC_ENABLE; //memory increments
	hdma_usart2_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE; //8-bit writes
	hdma_usart2_tx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE; //8-bit reads
	hdma_usart2_tx.Init.Mode = DMA_NORMAL; //not circular
	hdma_usart2_tx.Init.Priority = DMA_PRIORITY_LOW; //low priority
	hdma_usart2_tx.Init.FIFOMode = DMA_FIFOMODE_DISABLE; //no FIFO
	if (HAL_DMA_Init(&hdma_usart2_tx) != HAL_OK)
	{
		Error_Handler(); //checks error
	}
	__HAL_LINKDMA(huart, hdmatx, hdma_usart2_tx); //link TX DMA to UART

	/*4. Interrupts*/
	HAL_NVIC_SetPriority(DMA1_Stream5_IRQn, 2, 0); //RX DMA
	HAL_NVIC_EnableIRQ(DMA1_Stream5_IRQn); //enable DMA RX interrupt
	HAL_NVIC_SetPriority(DMA1_Stream6_IRQn, 2, 0); //TX transfer
	HAL_NVIC_EnableIRQ(DMA1_Stream6_IRQn); //enable DMA TX interrupt
	HAL_NVIC_SetPriority(USART2_IRQn, 2, 0); //IDLE, errors, end of TX
	HAL_NVIC_EnableIRQ(USART2_IRQn); //enable USART2 interrupt
}

static void uart2_start_rx(void)
{
	uint8_t index = rx_dma_index; //read the volatile once

	if (HAL_UARTEx_ReceiveToIdle_DMA(&huart2, rx_block[index].data, RX_BLOCK_SIZE) == HAL_OK) //ends at standby or when full
	{
		__HAL_DMA_DISABLE_IT(&hdma_usart2_rx, DMA_IT_HT); //HT interrupt off
	}
}

static void uart2_send_bytes(const uint8_t *data, uint16_t size)
{
	uint32_t start = HAL_GetTick(); //start time

	if (size > TX_BUF_SIZE) //checks limit
	{
		size = TX_BUF_SIZE; //limit size
	}

	while ((uint32_t)(tx_fill_len + size) > TX_BUF_SIZE) //no room
	{
		uart2_flush(); //try to send
		if ((HAL_GetTick() - start) > TX_WAIT_MS)
		{
			return; //timeout: drop text
		}
	}

	memcpy(&tx_fill[tx_fill_len], data, size); //queue bytes
	tx_fill_len = (uint16_t)(tx_fill_len + size); //new length
}

static void uart2_send_text(const char *text)
{
	uart2_send_bytes((const uint8_t *)text, (uint16_t)strlen(text)); //strlen stops at '\0'
}

static void uart2_flush(void)
{
	if (tx_busy || (tx_fill_len == 0U))
	{
		return; //busy or empty
	}

	memcpy(tx_dma_buf, tx_fill, tx_fill_len); //copy for DMA
	tx_busy = true; //TX running
	if (HAL_UART_Transmit_DMA(&huart2, tx_dma_buf, tx_fill_len) == HAL_OK) //start DMA TX
	{
		tx_fill_len = 0U; //tx_fill empty
	}
	else
	{
		tx_busy = false; //retry later
	}
}

//Interrupt handlers
void USART2_IRQHandler(void)
{
	HAL_UART_IRQHandler(&huart2); //IDLE, errors, end of TX
}

void DMA1_Stream5_IRQHandler(void)
{
	HAL_DMA_IRQHandler(&hdma_usart2_rx); //RX block full (TC)
}

void DMA1_Stream6_IRQHandler(void)
{
	HAL_DMA_IRQHandler(&hdma_usart2_tx); //TX transfer
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
	if ((huart->Instance == USART2) &&
	    (HAL_UARTEx_GetRxEventType(huart) != HAL_UART_RXEVENT_HT)) //ignore HT
	{
		uint8_t full = rx_dma_index; //full block
		uint8_t next = (uint8_t)(1U - full); //the other block

		rx_block[full].size = Size; //bytes received
		rx_block[full].ready = true; //ready to read

		if (!rx_block[next].ready) //other block free?
		{
			rx_dma_index = next; //switch block
			uart2_start_rx(); //restart reception
		}
		else
		{
			rx_paused = true; //both busy: pause
			rx_pause_count++; //app_loop reports it
		}
	}
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
	if (huart->Instance == USART2)
	{
		tx_busy = false; //TX done
	}
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
	if (huart->Instance == USART2)
	{
		rx_error = huart->ErrorCode; //app_loop prints it
		if (huart->gState == HAL_UART_STATE_READY)
		{
			tx_busy = false; //TX stopped
		}
		if (!rx_paused)
		{
			uart2_start_rx(); //restart reception if not paused
		}
	}
}
