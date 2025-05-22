/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32l0xx_hal.h"

#include "nfc_conf.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

int bprintf(const char *fmt, ...);

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define BUTTON0_Pin GPIO_PIN_0
#define BUTTON0_GPIO_Port GPIOC
#define BUTTON0_EXTI_IRQn EXTI0_1_IRQn
#define BUTTON1ALT_Pin GPIO_PIN_1
#define BUTTON1ALT_GPIO_Port GPIOC
#define BUTTON1ALT_EXTI_IRQn EXTI0_1_IRQn
#define BUTTON1_Pin GPIO_PIN_2
#define BUTTON1_GPIO_Port GPIOC
#define BUTTON1_EXTI_IRQn EXTI2_3_IRQn
#define NFC_BSS_Pin GPIO_PIN_3
#define NFC_BSS_GPIO_Port GPIOA
#define NFC_IRQ_Pin GPIO_PIN_4
#define NFC_IRQ_GPIO_Port GPIOA
#define NFC_IRQ_EXTI_IRQn EXTI4_15_IRQn
#define LED_NINT_Pin GPIO_PIN_8
#define LED_NINT_GPIO_Port GPIOB
#define LED_NINT_EXTI_IRQn EXTI4_15_IRQn
#define LED_NSD_Pin GPIO_PIN_9
#define LED_NSD_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */


typedef enum
{
  COM1 = 0U,
  COMn
}COM_TypeDef;


/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
