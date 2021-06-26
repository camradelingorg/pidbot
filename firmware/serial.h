#ifndef SERIAL_H
#define SERIAL_H
//------------------------------------------------------------------------------
#include "includes.h"
//------------------------------------------------------------------------------
#define BAUDRATE_115200 115200
#define BAUDRATE_115200_TIMER_PERIOD configCPU_CLOCK_HZ/BAUDRATE_115200*28
#define BAUDRATE_115200_TIMER_PRESCALER 0
#define BAUDRATE_115200_TIMER_CLOCK_DIVIDER TIM_CKD_DIV1
//------------------------------------------------------------------------------
#define MAX_COM_QUEUE_LENGTH                            2
//------------------------------------------------------------------------------
void DMA_USART_prepare_transmit(uint8_t length);
//------------------------------------------------------------------------------
void DMA_USART_prepare_recieve(void);
//------------------------------------------------------------------------------
void init_com_timer(void);
//------------------------------------------------------------------------------
void vPacketsManagerTask(void *pvParameters);
//------------------------------------------------------------------------------
#endif/*SERIAL_H*/