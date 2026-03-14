/*
 * =====================================================================================
 * File Name:    systemHealthCheckup.c
 * Author:       Vanix Technologies PVT. LTD.
 * Date Created: 2025-02-17
 * Description:  This source file contains the implementation of the functions that
 *               perform the system health checkup for the Electric Vehicle Supply
 *               Equipment (EVSE) charger. These functions monitor and diagnose the
 *               status and performance of key system components, including hardware
 *               modules, communication interfaces, and sensors.
 *
 *               The file provides the logic for detecting faults, verifying system
 *               operation, and performing corrective actions to ensure the safe and
 *               efficient functioning of the EVSE charger. The health checkup routines
 *               are critical for maintaining reliability and identifying potential issues
 *               before they affect system performance.
 *
 * Revision History:
 * Version 1.0 - 2025-02-17 - Initial version.
 * =====================================================================================
 */

/*
 * =====================================================================================
 * Include Headers
 * =====================================================================================
 */
#include "systemHealthCheckup.h"

/*
 * =====================================================================================
 * Global Variables
 * =====================================================================================
 */
bool g_systemHealthIsOK = FALSE;						// Global flag to store the System Health live data
TaskHandle_t xSystemHealthCheckupTask;					// System Health Checkup Task handler pointer

/*
 * =====================================================================================
 * Static Global Variables
 * =====================================================================================
 */

/*
 * =====================================================================================
 * Static Function Definitions
 * =====================================================================================
 */

/**
 * @brief  Checks the health status of the ESP32 module.
 *
 * This function sends a command to the ESP32 module to check its health or operational status.
 * It can be used to verify if the module is responsive and functioning properly. The function
 * may include checking the module�s connection, communication status, or internal diagnostics.
 *
 * @param  None
 * @retval XST_SUCCESS   Indicates that the ESP32 module is healthy and operational.
 * @retval XST_FAILURE   Indicates that the ESP32 module is not functioning properly.
 */
static u8 checkESPModuleHealth(void)
{
	char responseBuffer[BUFFER_SIZE] = {0};
	char sendBuffer[] = "AT";
	u8 status, loopCnt;

	for(loopCnt = 0; loopCnt < 3; loopCnt++) {
		memset(responseBuffer, 0, sizeof(responseBuffer));

		// Wait for the mutex
		if (xSemaphoreTake(xATMutex, portMAX_DELAY) == pdTRUE) {
			status = SendUartATCommandWithResponse("AT", 10, 300, responseBuffer, sizeof(responseBuffer));
			if(status == XST_SUCCESS) {
				if(strstr(responseBuffer, "OK") != NULL) {
					xil_printf("ESP module health status is OK!\r\n");
					// Release the mutex
					xSemaphoreGive(xATMutex);
					return XST_SUCCESS;
				}
				else {
					xil_printf("Invalid Response of %s command : %s\r\n", sendBuffer, responseBuffer);
				}
			}
			// Release the mutex
			xSemaphoreGive(xATMutex);
		}
	}
	return XST_FAILURE;
}

/**
 * @brief  Checks the health status of the RFID module.
 *
 * This function checks the operational status of the RFID module to ensure that it is functioning
 * properly. It may involve verifying communication with the module, checking for any errors, or
 * confirming that the module is responsive to commands.
 *
 * @param  None
 * @retval XST_SUCCESS   Indicates that the RFID module is healthy and operational.
 * @retval XST_FAILURE   Indicates that the RFID module is not functioning properly.
 */
static u8 checkRFIDModuleHealth(void)
{
	u32 versionData = 0;

	// Wait for the RFID mutex
	if (xSemaphoreTake(xLCDMutex, portMAX_DELAY) == pdTRUE) {
		versionData = nfc_getFirmwareVersion();

		// Release the mutex
		xSemaphoreGive(xLCDMutex);
	}

	if (!versionData)
	{
		xil_printf("NFC/RFID module not responding!\n\r");
		return XST_FAILURE;
	}
	else {
		xil_printf("NFC/RFID module health status is OK!\r\n");
		return XST_SUCCESS;
	}
}

/**
 * @brief  Checks the health status of the Control Pilot (CP) module.
 *
 * This function verifies the operational status of the Control Pilot (CP) module
 * to ensure that it is functioning correctly. It may include checking signal integrity,
 * communication status, or other diagnostics related to CP functionality.
 *
 * @param  None
 * @retval XST_SUCCESS   Indicates that the CP module is healthy and operational.
 * @retval XST_FAILURE   Indicates that the CP module is not functioning properly.
 */
static u8 checkCPModuleHealth(void)
{
	bool overallFaultStatus = GetOverallFaultInfo();

	if (overallFaultStatus) {
		xil_printf("CP module fault detected!\n\r");
		return XST_FAILURE;
	}
	else {
		xil_printf("CP module health status is OK!\n\r");
		return XST_SUCCESS;
	}
}

/**
 * @brief  Checks the health status of the QSPI module.
 *
 * This function verifies the operational status of the QSPI module to ensure that it
 * is functioning correctly. It may involve communication checks, error detection,
 * or status verification of the QSPI interface.
 *
 * @param  None
 * @retval XST_SUCCESS   Indicates that the QSPI module is healthy and operational.
 * @retval XST_FAILURE   Indicates that the QSPI module is not functioning properly.
 */
static u8 checkQSPIModuleHealth(void)
{
	u8 retVal = XST_FAILURE;

#if 0
	// Wait for the QSPI mutex
	if (xSemaphoreTake(xQSPIMutex, portMAX_DELAY) == pdTRUE) {
		retVal = qspiSelfTest(&g_qspiInstance);

		// Release the mutex
		xSemaphoreGive(xQSPIMutex);
	}
#endif

	FIL fil;							/* File object */
	FRESULT ret;						/* Return variable for fatfs api return */

	/* Open a test.txt file for write and read operation */
	ret = f_open(&fil, "test.txt", FA_OPEN_ALWAYS | FA_WRITE);
	if (ret == FR_OK)
	{
		/* Write test buffer on file */
		char buffWrite[] = "0123456789";
		unsigned int writecount = 0;
		if (f_write(&fil, buffWrite, strlen(buffWrite), &writecount) > 0)
		{
			//xil_printf("test.txt write successfully!\r\n Data written %d\r\n",writecount);
		}

		/* Closing file */
		f_close(&fil);

		/* ReOpen a test.txt file for write and read operation */
		ret = f_open(&fil, "test.txt", FA_OPEN_ALWAYS | FA_READ);
		if (ret == FR_OK)
		{
			/* Read buffer from file */
			char buffRead[100] = "";
			unsigned int readcount = 0;
			if (f_read(&fil, buffRead, sizeof(buffRead), &readcount) == FR_OK)
			{
//				xil_printf("test.txt read successfully!\r\nFile Data %s, Length %d\r\n",buff,readcount);

				/* Check if Data matches */
				if(strlen(buffWrite) == strlen(buffRead))
				{
//					xil_printf("data on test.txt matched!\r\n\r\n");
					retVal = XST_SUCCESS;
				}
				else
				{
					xil_printf("test data on test.txt not matching!\r\n\r\n");
				}
			}

			/* Closing file */
			f_close(&fil);
		}
		else
		{
			xil_printf("opening test.txt file for reading failed!\n\r",ret);
		}
	}
	else
	{
		xil_printf("opening test.txt file for writing failed!\n\r");
	}

	if (retVal != XST_SUCCESS)
	{
		xil_printf("QSPI module not responding!\n\r");
		return XST_FAILURE;
	}
	else
	{
		xil_printf("QSPI module health status is OK!\r\n");
		return XST_SUCCESS;
	}
}

/*
 * =====================================================================================
 * Global Function Definitions
 * =====================================================================================
 */

/**
 * @brief  Checks the overall health status of the system.
 *
 * This function performs a series of checks on various system components to ensure that the system
 * is functioning properly. It may include verifying hardware status, communication links, or other
 * critical system functions. The result indicates whether the system is in a healthy state or requires attention.
 */
u8 checkSystemHealth(void)
{
	u8 status;

	/* Going to check the ESP module health by sending the AT command to confirm either module is responding or not */
	status = checkESPModuleHealth();
	if(status != XST_SUCCESS) {
		xil_printf("Issue with the ESP module!\r\n");
		g_systemHealthIsOK = FALSE;
		return XST_FAILURE;
	}

	/* Going to check the RFID module health by reading the firmware version to confirm either module is responding or not */
	status = checkRFIDModuleHealth();
	if(status != XST_SUCCESS) {
		xil_printf("Issue with the RFID module!\r\n");
		g_systemHealthIsOK = FALSE;
		return XST_FAILURE;
	}

	/* Going to check the CP module health by checking all faults to confirm either module is proper or not */
	status = checkCPModuleHealth();
	if(status != XST_SUCCESS) {
		xil_printf("Issue with the CP module!\r\n");
		g_systemHealthIsOK = FALSE;
		return XST_FAILURE;
	}

	/* Going to check the QSPI module health by calling its self test API to confirm either module is proper or not */
	/*status = checkQSPIModuleHealth();
	if(status != XST_SUCCESS) {
		xil_printf("Issue with the QSPI module!\r\n");
		g_systemHealthIsOK = FALSE;
		return XST_FAILURE;
	}
*/
	g_systemHealthIsOK = TRUE;

	return XST_SUCCESS;
}

/**
 * @brief  Performs a system health checkup task.
 *
 * This function is a task designed to periodically check the health of various system components.
 * It performs multiple checks, such as verifying hardware functionality, communication status, and
 * other critical system parameters. If any issues are detected, appropriate actions or alerts may be triggered.
 */
void prvSystemHealthCheckupTask( void *pvParameters )
{
	/* Delay for 30 seconds */
	const TickType_t x30second = INT_TO_TICKS( 3 * DELAY_10_SECONDS );

	/* Initialize UART with full configuration including interrupt setup */
	UartIntpInit(&IntcInstance, &UartInst, UART_DEVICE_ID, UART_IRPT_INTR);

	u8 retVal;

	while(1)
	{
		xil_printf("\t\t\t###   %s   ###\t\t\t\r\n", __func__);

		if (g_isOtaInProgress) {
			/* Delay for 30 second. */
			vTaskDelay( x30second );
		}

		/* Perform the System Health checkup functionality to get the system health status */
		retVal = checkSystemHealth();
		if(retVal != XST_SUCCESS) {
			xil_printf("System Health checkup Failed!\r\n");
		}
		else {
			xil_printf("System Health checkup Passed!\r\n");
		}

		/* Delay for 30 second. */
		vTaskDelay( x30second );
	}
}
