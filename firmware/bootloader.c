#include <stdbool.h>
#include "includes.h"
#include "eeprom.h"
#include "tusb.h"
//------------------------------------------------------------------------------
#define USBD_STACK_SIZE     (3*configMINIMAL_STACK_SIZE/2)
//------------------------------------------------------------------------------
StackType_t  usb_device_stack[USBD_STACK_SIZE];
StaticTask_t usb_device_taskdef;
//------------------------------------------------------------------------------
/*Configure the clocks, GPIO and other peripherals */
static void prvSetupHardware( void );
//------------------------------------------------------------------------------
static void vI2CTask (void *pvParameters);
//------------------------------------------------------------------------------
static void vInoutsTask (void *pvParameters);
//------------------------------------------------------------------------------
static void vJumpFirmware (void *pvParameters);
//------------------------------------------------------------------------------
void vApplicationTickHook( void );
//------------------------------------------------------------------------------
extern register_cb isregwrtbl_cb;
extern register_cb regwr_cb;
extern uint16_t* UsartPackets;
extern uint16_t* UsartErrors;
//------------------------------------------------------------------------------
uint32_t jumpCounter=0;
volatile uint32_t msTick=0;
SemaphoreHandle_t jumpMutex;
//------------------------------------------------------------------------------
int is_writeable(uint16_t addr)
{
  jumpCounter=msTick;
	switch(addr)
	{
	case MBHR_REG_FLASH_PAGE_SIZE:
	case MBHR_REG_IN_UART_PACKS:
	case MBHR_REG_IN_UART_PACKS_ERR:
	case MBHR_COMMAND_STATUS:
	{
		return 0;
	}
	default:
		return 1;  
	}
	return 1;
}
//------------------------------------------------------------------------------
int do_modbus_command()
{
	int res = 0;
	MODBUS_HR[MBHR_COMMAND_STATUS] = COMMAND_STATUS_OK;
	switch(MODBUS_HR[MBHR_REG_COMMAND])
	{
	case CMD_REBOOT:
	{
		REBOOT();
		break;
	}	
	case CMD_WRITE_FIRMWARE_BLOCK:
	{
		uint16_t crctmp = calc_crc((uint8_t*)&MODBUS_HR[MBHR_WRITE_FLASH_BUF_0], MODBUS_HR[MBHR_FIRMWARE_BLOCK_LEN]);
		if(crctmp != MODBUS_HR[MBHR_FIRMWARE_BLOCK_CRC])
		{
			MODBUS_HR[MBHR_COMMAND_STATUS] = COMMAND_STATUS_FAILED;
			return -1;
		}
		//if address is aligned with flash page size - then erase page
		bool erase = ((MODBUS_HR[MBHR_WRITE_FLASH_ADDR] & FLASH_PAGE_SIZE_MASK) == MODBUS_HR[MBHR_WRITE_FLASH_ADDR]) ? true: false;
    if(xSemaphoreTake(jumpMutex,200)== pdTRUE)
    {
      res = write_firmware_block(FIRMWARE_START + MODBUS_HR[MBHR_WRITE_FLASH_ADDR],&MODBUS_HR[MBHR_WRITE_FLASH_BUF_0], \
        MODBUS_HR[MBHR_FIRMWARE_BLOCK_LEN], erase);
      if(res != 0)
        MODBUS_HR[MBHR_COMMAND_STATUS] = COMMAND_STATUS_FAILED;
      xSemaphoreGive(jumpMutex);
    }
		else
    {
      MODBUS_HR[MBHR_COMMAND_STATUS] = COMMAND_STATUS_FAILED;
      res=-1;
    }
		break;
	}
	default:
    res=-1;
		break;	
	}
	return res;
}
//------------------------------------------------------------------------------
int process_register(uint16_t addr)
{
	int res = 0;
  switch(addr)
  {
  case MBHR_BOOTLOADER_STATUS:
  case MBHR_REG_MY_MBADDR:
  case MBHR_FIRMWARE_FULL_LEN:
  case MBHR_FIRMWARE_CRC16:
  {
    if(xSemaphoreTake(jumpMutex,200)== pdTRUE)
    {
        write_eeprom();
        xSemaphoreGive(jumpMutex);
    }
    break;
  }
  case MBHR_REG_COMMAND:
  	res = do_modbus_command();
    if(res)
    	return res;
  	break;
  default:
    return 0;  
  }
  return res;
}
//------------------------------------------------------------------------------
void init_modbus()
{
  MyMBAddr = &MODBUS_HR[MBHR_REG_MY_MBADDR];
  UsartPackets = &MODBUS_HR[MBHR_REG_IN_UART_PACKS];
  UsartErrors = &MODBUS_HR[MBHR_REG_IN_UART_PACKS_ERR];
  MODBUS_HR[MBHR_REG_FLASH_PAGE_SIZE] = FLASH_PAGE_SIZE;
  init_eeprom();
}
//------------------------------------------------------------------------------
// USB Device Driver task
// This top level thread process all usb events and invoke callbacks
void usb_device_task(void* param)
{
  (void) param;

  // This should be called after scheduler/kernel is started.
  // Otherwise it could cause kernel issue since USB IRQ handler does use RTOS queue API.
  tusb_init();

  // RTOS forever loop
  while (1)
  {
    // tinyusb device task
    tud_task();
  }
}
//------------------------------------------------------------------------------
int main( void )
{
  prvSetupHardware();
  //isregwrtbl_cb = &is_writeable;
  //regwr_cb = &process_register;
  //init_modbus();
  //jumpMutex = xSemaphoreCreateMutex();
  //Com1RxSemaphore = xSemaphoreCreateCounting(MAX_COM_QUEUE_LENGTH, 0);
  //xTaskCreate(vPacketsManagerTask, "Packets_manager", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 2, NULL);
  //xTaskCreate(vJumpFirmware, "JumpFirmware", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, NULL);
  xTaskCreateStatic( usb_device_task, "usbd", USBD_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, usb_device_stack, &usb_device_taskdef);
  vTaskStartScheduler();

  return 0;
}
//------------------------------------------------------------------------------
void vJumpFirmware (void *pvParameters)
{
  for(;;)
  {
    while(MODBUS_HR[MBHR_BOOTLOADER_STATUS] != BOOTLOADER_JUMP && msTick-jumpCounter < BOOTLOADER_JUMP_COUNTER)
    {
      vTaskDelay(50 / portTICK_RATE_MS);
      MODBUS_HR[MBHR_BOOTLOADER_COUNTDOWN] = BOOTLOADER_JUMP_COUNTER - (msTick-jumpCounter);
    }
    xSemaphoreTake(jumpMutex,portMAX_DELAY);
    uint16_t crctmp = calc_crc((uint8_t*)FIRMWARE_START, MODBUS_HR[MBHR_FIRMWARE_FULL_LEN]);
  	if(crctmp != MODBUS_HR[MBHR_FIRMWARE_CRC16])
  	{
  		MODBUS_HR[MBHR_BOOTLOADER_STATUS] = BOOTLOADER_WAIT_30S;
  		MODBUS_HR[MBHR_COMMAND_STATUS] = COMMAND_STATUS_FAILED;
  		jumpCounter = msTick;
      xSemaphoreGive(jumpMutex);
  		continue;
  	}
    if(MODBUS_HR[MBHR_BOOTLOADER_STATUS] != BOOTLOADER_JUMP)
    {
      MODBUS_HR[MBHR_BOOTLOADER_STATUS] = BOOTLOADER_JUMP;
      write_eeprom();
    }
    //выключаем периферию
    TIM_Cmd(TIM3, DISABLE);
    USART_Cmd(USART1, DISABLE);
    //отключаем все прерывания в NVIC
    uint32_t ISER[3];
    ISER[0] = NVIC->ISER[0];
    ISER[1] = NVIC->ISER[1];
    ISER[2] = NVIC->ISER[2];
    NVIC->ICER[0] = 0xFFFFFFFF;
    NVIC->ICER[1] = 0xFFFFFFFF;
    NVIC->ICER[2] = 0xFFFFFFFF;

    //очищаем все pending bit
    NVIC->ICPR[0] = 0xFFFFFFFF;
    NVIC->ICPR[1] = 0xFFFFFFFF;
    NVIC->ICPR[2] = 0xFFFFFFFF;
    uint32_t jumpAddr = *(uint32_t*)(FIRMWARE_START+4);
    
    RCC_DeInit();
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL = 0;
    SCB->VTOR = FIRMWARE_START;
    portDISABLE_INTERRUPTS();
    __set_MSP(*(uint32_t*) FIRMWARE_START);
    __set_PSP(*(uint32_t*) FIRMWARE_START);
    ((void (*)(void))jumpAddr)();
  }
}
//------------------------------------------------------------------------------
void vApplicationTickHook( void )//вызывается каждую миллисекунду
{
  msTick++;
  /*if((msTick%500) == 0)
  {
    if(GPIOB->IDR & (1 << 5))
    {
      SET_PIN_LOW(GPIOB,5);
    }
    else
    {
      SET_PIN_HIGH(GPIOB,5);
    }
  }*/
  return;
}
//------------------------------------------------------------------------------
static void prvSetupHardware( void )
{
  /* Start with the clocks in their expected state. */
  RCC_DeInit();
  RCC_HSICmd(ENABLE);//needed for flash programming
  while( RCC_GetFlagStatus( RCC_FLAG_HSIRDY ) == RESET ){}
  /* Enable HSE (high speed external clock). */
  RCC_HSEConfig( RCC_HSE_ON );
  /* Wait till HSE is ready. */
  while( RCC_GetFlagStatus( RCC_FLAG_HSERDY ) == RESET ){}
  /* 2 wait states required on the flash. */
  *( ( unsigned long * ) 0x40022000 ) = 0x02;
  /* HCLK = SYSCLK */
  RCC_HCLKConfig( RCC_SYSCLK_Div1 );
  /* PCLK2 = HCLK */
  RCC_PCLK2Config( RCC_HCLK_Div1 );
  /* PCLK1 = HCLK/2 */
  RCC_PCLK1Config( RCC_HCLK_Div2 );
  /* PLLCLK = 8MHz * 9 = 72 MHz. */
  RCC_PLLConfig( RCC_PLLSource_HSE_Div1, RCC_PLLMul_9 );
  /* Enable PLL. */
  RCC_PLLCmd( ENABLE );
  /* Wait till PLL is ready. */
  while(RCC_GetFlagStatus(RCC_FLAG_PLLRDY) == RESET)
  {
  }
  /* Select PLL as system clock source. */
  RCC_SYSCLKConfig( RCC_SYSCLKSource_PLLCLK );
  /* Wait till PLL is used as system clock source. */
  while( RCC_GetSYSCLKSource() != 0x08 )
  {
  }
  /* Enable GPIOA, GPIOB, GPIOC, GPIOD, GPIOE and AFIO clocks */
  RCC_APB2PeriphClockCmd(	RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB |RCC_APB2Periph_GPIOC \
						| RCC_APB2Periph_GPIOD | RCC_APB2Periph_GPIOE |  \
                                                  RCC_APB2Periph_AFIO | RCC_APB2Periph_SPI1 | \
                                                    RCC_APB2Periph_ADC1 | RCC_APB2Periph_USART1, ENABLE );
  /* Configure the ADC clock */
  RCC_ADCCLKConfig(RCC_PCLK2_Div8);
  //enable dma clock
  RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);
  RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C1, ENABLE);
  /* Set the Vector Table base address at 0x08000000 */
  NVIC_SetVectorTable( NVIC_VectTab_FLASH, 0x0 );        
  NVIC_PriorityGroupConfig( NVIC_PriorityGroup_4 );
  /* Configure HCLK clock as SysTick clock source. */
  SysTick_CLKSourceConfig( SysTick_CLKSource_HCLK );
  
  RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM1, ENABLE);
  //пока что для отладки
  /*RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);   
  TIM_TimeBaseInitTypeDef timerInitStructure;
  timerInitStructure.TIM_Prescaler = 35;
  timerInitStructure.TIM_CounterMode = TIM_CounterMode_Up;
  timerInitStructure.TIM_Period = 0xffff;
  timerInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
  TIM_TimeBaseInit(TIM3, &timerInitStructure);
  TIM_Cmd(TIM3, ENABLE);*/
  RCC_APB1PeriphClockCmd(RCC_APB1Periph_USB, ENABLE);

  
  //run led
  SET_PIN_LOW(GPIOB,5);
  SET_PIN_OUTPUT_PP(GPIOB,5);
  SET_PIN_HIGH(GPIOB,5);
  //coil pins
  /*SET_PIN_LOW(GPIOB,15);//coil 1
  SET_PIN_OUTPUT_PP(GPIOB,15);
  SET_PIN_LOW(GPIOB,14);//coil 2
  SET_PIN_OUTPUT_PP(GPIOB,14);
  
  USART_InitTypeDef USART_InitStructure;
  USART_InitStructure.USART_BaudRate = INIT_BAUDRATE;
  USART_InitStructure.USART_WordLength = USART_WordLength_8b;
  USART_InitStructure.USART_StopBits = USART_StopBits_1;
  USART_InitStructure.USART_Parity = USART_Parity_No;
  USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
  USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
  //НОги Rx Tx USART1
  SET_PIN_ALTMODE_PP(GPIOA,9);
  SET_PIN_INPUT(GPIOA,10);
  USART_Init(USART1, &USART_InitStructure);
  USART_Cmd(USART1, ENABLE);
  
  //
  NVIC_InitTypeDef NVIC_InitStructure;
  //usart rx interrupt
  NVIC_InitStructure.NVIC_IRQChannel = USART1_IRQn;
  NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = configLIBRARY_KERNEL_INTERRUPT_PRIORITY;
  NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
  NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
  NVIC_Init( &NVIC_InitStructure );
  //usart dma tx interrupt
  NVIC_InitStructure.NVIC_IRQChannel = DMA1_Channel4_IRQn;
  NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = configLIBRARY_KERNEL_INTERRUPT_PRIORITY;
  NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0; //Not used as 4 bits are used for the pre-emption priority. 
  NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
  NVIC_Init( &NVIC_InitStructure );
  //usart dma rx interrupt
  NVIC_InitStructure.NVIC_IRQChannel = DMA1_Channel5_IRQn;
  NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = configLIBRARY_KERNEL_INTERRUPT_PRIORITY;
  NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0; //Not used as 4 bits are used for the pre-emption priority. 
  NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
  NVIC_Init( &NVIC_InitStructure );
  //usart tim rx interrupt cc
  NVIC_InitStructure.NVIC_IRQChannel = TIM1_CC_IRQn;
  NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = configLIBRARY_KERNEL_INTERRUPT_PRIORITY;
  NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0; //Not used as 4 bits are used for the pre-emption priority. 
  NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
  NVIC_Init( &NVIC_InitStructure );
  //usart tim rx interrupt upd
  NVIC_InitStructure.NVIC_IRQChannel = TIM1_UP_IRQn;
  NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = configLIBRARY_KERNEL_INTERRUPT_PRIORITY;
  NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0; //Not used as 4 bits are used for the pre-emption priority. 
  NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
  NVIC_Init( &NVIC_InitStructure );*/
}
//------------------------------------------------------------------------------
#ifdef  DEBUG
/* Keep the linker happy. */
void assert_failed( unsigned char* pcFile, unsigned long ulLine )
{
	for( ;; )
	{
	}
}
#endif
