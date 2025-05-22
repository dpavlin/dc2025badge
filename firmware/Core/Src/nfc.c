/*
 * nfc.c
 *
 *  Created on: May 15, 2025
 *      Author: i
 */


#include "nfc.h"
#include "custom.h"
#include "custom_board.h"

#include<stdio.h>

uint8_t globalCommProtectCnt = 0;   /*!< Global Protection counter     */

bool nfc_init(){
	BSP_NFC0XCOMM_Init();
	BADGEBSP_COM_Init(COM1);
	USR_INT_LINE.Line = USR_INT_LINE_NUM;
	USR_INT_LINE.PendingCallback = st25r3916Isr;
	//BSP_PB_Init(BUTTON_USER, BUTTON_MODE_GPIO);

	// Configure interrupt callback
	(void)HAL_EXTI_GetHandle(&USR_INT_LINE, USR_INT_LINE.Line);
	(void)HAL_EXTI_RegisterCallback(&USR_INT_LINE, HAL_EXTI_COMMON_CB_ID, BSP_NFC0XCOMM_IRQ_Callback);

	// Initialize RFAL
	return pollingDemoIni();
}


void nfc_loop(){
	pollingDemoCycle();
}


/**
  * @brief      SPI Read and Write byte(s) to device
  * @param[in]  pTxData : Pointer to data buffer to write
  * @param[out] pRxData : Pointer to data buffer for read data
  * @param[in]  Length : number of bytes to write
  * @return     BSP status
  */
int32_t BSP_NFC0XCOMM_SendRecv(const uint8_t * const pTxData, uint8_t * const pRxData, uint16_t Length)
{
  HAL_StatusTypeDef status = HAL_ERROR;
  int32_t ret = BSP_ERROR_NONE;

  if((pTxData != NULL) && (pRxData != NULL))
  {
    status = HAL_SPI_TransmitReceive(&COMM_HANDLE, (uint8_t *)pTxData, (uint8_t *)pRxData, Length, 2000);
  }
  else if ((pTxData != NULL) && (pRxData == NULL))
  {
    status = HAL_SPI_Transmit(&COMM_HANDLE, (uint8_t *)pTxData, Length, 2000);
  }
  else if ((pTxData == NULL) && (pRxData != NULL))
  {
    status = HAL_SPI_Receive(&COMM_HANDLE, (uint8_t *)pRxData, Length, 2000);
  }
  else
  {
  	ret = BSP_ERROR_WRONG_PARAM;
  }

  /* Check the communication status */
  if (status != HAL_OK)
  {
    /* Execute user timeout callback */
    ret = BSP_NFC0XCOMM_Init();
  }

  return ret;
}

/**
  * @brief  BSP SPI1 callback
  * @param  None
  * @return None
  */
__weak void BSP_NFC0XCOMM_IRQ_Callback(void)
{
  /* Prevent unused argument(s) compilation warning */

  /* This function should be implemented by the user application.
   * It is called into this driver when an event from ST25R3916 is triggered.
   */
  st25r3916Isr();
}
