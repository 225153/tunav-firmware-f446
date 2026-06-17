/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "FreeRTOS.h"
#include "queue.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
UART_HandleTypeDef huart4;
UART_HandleTypeDef huart2;

/* Definitions for TaskLed */
osThreadId_t TaskLedHandle;
const osThreadAttr_t TaskLed_attributes = {
  .name = "TaskLed",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for TaskMqtt */
osThreadId_t TaskMqttHandle;
const osThreadAttr_t TaskMqtt_attributes = {
  .name = "TaskMqtt",
  .stack_size = 1024 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for Tasklog */
osThreadId_t TasklogHandle;
const osThreadAttr_t Tasklog_attributes = {
  .name = "Tasklog",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for TaskPriorityMqt */
osThreadId_t TaskPriorityMqtHandle;
const osThreadAttr_t TaskPriorityMqt_attributes = {
  .name = "TaskPriorityMqt",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityHigh,
};
/* Definitions for MqttCommandQueue */
osMessageQueueId_t MqttCommandQueueHandle;
const osMessageQueueAttr_t MqttCommandQueue_attributes = {
  .name = "MqttCommandQueue"
};
/* Definitions for GsmMutex */
osMutexId_t GsmMutexHandle;
const osMutexAttr_t GsmMutex_attributes = {
  .name = "GsmMutex"
};
/* USER CODE BEGIN PV */
#define RX_BUF_SIZE 1024  // <-- Double-check that this line is at the very top of PV!
uint8_t rx_ring_buf[RX_BUF_SIZE];
volatile uint16_t rx_head = 0;
volatile uint16_t rx_tail = 0;
uint8_t rx_tmp_byte; // Octet temporaire pour l'interruption

QueueHandle_t LogQueueHandle = NULL;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_UART4_Init(void);
static void MX_USART2_UART_Init(void);
void StartDefaultTask(void *argument);
void StartTask02(void *argument);
void StartTask03(void *argument);
void StartTask04(void *argument);

/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
#include <stdio.h>
#include <string.h>

void Log_String(const char* str) {
    char msg_buffer[128]; 
    strncpy(msg_buffer, str, sizeof(msg_buffer) - 1);
    msg_buffer[sizeof(msg_buffer) - 1] = '\0';
    
    if (LogQueueHandle != NULL) {
        xQueueSend(LogQueueHandle, msg_buffer, 0);
    }
}

// Protected by GsmMutex!
uint8_t EC200U_SendAT(char* cmd, uint32_t timeout_ms) {
    char log[128];
    char buffer[512]; 
    uint16_t idx = 0;
    uint32_t start_time;
    uint8_t status = 0;
    
    // 1. TRAP PROTECTION: Wait for the Mutex before doing anything
    if (osMutexAcquire(GsmMutexHandle, osWaitForever) != osOK) {
        return 0; // Failed to get lock
    }

    start_time = HAL_GetTick();
    memset(buffer, 0, sizeof(buffer));
    
    if (__HAL_UART_GET_FLAG(&huart4, UART_FLAG_ORE)) __HAL_UART_CLEAR_OREFLAG(&huart4);
    if (__HAL_UART_GET_FLAG(&huart4, UART_FLAG_NE))  __HAL_UART_CLEAR_NEFLAG(&huart4);

    snprintf(log, sizeof(log), "[SEND] %s", cmd);
    Log_String(log);
    
    HAL_UART_Transmit(&huart4, (uint8_t*)cmd, strlen(cmd), 100);
    
    while ((HAL_GetTick() - start_time) < timeout_ms) {
        while (rx_tail != rx_head) {
            uint8_t ch = rx_ring_buf[rx_tail];
            rx_tail = (rx_tail + 1) % RX_BUF_SIZE;
            
            if (idx < sizeof(buffer) - 1) {
                buffer[idx++] = ch;
                buffer[idx] = '\0'; 
            }
        }
        
        if (strstr(cmd, "AT+QMTOPEN") != NULL) {
            if (strstr(buffer, "+QMTOPEN: 0,0") != NULL || strstr(buffer, "+QMTOPEN: 1,0") != NULL) { status = 1; break; }
            if (strstr(buffer, "ERROR\r\n") != NULL) { status = 0; break; }
        }
        else if (strstr(cmd, "AT+QMTCONN") != NULL) {
            if (strstr(buffer, "+QMTCONN: 0,0,0") != NULL || strstr(buffer, "+QMTCONN: 1,0,0") != NULL) { status = 1; break; }
            if (strstr(buffer, "ERROR\r\n") != NULL) { status = 0; break; }
        }
        else {
            if (strstr(buffer, "OK\r\n") != NULL) { status = 1; break; }
            if (strstr(buffer, "ERROR\r\n") != NULL) { status = 0; break; }
        }
        
        osDelay(1); 
    }
    
    if (idx > 0) {
        Log_String("[RECV] ");
        Log_String(buffer);
        Log_String("\r\n");
    } else {
        Log_String("[ERROR] No response from EC200U\r\n");
    }
    
    // 2. TRAP PROTECTION: Release the Mutex so other tasks can use the GSM
    osMutexRelease(GsmMutexHandle);
    
    return status;
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_UART4_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */
   HAL_UART_Transmit(&huart2, (uint8_t*)"System Ready. Booting RTOS...\r\n", 31, 100);
   HAL_UART_Receive_IT(&huart4, &rx_tmp_byte, 1);
  
  // Create Message Queue for 10 entries of 128 bytes length strings
  LogQueueHandle = xQueueCreate(10, 128);

  /* USER CODE END 2 */

  /* Init scheduler */
  osKernelInitialize();
  /* Create the mutex(es) */
  /* creation of GsmMutex */
  GsmMutexHandle = osMutexNew(&GsmMutex_attributes);

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* Create the queue(s) */
  /* creation of MqttCommandQueue */
  MqttCommandQueueHandle = osMessageQueueNew (4, 128, &MqttCommandQueue_attributes);

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of TaskLed */
  TaskLedHandle = osThreadNew(StartDefaultTask, NULL, &TaskLed_attributes);

  /* creation of TaskMqtt */
  TaskMqttHandle = osThreadNew(StartTask02, NULL, &TaskMqtt_attributes);

  /* creation of Tasklog */
  TasklogHandle = osThreadNew(StartTask03, NULL, &Tasklog_attributes);

  /* creation of TaskPriorityMqt */
  TaskPriorityMqtHandle = osThreadNew(StartTask04, NULL, &TaskPriorityMqt_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief UART4 Initialization Function
  * @param None
  * @retval None
  */
static void MX_UART4_Init(void)
{

  /* USER CODE BEGIN UART4_Init 0 */

  /* USER CODE END UART4_Init 0 */

  /* USER CODE BEGIN UART4_Init 1 */

  /* USER CODE END UART4_Init 1 */
  huart4.Instance = UART4;
  huart4.Init.BaudRate = 115200;
  huart4.Init.WordLength = UART_WORDLENGTH_8B;
  huart4.Init.StopBits = UART_STOPBITS_1;
  huart4.Init.Parity = UART_PARITY_NONE;
  huart4.Init.Mode = UART_MODE_TX_RX;
  huart4.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart4.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart4) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN UART4_Init 2 */

  /* USER CODE END UART4_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(Debug_Led_GPIO_Port, Debug_Led_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : Debug_Led_Pin */
  GPIO_InitStruct.Pin = Debug_Led_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(Debug_Led_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == UART4) {
        // Calculer la prochaine position de la tête du tampon circulaire
        uint16_t next_head = (rx_head + 1) % RX_BUF_SIZE;
        
        if (next_head != rx_tail) { // Sécurité anti-débordement
            rx_ring_buf[rx_head] = rx_tmp_byte;
            rx_head = next_head;
        }
        
        // Relancer immédiatement l'interruption pour le prochain octet
        HAL_UART_Receive_IT(&huart4, &rx_tmp_byte, 1);
    }
}

/* USER CODE END 4 */

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the TaskLed thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN 5 */
  /* Infinite loop */
  for(;;)
  {
    HAL_GPIO_TogglePin(Debug_Led_GPIO_Port, Debug_Led_Pin);
    osDelay(500);
  }
  /* USER CODE END 5 */
}

/* USER CODE BEGIN Header_StartTask02 */
/**
* @brief Function implementing the TaskMqtt thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartTask02 */
void StartTask02(void *argument)
{
  /* USER CODE BEGIN StartTask02 */
  char payload_cmd[256];
  unsigned long msg_counter = 0; // Changed to unsigned long to match %lu perfectly without warnings
  uint8_t link_is_up = 0;

  /* Infinite loop */
  for(;;)
  {
    // If our connection link is down, run the configuration state machine
    if (!link_is_up) {
        Log_String("\r\n--- [NET] Connection lost/down. Initializing Network... ---\r\n");
        
        EC200U_SendAT("AT\r\n", 500);
        EC200U_SendAT("AT+CPIN?\r\n", 500);
        EC200U_SendAT("AT+QICSGP=1,1,\"internet.tn\",\"\",\"\",0\r\n", 1000);
        EC200U_SendAT("AT+QIACT=1\r\n", 4000);
        
        Log_String("\r\n--- [MQTT] Connecting to Broker... ---\r\n");
        EC200U_SendAT("AT+QMTCLOSE=0\r\n", 1000);
        EC200U_SendAT("AT+QMTCFG=\"version\",0,4\r\n", 1000);
        EC200U_SendAT("AT+QMTCFG=\"session\",0,1\r\n", 1000);
        
        if (EC200U_SendAT("AT+QMTOPEN=0,\"broker.hivemq.com\",1883\r\n", 5000)) {
            if (EC200U_SendAT("AT+QMTCONN=0,\"TUNAV_STM32_RTOS_8831\"\r\n", 5000)) {
                Log_String("\r\n--- [STATUS] Connected & Ready! ---\r\n");
                link_is_up = 1; // Handshake successful!
            }
        }
        
        if (!link_is_up) {
            Log_String("\r\n--- [RETRY] Setup failed. Retrying cycle in 5s... ---\r\n");
            osDelay(5000);
            continue; // Jump to next iteration to try again
        }
    }

    // Link is active: Formulate and stream payload
    snprintf(payload_cmd, sizeof(payload_cmd), 
             "AT+QMTPUB=0,0,0,0,\"tunav/telemetry\",\"Hello from RTOS Thread! Msg #%lu\"\r\n", 
             msg_counter);
                 
    // If the publish command returns a failure/error, mark link as down!
    if (EC200U_SendAT(payload_cmd, 3000)) {
        msg_counter++; // Only increment counter if broker acknowledged transmission
    } else {
        Log_String("\r\n--- [WARN] Publish failed. Resetting session link... ---\r\n");
        link_is_up = 0; 
    }
        
    osDelay(5000);
  }
  /* USER CODE END StartTask02 */
}

/* USER CODE BEGIN Header_StartTask03 */
/**
* @brief Function implementing the Tasklog thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartTask03 */
void StartTask03(void *argument)
{
  /* USER CODE BEGIN StartTask03 */
  char received_msg[128];
  /* Infinite loop */
  for(;;)
  {
    // Use portMAX_DELAY for native FreeRTOS queues
    if (xQueueReceive(LogQueueHandle, received_msg, portMAX_DELAY) == pdTRUE) {
        HAL_UART_Transmit(&huart2, (uint8_t*)received_msg, strlen(received_msg), 200);
    }
  }
  /* USER CODE END StartTask03 */
}

/* USER CODE BEGIN Header_StartTask04 */
/**
* @brief Function implementing the TaskPriorityMqt thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartTask04 */
void StartTask04(void *argument)
{
  /* USER CODE BEGIN StartTask04 */
 uint8_t link_1_is_up = 0;
  char priority_buffer[128];
  uint16_t p_idx = 0;

  // Wait 15 seconds at boot to allow Task02 to configure the network internet.tn
  osDelay(15000); 

  /* Infinite loop */
  for(;;)
  {
    // 1. Establish Link 1 for Subscriptions
    if (!link_1_is_up) {
        Log_String("\r\n--- [HIGH PRIORITY] Starting Link 1 for Subscription... ---\r\n");
        EC200U_SendAT("AT+QMTCLOSE=1\r\n", 1000);
        
        if (EC200U_SendAT("AT+QMTOPEN=1,\"broker.hivemq.com\",1883\r\n", 5000)) {
            if (EC200U_SendAT("AT+QMTCONN=1,\"TUNAV_SUB_CLIENT_99\"\r\n", 5000)) {
                if (EC200U_SendAT("AT+QMTSUB=1,1,\"tunav/commands\",1\r\n", 3000)) {
                    Log_String("--- [HIGH PRIORITY] Successfully Subscribed! ---\r\n");
                    link_1_is_up = 1;
                }
            }
        }
        
        if (!link_1_is_up) {
            osDelay(5000); // Retry later if failed
            continue;
        }
    }

    // 2. High-Speed Ring Buffer Parsing
    // We only wait 5 ticks for the Mutex. If Task02 is sending telemetry, 
    // we just skip this cycle and try again in 5ms.
    if (osMutexAcquire(GsmMutexHandle, 5) == osOK) {
        while (rx_tail != rx_head) {
            uint8_t ch = rx_ring_buf[rx_tail];
            rx_tail = (rx_tail + 1) % RX_BUF_SIZE;

            if (p_idx < sizeof(priority_buffer) - 1) {
                priority_buffer[p_idx++] = ch;
                priority_buffer[p_idx] = '\0';
            }

            // End of line detected
            if (ch == '\n') {
                // Did HiveMQ send us data?
                if (strstr(priority_buffer, "+QMTRECV:") != NULL) {
                    Log_String("\r\n[URGENT] MQTT Command Received:\r\n");
                    Log_String(priority_buffer);
                    
                    // Send to the command queue for processing
                    osMessageQueuePut(MqttCommandQueueHandle, priority_buffer, 0, 0);
                }
                
                // Clear the line buffer for the next incoming text
                p_idx = 0; 
                memset(priority_buffer, 0, sizeof(priority_buffer));
            }
        }
        osMutexRelease(GsmMutexHandle);
    }

    // Yield back to Task02 (Telemetry) and TaskLed so they aren't starved
    osDelay(5); 
  }
  /* USER CODE END StartTask04 */
}

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM1 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM1)
  {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
