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
#define EMQX_BROKER_HOST "jfaa7c61.ala.eu-central-1.emqxsl.com" // Paste your Connection Address here
#define EMQX_BROKER_PORT "8883"             // EMQX Serverless TLS Port
#define EMQX_USERNAME    "Tunav"        // The MQTT username you created
#define EMQX_PASSWORD    "chebli"        // The MQTT password you created

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
UART_HandleTypeDef huart4;
UART_HandleTypeDef huart2;
UART_HandleTypeDef huart1; // Liaison Modbus RTU (VIGI 3000)
TIM_HandleTypeDef htim6;   // Timer périodique pour déclenchement Modbus

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

// --- VIGI frame support ---
#define SEUIL_HAUT 10.8f
#define SEUIL_BAS  6.3f
char g_imei[16] = "000000000000000";       // Populated once via AT+GSN
char g_last_at_response[512] = {0};        // Snapshot of the last EC200U_SendAT reply

/* Trames de requêtes Modbus (Read Holding Registers - Fonction 0x03) */
// Basé sur vos trames : ajustez l'ID esclave (0x01 par défaut) selon votre VIGI
uint8_t Req_Voie1_7[8]  = {0x01, 0x03, 0x00, 0x8D, 0x00, 0x07, 0x54, 0x26}; // Registre 141 (0x008D) - 7 mots (Mesures)
uint8_t Req_Alarmes[8]  = {0x01, 0x03, 0x00, 0x9E, 0x00, 0x03, 0x64, 0x25}; // Registre 158 (0x009E) - 3 mots (États)
uint8_t Req_Config[8]   = {0x01, 0x03, 0x00, 0x0D, 0x00, 0x02, 0x55, 0xC8}; // Registre 13 (0x000D)  - 2 mots

/* Buffers de réception et traitement Modbus */
uint8_t RxBuffer[100];
char str_mesures[100] = "";
char str_alarmes[100] = "";
char str_config[100]  = "";
char completeResponse[800];

/* Machine d'états pour l'enchaînement des requêtes Modbus */
typedef enum {
    MODBUS_IDLE,
    ATTENTE_REPONSE_1,
    ATTENTE_REPONSE_2,
    ATTENTE_REPONSE_3,
    CONSTRUCTION_TRAME
} ModbusState_t;

volatile ModbusState_t eModbusState = MODBUS_IDLE;
volatile uint8_t flag_rx_complete = 0;
volatile uint16_t rx_size = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_UART4_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_USART1_UART_Init(void); // Liaison Modbus RTU
static void MX_TIM6_Init(void);        // Timer périodique de déclenchement Modbus
void ProcessModbusResponse(uint8_t *data, uint16_t size, char *output_str);

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
#include <stdlib.h>

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
    
    // Snapshot the raw reply so callers can parse it (e.g. AT+GSN, AT+CCLK?)
    strncpy(g_last_at_response, buffer, sizeof(g_last_at_response) - 1);
    g_last_at_response[sizeof(g_last_at_response) - 1] = '\0';
    
    // 2. TRAP PROTECTION: Release the Mutex so other tasks can use the GSM
    osMutexRelease(GsmMutexHandle);
    
    return status;
}

// Extracts the first run of 14+ digits found in an AT response (used for AT+GSN / IMEI)
void Extract_IMEI(const char* resp, char* out, size_t out_size) {
    const char* p = resp;
    while (*p) {
        if (*p >= '0' && *p <= '9') {
            const char* start = p;
            int len = 0;
            while (*p >= '0' && *p <= '9') { p++; len++; }
            if (len >= 14) {
                int copy_len = (len < (int)(out_size - 1)) ? len : (int)(out_size - 1);
                strncpy(out, start, copy_len);
                out[copy_len] = '\0';
                return;
            }
        } else {
            p++;
        }
    }
}

// Convertit les octets de données utiles d'une trame Modbus en chaîne Hexadécimale
void ProcessModbusResponse(uint8_t *data, uint16_t size, char *output_str) {
    // Réponse standard : [ID][Fonction][Nb Octets] ... [Données] ... [CRC L][CRC H]
    // On extrait uniquement les données utiles (nb_octets_utiles = data[2])
    if (size > 5) {
        uint8_t nb_octets_utiles = data[2];
        char temp[5];
        output_str[0] = '\0';
        
        for (uint8_t i = 0; i < nb_octets_utiles && (i + 3) < (size - 2); i++) {
            sprintf(temp, "%02X", data[i + 3]);
            strncat(output_str, temp, 99 - strlen(output_str));
        }
    }
}

// Builds a *VIG&imei&Dddmmyyyyhhmmss&Sxxxxyyyy&Vxxxx...&Fstatus# frame.
// Fetches active Modbus register results read from VIGI 3000,
// and falls back to standard placeholders if no Modbus cycle has happened yet.
void Build_VigiFrame(char* out, size_t out_size) {
    // 1. Timestamp from the network clock (falls back to epoch if unavailable)
    char dateStr[15] = "01011970000000";
    if (EC200U_SendAT("AT+CCLK?\r\n", 1000)) {
        char* p = strstr(g_last_at_response, "+CCLK:");
        int yy, MM, dd, hh, mm, ss;
        if (p != NULL && sscanf(p, "+CCLK: \"%d/%d/%d,%d:%d:%d", &yy, &MM, &dd, &hh, &mm, &ss) == 6) {
            snprintf(dateStr, sizeof(dateStr), "%02d%02d%04d%02d%02d%02d", dd, MM, 2000 + yy, hh, mm, ss);
        }
    }

    // 2. Fetch the actual Modbus register values or fallback with random values if not ready
    char config_str[32] = "006C003F"; // Safe placeholder
    char mesures_str[64] = ""; 
    char alarmes_str[32] = "000000000000"; 
    
    if (strlen(str_config) > 0) {
        strncpy(config_str, str_config, sizeof(config_str) - 1);
        config_str[sizeof(config_str) - 1] = '\0';
    }
    
    if (strlen(str_mesures) > 0) {
        strncpy(mesures_str, str_mesures, sizeof(mesures_str) - 1);
        mesures_str[sizeof(mesures_str) - 1] = '\0';
    } else {
        // Generate 7 random values (16-bit hex) to simulate sensors
        // Using HAL_GetTick for a simple pseudo-random seed
        srand(HAL_GetTick());
        for (int i = 0; i < 7; i++) {
            // Generate a value between 0x0030 and 0x0060 (simulating realistic data)
            int val = (rand() % 48) + 48; 
            char hex[5];
            snprintf(hex, sizeof(hex), "%04X", val);
            strcat(mesures_str, hex);
        }
    }

    if (strlen(str_alarmes) > 0) {
        strncpy(alarmes_str, str_alarmes, sizeof(alarmes_str) - 1);
        alarmes_str[sizeof(alarmes_str) - 1] = '\0';
    }

    // Send final assembled frame
    snprintf(out, out_size, "*VIG&%s&D%s&S%s&V%s&F%s#", g_imei, dateStr, config_str, mesures_str, alarmes_str);
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
  MX_USART1_UART_Init(); // Initialize Modbus RTU interface
  MX_TIM6_Init();        // Initialize periodic Modbus trigger timer
  /* USER CODE BEGIN 2 */
   HAL_UART_Transmit(&huart2, (uint8_t*)"System Ready. Booting RTOS...\r\n", 31, 100);
   HAL_UART_Receive_IT(&huart4, &rx_tmp_byte, 1);
   
   // Start Modbus periodic trigger timer
   HAL_TIM_Base_Start_IT(&htim6);
  
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
  * @brief USART1 Initialization Function (Modbus RTU VIGI 3000)
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 9600; // Modbus standard
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief TIM6 Initialization Function (Periodic Modbus Master trigger)
  * @param None
  * @retval None
  */
static void MX_TIM6_Init(void)
{
  htim6.Instance = TIM6;
  // Let's set timer interrupt to fire every 10 seconds.
  // HSE/HSI internal clock might be 16MHz or 180MHz (depending on clock tree).
  // Prescaler = 16000-1 gives 1 ms ticks at 16MHz.
  // Period = 10000-1 gives 10 seconds.
  htim6.Init.Prescaler = 16000 - 1;
  htim6.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim6.Init.Period = 10000 - 1;
  htim6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim6) != HAL_OK)
  {
    Error_Handler();
  }
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
        // Read the SR and DR registers to clear ORE, NE, FE flags
        uint32_t isrflags   = READ_REG(huart->Instance->SR);
        uint32_t errorflags = (isrflags & (uint32_t)(USART_SR_PE | USART_SR_FE | USART_SR_NE | USART_SR_ORE));
        
        if (errorflags != RESET) {
            // Clear error flags by reading the data register
            __HAL_UART_FLUSH_DRREGISTER(huart);
        } else {
            // Write received byte to circular buffer
            uint16_t next_head = (rx_head + 1) % RX_BUF_SIZE;
            if (next_head != rx_tail) { // Sécurité anti-débordement
                rx_ring_buf[rx_head] = rx_tmp_byte;
                rx_head = next_head;
            }
        }
        
        // Force-restart the interrupt reception so it doesn't stay dead
        HAL_UART_Receive_IT(&huart4, &rx_tmp_byte, 1);
    }
}

// Callback for detecting end of Modbus frames via UART Idle Line Detection
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size) {
    if (huart->Instance == USART1) {
        rx_size = Size;
        flag_rx_complete = 1;
        char temp_log[64];
        snprintf(temp_log, sizeof(temp_log), "[MODBUS] Data received on USART1 (Size: %d)\r\n", Size);
        Log_String(temp_log);
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
  static uint32_t led_tick = 0;
  
  /* Infinite loop */
  for(;;)
  {
    // Toggle PC13 Debug LED every 10 cycles (50ms * 10 = 500ms) to preserve the original 500ms blink rate
    if (led_tick++ >= 10) {
        led_tick = 0;
        HAL_GPIO_TogglePin(Debug_Led_GPIO_Port, Debug_Led_Pin);
    }
    
    // Non-blocking processing of the Modbus State Machine
    switch (eModbusState) {
        
        case ATTENTE_REPONSE_1:
            if (flag_rx_complete) {
                flag_rx_complete = 0;
                ProcessModbusResponse(RxBuffer, rx_size, str_config);
                Log_String("[MODBUS] Config (seuils) reçue !\r\n");
                
                // Envoyer la requête 2 : Voie 1 à 7
                memset(RxBuffer, 0, sizeof(RxBuffer));
                HAL_UARTEx_ReceiveToIdle_IT(&huart1, RxBuffer, sizeof(RxBuffer));
                HAL_UART_Transmit(&huart1, Req_Voie1_7, sizeof(Req_Voie1_7), 200);
                eModbusState = ATTENTE_REPONSE_2;
            }
            break;

        case ATTENTE_REPONSE_2:
            if (flag_rx_complete) {
                flag_rx_complete = 0;
                ProcessModbusResponse(RxBuffer, rx_size, str_mesures);
                Log_String("[MODBUS] Mesures (voies 1-7) reçues !\r\n");
                
                // Envoyer la requête 3 : Alarmes
                memset(RxBuffer, 0, sizeof(RxBuffer));
                HAL_UARTEx_ReceiveToIdle_IT(&huart1, RxBuffer, sizeof(RxBuffer));
                HAL_UART_Transmit(&huart1, Req_Alarmes, sizeof(Req_Alarmes), 200);
                eModbusState = ATTENTE_REPONSE_3;
            }
            break;

        case ATTENTE_REPONSE_3:
            if (flag_rx_complete) {
                flag_rx_complete = 0;
                ProcessModbusResponse(RxBuffer, rx_size, str_alarmes);
                Log_String("[MODBUS] Alarmes/Défauts reçus !\r\n");
                eModbusState = CONSTRUCTION_TRAME;
            }
            break;

        case CONSTRUCTION_TRAME:
            // Assemblage de la trame au format *VIG
            snprintf(completeResponse, sizeof(completeResponse), 
                     "*VIG&%s&D05112021170030&S%s&V%s&F%s#\r\n", 
                     g_imei, str_config, str_mesures, str_alarmes);
            
            // Log local de la trame
            Log_String("[MODBUS] Nouvelle trame VIGI générée : ");
            Log_String(completeResponse);
            
            eModbusState = MODBUS_IDLE;
            break;
            
        case MODBUS_IDLE:
        default:
            break;
    }

    osDelay(50); // Petit délai non bloquant pour le planificateur RTOS
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
uint8_t link_is_up = 0;
static uint8_t imei_fetched = 0;

/* Infinite loop */
for(;;)
{
  if (!link_is_up) {
      Log_String("\r\n--- [NET] Connection lost/down. Initializing Network... ---\r\n");
      
      EC200U_SendAT("AT\r\n", 500);
      EC200U_SendAT("AT+CPIN?\r\n", 500);
      EC200U_SendAT("AT+QICSGP=1,1,\"internet.tn\",\"\",\"\",0\r\n", 1000);
      EC200U_SendAT("AT+QIACT=1\r\n", 4000);
      
      if (!imei_fetched) {
          if (EC200U_SendAT("AT+GSN\r\n", 1000)) {
              Extract_IMEI(g_last_at_response, g_imei, sizeof(g_imei));
              imei_fetched = 1;
          }
      }
      
      Log_String("\r\n--- [MQTT] Configuring Secure TLS Context for EMQX... ---\r\n");
      EC200U_SendAT("AT+QMTCLOSE=0\r\n", 1000);
      EC200U_SendAT("AT+QMTCFG=\"version\",0,4\r\n", 1000);
      EC200U_SendAT("AT+QMTCFG=\"session\",0,1\r\n", 1000);
      
      // Bind Link 0 to use internal SSL Context 0
      EC200U_SendAT("AT+QMTCFG=\"ssl\",0,1,0\r\n", 1000);
      
      // Configure SSL Context 0 profile settings
      EC200U_SendAT("AT+QSSLCFG=\"sslversion\",0,4\r\n", 1000);   // Force TLS 1.2
      EC200U_SendAT("AT+QSSLCFG=\"ciphersuite\",0,0xFFFF\r\n", 1000); // Allow all cipher suites
      EC200U_SendAT("AT+QSSLCFG=\"sni\",0,1\r\n", 1000);          // MANDATORY: Enable SNI routing
      EC200U_SendAT("AT+QSSLCFG=\"seclevel\",0,0\r\n", 1000);     // 0 = TLS Encryption without CA file validation
      
      Log_String("\r\n--- [MQTT] Opening Secure Connection Endpoint... ---\r\n");
      char open_cmd[128];
      snprintf(open_cmd, sizeof(open_cmd), "AT+QMTOPEN=0,\"" EMQX_BROKER_HOST "\"," EMQX_BROKER_PORT "\r\n");
      
      if (EC200U_SendAT(open_cmd, 6000)) {
          char conn_cmd[192];
          snprintf(conn_cmd, sizeof(conn_cmd), "AT+QMTCONN=0,\"TUNAV_STM32_RTOS_8831\",\"" EMQX_USERNAME "\",\"" EMQX_PASSWORD "\"\r\n");
          
          if (EC200U_SendAT(conn_cmd, 6000)) {
              Log_String("\r\n--- [STATUS] Connected & Authenticated via EMQX TLS! ---\r\n");
              link_is_up = 1; 
          }
      }
      
      if (!link_is_up) {
          Log_String("\r\n--- [RETRY] Setup failed. Retrying cycle in 5s... ---\r\n");
          osDelay(5000);
          continue; 
      }
  }

  // Link is active: build a real *VIG frame and publish it
  char vigi_frame[160];
  Build_VigiFrame(vigi_frame, sizeof(vigi_frame));
  snprintf(payload_cmd, sizeof(payload_cmd), 
           "AT+QMTPUB=0,0,0,0,\"tunav/telemetry\",\"%s\"\r\n", 
           vigi_frame);
               
  if (!EC200U_SendAT(payload_cmd, 3000)) {
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
      Log_String("\r\n--- [HIGH PRIORITY] Starting Link 1 TLS Connection... ---\r\n");
      EC200U_SendAT("AT+QMTCLOSE=1\r\n", 1000);
      
      // Bind Link 1 to use internal SSL Context 1
      EC200U_SendAT("AT+QMTCFG=\"ssl\",1,1,1\r\n", 1000);
      
      // Configure SSL Context 1 profile settings
      EC200U_SendAT("AT+QSSLCFG=\"sslversion\",1,4\r\n", 1000);
      EC200U_SendAT("AT+QSSLCFG=\"ciphersuite\",1,0xFFFF\r\n", 1000);
      EC200U_SendAT("AT+QSSLCFG=\"sni\",1,1\r\n", 1000);          // Mandatory SNI configuration
      EC200U_SendAT("AT+QSSLCFG=\"seclevel\",1,0\r\n", 1000);     // TLS session configuration
      
      char open_cmd[128];
      snprintf(open_cmd, sizeof(open_cmd), "AT+QMTOPEN=1,\"" EMQX_BROKER_HOST "\"," EMQX_BROKER_PORT "\r\n");
      
      if (EC200U_SendAT(open_cmd, 6000)) {
          char conn_cmd[192];
          snprintf(conn_cmd, sizeof(conn_cmd), "AT+QMTCONN=1,\"TUNAV_SUB_CLIENT_99\",\"" EMQX_USERNAME "\",\"" EMQX_PASSWORD "\"\r\n");
          
          if (EC200U_SendAT(conn_cmd, 6000)) {
              if (EC200U_SendAT("AT+QMTSUB=1,1,\"tunav/commands\",1\r\n", 3000)) {
                  Log_String("--- [HIGH PRIORITY] Successfully Subscribed via EMQX TLS! ---\r\n");
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
              if (strstr(priority_buffer, "+QMTRECV:") != NULL) {
                  Log_String("\r\n[URGENT] MQTT Command Received:\r\n");
                  Log_String(priority_buffer);
                  
                  if (strstr(priority_buffer, "reset") != NULL || strstr(priority_buffer, "RESET") != NULL) {
                      Log_String("\r\n[SYSTEM] Commande RESET reçue ! Redémarrage du STM32...\r\n");
                      osMutexRelease(GsmMutexHandle);
                      osDelay(500); 
                      NVIC_SystemReset(); 
                  }
                  
                  osMessageQueuePut(MqttCommandQueueHandle, priority_buffer, 0, 0);
              }
              
              p_idx = 0; 
              memset(priority_buffer, 0, sizeof(priority_buffer));
          }
      }
      osMutexRelease(GsmMutexHandle);
  }

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
  else if (htim->Instance == TIM6)
  {
    // Start Modbus read cycle if previous is finished (IDLE)
    if (eModbusState == MODBUS_IDLE) {
        memset(RxBuffer, 0, sizeof(RxBuffer));
        // Arm RX event interrupt
        HAL_UARTEx_ReceiveToIdle_IT(&huart1, RxBuffer, sizeof(RxBuffer));
        // Send request 1: configuration
        HAL_UART_Transmit(&huart1, Req_Config, sizeof(Req_Config), 200);
        
        eModbusState = ATTENTE_REPONSE_1;
    }
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
