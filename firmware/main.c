#include "includes.h"
#include "tusb.h"
//------------------------------------------------------------------------------
/*Configure the clocks, GPIO and other peripherals */
static void prvSetupHardware( void );
//------------------------------------------------------------------------------
static void vI2CTask (void *pvParameters);
//------------------------------------------------------------------------------
static void vInoutsTask (void *pvParameters);
//------------------------------------------------------------------------------
void vApplicationTickHook( void );
//------------------------------------------------------------------------------
extern int write_eeprom();
extern int init_eeprom();
//------------------------------------------------------------------------------
int main( void )
{
  prvSetupHardware();

  //MODBUS_HR[MBHR_REG_MY_MBADDR] = 1;
  //MODBUS_HR[MBHR_TEST_VALUE] = 5;
  //write_eeprom();
  //init_eeprom();
  tusb_init();
  Com1RxSemaphore = xSemaphoreCreateCounting(MAX_COM_QUEUE_LENGTH, 0);
  xTaskCreate(vPacketsManagerTask, "Packets_manager", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 2, NULL);
  //xTaskCreate(vInoutsTask, "Inouts", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, NULL);
  vTaskStartScheduler();

  return 0;
}
//------------------------------------------------------------------------------
void vInoutsTask (void *pvParameters)
{
  for(;;)
  {
    //MODBUS_HR[MBHR_DISCRETE_OUTPUTS_LOW] = MODBUS_HR[MBHR_DISCRETE_INPUTS_LOW];
    while(MODBUS_HR[MBHR_TEST_VALUE] != 0)
    {
      SET_PIN_HIGH(COIL1_PORT,COIL1);
      SET_PIN_LOW(COIL2_PORT,COIL2);
      vTaskDelay(500 / portTICK_RATE_MS);
      SET_PIN_LOW(COIL1_PORT,COIL1);
      SET_PIN_HIGH(COIL2_PORT,COIL2);
      vTaskDelay(500 / portTICK_RATE_MS);
      MODBUS_HR[MBHR_TEST_VALUE]--;
    }
  }
}
//------------------------------------------------------------------------------
void vApplicationTickHook( void )//вызывается каждую миллисекунду
{
  return;
}
//------------------------------------------------------------------------------
static void prvSetupHardware( void )
{
  /* Start with the clocks in their expected state. */
  RCC_DeInit();
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
  RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);   
  TIM_TimeBaseInitTypeDef timerInitStructure;
  timerInitStructure.TIM_Prescaler = 35;
  timerInitStructure.TIM_CounterMode = TIM_CounterMode_Up;
  timerInitStructure.TIM_Period = 0xffff;
  timerInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
  TIM_TimeBaseInit(TIM3, &timerInitStructure);
  TIM_Cmd(TIM3, ENABLE);
  
  //run led
  SET_PIN_LOW(GPIOB,5);
  SET_PIN_OUTPUT_PP(GPIOB,5);
  SET_PIN_HIGH(GPIOB,5);
  //coil pins
  SET_PIN_LOW(GPIOB,15);//coil 1
  SET_PIN_OUTPUT_PP(GPIOB,15);
  //SET_PIN_HIGH(GPIOB,15);
  SET_PIN_LOW(GPIOB,14);//coil 2
  SET_PIN_OUTPUT_PP(GPIOB,14);
  //SET_PIN_HIGH(GPIOB,14);
  
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
  NVIC_Init( &NVIC_InitStructure );
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