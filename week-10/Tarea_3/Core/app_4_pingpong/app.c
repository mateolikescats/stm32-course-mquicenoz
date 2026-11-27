/**
 ******************************************************************************
 * @file           : app.c (Stage 4/5/6: ping-pong)
 * @author         : Mateo Quiceno Zapata
 * @brief          : Final design: two DMA reception blocks, DMA transmission,
 *                   line assembly and every command (blink, pwm, ...).
 ******************************************************************************
 *** DESCRIPTION

 Reception: DMA + IDLE into two blocks, A and B, used in turn, so the DMA
 never writes into the block app_loop() is reading. Transmission: DMA from a
 buffer nobody touches while it is being sent, so uart2_send_text() returns
 at once. The line assembler from Stage 2 puts commands back together, even
 when one arrives split across two blocks.

 Stage 5: pwm_init() starts the PWM output (TIM2_CH2 on PA1).
 Stage 6: Backspace edits the line; "blink" and "pwm" alone show the value.

 ***PING-PONG (who owns each block)

   ready = false -> the block belongs to the DMA side (it may be filled)
   ready = true  -> the block belongs to app_loop() (it is being read)

   time ->       1. start     2. A full     3. B full     4. loop slow
   block A       DMA fills    loop reads    DMA fills     loop reads
   block B       free         DMA fills     loop reads    full, waiting
                                                          -> reception paused

 When both blocks are busy the callback sets rx_paused and stops: data may
 be lost, but it is never corrupted, and the terminal says so. app_loop()
 restarts the reception when it gives its block back.

 ***TRANSMISSION

   uart2_send_text() --> tx_fill --(uart2_flush, DMA free)--> tx_dma_buf --DMA--> TX
   tx_busy: set before the DMA starts, cleared by HAL_UART_TxCpltCallback().

 ***INTERRUPT / DMA MAP

 +-------------------+-------------------------+----------+------------------------------+
 | IRQ               | Handler                 | Priority | Brings                       |
 +-------------------+-------------------------+----------+------------------------------+
 | USART2_IRQn       | USART2_IRQHandler       | 2        | IDLE, errors, end of TX      |
 | DMA1_Stream5_IRQn | DMA1_Stream5_IRQHandler | 2        | RX block full (TC)           |
 | DMA1_Stream6_IRQn | DMA1_Stream6_IRQHandler | 2        | TX transfer                  |
 | TIM1_BRK_TIM9_IRQn| (blinky.c)              | 3        | blinky toggle                |
 +-------------------+-------------------------+----------+------------------------------+
 USART2_RX = DMA1 stream 5 channel 4, USART2_TX = DMA1 stream 6 channel 4.
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
#define TX_WAIT_MS       50U //max wait when tx_fill is full
#define LINE_MAX_LEN     63U //longest line accepted
#define REPLY_SIZE       96U //longest command answer
#define KEY_BACKSPACE    0x08U //Backspace in most terminals
#define KEY_DELETE       0x7FU //what some terminals send for Backspace

//One reception block: its bytes, how many arrived and who owns it
typedef struct
{
	uint8_t data[RX_BLOCK_SIZE]; //the DMA writes here
	volatile uint16_t size; //bytes received, set by the callback
	volatile bool ready; //false: DMA side, true: app_loop side
} rx_block_t;

//Handles (static at file level: the HAL keeps their addresses after init)
static UART_HandleTypeDef huart2;
static DMA_HandleTypeDef hdma_usart2_rx; //linked as huart2.hdmarx
static DMA_HandleTypeDef hdma_usart2_tx; //linked as huart2.hdmatx

//Reception (written by the callbacks, read by app_loop)
static rx_block_t rx_block[2]; //block A = 0, block B = 1
static volatile uint8_t rx_dma_index = 0U; //block the DMA is filling
static uint8_t read_index = 0U; //next block app_loop reads (only the main loop uses it)
static volatile bool rx_paused = false; //both blocks busy: reception stopped
static volatile uint32_t rx_pause_count = 0U; //pauses since reset
static volatile uint32_t rx_error = 0U; //HAL error code, cleared by app_loop
static uint32_t rx_pause_reported = 0U; //pauses already printed

//Transmission
static uint8_t tx_fill[TX_BUF_SIZE]; //send_text appends here
static uint16_t tx_fill_len = 0U; //bytes waiting in tx_fill
static uint8_t tx_dma_buf[TX_BUF_SIZE]; //the DMA reads here, nobody touches it while sending
static volatile bool tx_busy = false; //DMA transmission running

//Line being typed (only the main loop uses them)
static char line[LINE_MAX_LEN + 1U]; //+1 for the final '\0'
static uint16_t line_len = 0U; //characters stored in line[]
static bool line_too_long = false; //set when the user typed past LINE_MAX_LEN
static uint8_t previous_byte = 0U; //last byte seen, to detect the '\n' of "\r\n"

//Functions
static void uart2_init(void); //USART2 handle settings (hardware goes in HAL_UART_MspInit)
static void uart2_start_rx(void); //DMA until idle into rx_block[rx_dma_index], HT off
static void uart2_send_bytes(const uint8_t *data, uint16_t size); //appends bytes to tx_fill, returns at once
static void uart2_send_text(const char *text); //appends a string to tx_fill
static void uart2_flush(void); //starts the TX DMA if it is free and text is waiting
static void line_add_byte(uint8_t new_byte); //adds one byte to the line, echoes it, runs the line on Enter
static void line_execute(void); //parses and runs the finished line, then prints a new prompt

void app_init(void)
{
	uart2_init(); //handle + HAL_UART_MspInit (clocks, pins, 2 DMA streams, IRQs)
	pwm_init(); //Stage 5: PWM output on PA1, starts at 0 %
	uart2_send_text("\r\nStage 4: ping-pong reception.\r\n> "); //banner and first prompt
	uart2_start_rx(); //DMA starts filling block A
}

void app_loop(void)
{
	/*Part 1: process the next full block*/
	if (rx_block[read_index].ready) //block belongs to app_loop
	{
		bool restart_aux; //reception must be restarted
		uint16_t size = rx_block[read_index].size; //bytes in the block

		for (uint16_t i = 0U; i < size; i++)
		{
			line_add_byte(rx_block[read_index].data[i]); //byte by byte: blocks are not lines
		}

		__disable_irq(); //give the block back and read rx_paused together
		rx_block[read_index].ready = false; //block back to the DMA side
		restart_aux = rx_paused; //was the DMA waiting for a free block?
		if (restart_aux)
		{
			rx_paused = false; //pause over
			rx_dma_index = read_index; //the DMA continues in the block just freed
		}
		__enable_irq(); //only a few instructions with interrupts off

		if (restart_aux)
		{
			uart2_start_rx(); //reception again
		}
		read_index = (uint8_t)(1U - read_index); //0 -> 1 -> 0: A, B, A, ...
	}

	/*Part 2: report pauses and errors*/
	if (rx_pause_count != rx_pause_reported) //new pause since last report
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

	/*Part 3: start sending the text that is waiting*/
	uart2_flush();
}

//Actions: line assembly (Stage 2)
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
	else if ((new_byte == KEY_BACKSPACE) || (new_byte == KEY_DELETE)) //Stage 6: Backspace
	{
		if (line_len > 0U) //something to erase
		{
			line_len--; //forget the last character
			uart2_send_text("\b \b"); //back, overwrite with space, back again
		}
	}
	else if (line_len < LINE_MAX_LEN) //still room in line[]
	{
		line[line_len] = (char)new_byte; //store the character
		line_len++; //next free position
		uart2_send_bytes(&new_byte, 1U); //shows what is typed (echo, by DMA now)
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

	/*3a. DMA1 stream 5 channel 4: USART2_RX*/
	hdma_usart2_rx.Instance = DMA1_Stream5; //fixed pair in RM0383
	hdma_usart2_rx.Init.Channel = DMA_CHANNEL_4; //request channel of USART2_RX
	hdma_usart2_rx.Init.Direction = DMA_PERIPH_TO_MEMORY; //USART2->DR -> rx_block
	hdma_usart2_rx.Init.PeriphInc = DMA_PINC_DISABLE; //always the same data register
	hdma_usart2_rx.Init.MemInc = DMA_MINC_ENABLE; //one position further each byte
	hdma_usart2_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE; //8-bit reads
	hdma_usart2_rx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE; //8-bit writes
	hdma_usart2_rx.Init.Mode = DMA_NORMAL; //stops when the block is full
	hdma_usart2_rx.Init.Priority = DMA_PRIORITY_LOW; //same as TX
	hdma_usart2_rx.Init.FIFOMode = DMA_FIFOMODE_DISABLE; //direct mode, byte by byte
	if (HAL_DMA_Init(&hdma_usart2_rx) != HAL_OK)
	{
		Error_Handler(); //checks error
	}
	__HAL_LINKDMA(huart, hdmarx, hdma_usart2_rx); //UART handle <-> RX DMA handle

	/*3b. DMA1 stream 6 channel 4: USART2_TX*/
	hdma_usart2_tx.Instance = DMA1_Stream6; //fixed pair in RM0383
	hdma_usart2_tx.Init.Channel = DMA_CHANNEL_4; //request channel of USART2_TX
	hdma_usart2_tx.Init.Direction = DMA_MEMORY_TO_PERIPH; //tx_dma_buf -> USART2->DR
	hdma_usart2_tx.Init.PeriphInc = DMA_PINC_DISABLE; //always the same data register
	hdma_usart2_tx.Init.MemInc = DMA_MINC_ENABLE; //next byte of tx_dma_buf each time
	hdma_usart2_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE; //8-bit writes
	hdma_usart2_tx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE; //8-bit reads
	hdma_usart2_tx.Init.Mode = DMA_NORMAL; //one transfer per flush
	hdma_usart2_tx.Init.Priority = DMA_PRIORITY_LOW; //same as RX
	hdma_usart2_tx.Init.FIFOMode = DMA_FIFOMODE_DISABLE; //direct mode
	if (HAL_DMA_Init(&hdma_usart2_tx) != HAL_OK)
	{
		Error_Handler(); //checks error
	}
	__HAL_LINKDMA(huart, hdmatx, hdma_usart2_tx); //without it Transmit_DMA uses a NULL pointer: CPU fault

	/*4. Interrupts*/
	HAL_NVIC_SetPriority(DMA1_Stream5_IRQn, 2, 0); //RX block full and DMA errors
	HAL_NVIC_EnableIRQ(DMA1_Stream5_IRQn); //the NVIC accepts DMA1 stream 5
	HAL_NVIC_SetPriority(DMA1_Stream6_IRQn, 2, 0); //TX transfer
	HAL_NVIC_EnableIRQ(DMA1_Stream6_IRQn); //the NVIC accepts DMA1 stream 6
	HAL_NVIC_SetPriority(USART2_IRQn, 2, 0); //IDLE, errors, end of TX
	HAL_NVIC_EnableIRQ(USART2_IRQn); //the NVIC accepts USART2
}

static void uart2_start_rx(void)
{
	uint8_t index = rx_dma_index; //read the volatile once

	if (HAL_UARTEx_ReceiveToIdle_DMA(&huart2, rx_block[index].data, RX_BLOCK_SIZE) == HAL_OK) //ends at IDLE or when full
	{
		__HAL_DMA_DISABLE_IT(&hdma_usart2_rx, DMA_IT_HT); //HAL turns HT on at every start: switch it off every time
	}
}

static void uart2_send_bytes(const uint8_t *data, uint16_t size)
{
	uint32_t start = HAL_GetTick(); //for the wait limit

	if (size > TX_BUF_SIZE)
	{
		size = TX_BUF_SIZE; //never more than the buffer
	}

	while ((uint32_t)(tx_fill_len + size) > TX_BUF_SIZE) //tx_fill full: let the DMA empty it
	{
		uart2_flush(); //hand tx_fill to the DMA as soon as it is free
		if ((HAL_GetTick() - start) > TX_WAIT_MS)
		{
			return; //never wait forever (TX stuck): text dropped
		}
	}

	memcpy(&tx_fill[tx_fill_len], data, size); //raw bytes, no '\0'
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
		return; //DMA still sending, or nothing to send
	}

	memcpy(tx_dma_buf, tx_fill, tx_fill_len); //the DMA gets its own copy
	tx_busy = true; //set before the DMA starts
	if (HAL_UART_Transmit_DMA(&huart2, tx_dma_buf, tx_fill_len) == HAL_OK) //returns at once
	{
		tx_fill_len = 0U; //tx_fill free again
	}
	else
	{
		tx_busy = false; //keep the text in tx_fill and try again later
	}
}

//Interrupt handlers: move data + flag, no decisions
void USART2_IRQHandler(void)
{
	HAL_UART_IRQHandler(&huart2); //IDLE, errors and end of transmission
}

void DMA1_Stream5_IRQHandler(void)
{
	HAL_DMA_IRQHandler(&hdma_usart2_rx); //RX block full (TC), name must match the vector table
}

void DMA1_Stream6_IRQHandler(void)
{
	HAL_DMA_IRQHandler(&hdma_usart2_tx); //TX transfer, name must match the vector table
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
	if ((huart->Instance == USART2) &&
	    (HAL_UARTEx_GetRxEventType(huart) != HAL_UART_RXEVENT_HT)) //only whole blocks (IDLE or TC)
	{
		uint8_t full = rx_dma_index; //block the DMA just finished
		uint8_t next = (uint8_t)(1U - full); //the other block

		rx_block[full].size = Size; //bytes received
		rx_block[full].ready = true; //block goes to app_loop

		if (!rx_block[next].ready) //other block free?
		{
			rx_dma_index = next; //DMA continues there
			uart2_start_rx(); //IDLE and TC stop the reception: start a new one
		}
		else
		{
			rx_paused = true; //both blocks busy: nothing is overwritten
			rx_pause_count++; //app_loop reports it
		}
	}
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
	if (huart->Instance == USART2)
	{
		tx_busy = false; //last byte left: tx_dma_buf free
	}
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
	if (huart->Instance == USART2)
	{
		rx_error = huart->ErrorCode; //app_loop prints it
		if (huart->gState == HAL_UART_STATE_READY)
		{
			tx_busy = false; //a transmission error also ends here
		}
		if (!rx_paused)
		{
			uart2_start_rx(); //every error stops the reception, but never restart a paused one
		}
	}
}
