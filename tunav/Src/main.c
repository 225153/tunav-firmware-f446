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

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

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

/* USER CODE BEGIN PV */
#define RX_BUF_SIZE 1024
uint8_t rx_ring_buf[RX_BUF_SIZE];
volatile uint16_t rx_head = 0;
volatile uint16_t rx_tail = 0;
uint8_t rx_tmp_byte; // Octet temporaire pour l'interruption
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_UART4_Init(void);
static void MX_USART2_UART_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
#include <stdio.h>
#include <string.h>

#include <stdio.h>
#include <string.h>

void EC200U_SendAT(char* cmd, uint32_t timeout_ms) {
    char log[256];
    char buffer[512]; // Large buffer local pour reconstruire la réponse
    uint16_t idx = 0;
    uint32_t start_time = HAL_GetTick();
    
    memset(buffer, 0, sizeof(buffer));
    
    // Nettoyage préventif des erreurs matérielles UART4
    if (__HAL_UART_GET_FLAG(&huart4, UART_FLAG_ORE)) {
        __HAL_UART_CLEAR_OREFLAG(&huart4);
    }
    if (__HAL_UART_GET_FLAG(&huart4, UART_FLAG_NE)) {
        __HAL_UART_CLEAR_NEFLAG(&huart4);
    }

    // Affichage Log PC
    snprintf(log, sizeof(log), "\r\n[SEND] %s", cmd);
    HAL_UART_Transmit(&huart2, (uint8_t*)log, strlen(log), 100);
    
    // Envoi au modem (Pendant ce temps, l'interruption capture l'écho sans perte !)
    HAL_UART_Transmit(&huart4, (uint8_t*)cmd, strlen(cmd), 100);
    
    // Boucle de lecture asynchrone du Ring Buffer
    while ((HAL_GetTick() - start_time) < timeout_ms) {
        
        // Vider le ring buffer vers notre buffer local de traitement
        while (rx_tail != rx_head) {
            uint8_t ch = rx_ring_buf[rx_tail];
            rx_tail = (rx_tail + 1) % RX_BUF_SIZE;
            
            if (idx < sizeof(buffer) - 1) {
                buffer[idx++] = ch;
                buffer[idx] = '\0'; // Garder la chaîne valide pour strstr
            }
        }
        
        // --- Analyse des conditions d'arrêt ---
        if (strstr(cmd, "AT+QMTOPEN") != NULL) {
            if (strstr(buffer, "+QMTOPEN:") != NULL && strchr(strstr(buffer, "+QMTOPEN:"), '\n') != NULL) {
                break; // URC reçu et complet
            }
            if (strstr(buffer, "ERROR\r\n") != NULL) break;
        }
        else if (strstr(cmd, "AT+QMTCONN") != NULL) {
            if (strstr(buffer, "+QMTCONN:") != NULL && strchr(strstr(buffer, "+QMTCONN:"), '\n') != NULL) {
                break; // URC reçu et complet
            }
            if (strstr(buffer, "ERROR\r\n") != NULL) break;
        }
        else {
            if (strstr(buffer, "OK\r\n") != NULL || strstr(buffer, "ERROR\r\n") != NULL) {
                break;
            }
        }
        
        HAL_Delay(1); // Laisse le temps aux octets d'arriver dans le Ring Buffer
    }
    
    // Affichage du résultat propre sur le PC
    if (idx > 0) {
        HAL_UART_Transmit(&huart2, (uint8_t*)"\r\n[RECV] ", 9, 100);
        HAL_UART_Transmit(&huart2, (uint8_t*)buffer, idx, 500);
        HAL_UART_Transmit(&huart2, (uint8_t*)"\r\n", 2, 100);
    } else {
        HAL_UART_Transmit(&huart2, (uint8_t*)"\r\n[ERROR] No response from EC200U\r\n", 35, 100);
    }
}

void EC200U_MQTTSend(char* payload) {
    char mqtt_cmd[300];

    HAL_UART_Transmit(&huart2, (uint8_t*)"\r\n--- Sending Simple Unencrypted MQTT ---\r\n", 43, 100);

    // 1. Force close any stuck previous sessions
    EC200U_SendAT("AT+QMTCLOSE=0\r\n", 1500);
    HAL_Delay(200);

    // 2. Set MQTT Protocol version to v3.1.1 (Required by HiveMQ)
    EC200U_SendAT("AT+QMTCFG=\"version\",0,4\r\n", 1000);
    HAL_Delay(150);

    // 3. Force Clean Session = 1 (Tells HiveMQ to clear old failed attempts immediately)
    EC200U_SendAT("AT+QMTCFG=\"session\",0,1\r\n", 1000);
    HAL_Delay(150);

    // 4. Configure Keepalive timeout to 60 seconds
    EC200U_SendAT("AT+QMTCFG=\"keepalive\",0,60\r\n", 1000);
    HAL_Delay(150);

    // 5. Keep SSL disabled on the microcontroller side (Uses raw TCP)
    EC200U_SendAT("AT+QMTCFG=\"ssl\",0,0\r\n", 1000);
    HAL_Delay(200);

    // 6. Open connection to HiveMQ using port 1883 
    EC200U_SendAT("AT+QMTOPEN=0,\"broker.hivemq.com\",1883\r\n", 6000);
    HAL_Delay(500);

    // 7. Connect with a completely UNIQUE Client ID string (No spaces!)
    EC200U_SendAT("AT+QMTCONN=0,\"TUNAV_STM32_Board_8831\"\r\n", 6000);
    HAL_Delay(500);

    // 8. Publish your message payload to the shared topic
    snprintf(mqtt_cmd, sizeof(mqtt_cmd), "AT+QMTPUB=0,0,0,0,\"tunav/telemetry\",\"%s\"\r\n", payload);
    EC200U_SendAT(mqtt_cmd, 4000);
    HAL_Delay(200);

    // 9. Disconnect gracefully
    EC200U_SendAT("AT+QMTDISC=0\r\n", 1500);
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
 HAL_UART_Transmit(&huart2, (uint8_t*)"System Ready. Testing EC200U...\r\n", 33, 100);
  
  // TOUT PREMIER ÉLÉMENT : Lancer l'écoute par interruption sur 1 octet
  HAL_UART_Receive_IT(&huart4, &rx_tmp_byte, 1);

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    // Toggle LED
    HAL_GPIO_TogglePin(Debug_Led_GPIO_Port, Debug_Led_Pin);
    
    // Verification de base du modem (500ms d'attente suffisent amplement ici)
    EC200U_SendAT("AT\r\n", 500);
    EC200U_SendAT("AT+CPIN?\r\n", 500);

    // Verifications réseau cellulaire
    EC200U_SendAT("AT+CREG?\r\n", 500);
    EC200U_SendAT("AT+CGREG?\r\n", 500);

    // Activation du contexte GPRS Orange Tunisie
    HAL_UART_Transmit(&huart2, (uint8_t*)"\r\n--- Activating Orange GPRS ---\r\n", 34, 100);
    
    // Configuration APN et activation du contexte (Temps adapté pour la recherche réseau)
    EC200U_SendAT("AT+QICSGP=1,1,\"weborange\",\"\",\"\",0\r\n", 1000);
    EC200U_SendAT("AT+QIACT=1\r\n", 4000); 
    EC200U_SendAT("AT+QIACT?\r\n", 1000);

    // --- DEBUT TRANSMISSION MQTT ---
    HAL_UART_Transmit(&huart2, (uint8_t*)"\r\n--- Starting MQTT Send via Method B ---\r\n", 44, 100);
    
    EC200U_MQTTSend("Hello from TUNAV Board!");
    
    // --- FIN TRANSMISSION MQTT ---

    EC200U_SendAT("AT+CSQ\r\n", 1000);
    
    // Pause de 5 secondes avant de relancer un cycle complet de test
    HAL_Delay(5000);
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