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

#include <stdio.h>
#include <string.h>
#include "ssd1306.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/*--- Voltage divider scaling (ADC mV → sensor-side mV) ---
 * Your signal conditioning circuit divides 5V sensor range down to
 * 3.3V ADC-safe range. To recover the original sensor voltage:
 *   sensor_mv = adc_mv * VDIV_NUM / VDIV_DEN
 *
 * Bench testing (pot wired direct to PA0/PA1): 1/1 = no scaling.
 * Vehicle (through 5V→3.3V divider):          50/33 ≈ 1.515x.
 */
#define VDIV_NUM  1
#define VDIV_DEN  1

/* TPS calibration points (sensor-side millivolts).
 * FSM spec: VTA = 0.1–1.0V closed, ~3.5V WOT.
 * For bench testing, set these to your pot's actual range.
 * For vehicle use, restore to 500 / 3500. */
#define TPS_MV_CLOSED      0
#define TPS_MV_WOT      2250

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;

CAN_HandleTypeDef hcan1;

I2C_HandleTypeDef hi2c1;

UART_HandleTypeDef huart2;

/* Definitions for sensorTask */
osThreadId_t sensorTaskHandle;
const osThreadAttr_t sensorTask_attributes = {
  .name = "sensorTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityAboveNormal,
};
/* Definitions for canTask */
osThreadId_t canTaskHandle;
const osThreadAttr_t canTask_attributes = {
  .name = "canTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for displayTask */
osThreadId_t displayTaskHandle;
const osThreadAttr_t displayTask_attributes = {
  .name = "displayTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityBelowNormal,
};
/* USER CODE BEGIN PV */

volatile uint16_t adc_raw[2];		// Raw 12-bit ADC readings (0-4095)
volatile float sensor_voltage[2];	// Converted voltage (0-3.3V)

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_ADC1_Init(void);
static void MX_CAN1_Init(void);
static void MX_I2C1_Init(void);
static void MX_USART2_UART_Init(void);
void StartSensorTask(void *argument);
void StartCanTask(void *argument);
void StartDisplayTask(void *argument);

/* USER CODE BEGIN PFP */
static int16_t clt_voltage_to_celsius(int sensor_mv);
static int     tps_voltage_to_percent(int sensor_mv);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/*--- CLT lookup table: sensor-side millivolts → temperature °C ---
 * Data from 3VZ-E NTC thermistor specs (89422-20010) with ~2.2kΩ
 * pull-up to 5V reference. Voltage DECREASES as temp rises because
 * NTC resistance drops, pulling the divider output lower.
 *
 * The table is sorted by decreasing voltage (= increasing temp).
 * clt_voltage_to_celsius() walks the table and linearly interpolates
 * between the two nearest points.
 */
typedef struct {
    int16_t  temp_c;  /* temperature in °C */
    uint16_t mv;      /* sensor-side voltage in millivolts */
} CLT_Point;

static const CLT_Point clt_table[] = {
    { -20, 4400 },
    {   0, 3600 },
    {  20, 2700 },
    {  40, 1700 },
    {  60, 1000 },
    {  80,  600 },
    { 100,  500 },
    { 120,  300 },
};
#define CLT_TABLE_SIZE  (sizeof(clt_table) / sizeof(clt_table[0]))

/**
 * @brief  Convert sensor-side millivolts to coolant temperature in °C.
 *         Uses piecewise linear interpolation between table points.
 * @param  sensor_mv  Voltage at the CLT sensor pin (millivolts)
 * @retval Temperature in °C (clamped to table range: -20 to 120)
 */
static int16_t clt_voltage_to_celsius(int sensor_mv)
{
    /* Clamp to table endpoints */
    if (sensor_mv >= (int)clt_table[0].mv)
        return clt_table[0].temp_c;                       /* very cold */
    if (sensor_mv <= (int)clt_table[CLT_TABLE_SIZE - 1].mv)
        return clt_table[CLT_TABLE_SIZE - 1].temp_c;      /* very hot  */

    /* Walk the table — voltage decreases as index increases */
    for (int i = 0; i < (int)CLT_TABLE_SIZE - 1; i++) {
        if (sensor_mv <= (int)clt_table[i].mv &&
            sensor_mv >= (int)clt_table[i + 1].mv)
        {
            /* Linear interpolation:
             *   fraction = (upper_mv - sensor_mv) / (upper_mv - lower_mv)
             *   temp = upper_temp + fraction * (lower_temp - upper_temp)
             *
             * All integer math — multiply before divide to keep precision. */
            int dv = clt_table[i].mv - clt_table[i + 1].mv;     /* positive */
            int dt = clt_table[i + 1].temp_c - clt_table[i].temp_c; /* positive */
            int offset = clt_table[i].mv - sensor_mv;
            return clt_table[i].temp_c + (int16_t)((offset * dt) / dv);
        }
    }
    return -99; /* should never reach here */
}

/**
 * @brief  Convert sensor-side millivolts to throttle position 0–100%.
 *         Simple linear interpolation between closed and WOT voltages.
 * @param  sensor_mv  Voltage at the TPS VTA pin (millivolts)
 * @retval Throttle position percentage, clamped to 0–100
 */
static int tps_voltage_to_percent(int sensor_mv)
{
    if (sensor_mv <= TPS_MV_CLOSED) return 0;
    if (sensor_mv >= TPS_MV_WOT)   return 100;
    return ((sensor_mv - TPS_MV_CLOSED) * 100) / (TPS_MV_WOT - TPS_MV_CLOSED);
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
  MX_ADC1_Init();
  MX_CAN1_Init();
  MX_I2C1_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */

  // Pull-up on CAN_RX (PB8) so the peripheral sees recessive (high)
  // during the init-to-normal transition
  GPIO_InitTypeDef canrx_fix = {0};
  canrx_fix.Pin = GPIO_PIN_8;
  canrx_fix.Mode = GPIO_MODE_AF_PP;
  canrx_fix.Pull = GPIO_PULLUP;
  canrx_fix.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  canrx_fix.Alternate = GPIO_AF9_CAN1;
  HAL_GPIO_Init(GPIOB, &canrx_fix);

  CAN_FilterTypeDef filter;
  filter.FilterBank = 0;
  filter.FilterMode = CAN_FILTERMODE_IDMASK;
  filter.FilterScale = CAN_FILTERSCALE_32BIT;
  filter.FilterIdHigh = 0x0000;
  filter.FilterIdLow = 0x0000;
  filter.FilterMaskIdHigh = 0x0000;
  filter.FilterMaskIdLow = 0x0000;
  filter.FilterFIFOAssignment = CAN_RX_FIFO0;
  filter.FilterActivation = ENABLE;
  HAL_CAN_ConfigFilter(&hcan1, &filter);

  HAL_CAN_Start(&hcan1);
  HAL_CAN_ActivateNotification(&hcan1, CAN_IT_RX_FIFO0_MSG_PENDING);

  /* USER CODE END 2 */

  /* Init scheduler */
  osKernelInitialize();

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of sensorTask */
  sensorTaskHandle = osThreadNew(StartSensorTask, NULL, &sensorTask_attributes);

  /* creation of canTask */
  canTaskHandle = osThreadNew(StartCanTask, NULL, &canTask_attributes);

  /* creation of displayTask */
  displayTaskHandle = osThreadNew(StartDisplayTask, NULL, &displayTask_attributes);

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
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Configure the global features of the ADC (Clock, Resolution, Data Alignment and number of conversion)
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV2;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.ScanConvMode = DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 1;
  hadc1.Init.DMAContinuousRequests = DISABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel its corresponding rank in the sequencer and its sample time.
  */
  sConfig.Channel = ADC_CHANNEL_0;
  sConfig.Rank = 1;
  sConfig.SamplingTime = ADC_SAMPLETIME_3CYCLES;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}

/**
  * @brief CAN1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_CAN1_Init(void)
{

  /* USER CODE BEGIN CAN1_Init 0 */

  /* USER CODE END CAN1_Init 0 */

  /* USER CODE BEGIN CAN1_Init 1 */

  /* USER CODE END CAN1_Init 1 */
  hcan1.Instance = CAN1;
  hcan1.Init.Prescaler = 2;
  hcan1.Init.Mode = CAN_MODE_NORMAL;
  hcan1.Init.SyncJumpWidth = CAN_SJW_1TQ;
  hcan1.Init.TimeSeg1 = CAN_BS1_13TQ;
  hcan1.Init.TimeSeg2 = CAN_BS2_2TQ;
  hcan1.Init.TimeTriggeredMode = DISABLE;
  hcan1.Init.AutoBusOff = DISABLE;
  hcan1.Init.AutoWakeUp = DISABLE;
  hcan1.Init.AutoRetransmission = DISABLE;
  hcan1.Init.ReceiveFifoLocked = DISABLE;
  hcan1.Init.TransmitFifoPriority = DISABLE;
  if (HAL_CAN_Init(&hcan1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN CAN1_Init 2 */

  /* USER CODE END CAN1_Init 2 */

}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 100000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

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
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  GPIO_InitTypeDef GPIO_InitStruct = {0};
  GPIO_InitStruct.Pin = GPIO_PIN_5;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
  CAN_RxHeaderTypeDef rx_header;
  uint8_t rx_data[8];
  HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &rx_header, rx_data);
  if (rx_header.StdId == 0x100)
  {
    HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
  }
}

/* USER CODE END 4 */

/* USER CODE BEGIN Header_StartSensorTask */
/**
  * @brief  Function implementing the sensorTask thread.
  * 		Starts the ADC, waits for the conversion to finish,
  * 		reads the 12-bit result (0-4095), converts to voltage
  * 		([raw / 4095] * 3.3V), then repeats for second channel.
  * 		Has a 10 Hz sample rate.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartSensorTask */
void StartSensorTask(void *argument)
{
  /* USER CODE BEGIN 5 */
	/* The ADC has a multiplexer (mux) that selects which pin to read.
	 * HAL_ADC_Start() does NOT change the selected channel — only
	 * HAL_ADC_ConfigChannel() does. So we must call ConfigChannel
	 * before each conversion to switch between PA0 and PA1. */
	ADC_ChannelConfTypeDef sConfig = {0};
	sConfig.Rank = 1;
	sConfig.SamplingTime = ADC_SAMPLETIME_3CYCLES;

	for(;;) {
	  // Select Channel 0 (PA0) - coolant temperature sensor
	  sConfig.Channel = ADC_CHANNEL_0;
	  HAL_ADC_ConfigChannel(&hadc1, &sConfig);
	  HAL_ADC_Start(&hadc1);
	  if (HAL_ADC_PollForConversion(&hadc1, 10) == HAL_OK) {
		  adc_raw[0] = HAL_ADC_GetValue(&hadc1);
		  sensor_voltage[0] = (float)adc_raw[0] * 3.3f / 4095.0f;
	  }
	  HAL_ADC_Stop(&hadc1);

	  // Select Channel 1 (PA1) - TPS
	  sConfig.Channel = ADC_CHANNEL_1;
	  HAL_ADC_ConfigChannel(&hadc1, &sConfig);
	  HAL_ADC_Start(&hadc1);
	  if (HAL_ADC_PollForConversion(&hadc1, 10) == HAL_OK) {
		  adc_raw[1] = HAL_ADC_GetValue(&hadc1);
		  sensor_voltage[1] = (float)adc_raw[1] * 3.3f / 4095.0f;
	  }
	  HAL_ADC_Stop(&hadc1);

	  osDelay(100);
	}
  /* USER CODE END 5 */
}

/* USER CODE BEGIN Header_StartCanTask */
/**
* @brief Function implementing the canTask thread.
* 		 Sets up a CAN transmit header with message ID 0x100
* 		 - StdId: an 11-bit identifier that tells receivers
* 		   "this frame contains sensor data"
* 		 - DLC: Data Length Code = 4 bytes, 2 per sensor
* 		 - RTR: DATA frame (not a Remote request)
* 		 Packs the two raw ADC values into the data payload
* 		 - Each value is 12-bit (0-4095), which fits in 2 bytes
* 		 - High byte first, then low byte (big-endian)
* 		 Sends the frame on the CAN bus
* 		 Has a 5 Hz transmit rate
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartCanTask */
void StartCanTask(void *argument)
{
  /* USER CODE BEGIN StartCanTask */

	CAN_TxHeaderTypeDef tx_header;
	uint8_t tx_data[8];
	uint32_t tx_mailbox;

	// COnfigure the CAN frame header
	tx_header.StdId = 0x100;					// Arbitration ID
	tx_header.ExtId = 0;						// Not using extended ID
	tx_header.RTR = CAN_RTR_DATA;				// Data frame, not remote request
	tx_header.IDE = CAN_ID_STD;				// Standard 11-bit ID
	tx_header.DLC = 4;						// 4 bytes of data
	tx_header.TransmitGlobalTime = DISABLE;

	for (;;) {
	  // Pack sensor 0 into bytes 0-1 (big-endian)
	  tx_data[0] = (adc_raw[0] >> 8) & 0xFF;
	  tx_data[1] = adc_raw[0] & 0xFF;

	  // Pack sensor 1 inot bytes 2-3 (big-endian)
	  tx_data[2] = (adc_raw[1] >> 8) & 0xFF;
	  tx_data[3] = adc_raw[1] & 0xFF;

	  // Transmit - HAL puts it in the next available mailbox
	  HAL_CAN_AddTxMessage(&hcan1, &tx_header, tx_data, &tx_mailbox);

	  osDelay(200);
	}
  /* USER CODE END StartCanTask */
}

/* USER CODE BEGIN Header_StartDisplayTask */
/**
* @brief Function implementing the displayTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartDisplayTask */
void StartDisplayTask(void *argument)
{
  /* USER CODE BEGIN StartDisplayTask */
	char buf[32];

	/* Initialize the OLED — sends the startup command sequence
	 * and clears the screen. Must happen once before drawing. */
	ssd1306_Init(&hi2c1);

	for(;;) {
		/* Convert raw ADC counts to millivolts, then scale
		 * through the voltage divider to get sensor-side mV. */
		int adc_mv0 = (adc_raw[0] * 3300) / 4095;
		int adc_mv1 = (adc_raw[1] * 3300) / 4095;
		int sen_mv0 = (adc_mv0 * VDIV_NUM) / VDIV_DEN;
		int sen_mv1 = (adc_mv1 * VDIV_NUM) / VDIV_DEN;

		/* Run the conversions */
		int16_t clt_c   = clt_voltage_to_celsius(sen_mv0);
		int     tps_pct = tps_voltage_to_percent(sen_mv1);

		/* Clear the framebuffer, then draw fresh content */
		ssd1306_Fill(SSD1306_COLOR_BLACK);

		/* Title bar */
		ssd1306_SetCursor(0, 0);
		ssd1306_WriteString("RetroLink", Font_11x18, SSD1306_COLOR_WHITE);

		/* Divider line under the title */
		ssd1306_DrawHLine(0, 20, 128, SSD1306_COLOR_WHITE);

		/* Sensor 0 — coolant temp: engineering unit + raw voltage */
		sprintf(buf, "CLT: %d C", (int)clt_c);
		ssd1306_SetCursor(0, 24);
		ssd1306_WriteString(buf, Font_7x10, SSD1306_COLOR_WHITE);

		sprintf(buf, "%d.%02dV", adc_mv0/1000, (adc_mv0%1000)/10);
		ssd1306_SetCursor(84, 24);
		ssd1306_WriteString(buf, Font_7x10, SSD1306_COLOR_WHITE);

		/* Sensor 1 — throttle position: engineering unit + raw voltage */
		sprintf(buf, "TPS: %d%%", tps_pct);
		ssd1306_SetCursor(0, 38);
		ssd1306_WriteString(buf, Font_7x10, SSD1306_COLOR_WHITE);

		sprintf(buf, "%d.%02dV", adc_mv1/1000, (adc_mv1%1000)/10);
		ssd1306_SetCursor(84, 38);
		ssd1306_WriteString(buf, Font_7x10, SSD1306_COLOR_WHITE);

		/* Push the framebuffer to the display */
		ssd1306_UpdateScreen();

		/* UART: engineering units + raw voltage for debugging */
		int len = sprintf(buf, "CLT:%dC(%dmV) TPS:%d%%(%dmV)\r\n",
		                  (int)clt_c, adc_mv0, tps_pct, adc_mv1);
		HAL_UART_Transmit(&huart2, (uint8_t *)buf, len, 100);

		osDelay(500);
	}
  /* USER CODE END StartDisplayTask */
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
