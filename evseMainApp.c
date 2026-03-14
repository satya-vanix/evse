/*
 * =====================================================================================
 * File Name:    evseMainApp.c
 * Author:       Vanix Technologies PVT. LTD.
 * Date Created: 2025-02-17
 * Description:  This source file contains the main application logic for handling
 *               the overall activities of the Electric Vehicle Supply Equipment (EVSE)
 *               charger. It integrates and manages all the components involved in the
 *               charging process, including communication modules, hardware control,
 *               and system monitoring.
 *
 *               The file is responsible for orchestrating the charging cycle,
 *               ensuring proper initialization of connected devices, handling user
 *               inputs, and providing real-time data such as charging status,
 *               voltage, current, and diagnostics information to the user interface.
 *               It also manages fault detection, safety protocols, and error handling
 *               throughout the charging operation.
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
#include "evseMainApp.h"
#include "lvgl/lvgl.h"
#include "lvgl_setup.h"

#define SIMULATION 0 /* i have changed it from 1 to 0 */
/*
 * =====================================================================================
 * Global Variables
 * =====================================================================================
 */
SemaphoreHandle_t xATMutex = NULL;	/* AT Mutex handler */
SemaphoreHandle_t xLCDMutex = NULL;	/* LCD Mutex handler */
SemaphoreHandle_t xRFIDMutex = NULL;/* RFID Mutex handler */
SemaphoreHandle_t xQSPIMutex = NULL;/* QSPI Mutex handler */

TaskHandle_t xEvseMainAppTask;		/* EVSE main application task handler pointer */
TimerHandle_t xSessionTimer;  		/* Session Timer handle */

EVSEStateMachineStatus_e g_EVSECurrentState;	/* Global variable to track the EVSE current state status */
int g_uniqueTransactionId = 0;					/* Global variable to store the unique transaction ID for the current session */
bool g_isEVChargingInprogress = FALSE;			/* Global variable to track the EV charging In Progress status */
XQspiPs g_qspiInstance;							/* Global variable to store the QSPI flash instance handler */

FATFS g_FatFs;									/* Global variable fot fatfs file system */

/*
 * =====================================================================================
 * Static Global Variables
 * =====================================================================================
 */
static bool s_lcdMessageDisplayed = FALSE;		/* Static flag to track either message displayed on the LCD module or not */
static bool s_diagnosticPerformed = FALSE;		/* Static flag to track either diagnostic performed in maintainer mode or not */
static TickType_t s_EVChargingStartTime = 0;	/* Static Variable to store the charging start tick time */
static TickType_t s_EVChargingCurrentTime = 0;	/* Static Variable to store the charging current tick time */
static TickType_t s_EVChargingStopTime = 0;		/* Static Variable to store the charging stop tick time */
static uint32_t s_ElapsedTimeInSeconds	= 0;	/* Static Variable to store the elapsed charging time in seconds */

/*
 * =====================================================================================
 * Static Function Definitions
 * =====================================================================================
 */

/**
 * @brief  Generates a unique transaction ID.
 *
 * This function generates a unique transaction identifier that can be used
 * for tracking and referencing individual charging sessions. The transaction
 * ID ensures that each session is uniquely identifiable.
 *
 * @param  None
 * @retval int A unique transaction ID.
 */
static int getUniqueTransactionId(void)
{
	/* Local variables declaration and initialization */
	XTime time;
	int transactionId = 0;

	/* Get the some timestamp value */
	XTime_GetTime(&time);
	transactionId = (int)(time & 0xFFFFFFFF);	// Extract lower 32 bits of time

	// XOR-Shift PRNG logic
	transactionId ^= transactionId << 13;
	transactionId ^= transactionId >> 17;
	transactionId ^= transactionId << 5;

	// Ensure the transaction ID is always positive (avoid negative IDs)
	if (transactionId < 0) {
		transactionId = -transactionId; // Convert negative to positive
	}

	return transactionId;
}

/**
 * @brief  Main application task for the EVSE (Electric Vehicle Supply Equipment) system.
 *
 * This function is a FreeRTOS task that handles the core functionality of the EVSE system. It is responsible
 * for managing the primary operations such as controlling charging, monitoring system status, processing
 * commands, and interfacing with the user or other systems. The task runs as part of the system�s task scheduler.
 *
 * @param  pvParameters Pointer to the parameters passed to the task (if any).
 * @retval None
 */
static void prvEvseMainAppTask( void *pvParameters )
{
	/* Delay for 2 seconds */
	const TickType_t x2second = INT_TO_TICKS( 2 * DELAY_1_SECOND );

	u8 retVal, counter = 0;
	u8 timeoutCnt = 10, timeout = 0;		// For the Wi-Fi connection waiting timeout count
	u8 hrs = 0, mins = 0;					// For the Charging time tracking
	char hashInfo[33] = {0};				// For the hash information
	char readHashInfo[33] = {0};			// For the read hash information
	bool l_appOtaStatus = FALSE, l_bitOtaStatus = FALSE, l_isAppOtaAttempted = FALSE;

	/* Initially delay the main task for few seconds to let others tasks perform their actions */
	vTaskDelay( INT_TO_TICKS( DELAY_10_SECONDS ) );

	/* Local Variable declaration and initialization */
	CPState_e l_CPState = 0;

	/* Initialize UART with full configuration including interrupt setup */
	UartIntpInit(&IntcInstance, &UartInst, UART_DEVICE_ID, UART_IRPT_INTR);

	/* Wait for the some more time to confirm Wi-Fi connection */
	while ((!g_isWifiConnected) && (timeout <= timeoutCnt)) {
		xil_printf("Waiting for Device to get connected with Wi-Fi Network!\r\n");
		/* Delay for 2 second. */
		vTaskDelay( x2second );
		timeout++;
	}

	/* Delay for 6 second to confirm the Internet network connectivity. */
	vTaskDelay( 3 * x2second );

	/* Synchronizing RTC with UTC network time */
	if (g_networkConnectivity) {
		xil_printf("Synchronizing RTC with UTC network time...!\r\n");
		if (rtcSyncDateTime() != XST_SUCCESS) {
			xil_printf("RTC Time Sync Failed!\r\n");
		}
	}

	/* Handle OTA related stuffs first before starting normal system operations */
	/* ============================================================================ */
	/* SKIP OTA CHECKING - Commented out to bypass OTA update check at startup     */
	/* ============================================================================ */
//#if 1  // Set to 1 to enable OTA checking, 0 to skip
	// Wait for the LCD mutex
	if (xSemaphoreTake(xLCDMutex, portMAX_DELAY) == pdTRUE) {
		/* Print the details on the LCD module */
		lcdPrintOtaUpdateCheckMessage();

		// Release the mutex
		xSemaphoreGive(xLCDMutex);
	}

	/* Check if system has the network connectivity to perform the OTA operations */
	if (g_networkConnectivity) {
		/* First Check if the Application file is available on cloud and it's version is different */
		xil_printf("Going to check the Application firmware file!\r\n");
		retVal = OtaGetFileInfo(OTA_APP_FILE_NAME, &g_OtaAppFileSize, &g_OtaAppHashValue, &g_OtaAppVersionValue);
		if (retVal == XST_SUCCESS) {
			if (g_OtaAppFileSize && (strncmp((const char*)&g_OtaAppVersionValue, APP_VERSION_INFO, 5))) {
				xil_printf("%s application file found on server!\r\n", OTA_APP_FILE_NAME);
				// Wait for the LCD mutex
				if (xSemaphoreTake(xLCDMutex, portMAX_DELAY) == pdTRUE) {
					/* Print the details on the LCD module */
					lcdPrintOtaUpdateFoundMessage();

					// Release the mutex
					xSemaphoreGive(xLCDMutex);
				}

				/* Set the OTA in progress flag */
				g_isOtaInProgress = TRUE;

				/* Delay for 6 second to confirm the Internet network connectivity. */
				vTaskDelay( 3 * x2second );

				retVal = OtaDownloadFirmwareFile(OTA_APP_FILE_NAME, g_OtaAppFileSize);
				if (retVal != XST_SUCCESS) {
					xil_printf("%s application file download failed!\r\n", OTA_APP_FILE_NAME);
					// Wait for the LCD mutex
					if (xSemaphoreTake(xLCDMutex, portMAX_DELAY) == pdTRUE) {
						/* Print the details on the LCD module */
						lcdPrintOtaUpdateFailedMessage();

						// Release the mutex
						xSemaphoreGive(xLCDMutex);
					}

					/* Reset the OTA in progress flag & Set the OTA process completed flag */
					g_isOtaInProgress = FALSE;
					g_isOtaProcessCompleted = TRUE;
				}
				else if(retVal == XST_SUCCESS) {
					/* Clear the hash information buffer */
					memset(hashInfo, 0, sizeof(hashInfo));
					memset(readHashInfo, 0, sizeof(readHashInfo));

					/* Convert the received data calculated hash information into the string format */
					MD5_To_String(&g_OtaRecvAppHashValue, (char *)&hashInfo);
					xil_printf("hashInfo : %s\r\n", hashInfo);
					MD5_To_String(&g_OtaReadAppHashValue, (char *)&readHashInfo);
					xil_printf("readHashInfo : %s\r\n", readHashInfo);

					/* If the received application data hash information matched with cloud hash data */
					if((strncmp((const char*)&g_OtaAppHashValue, (const char*)hashInfo, 32) == 0) &&
							(strncmp((const char*)&g_OtaAppHashValue, (const char*)readHashInfo, 32) == 0)) {
//					if(strncmp((const char*)&g_OtaAppHashValue, (const char*)hashInfo, 32) == 0) {
						xil_printf("Successfully installed %s application firmware!\r\n", OTA_APP_FILE_NAME);

						/* Set the OTA process completed flag */
						g_isOtaProcessCompleted = TRUE;

						/* Set the Application OTA status flag as true */
						l_appOtaStatus = TRUE;
					}
					/* If the received application data hash information is not matching with cloud hash data */
					else {
						xil_printf("Downloaded %s application file hash not matched!\r\n", OTA_APP_FILE_NAME);
						// Wait for the LCD mutex
						if (xSemaphoreTake(xLCDMutex, portMAX_DELAY) == pdTRUE) {
							/* Print the details on the LCD module */
							lcdPrintOtaUpdateFailedMessage();

							// Release the mutex
							xSemaphoreGive(xLCDMutex);
						}

						/* Reset the OTA in progress flag & Set the OTA process completed flag */
						g_isOtaInProgress = FALSE;
						g_isOtaProcessCompleted = TRUE;
					}
				}

				/* Set the Application OTA Attempted status flag as true */
				l_isAppOtaAttempted = TRUE;
			}
			else if (g_OtaAppFileSize == 0) {
				xil_printf("No %s application file found on server!\r\n", OTA_APP_FILE_NAME);
			}
			else {
				xil_printf("Same version %s of %s application file found on server!\r\n", APP_VERSION_INFO, OTA_APP_FILE_NAME);
			}
		}

		/* Now Check if the Bit stream file is available on cloud and it's version is different */
		xil_printf("Going to check the Bit stream hardware file!\r\n");
		retVal = OtaGetFileInfo(OTA_BIT_FILE_NAME, &g_OtaBitFileSize, &g_OtaBitHashValue, &g_OtaBitVersionValue);
		if (retVal == XST_SUCCESS) {
			if (g_OtaBitFileSize == 0) {
				xil_printf("No %s bit stream file found on server!\r\n", OTA_BIT_FILE_NAME);
			}
			else if (l_isAppOtaAttempted && !l_appOtaStatus) {
				xil_printf("In Both files OTA update %s application download failed so Skipping the %s Bit file download part!\r\n", OTA_APP_FILE_NAME, OTA_BIT_FILE_NAME);
			}
			else if (g_OtaBitFileSize && (strncmp((const char*)&g_OtaBitVersionValue, (const char*)&g_bitFirmwareVersionStr, 5))) {
				xil_printf("%s bit stream file found on server!\r\n", OTA_BIT_FILE_NAME);
				// Wait for the LCD mutex
				if (xSemaphoreTake(xLCDMutex, portMAX_DELAY) == pdTRUE) {
					/* Print the details on the LCD module */
					lcdPrintOtaUpdateFoundMessage();

					// Release the mutex
					xSemaphoreGive(xLCDMutex);
				}

				/* Set the OTA in progress flag as TRUE */
				g_isOtaInProgress = TRUE;

				/* Delay for 6 second to confirm the Internet network connectivity. */
				vTaskDelay( 3 * x2second );

				retVal = OtaDownloadHardwareFile(OTA_BIT_FILE_NAME, g_OtaBitFileSize);
				if (retVal != XST_SUCCESS) {
					xil_printf("%s bit stream file download failed!\r\n", OTA_BIT_FILE_NAME);
					// Wait for the LCD mutex
					if (xSemaphoreTake(xLCDMutex, portMAX_DELAY) == pdTRUE) {
						/* Print the details on the LCD module */
						lcdPrintOtaUpdateFailedMessage();

						// Release the mutex
						xSemaphoreGive(xLCDMutex);
					}

					/* Reset the OTA in progress flag & Set the OTA process completed flag */
					g_isOtaInProgress = FALSE;
					g_isOtaProcessCompleted = TRUE;
				}
				else if(retVal == XST_SUCCESS) {
					/* Clear the hash information buffer */
					memset(hashInfo, 0, sizeof(hashInfo));
					memset(readHashInfo, 0, sizeof(readHashInfo));

					/* Convert the received data calculated hash information into the string format */
					MD5_To_String(&g_OtaRecvBitHashValue, (char *)&hashInfo);
					xil_printf("hashInfo : %s\r\n", hashInfo);
					MD5_To_String(&g_OtaReadBitHashValue, (char *)&readHashInfo);
					xil_printf("readHashInfo : %s\r\n", readHashInfo);

					/* If the received bit stream data hash information matched with cloud hash data */
					if((strncmp((const char*)&g_OtaBitHashValue, (const char*)hashInfo, 32) == 0) &&
							(strncmp((const char*)&g_OtaBitHashValue, (const char*)readHashInfo, 32) == 0)) {
//					if(strncmp((const char*)&g_OtaBitHashValue, (const char*)hashInfo, 32) == 0) {
						xil_printf("Successfully installed %s bit stream hardware!\r\n", OTA_BIT_FILE_NAME);

						/* Set the OTA process completed flag */
						g_isOtaProcessCompleted = TRUE;

						/* Set the Bit stream OTA status flag as true */
						l_bitOtaStatus = TRUE;
					}
					/* If the received bit stream data hash information is not matching with cloud hash data */
					else {
						xil_printf("Downloaded %s bit stream file hash not matched!\r\n", OTA_BIT_FILE_NAME);
						// Wait for the LCD mutex
						if (xSemaphoreTake(xLCDMutex, portMAX_DELAY) == pdTRUE) {
							/* Print the details on the LCD module */
							lcdPrintOtaUpdateFailedMessage();

							// Release the mutex
							xSemaphoreGive(xLCDMutex);
						}

						/* Reset the OTA in progress flag & Set the OTA process completed flag */
						g_isOtaInProgress = FALSE;
						g_isOtaProcessCompleted = TRUE;
					}
				}
			}
			else {
				xil_printf("Same version %s of %s bit stream file found on server!\r\n", g_bitFirmwareVersionStr, OTA_BIT_FILE_NAME);
			}
		}

		if(g_isOtaInProgress && g_isOtaProcessCompleted) {
			if (!l_appOtaStatus && !l_bitOtaStatus) {
				// Wait for the LCD mutex
				if (xSemaphoreTake(xLCDMutex, portMAX_DELAY) == pdTRUE) {
					/* Print the details on the LCD module */
					lcdPrintOtaUpdateFailedMessage();

					// Release the mutex
					xSemaphoreGive(xLCDMutex);
				}
				/* Reset the OTA in progress flag */
				g_isOtaInProgress = FALSE;
			}
			else {
				xil_printf("OTA is successful and going to reboot the system!\r\n");

				// Wait for the LCD mutex
				if (xSemaphoreTake(xLCDMutex, portMAX_DELAY) == pdTRUE) {
					/* Print the details on the LCD module */
					lcdPrintOtaUpdateSuccessMessage();

					// Release the mutex
					xSemaphoreGive(xLCDMutex);
				}

				/* Configure QSPI options including Quad IO Mode */
				XQspiPs_SetClkPrescaler(&g_qspiInstance, XQSPIPS_CLK_PRESCALE_8);

				/* Now once we have confirmed that if application image is downloaded successfully and its
				 * hash value is matching with the cloud information then we need to switch the current
				 * partition information before the reboot so on next reboot it will boot with new updated
				 * image file */
				if (l_appOtaStatus) {
					uint8_t currentAppPartition = 0;

					// Wait for the QSPI mutex
					if (xSemaphoreTake(xQSPIMutex, portMAX_DELAY) == pdTRUE) {
						/* Read the current application partition number */
						retVal = qspiRead(&g_qspiInstance, &currentAppPartition, QSPI_ADDR_CURR_APPLICATION_STATUS_FLAG, 1);
						if (retVal == XST_SUCCESS) {
							xil_printf("currentAppPartition : %d\r\n", currentAppPartition);

							/* If current app partition is 1 then switch it to 2 */
							if (currentAppPartition == 1) {
								currentAppPartition = 2;
								/* Write the current application partition number with updated value */
								retVal = qspiWrite(&g_qspiInstance, &currentAppPartition, QSPI_ADDR_CURR_APPLICATION_STATUS_FLAG, 1);
								if (retVal == XST_SUCCESS) {
									xil_printf("currentAppPartition is updated successfully with value %d\r\n", currentAppPartition);
								}
							}
							/* If current app partition is 2 then switch it to 1 */
							else if (currentAppPartition == 2) {
								currentAppPartition = 1;
								/* Write the current application partition number with updated value */
								retVal = qspiWrite(&g_qspiInstance, &currentAppPartition, QSPI_ADDR_CURR_APPLICATION_STATUS_FLAG, 1);
								if (retVal == XST_SUCCESS) {
									xil_printf("currentAppPartition is updated successfully with value %d\r\n", currentAppPartition);
								}
							}
						}
						// Release the mutex
						xSemaphoreGive(xQSPIMutex);
					}
				}

				/* Now once we have confirmed that if bit stream image is downloaded successfully and its
				 * hash value is matching with the cloud information then we need to switch the current
				 * partition information before the reboot so on next reboot it will boot with new updated
				 * image file */
				if (l_bitOtaStatus) {
					uint8_t currentBitPartition = 0;

					// Wait for the QSPI mutex
					if (xSemaphoreTake(xQSPIMutex, portMAX_DELAY) == pdTRUE) {
						/* Read the current bit stream partition number */
						retVal = qspiRead(&g_qspiInstance, &currentBitPartition, QSPI_ADDR_CURR_BIT_STREAM_STATUS_FLAG, 1);
						if (retVal == XST_SUCCESS) {
							xil_printf("currentBitPartition : %d\r\n", currentBitPartition);

							/* If current bit stream partition is 1 then switch it to 2 */
							if (currentBitPartition == 1) {
								currentBitPartition = 2;
								/* Write the current bit stream partition number with updated value */
								retVal = qspiWrite(&g_qspiInstance, &currentBitPartition, QSPI_ADDR_CURR_BIT_STREAM_STATUS_FLAG, 1);
								if (retVal == XST_SUCCESS) {
									xil_printf("currentBitPartition is updated successfully with value %d\r\n", currentBitPartition);
								}
							}
							/* If current bit stream partition is 2 then switch it to 1 */
							else if (currentBitPartition == 2) {
								currentBitPartition = 1;
								/* Write the current bit stream partition number with updated value */
								retVal = qspiWrite(&g_qspiInstance, &currentBitPartition, QSPI_ADDR_CURR_BIT_STREAM_STATUS_FLAG, 1);
								if (retVal == XST_SUCCESS) {
									xil_printf("currentBitPartition is updated successfully with value %d\r\n", currentBitPartition);
								}
							}
						}
						// Release the mutex
						xSemaphoreGive(xQSPIMutex);
					}
				}

				/* Update the retry flag value as 0 into the QSPI storage */
				u8 retry = 0;

				// Wait for the QSPI mutex
				if (xSemaphoreTake(xQSPIMutex, portMAX_DELAY) == pdTRUE) {
					retVal = qspiWrite(&g_qspiInstance, &retry, QSPI_ADDR_RETRY_COUNT_STATUS_FLAG, 1);
					if (retVal == XST_SUCCESS) {
						xil_printf("Retry status flag is updated with value %d\r\n", retry);
					}
					else {
						xil_printf("Retry status flag update failed\r\n");
					}
					// Release the mutex
					xSemaphoreGive(xQSPIMutex);
				}

				/* Reset the OTA in progress flag */
				g_isOtaInProgress = FALSE;

				/* Perform the EVSE System hard reset */
				EVSE_PerformHardReset();
			}
		}

		if(!g_isOtaProcessCompleted) {
			// Wait for the LCD mutex
			if (xSemaphoreTake(xLCDMutex, portMAX_DELAY) == pdTRUE) {
				/* Print the details on the LCD module */
				lcdPrintOtaUpdateNotFoundMessage();

				// Release the mutex
				xSemaphoreGive(xLCDMutex);
			}
		}
	}
	else {
		// Wait for the LCD mutex
		if (xSemaphoreTake(xLCDMutex, portMAX_DELAY) == pdTRUE) {
			/* Print the details on the LCD module */
			lcdPrintOtaUpdateSkipMessage();

			// Release the mutex
			xSemaphoreGive(xLCDMutex);
		}
	}
//#endif  // End of OTA checking skip (Set to 1 to enable OTA)
	/* ============================================================================ */

	/* Handle OCPP messages operations as per the design flow */

	/* Sending the BootNotification OCPP message */
	if (!g_isOCPPBootNotificationSent && g_networkConnectivity)
	{
		xil_printf("Sending OCPP BootNotification messages!\r\n");

		/* Update the BootNotification structure fields to send the message */
		strcpy(g_BootNotificationRequest.chargePointModel, "Vanix-FPGA-EVSE");
		strcpy(g_BootNotificationRequest.chargePointVendor, "Vanix Technologies");
		strcpy(g_BootNotificationRequest.firmwareVersion, "1.1");
		g_BootNotificationRequest.serialNumbers = &g_ProductSerialNumbers;

		retVal = OCPPSendBootNotificationMessage(&g_BootNotificationRequest);
		if (retVal == XST_SUCCESS) {
			/* If message sending part success then check its response if it is accepted or not */
			if (strcmp(g_BootNotificationResponse.status, "Accepted") == 0) {
				xil_printf("OCPP BootNotification message sent successfully!\r\n");
				g_isOCPPBootNotificationSent = TRUE;

				// Stop the OCPP timer
				if (xTimerStop(xOCPPTimer, 0) != pdPASS) {
					// If failed to stop the timer
					xil_printf("Failed to stop OCPP Timer!\r\n");
				}
				else {
					// If timer successfully stopped
					xil_printf("OCPP Timer stopped!\r\n");
				}
			}
			else {
				xil_printf("OCPP BootNotification message sent successfully But got response as Rejected!\r\n");
				g_isOCPPBootNotificationSent = FALSE;
			}
		}
		else {
			xil_printf("OCPP BootNotification message sending failed!\r\n");
			g_isOCPPBootNotificationSent = FALSE;
		}
	}

	/* Sending the HeartBeat OCPP message */
	if (g_isOCPPBootNotificationSent && g_networkConnectivity)
	{
		xil_printf("Sending OCPP HeartBeat messages!\r\n");

		retVal = OCPPSendHeartBeatMessage();
		if (retVal == XST_SUCCESS) {
			xil_printf("OCPP HeartBeat message sent successfully!\r\n");
			/* Set the OCPP server connection flag */
			g_isOCPPServerConnected = TRUE;
			g_isOCPPServerConnectedPrevious = TRUE;
		}
		else {
			xil_printf("OCPP HeartBeat message sending failed!\r\n");
			/* Clear the OCPP server connection flag */
			g_isOCPPServerConnected = FALSE;

			/* Clear the OCPP server previous connection flag */
			g_isOCPPServerConnectedPrevious = FALSE;
		}
	}
	else
	{
		/* Clear the OCPP server connection flag */
		g_isOCPPServerConnected = FALSE;

		/* Clear the OCPP server previous connection flag */
		g_isOCPPServerConnectedPrevious = FALSE;
	}

	/* Check the system health first then send status notification to the backend OCPP server */

	/* If system health is not okay and GFCI fault is detected then enter the system into the
	 * fault mode, update user over LCD, send the backend OCPP status notification */
	if (g_isDeviceFaulted) {
		// Wait for the LCD mutex
		if (xSemaphoreTake(xLCDMutex, portMAX_DELAY) == pdTRUE) {
			/* Print the details on the LCD module */
			lcdPrintFaultMessage();

			// Release the mutex
			xSemaphoreGive(xLCDMutex);
		}

		/* Sending the StatusNotification OCPP message */
		xil_printf("Sending OCPP StatusNotification messages!\r\n");

		/* Update the StatusNotification structure fields to send the message */
		g_StatusNotificationRequest.serialNumbers = &g_ProductSerialNumbers;
		if (GetGFCIFaultInfo()) {
			strcpy(g_StatusNotificationRequest.errorCode, "GroundFailure");
		}
		else if (GetRelayFaultInfo()) {
			strcpy(g_StatusNotificationRequest.errorCode, "RelayFailure");
		}
		else if (GetVolatgeInfo()) {
			strcpy(g_StatusNotificationRequest.errorCode, "VoltageFailure");
		}
		else if (GetCurrentFaultInfo()) {
			strcpy(g_StatusNotificationRequest.errorCode, "CurrentFailure");
		}
		strcpy(g_StatusNotificationRequest.status, "Faulted");
		rtcGetDateTime(g_StatusNotificationRequest.timeStamp, sizeof(g_StatusNotificationRequest.timeStamp));

		retVal = OCPPSendStatusNotificationMessage(MSG_TYPE_ONLINE,&g_StatusNotificationRequest,NULL);
		if (retVal == XST_SUCCESS) {
			xil_printf("OCPP StatusNotification message sent successfully!\r\n");
		}
		else if (retVal == XST_NO_ACCESS) {
			xil_printf("OCPP StatusNotification message saved into memory successfully!\r\n");
		}
		else {
			xil_printf("OCPP StatusNotification message sending failed!\r\n");
		}

		/* Keeps the System into the Faulted state only until its fault will resolved. */
		while (g_isDeviceFaulted) {
			/* Delay for 2 second. */
			vTaskDelay( x2second );
		}

		/* Clear the LCD displayed fault message */
		lcd_clear();
	}

	/* If system health is okay and everything is fine then just proceed with normal operations */
	if (g_systemHealthIsOK) {
		/* Sending the StatusNotification OCPP message */
		xil_printf("Sending OCPP StatusNotification messages!\r\n");

		/* Update the StatusNotification structure fields to send the message */
		g_StatusNotificationRequest.serialNumbers = &g_ProductSerialNumbers;
		strcpy(g_StatusNotificationRequest.errorCode, "NoError");
		strcpy(g_StatusNotificationRequest.status, "Available");
		rtcGetDateTime(g_StatusNotificationRequest.timeStamp, sizeof(g_StatusNotificationRequest.timeStamp));

		retVal = OCPPSendStatusNotificationMessage(MSG_TYPE_ONLINE,&g_StatusNotificationRequest,NULL);
		if (retVal == XST_SUCCESS) {
			xil_printf("OCPP StatusNotification message sent successfully!\r\n");
		}
		else if (retVal == XST_NO_ACCESS) {
			xil_printf("OCPP StatusNotification message saved into memory successfully!\r\n");
		}
		else {
			xil_printf("OCPP StatusNotification message sending failed!\r\n");
		}
	}

	/* Change the EVSE state as in User Authentication pending mode */
	g_EVSECurrentState = EVSE_STATE_USER_AUTHENTICATION_PENDING;

	/* Change the LCD message displayed flag as FLASE to show new message for the next EVSE state */
	s_lcdMessageDisplayed = FALSE;

	while(1)
	{
		xil_printf("\t\t\t###   %s   ###\t\t\t\r\n", __func__);

		/* If Maintainer authenticated then we have to change the EVSE state into the maintenance mode and
		 * stop the normal executions to provide the access of the EVSE to maintainer for troubleshooting */
		if(g_isMaintainerAuthenticated && !s_diagnosticPerformed) {
			/* Change the EVSE state as in EVSE maintenance mode */
			g_EVSECurrentState = EVSE_STATE_MAINTENANCE;

			/* Change the LCD message displayed flag as FLASE to show new message for the next EVSE state */
			s_lcdMessageDisplayed = FALSE;

			/* Change the diagnostic performed flag as FLASE to perform it once for the diagnostic session */
			s_diagnosticPerformed = FALSE;
		}

		/* Keep monitor the device is in fault mode or not, in case it is in fault mode and charging is not in progress state then
		 * send the backend Fault Status Notification OCPP message and change the device state as the fault state */
		if (g_isDeviceFaulted && !g_isEVChargingInprogress && (g_EVSECurrentState <= EVSE_STATE_CHARGING_STARTING)) {
			/* Sending the StatusNotification OCPP message */
			xil_printf("Sending OCPP StatusNotification messages!\r\n");

			/* Update the StatusNotification structure fields to send the message */
			g_StatusNotificationRequest.serialNumbers = &g_ProductSerialNumbers;
			if (GetGFCIFaultInfo()) {
				strcpy(g_StatusNotificationRequest.errorCode, "GroundFailure");
			}
			else if (GetRelayFaultInfo()) {
				strcpy(g_StatusNotificationRequest.errorCode, "RelayFailure");
			}
			else if (GetVolatgeInfo()) {
				strcpy(g_StatusNotificationRequest.errorCode, "VoltageFailure");
			}
			else if (GetCurrentFaultInfo()) {
				strcpy(g_StatusNotificationRequest.errorCode, "CurrentFailure");
			}
			strcpy(g_StatusNotificationRequest.status, "Faulted");
			rtcGetDateTime(g_StatusNotificationRequest.timeStamp, sizeof(g_StatusNotificationRequest.timeStamp));

			retVal = OCPPSendStatusNotificationMessage(MSG_TYPE_ONLINE,&g_StatusNotificationRequest,NULL);
			if (retVal == XST_SUCCESS) {
				xil_printf("OCPP StatusNotification message sent successfully!\r\n");
			}
			else if (retVal == XST_NO_ACCESS) {
				xil_printf("OCPP StatusNotification message saved into memory successfully!\r\n");
			}
			else {
				xil_printf("OCPP StatusNotification message sending failed!\r\n");
			}

			/* Change the EVSE state as in EVSE Fault mode */
			g_EVSECurrentState = EVSE_STATE_FAULT;

			/* Change the LCD message displayed flag as FLASE to show new message for the next EVSE state */
			s_lcdMessageDisplayed = FALSE;
		}

#if SIMULATION
		if((g_EVSECurrentState >= EVSE_STATE_USER_AUTHENTICATION_DONE) && (g_EVSECurrentState <= EVSE_STATE_VEHICLE_CONNECTED)) {
			//l_CPState++;
		}
#endif

		switch (g_EVSECurrentState) {
		/* User Authentication Pending State */
		case EVSE_STATE_USER_AUTHENTICATION_PENDING:
			xil_printf("User Authentication Pending State\r\n");

#if !SIMULATION
			l_CPState = GetCPStateInfo();
#endif
			xil_printf("Control pilot state : %s\r\n", GetCPStateInfoString(l_CPState));

			/* Check for RFID authentication ONLY in STATE_B (Vehicle detected) */
			if (l_CPState == CP_STATE_VEHICLE_CONNECTED) {
				/* Check for user authentication (RFID or HTTP API) */
				if (CheckUserAuthentication() == XST_SUCCESS)
				{
					xil_printf("User authentication successful!\r\n");

					g_isUserAuthenticated = TRUE;
					  
					/* Indicate that RFID authenticated by turning ON the RFID status LED */
					setLedPin(LED_RFID);
					
					/* Write authentication success status to PL register */
					SetAuthenticationStatus(1);
				}
				else {
					/* Indicate that RFID not authenticated by turning OFF the RFID status LED */
					resetLedPin(LED_RFID);
					
					/* Write authentication failure status to PL register */
					SetAuthenticationStatus(0);

					/* Write power OFF status to PL register */
					SetPowerOnStatus(0);
				}
			}
			else {
				/* In STATE_A (Idle), don't check for RFID - just reset authentication */
				/* Indicate that RFID not authenticated by turning OFF the RFID status LED */
				resetLedPin(LED_RFID);
				
				/* Write authentication failure status to PL register */
				SetAuthenticationStatus(0);

				/* Write power OFF status to PL register */
				SetPowerOnStatus(0);
			}
			
			if(!g_isUserAuthenticated) {
					// If System Ready LCD message was not displayed earlier
					if(!s_lcdMessageDisplayed) {
						// Wait for the LCD mutex
						if (xSemaphoreTake(xLCDMutex, portMAX_DELAY) == pdTRUE) {
							/* Print the details on the LCD module */
							lcdPrintSystemReadyStateMessage();

							// Release the mutex
							xSemaphoreGive(xLCDMutex);
						}
						// Set the LCD message displayed flag as true to indicate System Ready message already displayed
						s_lcdMessageDisplayed = TRUE;
					}

					/* Display different messages based on CP state */
					if (l_CPState == CP_STATE_VEHICLE_CONNECTED) {
						/* STATE_B - Vehicle detected, ask for RFID authentication */
						if (g_networkConnectivity && !g_isTagDetected) {
							// Wait for the LCD mutex
							if (xSemaphoreTake(xLCDMutex, portMAX_DELAY) == pdTRUE) {
								/* Print the details on the LCD module */
								lcdPrintUserAuthenticateMessage();

								// Release the mutex
								xSemaphoreGive(xLCDMutex);
							}
						}
						else if (!g_networkConnectivity) {
							// Wait for the LCD mutex
							if (xSemaphoreTake(xLCDMutex, portMAX_DELAY) == pdTRUE) {
								/* Print the details on the LCD module */
								lcdPrintAuthMobileAppFailMessage();

								// Release the mutex
								xSemaphoreGive(xLCDMutex);
							}
						}
					}
					else {
						/* STATE_A (Idle) - Vehicle NOT connected, ask user to connect EV */
						// Wait for the LCD mutex
						if (xSemaphoreTake(xLCDMutex, portMAX_DELAY) == pdTRUE) {
							/* Print the details on the LCD module */
							lcdPrintEVNotDetectedMessage();  // Display "Connect your EV"

							// Release the mutex
							xSemaphoreGive(xLCDMutex);
						}
					}
				}
				else {
					/* If user authenticated then change the system current state */
					g_EVSECurrentState = EVSE_STATE_USER_AUTHENTICATION_DONE;

					/* Change the LCD message displayed flag as FLASE to show new message for the next EVSE state */
					s_lcdMessageDisplayed = FALSE;

					/* When user is authenticated so for that new session will start and need to reset all the old session info */
					g_uniqueTransactionId = 0;
					g_isEVChargingInprogress = FALSE;
					s_EVChargingStartTime = 0;
					s_EVChargingCurrentTime = 0;
					s_EVChargingStopTime = 0;
					s_ElapsedTimeInSeconds = 0;

					/* Update SessionId for Data Saving */
					int ret = flashUpdateCurrentSessionID();
					if(ret == XST_FAILURE)
					{
						xil_printf("%s, Session Id update failed, Error : %d\r\n", __func__, ret);
					}

					ret = flashReadCurrentSessionID(&g_CurrentSessionId);
					if(ret == XST_FAILURE)
					{
						xil_printf("%s, Session Id read failed, Error : %d\r\n", __func__, ret);
					}

					ret = flashCreateDirForSessionId(g_CurrentSessionId);
					if(ret == XST_FAILURE)
					{
						xil_printf("%s, Session Id creat dir failed, Error : %d\r\n", __func__, ret);
					}

					xil_printf("%s, New Session Started, Updated session id in %d\r\n", __func__, g_CurrentSessionId);

					/* Check if older session exist's which needs to be deleted */
					if((g_CurrentSessionId > OFFLINE_MODE_MAX_SESSIONS) && (flashIfDirForSessionIdExists(g_CurrentSessionId - OFFLINE_MODE_MAX_SESSIONS) == XST_SUCCESS))
					{
						xil_printf("%s, Removing dir for session id %d\r\n", __func__,g_CurrentSessionId - OFFLINE_MODE_MAX_SESSIONS);
						ret = flashRemoveDirForSessionId(g_CurrentSessionId - OFFLINE_MODE_MAX_SESSIONS);
						if(ret == XST_FAILURE)
						{
							xil_printf("%s, Session Id dir remove failed, Sessionid : %d, Error : %d\r\n", __func__, g_CurrentSessionId - OFFLINE_MODE_MAX_SESSIONS,ret);
						}
					}
				}

				break;

			/* User Authentication Done State */
			case EVSE_STATE_USER_AUTHENTICATION_DONE:
				xil_printf("User Authentication Done State\r\n");

#if !SIMULATION
				l_CPState = GetCPStateInfo();
#endif
				xil_printf("Control pilot state : %s\r\n", GetCPStateInfoString(l_CPState));

				if(l_CPState >= CP_STATE_EVSE_IDLE) {
					/* Change the EVSE state as in EVSE Idle mode */
					g_EVSECurrentState = EVSE_STATE_IDLE;

					/* Change the LCD message displayed flag as FLASE to show new message for the next EVSE state */
					s_lcdMessageDisplayed = FALSE;
				}

				break;

			/* EVSE Idle State */
			case EVSE_STATE_IDLE:
				xil_printf("EVSE Idle State\r\n");

				// If LCD message was not displayed earlier
				if(!s_lcdMessageDisplayed) {
					// Wait for the LCD mutex
					if (xSemaphoreTake(xLCDMutex, portMAX_DELAY) == pdTRUE) {
						/* Print the details on the LCD module */
						lcdPrintEVNotDetectedMessage();

						// Release the mutex
						xSemaphoreGive(xLCDMutex);
					}
					// Set the LCD message displayed flag as true to indicate message already displayed
					s_lcdMessageDisplayed = TRUE;
				}

#if !SIMULATION
				l_CPState = GetCPStateInfo();
#endif
				xil_printf("Control pilot state : %s\r\n", GetCPStateInfoString(l_CPState));

				if (l_CPState >= CP_STATE_VEHICLE_CONNECTED) {
					/* If Vehicle is getting connected within the 30 seconds session after user
					 * authentication then stop the session timer */

					/* Check if the Session timer active or not */
					if( xTimerIsTimerActive( xSessionTimer ) == pdTRUE ) {
						/* Stop the Session timer */
						if (xTimerStop(xSessionTimer, 0) != pdPASS) {
							/* If failed to stop the timer */
							xil_printf("Failed to stop Session Timer!\r\n");
						}
						else {
							/* If timer successfully stopped */
							xil_printf("Session Timer stopped!\r\n");
						}
					}

					/* Change the EVSE state as in EVSE Vehicle connected mode */
					g_EVSECurrentState = EVSE_STATE_VEHICLE_CONNECTED;

					/* Change the LCD message displayed flag as FLASE to show new message for the next EVSE state */
					s_lcdMessageDisplayed = FALSE;
				}

				break;

			/* EVSE Vehicle Connected State */
			case EVSE_STATE_VEHICLE_CONNECTED:
				xil_printf("EVSE Vehicle Connected State\r\n");

				// If LCD message was not displayed earlier
				if(!s_lcdMessageDisplayed) {
					// Wait for the LCD mutex
					if (xSemaphoreTake(xLCDMutex, portMAX_DELAY) == pdTRUE) {
						/* Print the details on the LCD module */
						lcdPrintEVWaitingForChargingMessage();

						// Release the mutex
						xSemaphoreGive(xLCDMutex);
					}
					// Set the LCD message displayed flag as true to indicate message already displayed
					s_lcdMessageDisplayed = TRUE;
				}

#if !SIMULATION
				l_CPState = GetCPStateInfo();
#endif
				xil_printf("Control pilot state : %s\r\n", GetCPStateInfoString(l_CPState));

				/* After the Vehicle connection with the EVSE in case of the EV is disconnected before
				 * charging starts then in that case we have to restart the session again */
				if (l_CPState < CP_STATE_VEHICLE_CONNECTED) {
					// Wait for the LCD mutex
					if (xSemaphoreTake(xLCDMutex, portMAX_DELAY) == pdTRUE) {
						/* Print the details on the LCD module */
						lcdPrintEVDisconnectBeforeChargingMessage();

						// Release the mutex
						xSemaphoreGive(xLCDMutex);
					}

					/* Change the EVSE state as in User Authentication Pending mode */
					g_EVSECurrentState = EVSE_STATE_USER_AUTHENTICATION_PENDING;

					/* Reset the User Authenticated Global flag */
					g_isUserAuthenticated = FALSE;
					
					/* Write authentication failure status to PL register - session ended */
					SetAuthenticationStatus(0);

					/* Write power OFF status to PL register - session ended */
					SetPowerOnStatus(0);

					/* Change the LCD message displayed flag as FLASE to show new message for the next EVSE state */
					s_lcdMessageDisplayed = FALSE;
				}
				else if (l_CPState >= CP_STATE_CHARGING_INPROGRESS) {
					/* Change the EVSE state as in EVSE Charging Ready mode */
					g_EVSECurrentState = EVSE_STATE_CHARGING_READY;

					/* Change the LCD message displayed flag as FLASE to show new message for the next EVSE state */
					s_lcdMessageDisplayed = FALSE;

					//mssleep(20);  // Fast state transition

					SetPowerOnStatus(1);
				}

				break;

			/* EVSE Charging Ready State */
			case EVSE_STATE_CHARGING_READY:
				xil_printf("EVSE Charging Ready State\r\n");
			//	mssleep(10);  // Fast state transition

			//	SetPowerOnStatus(1);

				// If LCD message was not displayed earlier
				if(!s_lcdMessageDisplayed) {
					// Wait for the LCD mutex
					if (xSemaphoreTake(xLCDMutex, portMAX_DELAY) == pdTRUE) {
						/* Print the details on the LCD module */
						lcdPrintEVReadyForChargingMessage();

						// Release the mutex
						xSemaphoreGive(xLCDMutex);
					}
					// Set the LCD message displayed flag as true to indicate message already displayed
					s_lcdMessageDisplayed = TRUE;
				}

#if !SIMULATION
				l_CPState = GetCPStateInfo();
#endif
				xil_printf("Control pilot state : %s\r\n", GetCPStateInfoString(l_CPState));

				if (l_CPState == CP_STATE_VEHICLE_CONNECTED) {
					/* Change the EVSE state as in EVSE Vehicle connected mode */
					g_EVSECurrentState = EVSE_STATE_VEHICLE_CONNECTED;

					/* Change the LCD message displayed flag as FLASE to show new message for the next EVSE state */
					s_lcdMessageDisplayed = FALSE;
				}
				else if (l_CPState < CP_STATE_VEHICLE_CONNECTED) {
					// Wait for the LCD mutex
					if (xSemaphoreTake(xLCDMutex, portMAX_DELAY) == pdTRUE) {
						/* Print the details on the LCD module */
						lcdPrintEVDisconnectBeforeChargingMessage();

						// Release the mutex
						xSemaphoreGive(xLCDMutex);
					}

					/* Change the EVSE state as in User Authentication Pending mode */
					g_EVSECurrentState = EVSE_STATE_USER_AUTHENTICATION_PENDING;

					/* Reset the User Authenticated Global flag */
					g_isUserAuthenticated = FALSE;
					
					/* Write authentication failure status to PL register - session ended */
					SetAuthenticationStatus(0);

					/* Write power OFF status to PL register - session ended */
					SetPowerOnStatus(0);

					/* Change the LCD message displayed flag as FLASE to show new message for the next EVSE state */
					s_lcdMessageDisplayed = FALSE;
				}
				else if (l_CPState == CP_STATE_CHARGING_INPROGRESS) {
					/* Perform the Pre-charge safety checks and if any faults detected then change
					 * the EVSE state as Fault mode*/
					if (g_isDeviceFaulted) {
						// Wait for the LCD mutex
						if (xSemaphoreTake(xLCDMutex, portMAX_DELAY) == pdTRUE) {
							/* Print the details on the LCD module */
							lcdPrintSafetyCheckFailMessage();

							// Release the mutex
							xSemaphoreGive(xLCDMutex);
						}

						/* Sending the StatusNotification OCPP message */
						xil_printf("Sending OCPP StatusNotification messages!\r\n");

						/* Update the StatusNotification structure fields to send the message */
						g_StatusNotificationRequest.serialNumbers = &g_ProductSerialNumbers;
						if (GetGFCIFaultInfo()) {
							strcpy(g_StatusNotificationRequest.errorCode, "GroundFailure");
						}
						else if (GetRelayFaultInfo()) {
							strcpy(g_StatusNotificationRequest.errorCode, "RelayFailure");
						}
						else if (GetVolatgeInfo()) {
							strcpy(g_StatusNotificationRequest.errorCode, "VoltageFailure");
						}
						else if (GetCurrentFaultInfo()) {
							strcpy(g_StatusNotificationRequest.errorCode, "CurrentFailure");
						}
						strcpy(g_StatusNotificationRequest.status, "Faulted");
						rtcGetDateTime(g_StatusNotificationRequest.timeStamp, sizeof(g_StatusNotificationRequest.timeStamp));

						retVal = OCPPSendStatusNotificationMessage(MSG_TYPE_ONLINE,&g_StatusNotificationRequest,NULL);
						if (retVal == XST_SUCCESS) {
							xil_printf("OCPP StatusNotification message sent successfully!\r\n");
						}
						else if (retVal == XST_NO_ACCESS) {
							xil_printf("OCPP StatusNotification message saved into memory successfully!\r\n");
						}
						else {
							xil_printf("OCPP StatusNotification message sending failed!\r\n");
						}

						/* Change the EVSE state as in EVSE Fault mode */
						g_EVSECurrentState = EVSE_STATE_FAULT;

						/* Change the LCD message displayed flag as FLASE to show new message for the next EVSE state */
						s_lcdMessageDisplayed = FALSE;
					}
					/* If there is no faults detected in the Pre-charge safety checks then proceed for the charging ON
					 * process and update the user over the LCD module */
					else {
						// Wait for the LCD mutex
						if (xSemaphoreTake(xLCDMutex, portMAX_DELAY) == pdTRUE) {
							/* Print the details on the LCD module */
							lcdPrintSafetyCheckPassMessage();

							// Release the mutex
							xSemaphoreGive(xLCDMutex);
						}

					/* Change the EVSE state as in EVSE Charging Starting mode */
					g_EVSECurrentState = EVSE_STATE_CHARGING_STARTING;

					/* Set the Power ON status flag to indicate charging power is about to be enabled */
				//	SetPowerOnStatus(1);

					/* Change the LCD message displayed flag as FLASE to show new message for the next EVSE state */
					s_lcdMessageDisplayed = FALSE;
				}
			}

			break;			/* EVSE Charging Starting State */
			case EVSE_STATE_CHARGING_STARTING:
				xil_printf("EVSE Charging Starting State\r\n");

#if !SIMULATION
				l_CPState = GetCPStateInfo();
#endif
				xil_printf("Control pilot state : %s\r\n", GetCPStateInfoString(l_CPState));

				if (l_CPState == CP_STATE_VEHICLE_CONNECTED) {
					/* Change the EVSE state as in EVSE Vehicle connected mode */
					g_EVSECurrentState = EVSE_STATE_VEHICLE_CONNECTED;

					/* Change the LCD message displayed flag as FLASE to show new message for the next EVSE state */
					s_lcdMessageDisplayed = FALSE;
				}
				else if (l_CPState < CP_STATE_VEHICLE_CONNECTED) {
					// Wait for the LCD mutex
					if (xSemaphoreTake(xLCDMutex, portMAX_DELAY) == pdTRUE) {
						/* Print the details on the LCD module */
						lcdPrintEVDisconnectBeforeChargingMessage();

						// Release the mutex
						xSemaphoreGive(xLCDMutex);
					}

					/* Change the EVSE state as in User Authentication Pending mode */
					g_EVSECurrentState = EVSE_STATE_USER_AUTHENTICATION_PENDING;

					/* Reset the User Authenticated Global flag */
					g_isUserAuthenticated = FALSE;
					
					/* Write authentication failure status to PL register - session ended */
					SetAuthenticationStatus(0);

					/* Write power OFF status to PL register - session ended */
					SetPowerOnStatus(0);

					/* Change the LCD message displayed flag as FLASE to show new message for the next EVSE state */
					s_lcdMessageDisplayed = FALSE;
				}
				/* Check if the Control pilot state is still in charging in progress mode */
				else if (l_CPState == CP_STATE_CHARGING_INPROGRESS) {
					/* Wait for the Relay to get switch ON the power supply and if it is ON then send the backend
					 * OCPP StartTransaction message and update the user over the LCD module. */
					if (GetRelayStateInfo() == 1) {
						/* Get the unique transaction ID */
						if (!g_uniqueTransactionId) {
							g_uniqueTransactionId = getUniqueTransactionId();
						}

						/* Sending the StartTransaction OCPP message */
						xil_printf("Sending OCPP StartTransaction messages!\r\n");

						/* Update the StartTransaction structure fields to send the message */
						g_StartTransactionRequest.serialNumbers = &g_ProductSerialNumbers;
						strcpy(g_StartTransactionRequest.idTag, g_AuthorizeRequest.idTag);
						rtcGetDateTime(g_StartTransactionRequest.timeStamp, sizeof(g_StartTransactionRequest.timeStamp));

						g_StartTransactionRequest.transactionId = g_uniqueTransactionId;

						retVal = OCPPSendStartTransactionMessage(MSG_TYPE_ONLINE,&g_StartTransactionRequest,NULL);
						if (((retVal == XST_SUCCESS) && (strcmp(g_StartTransactionResponse.idTagInfoStatus, "Accepted") == 0)) || (retVal == XST_NO_ACCESS))
						{
							/* If message sending part success then check its response if it is accepted or not */

							/* TODO : We have to Check if the accepted status in response received from the
								* backend server is for the same transaction which we sent or not But as of now
								* Cloud side that dynamic logic is not working so disabled that check */
//								if ((g_StartTransactionResponse.transactionId == g_uniqueTransactionId) &&
//										strcmp(g_StartTransactionResponse.idTagInfoStatus, "Accepted") == 0) {

							xil_printf("OCPP StartTransaction message sent successfully!\r\n");

							// If LCD message was not displayed earlier
							if(!s_lcdMessageDisplayed) {
								// Wait for the LCD mutex
								if (xSemaphoreTake(xLCDMutex, portMAX_DELAY) == pdTRUE) {
									/* Print the details on the LCD module */
									lcdPrintEVChargingStartedMessage();

									// Release the mutex
									xSemaphoreGive(xLCDMutex);
								}
								// Set the LCD message displayed flag as true to indicate message already displayed
								s_lcdMessageDisplayed = TRUE;
							}

							/* Change the EVSE state as in EVSE Charging In Progress mode */
							g_EVSECurrentState = EVSE_STATE_CHARGING_INPROGRESS;

							/* Change the LCD message displayed flag as FLASE to show new message for the next EVSE state */
							s_lcdMessageDisplayed = FALSE;

							/* Set the EV charging in progress flag as TRUE to indicate that actual charging is going on */
							g_isEVChargingInprogress = TRUE;

							/* Store the EV charging start tick time in the static flag */
							s_EVChargingStartTime = xTaskGetTickCount();
						}
						else 
						{
							xil_printf("OCPP StartTransaction message sent successfully But got response as Rejected!\r\n");

							/* Change the EVSE state as in User Authentication Pending mode */
							g_EVSECurrentState = EVSE_STATE_USER_AUTHENTICATION_PENDING;

							/* Reset the User Authenticated Global flag */
							g_isUserAuthenticated = FALSE;
							
							/* Write authentication failure status to PL register - session ended */
							SetAuthenticationStatus(0);

							/* Write power OFF status to PL register - session ended */
							SetPowerOnStatus(0);

							/* Change the LCD message displayed flag as FLASE to show new message for the next EVSE state */
							s_lcdMessageDisplayed = FALSE;
						}
					}
				}

				break;

			/* EVSE Charging In Progress State */
			case EVSE_STATE_CHARGING_INPROGRESS:
				xil_printf("EVSE Charging In Progress State\r\n");

				s_EVChargingCurrentTime = xTaskGetTickCount();	// Get the current tick time
				s_ElapsedTimeInSeconds =
						(((s_EVChargingCurrentTime - s_EVChargingStartTime) * portTICK_PERIOD_MS) / 1000);	// Calculate the difference	in seconds

				hrs = (s_ElapsedTimeInSeconds / 3600);			// Convert the seconds into hrs
				mins = ((s_ElapsedTimeInSeconds % 3600) / 60);	// Convert the seconds into mins

				// If LCD message was not displayed earlier
				if(!s_lcdMessageDisplayed && g_isEVChargingInprogress) {
					// Wait for the LCD mutex
					if (xSemaphoreTake(xLCDMutex, portMAX_DELAY) == pdTRUE) {
						/* Print the details on the LCD module */
						lcdPrintChargingStartedMessage(hrs, mins);

						// Release the mutex
						xSemaphoreGive(xLCDMutex);
					}
					// Set the LCD message displayed flag as true to indicate message already displayed
					s_lcdMessageDisplayed = TRUE;
				}

#if !SIMULATION
				l_CPState = GetCPStateInfo();
#endif
				xil_printf("Control pilot state : %s\r\n", GetCPStateInfoString(l_CPState));

				/* Check if the Control pilot state is still in charging in progress mode */
				if ((l_CPState == CP_STATE_CHARGING_INPROGRESS) && g_isEVChargingInprogress) {
					/* Keep monitor for the device fault condition while charging is in progress */

					/* If system fault detected then we have to terminate the charging session */
					if (g_isDeviceFaulted) {
						/* Change the EVSE state as in EVSE Charging Stopping mode */
						g_EVSECurrentState = EVSE_STATE_CHARGING_STOPPING;

						/* Change the LCD message displayed flag as FLASE to show new message for the next EVSE state */
						s_lcdMessageDisplayed = FALSE;
					}

					/* If there is fault not occurred while charging in progress the update the progress to the
					 * backend server and end user over the LCD module. */

					/* Send the backend message at every 60 seconds of interval and also update the LCD module update */
					if(counter >= 60) {
						/* Sending the MeterValues Progress update OCPP message */
						xil_printf("Sending OCPP MeterValues Progress update messages!\r\n");

						/* Update the MeterValues structure fields to send the message */
						g_MeterValuesRequest.serialNumbers = &g_ProductSerialNumbers;
						g_MeterValuesRequest.transactionId = g_uniqueTransactionId;
						itoa(GetVolatgeInfo(), g_MeterValuesRequest.voltageData.value, 10);
						itoa(GetCurrentInfo(), g_MeterValuesRequest.currentData.value, 10);
						rtcGetDateTime(g_MeterValuesRequest.timeStamp, sizeof(g_MeterValuesRequest.timeStamp));

						retVal = OCPPSendMeterValuesMessage(MSG_TYPE_ONLINE,METER_VALUES_PROGRESS_UPDATE,&g_MeterValuesRequest,NULL,NULL,NULL);
						if (retVal == XST_SUCCESS) {
							xil_printf("OCPP MeterValues Progress update message sent successfully!\r\n");
						}
						else if (retVal == XST_NO_ACCESS) {
							xil_printf("OCPP MeterValues Progress update message saved into memory successfully!\r\n");
						}
						else {
							xil_printf("OCPP MeterValues Progress update message sending failed!\r\n");
						}

						// Wait for the LCD mutex
						if (xSemaphoreTake(xLCDMutex, portMAX_DELAY) == pdTRUE) {
							/* Print the details on the LCD module */
							lcdPrintChargingProgressMessage(GetVolatgeInfo(), GetCurrentInfo(), hrs, mins);

							// Release the mutex
							xSemaphoreGive(xLCDMutex);
						}

						/* Reset the local counter */
						counter = 0;
					}
					else if(counter >=30) { //30
						// Wait for the LCD mutex
						if (xSemaphoreTake(xLCDMutex, portMAX_DELAY) == pdTRUE) {
							/* Print the details on the LCD module */
							lcdPrintChargingProgressMessage(GetVolatgeInfo(), GetCurrentInfo(), hrs, mins);

							// Release the mutex
							xSemaphoreGive(xLCDMutex);
						}
					}
					counter += 2;
				}
				/* Check if the Control pilot state is change to the EVSE Idle mode or Vehicle Connected mode */
				else if (((l_CPState == CP_STATE_EVSE_IDLE) || (l_CPState == CP_STATE_VEHICLE_CONNECTED)) && g_isEVChargingInprogress) {
					/* If this condition triggers that means either EV is fully
					 * charged or it is disconnected in between the charging session */

					/* IMMEDIATELY disable authentication and power to prevent relay re-activation */
					SetAuthenticationStatus(0);
					SetPowerOnStatus(0);

					/* Change the EVSE state as in EVSE Charging Stopping mode */
					g_EVSECurrentState = EVSE_STATE_CHARGING_STOPPING;

					/* Change the LCD message displayed flag as FLASE to show new message for the next EVSE state */
					s_lcdMessageDisplayed = FALSE;
				}
#if SIMULATION
				if (s_ElapsedTimeInSeconds > 200) {
					l_CPState = CP_STATE_EVSE_IDLE;
				}
#endif

				break;

			/* EVSE Charging Stopping State */
			case EVSE_STATE_CHARGING_STOPPING:
				xil_printf("EVSE Charging Stopping State\r\n");

				// If LCD message was not displayed earlier
				if(!s_lcdMessageDisplayed && g_isEVChargingInprogress && !g_isDeviceFaulted) {
					// Wait for the LCD mutex
					if (xSemaphoreTake(xLCDMutex, portMAX_DELAY) == pdTRUE) {
						/* Print the details on the LCD module */
						lcdPrintEVUnpluggedMessage();

						// Release the mutex
						xSemaphoreGive(xLCDMutex);
					}
					// Set the LCD message displayed flag as true to indicate message already displayed
					s_lcdMessageDisplayed = TRUE;
				}
				else if(!s_lcdMessageDisplayed && g_isEVChargingInprogress && g_isDeviceFaulted) {
					// Wait for the LCD mutex
					if (xSemaphoreTake(xLCDMutex, portMAX_DELAY) == pdTRUE) {
						/* Print the details on the LCD module */
						lcdPrintChargingInprogressFaultDetectedMessage();

						// Release the mutex
						xSemaphoreGive(xLCDMutex);
					}
					// Set the LCD message displayed flag as true to indicate message already displayed
					s_lcdMessageDisplayed = TRUE;
				}

#if !SIMULATION
				l_CPState = GetCPStateInfo();
#endif
				xil_printf("Control pilot state : %s\r\n", GetCPStateInfoString(l_CPState));

				/* Check if the Control pilot state is into the EVSE Idle mode or Vehicle Connected mode */
				if (((l_CPState == CP_STATE_EVSE_IDLE) || (l_CPState == CP_STATE_VEHICLE_CONNECTED)) && g_isEVChargingInprogress) {
					/* Wait for the Relay to get switch OFF the power supply and if it is OFF then send the backend
					 * OCPP StopTransaction message. */
#if  !SIMULATION
					if (GetRelayStateInfo() == 0) {
#endif
						/* Ensure authentication and power are OFF before finalizing session */
						SetAuthenticationStatus(0);
						SetPowerOnStatus(0);

						/* Sending the StopTransaction OCPP message */
						xil_printf("Sending OCPP StopTransaction messages!\r\n");

							/* Update the StopTransaction structure fields to send the message */
							g_StopTransactionRequest.serialNumbers = &g_ProductSerialNumbers;
							strcpy(g_StopTransactionRequest.idTag, g_AuthorizeRequest.idTag);
							rtcGetDateTime(g_StopTransactionRequest.timeStamp, sizeof(g_StopTransactionRequest.timeStamp));

						g_StopTransactionRequest.transactionId = g_uniqueTransactionId;

						if (g_isDeviceFaulted) {
							strcpy(g_StopTransactionRequest.reason, "DeviceFaulted");
						}
						else {
							strcpy(g_StopTransactionRequest.reason, "EVDisconnected");
						}

						retVal = OCPPSendStopTransactionMessage(MSG_TYPE_ONLINE,&g_StopTransactionRequest,NULL);
						if (((retVal == XST_SUCCESS) && (strcmp(g_StopTransactionResponse.status, "Accepted") == 0)) || (retVal == XST_NO_ACCESS))
						{
							/* If message sending part success then check its response if it is accepted or not */
							xil_printf("OCPP StopTransaction message sent successfully!\r\n");

							/* Change the EVSE state as in EVSE Charging Completed mode */
							g_EVSECurrentState = EVSE_STATE_CHARGING_COMPLETED;

							/* Change the LCD message displayed flag as FLASE to show new message for the next EVSE state */
							s_lcdMessageDisplayed = FALSE;

							/* Set the EV charging in progress flag as FALSE to indicate that actual charging is going off */
							g_isEVChargingInprogress = FALSE;

							/* Store the EV charging stop tick time in the static flag */
							s_EVChargingStopTime = xTaskGetTickCount();
						}
						else 
						{
							xil_printf("OCPP StopTransaction message sending failed!\r\n");
						}
#if  !SIMULATION
					}
#endif
				}

				break;

			/* EVSE Charging Completed State */
			case EVSE_STATE_CHARGING_COMPLETED:
				xil_printf("EVSE Charging Completed State\r\n");

				s_ElapsedTimeInSeconds =
						(((s_EVChargingStopTime - s_EVChargingStartTime) * portTICK_PERIOD_MS) / 1000);	// Calculate the difference	in seconds

				hrs = (s_ElapsedTimeInSeconds / 3600);			// Convert the seconds into hrs
				mins = ((s_ElapsedTimeInSeconds % 3600) / 60);	// Convert the seconds into mins

#if !SIMULATION
				l_CPState = GetCPStateInfo();
#endif
				xil_printf("Control pilot state : %s\r\n", GetCPStateInfoString(l_CPState));

				/* Check if the Control pilot state is into the EVSE Idle mode and device is in fault mode */
				if (((l_CPState == CP_STATE_EVSE_IDLE) || (l_CPState == CP_STATE_VEHICLE_CONNECTED)) && g_isDeviceFaulted) {
					/* Sending the StatusNotification OCPP message */
					xil_printf("Sending OCPP StatusNotification messages!\r\n");

					/* Update the StatusNotification structure fields to send the message */
					g_StatusNotificationRequest.serialNumbers = &g_ProductSerialNumbers;
					if (GetGFCIFaultInfo()) {
						strcpy(g_StatusNotificationRequest.errorCode, "GroundFailure");
					}
					else if (GetRelayFaultInfo()) {
						strcpy(g_StatusNotificationRequest.errorCode, "RelayFailure");
					}
					else if (GetVolatgeInfo()) {
						strcpy(g_StatusNotificationRequest.errorCode, "VoltageFailure");
					}
					else if (GetCurrentFaultInfo()) {
						strcpy(g_StatusNotificationRequest.errorCode, "CurrentFailure");
					}
					strcpy(g_StatusNotificationRequest.status, "Faulted");
					rtcGetDateTime(g_StatusNotificationRequest.timeStamp, sizeof(g_StatusNotificationRequest.timeStamp));

					retVal = OCPPSendStatusNotificationMessage(MSG_TYPE_ONLINE,&g_StatusNotificationRequest,NULL);
					if (retVal == XST_SUCCESS) {
						xil_printf("OCPP StatusNotification message sent successfully!\r\n");
					}
					else if (retVal == XST_NO_ACCESS) {
						xil_printf("OCPP StatusNotification message saved into memory successfully!\r\n");
					}
					else {
						xil_printf("OCPP StatusNotification message sending failed!\r\n");
					}

					/* Change the EVSE state as in EVSE Fault mode */
					g_EVSECurrentState = EVSE_STATE_FAULT;

					/* Change the LCD message displayed flag as FLASE to show new message for the next EVSE state */
					s_lcdMessageDisplayed = FALSE;
				}
				/* If the Control pilot state is into the EVSE Idle mode and device is not in fault mode */
				else if (((l_CPState == CP_STATE_EVSE_IDLE) || (l_CPState == CP_STATE_VEHICLE_CONNECTED)) && !g_isDeviceFaulted) {
					/* Sending the MeterValues Summary update OCPP message */
					xil_printf("Sending OCPP MeterValues Summary update messages!\r\n");

					/* Update the MeterValues structure fields to send the message */
					g_MeterValuesRequest.serialNumbers = &g_ProductSerialNumbers;
					g_MeterValuesRequest.transactionId = g_uniqueTransactionId;
					rtcGetDateTime(g_MeterValuesRequest.timeStamp, sizeof(g_MeterValuesRequest.timeStamp));

					retVal = OCPPSendMeterValuesMessage(MSG_TYPE_ONLINE,METER_VALUES_SUMMARY_UPDATE,&g_MeterValuesRequest,NULL,NULL,NULL);
					if (retVal == XST_SUCCESS) {
						xil_printf("OCPP MeterValues Summary update message sent successfully!\r\n");
					}
					else if (retVal == XST_NO_ACCESS) {
						xil_printf("OCPP MeterValues Progress update message saved into memory successfully!\r\n");
					}
					else {
						xil_printf("OCPP MeterValues Summary update message sending failed!\r\n");
					}
					/* Sending the StatusNotification OCPP message */
					xil_printf("Sending OCPP StatusNotification messages!\r\n");

					/* Update the StatusNotification structure fields to send the message */
					g_StatusNotificationRequest.serialNumbers = &g_ProductSerialNumbers;
					strcpy(g_StatusNotificationRequest.errorCode, "NoError");
					strcpy(g_StatusNotificationRequest.status, "Available");
					rtcGetDateTime(g_StatusNotificationRequest.timeStamp, sizeof(g_StatusNotificationRequest.timeStamp));

					retVal = OCPPSendStatusNotificationMessage(MSG_TYPE_ONLINE,&g_StatusNotificationRequest,NULL);
					if (retVal == XST_SUCCESS) {
						xil_printf("OCPP StatusNotification message sent successfully!\r\n");
					}
					else if (retVal == XST_NO_ACCESS) {
						xil_printf("OCPP StatusNotification message saved into memory successfully!\r\n");
					}
					else {
						xil_printf("OCPP StatusNotification message sending failed!\r\n");
					}

					// Wait for the LCD mutex
					if (xSemaphoreTake(xLCDMutex, portMAX_DELAY) == pdTRUE) {
						/* Print the details on the LCD module */
						lcdPrintChargingSessionSummaryMessage(hrs, mins);

						/* Print the details on the LCD module */
						lcdPrintSessionRestartMessage();

						// Release the mutex
						xSemaphoreGive(xLCDMutex);
					}

					/* Change the EVSE state as in User Authentication Pending mode */
					g_EVSECurrentState = EVSE_STATE_USER_AUTHENTICATION_PENDING;

					/* Reset the User Authenticated Global flag */
					g_isUserAuthenticated = FALSE;
					
					/* Write authentication failure status to PL register - session ended */
					SetAuthenticationStatus(0);

					/* Write power OFF status to PL register - session ended */
					SetPowerOnStatus(0);

					/* Change the LCD message displayed flag as FLASE to show new message for the next EVSE state */
					s_lcdMessageDisplayed = FALSE;
#if SIMULATION
					l_CPState = 0;
#endif
				}

				break;

			/* EVSE Fault State */
			case EVSE_STATE_FAULT:
				xil_printf("EVSE Fault State\r\n");

				// If LCD message was not displayed earlier
				if(!s_lcdMessageDisplayed && GetGFCIFaultInfo()) {
					// Wait for the LCD mutex
					if (xSemaphoreTake(xLCDMutex, portMAX_DELAY) == pdTRUE) {
						/* Print the details on the LCD module */
						lcdPrintFaultChargingInterruptedMessage();

						// Release the mutex
						xSemaphoreGive(xLCDMutex);
					}
					// Set the LCD message displayed flag as true to indicate message already displayed
					s_lcdMessageDisplayed = TRUE;
				}
				else if(!s_lcdMessageDisplayed) {
					// Wait for the LCD mutex
					if (xSemaphoreTake(xLCDMutex, portMAX_DELAY) == pdTRUE) {
						/* Print the details on the LCD module */
						lcdPrintFaultMessage();

						// Release the mutex
						xSemaphoreGive(xLCDMutex);
					}
					// Set the LCD message displayed flag as true to indicate message already displayed
					s_lcdMessageDisplayed = TRUE;
				}

				/* Validate if the all Raised Faults resolved then restart the charging session */
				if(!g_isDeviceFaulted) {
					/* Change the EVSE state as in User Authentication Pending mode */
					g_EVSECurrentState = EVSE_STATE_USER_AUTHENTICATION_PENDING;

					/* Reset the User Authenticated Global flag */
					g_isUserAuthenticated = FALSE;
					
					/* Write authentication failure status to PL register - session ended */
					SetAuthenticationStatus(0);

					/* Write power OFF status to PL register - session ended */
					SetPowerOnStatus(0);

					/* Change the LCD message displayed flag as FLASE to show new message for the next EVSE state */
					s_lcdMessageDisplayed = FALSE;
				}

				break;

			/* EVSE Maintenance State */
			case EVSE_STATE_MAINTENANCE:
				xil_printf("EVSE Maintenance State\r\n");

				/* If the maintainer mode entered then start maintenance session and perform the system diagnostics */
				if(g_isMaintainerAuthenticated)
				{
					if (!s_diagnosticPerformed) {
						/* Perform the Relay Trouble shooting operation */
						Maintenance_CheckRelayStatus();
						/* Perform the Voltage Trouble shooting operation */
						Maintenance_CheckVoltageStatus();
						/* Perform the Current Trouble shooting operation */
						Maintenance_CheckCurrentStatus();
						/* Perform the GFCI Trouble shooting operation */
						Maintenance_CheckGFCIStatus();
						/* Perform the Network Trouble shooting operation */
						Maintenance_CheckNetworkStatus();

						/* Once system trobleshooting and diagnostics completed then update the summary to user over LCD module */
						xil_printf("g_maintenanceStatus.relay = %d\r\n"
								"g_maintenanceStatus.voltage = %d\r\n"
								"g_maintenanceStatus.current = %d\r\n"
								"g_maintenanceStatus.gfci = %d\r\n"
								"g_maintenanceStatus.network = %d\r\n",
								g_maintenanceStatus.relay, g_maintenanceStatus.voltage,g_maintenanceStatus.current,
								g_maintenanceStatus.gfci, g_maintenanceStatus.network);

						// Wait for the LCD mutex
						if (xSemaphoreTake(xLCDMutex, portMAX_DELAY) == pdTRUE) {
							/* Print the details on the LCD module */
							lcdPrintDiagnosticsSummaryMessage();

							// Release the mutex
							xSemaphoreGive(xLCDMutex);
						}

						/* Once diagnostic completed and updated to the user set the diagnostic performed flag as true */
						s_diagnosticPerformed = TRUE;

						/* Now during the system diagnostic if any issues detected in any of the interfaces then we have to reboot the system
						 * For that:
						 * 		If there is any issue with the Network interface then we are just restarting the PS only
						 *		If there are any issues with the Relay / ADC sensors then we are restarting the PS + PL both as those are handling by the PL side
						 */

						/* Doing Soft reset for software issue on Maintenance testing */
						if (g_maintenanceStatus.network){
							xil_printf("software reseting...!\r\n");
							// Wait for the LCD mutex
							if (xSemaphoreTake(xLCDMutex, portMAX_DELAY) == pdTRUE) {
								/* Print the details on the LCD module */
								lcdPrintSystemResetMessage();

								// Release the mutex
								xSemaphoreGive(xLCDMutex);
							}
							/* Perform the EVSE System soft reset */
							EVSE_PerformSoftReset();
						}

						/* Doing Hard reset for hardware issue on Maintenance testing */
						if (g_maintenanceStatus.relay || g_maintenanceStatus.voltage || g_maintenanceStatus.current || g_maintenanceStatus.gfci) {
							xil_printf("Hardware reseting...!\r\n");
							// Wait for the LCD mutex
							if (xSemaphoreTake(xLCDMutex, portMAX_DELAY) == pdTRUE) {
								/* Print the details on the LCD module */
								lcdPrintSystemResetMessage();

								// Release the mutex
								xSemaphoreGive(xLCDMutex);
							}
							/* Perform the EVSE System hard reset */
							EVSE_PerformHardReset();
						}
					}
					else {
						xil_printf("Maintenance diagnostic already performed...!\r\n");
					}
				}
				/* If the maintenance session completed and maintainer mode exited then restart the charging session */
				else {
					/* Change the EVSE state as in User Authentication Pending mode */
					g_EVSECurrentState = EVSE_STATE_USER_AUTHENTICATION_PENDING;

					/* Reset the User Authenticated Global flag */
					g_isUserAuthenticated = FALSE;
					
					/* Write authentication failure status to PL register - session ended */
					SetAuthenticationStatus(0);

					/* Write power OFF status to PL register - session ended */
					SetPowerOnStatus(0);

					/* Change the LCD message displayed flag as FLASE to show new message for the next EVSE state */
					s_lcdMessageDisplayed = FALSE;

					/* Change the diagnostic performed flag as FLASE to perform it once for the diagnostic session */
					s_diagnosticPerformed = FALSE;
				}

				break;

			/* Default State */
			default:
				xil_printf("Default State\r\n");

				break;
		}

		/* Delay for 2 second. */
		vTaskDelay( x2second );
	}
}

/**
 * @brief  Callback function for the session timer.
 *
 * This function is executed when the session timer expires. It handles session-related
 * timeouts, such as ending an inactive charging session or triggering specific
 * session-related actions based on the elapsed time.
 *
 * @param  xTimer Handle to the timer that triggered the callback.
 * @retval None
 */
static void vSessionTimerCallback(TimerHandle_t xTimer)
{
	xil_printf("\t\t\t!!!   %s   !!!\t\t\t\r\n", __func__);

	/* Local Variable declaration and initialization */
	CPState_e l_CPState = 0;

	/* Get the Control Pilot state to confirm either EV is connected with EVSE or not */
	l_CPState = GetCPStateInfo();
	xil_printf("Control pilot state : %s\r\n", GetCPStateInfoString(l_CPState));

	/* If Vehicle is yet not detected until the session timeout triggered then Restart the session */
	if (l_CPState < CP_STATE_VEHICLE_CONNECTED) {
		/* Change the EVSE state as in User Authentication Pending mode */
		g_EVSECurrentState = EVSE_STATE_USER_AUTHENTICATION_PENDING;

		/* Reset the User Authenticated Global flag */
		g_isUserAuthenticated = FALSE;
		
		/* Write authentication failure status to PL register - session timeout */
		SetAuthenticationStatus(0);

		/* Write power OFF status to PL register - session timeout */
		SetPowerOnStatus(0);

		/* Change the LCD message displayed flag as FLASE to show new message for the next EVSE state */
		s_lcdMessageDisplayed = FALSE;

		xil_printf("Updated the EVSE state to the User Authentication pending state!\r\n");

		// Wait for the LCD mutex
		if (xSemaphoreTake(xLCDMutex, portMAX_DELAY) == pdTRUE) {
			/* Print the details on the LCD module */
			lcdPrintSessionTimeoutMessage();
			xil_printf("Session Timeout Triggered and Restarting the session!\r\n");

			// Release the mutex
			xSemaphoreGive(xLCDMutex);
		}
	}
}

/**
 * @brief  Retrieves the product serial numbers.
 *
 * This function obtains the serial numbers of the product, which may be
 * used for identification, logging, or communication with backend systems.
 * It ensures that the serial number is properly retrieved and formatted.
 *
 * @param  None
 * @retval XST_SUCCESS   Indicates successful retrieval of the product serial numbers.
 * @retval XST_FAILURE   Indicates failure in retrieving the serial numbers.
 */
static u8 getProductSerialNumbers(void)
{
	u8 retVal = XST_SUCCESS;
	u8 loopCnt = 0;
	char l_ResponseBuffer[BUFFER_SIZE] = {0};
	char l_SendBuffer[20] = "AT+CIPSTAMAC?";

	/* Fill the EVSE Serial Number */
	/* TODO: As of now setting this parameter as NA but later set it using the actuals data */
	strcpy(g_ProductSerialNumbers.EVSESerialNumber, "NA");

	/* Fill the FPGA ID Information */
	/* TODO: As of now setting this parameter as NA but later set it using the actuals data */
	strcpy(g_ProductSerialNumbers.FpgaID, "NA");

	/* Fill the ESP32 module MAC Address */
	memset(l_ResponseBuffer, 0, sizeof(l_ResponseBuffer));

	// Wait for the AT mutex
	if (xSemaphoreTake(xATMutex, portMAX_DELAY) == pdTRUE) {
		/* Send AT Command to get the ESP32 module MAC address */
		retVal = SendUartATCommandWithResponse(l_SendBuffer, 60, 300, l_ResponseBuffer, sizeof(l_ResponseBuffer));
		if(retVal == XST_SUCCESS) {
			if(strstr(l_ResponseBuffer, "OK") != NULL) {
				/* Extract the ESP32 MAC address from the response buffer */
				const char *macStartStr = strchr(l_ResponseBuffer, '"');		// To Find first quote
				macStartStr++;													// Move past the quote

				while (*macStartStr && *macStartStr != '"') { 					// Loop until the closing quote
					if (*macStartStr != ':') { 									// Ignore colons
						g_ProductSerialNumbers.Esp32MACAddress[loopCnt++] = toupper(*macStartStr);
					}
					macStartStr++;
				}
				/* Terminate the ESP32 MAC address string with the NULL character */
				g_ProductSerialNumbers.Esp32MACAddress[loopCnt] = '\0';

				xil_printf("g_ProductSerialNumbers.Esp32MACAddress : %s\r\n\n", g_ProductSerialNumbers.Esp32MACAddress);
			}
			else {
				xil_printf("Invalid Response of %s command : %s\r\n", l_SendBuffer, l_ResponseBuffer);
			}
		}
		// Release the mutex
		xSemaphoreGive(xATMutex);
	}

	/* Fill the Ethernet module MAC Address */
	/* TODO: As of now setting this parameter as NA but later set it using the actuals data */
	strcpy(g_ProductSerialNumbers.EthMACAddress, "NA");

	/* Fill the EC200 NIC MAC Address */
	/* TODO: As of now setting this parameter as NA but later set it using the actuals data */
	strcpy(g_ProductSerialNumbers.EC200MACAddress, "NA");

	return retVal;
}

/**
 * @brief  Initializes the system peripherals.
 *
 * This function initializes the necessary peripherals for the system, ensuring that all required
 * hardware components are configured and ready for operation. This may include setting up communication
 * interfaces, sensors, or other devices that the system depends on.
 *
 * @param  None
 * @retval XST_SUCCESS     Indicates successful initialization of the peripherals.
 * @retval XST_FAILURE     Indicates failure during peripheral initialization.
 */
static u8 initPeripherals(void)
{
	u8 retVal = XST_SUCCESS;

	/* Initialize the I2C control module */
	retVal = iic_init(IIC_DEVICE_ID);
	if(retVal != XST_SUCCESS) {
		xil_printf("IIC Initialization Failed!\r\n");
		return retVal;
	}

	/* Initialize the I2C0 control module for RTC */
	retVal = iic_init(IIC0_DEVICE_ID);
	if(retVal != XST_SUCCESS) {
		xil_printf("IIC0 Initialization Failed!\r\n");
		return retVal;
	}

	/* Initialize the LCD control module */
	lcd_init();
//	retVal = lcd_init();
//	if(retVal != XST_SUCCESS) {
//		xil_printf("LCD Initialization Failed!\r\n");
//		return retVal;
//	}

	/* Initialize the RTC module */
	retVal = rtcInit();
	if (retVal != XST_SUCCESS) {
		xil_printf("RTC Initialization Failed!\r\n");
		return retVal;
	}

	/* Initialize the NFC/RFID control module */
	retVal = nfc_init();
	if(retVal != XST_SUCCESS) {
		xil_printf("RFID/NFC Initialization Failed!\r\n");
		return retVal;
	}

	/* Initialize the ESP control module */
	retVal = InitESP();
	if(retVal != XST_SUCCESS) {
		xil_printf("ESP Initialization Failed!\r\n");
		return retVal;
	}
	else {
		SendUartATCommand("ATE0", 10, 300);
	}

	retVal = initLed(&ledGpioModule, XPAR_AXI_GPIO_0_DEVICE_ID);
	if (retVal != LED_STATUS_SUCCESS) {
		xil_printf("LED Initialization Failed!\r\n");
		return retVal;
	}

	/* Initialize the CP control module */
	retVal = InitCP();
	if(retVal != XST_SUCCESS) {
		xil_printf("Control Pilot Initialization Failed!\r\n");
		return retVal;
	}

	retVal = InitOCPP();
	if (retVal != XST_SUCCESS) {
		xil_printf("OCPP Initialization Failed!\r\n");
		return retVal;
	}

	retVal = qspiInit(&g_qspiInstance, XPAR_PS7_QSPI_0_DEVICE_ID);
	if (retVal != XST_SUCCESS) {
		xil_printf("QSPI Initialization Failed!\r\n");
		return retVal;
	}

	/* Initialize LVGL */
	lvgl_setup();

	return retVal;
}

/**
 * @brief Creates mutex locks for resource synchronization.
 *
 * This function initializes and creates mutex locks required for synchronizing
 * access to shared resources in the system. It ensures thread-safe operations
 * in a multi-threaded environment.
 *
 * @return XST_SUCCESS  If the mutex locks were created successfully.
 * @return XST_FAILURE  If there was an error creating the mutex locks.
 */
static u8 createMutexLocks(void)
{
	// Create the AT mutex
	xATMutex = xSemaphoreCreateMutex();
	if (xATMutex == NULL) {
		xil_printf("Failed to create the AT mutex lock!\r\n");
		return XST_FAILURE;
	}

	// Create the LCD mutex
	xLCDMutex = xSemaphoreCreateMutex();
	if (xLCDMutex == NULL) {
		xil_printf("Failed to create the LCD mutex lock!\r\n");
		return XST_FAILURE;
	}

	// Create the RFID mutex
	xRFIDMutex = xSemaphoreCreateMutex();
	if (xRFIDMutex == NULL) {
		xil_printf("Failed to create the RFID mutex lock!\r\n");
		return XST_FAILURE;
	}

	// Create the QSPI mutex
	xQSPIMutex = xSemaphoreCreateMutex();
	if (xQSPIMutex == NULL) {
		xil_printf("Failed to create the QSPI mutex lock!\r\n");
		return XST_FAILURE;
	}

	return XST_SUCCESS;
}

/**
 * @brief  Creates and initializes system timers.
 *
 * This function sets up the necessary timers required for system operations.
 * It initializes the timers, assigns their callback functions, and ensures
 * they are ready for use. The timers may be used for various periodic tasks
 * such as monitoring, timeouts, or scheduled operations.
 *
 * @param  None
 * @retval XST_SUCCESS   Indicates successful creation of timers.
 * @retval XST_FAILURE   Indicates failure in creating one or more timers.
 */
static u8 createTimers(void)
{
	// Create the Session Timer as one shot timer
	xSessionTimer = xTimerCreate("SessionTimer", SESSION_TIMER_PERIOD, pdFALSE, (void *)SESSION_TIMER_ID, vSessionTimerCallback);
	if (xSessionTimer == NULL) {
		xil_printf("Failed to create Session Timer!\r\n");
		return XST_FAILURE;
	}

	return XST_SUCCESS;
}

/**
 * @brief  Creates the tasks for the EVSE (Electric Vehicle Supply Equipment) system.
 *
 * This function initializes and creates the tasks required for managing the EVSE system. It sets up
 * various tasks such as handling charging, monitoring system health, and managing communication with
 * connected devices. The tasks are scheduled to run concurrently to ensure the proper operation of the system.
 *
 * @param  None
 * @retval None
 */
static void createEvseSystemTasks(void)
{
	xil_printf("Came to create the EVSE charger required tasks!\r\n");

	xTaskCreate(prvSystemHealthCheckupTask, 		/* The function that implements the task. */
			(const char *) "SystemHealthCheckup", 	/* Text name for the task, provided to assist debugging only. */
			2048, 									/* The stack allocated to the task. (Was: 2048 = 8 KB, Now: 2048 = 8 KB - No change, already sufficient) */
			NULL, 									/* The task parameter is not used, so set to NULL. */
			tskIDLE_PRIORITY + 1,					/* The task runs at the idle priority. */
			&xSystemHealthCheckupTask);

	xTaskCreate(prvRFIDTask, 						/* The function that implements the task. */
			(const char *) "RFIDModule", 			/* Text name for the task, provided to assist debugging only. */
			2048, 									/* The stack allocated to the task. (Was: 2048 = 8 KB, Now: 2048 = 8 KB - No change, already sufficient) */
			NULL, 									/* The task parameter is not used, so set to NULL. */
			tskIDLE_PRIORITY + 3,					/* The task runs at the idle priority. */
			&xRFIDTask);

	// Comment this to disable WiFi/ESP task - disables OTA and WiFi automatically

	xTaskCreate(prvESPTask,				 			/* The function that implements the task. */
			(const char *) "ESP", 					/* Text name for the task, provided to assist debugging only. */
			//2048, 								/* OLD: 2048 words = 8 KB */
			3072, 									/* NEW: 3072 words = 12 KB (+4 KB for WiFi TCP/IP buffers) */
			NULL, 									/* The task parameter is not used, so set to NULL. */
			tskIDLE_PRIORITY + 3,					/* The task runs at the idle priority. */
			&xESPTask);


	xTaskCreate(prvOCPPTask,				 		/* The function that implements the task. */
			(const char *) "OCPP", 					/* Text name for the task, provided to assist debugging only. */
			//1024, 								/* OLD: 1024 words = 4 KB */
			2048, 									/* NEW: 2048 words = 8 KB (+4 KB for JSON parsing and WebSocket) */
			NULL, 									/* The task parameter is not used, so set to NULL. */
			tskIDLE_PRIORITY + 4,					/* The task runs at the idle priority. */
			&xOCPPTask);

	xTaskCreate(prvCPTask,				 			/* The function that implements the task. */
			(const char *) "CP", 					/* Text name for the task, provided to assist debugging only. */
			//512, 									/* OLD: 512 words = 2 KB */
			1024, 									/* NEW: 1024 words = 4 KB (+2 KB for GPIO reads and printf debugging) */
			NULL, 									/* The task parameter is not used, so set to NULL. */
			tskIDLE_PRIORITY + 5,					/* The task runs at the idle priority. */
			&xCPTask);

	xTaskCreate(prvEvseMainAppTask,				 	/* The function that implements the task. */
			(const char *) "EvseMainApp", 			/* Text name for the task, provided to assist debugging only. */
			//3072, 								/* OLD: 3072 words = 12 KB */
			4096, 									/* NEW: 4096 words = 16 KB (+4 KB for state machine, OCPP messages, and LCD operations) */
			NULL, 									/* The task parameter is not used, so set to NULL. */
			tskIDLE_PRIORITY + 7,					/* The task runs at the idle priority. */
			&xEvseMainAppTask);

	xTaskCreate(prvOfflineControlTask,				/* The function that implements the task. */
			(const char *) "OfflineControlTask", 	/* Text name for the task, provided to assist debugging only. */
			4096, 									/* The stack allocated to the task. (Was: 4096 = 16 KB, Now: 4096 = 16 KB - No change, already sufficient) */
			NULL, 									/* The task parameter is not used, so set to NULL. */
			tskIDLE_PRIORITY + 2,					/* The task runs at the idle priority. */
			&xOfflineControlTask);

	xTaskCreate(lvgl_task,							/* The function that implements the task. */
			(const char *) "LVGL", 					/* Text name for the task, provided to assist debugging only. */
			2048, 									/* The stack allocated to the task. */
			NULL, 									/* The task parameter is not used, so set to NULL. */
			tskIDLE_PRIORITY + 1,					/* The task runs at the idle priority. */
			NULL);
}

/**
 * @brief  Initializes fatfs file system on QSPI flash.
 *
 * This function mounts fatfs file system on external qspi flash,and if
 * file system doesnt exist it formats flash and create file system on
 * it.
 *
 * @param  None
 * @retval XST_SUCCESS   Indicates successful init of file system.
 * @retval XST_FAILURE   Indicates failure in init of file system.
 */
static u8 initFatFs(void)
{
	u8 retVal = XST_FAILURE;

	FRESULT ret = f_mount(&g_FatFs, "0:/", 1);
	/* Try to mount fatfs file system on QSPI flash */
	if (ret == FR_OK)
	{
		xil_printf("Mounted fatfs on flash!\r\n");
		retVal = XST_SUCCESS;
	}
	else /* mount fails */
	{
		xil_printf("Fatfs mount on flash failed!\r\n");
		xil_printf("Creating file system on flash.\r\n");

		static uint8_t buffer[FF_MAX_SS];					/* a work buffer for the f_mkfs() */

		/* Create file system on flash */
		ret = f_mkfs("0:/",NULL ,buffer, sizeof(buffer));
		if(ret != FR_OK)
		{
			xil_printf("Creating file system on flash failed, error %d\r\n",ret);
		}
		else
		{
			if (f_mount(&g_FatFs, "0:/", 1) == FR_OK)
			{
				retVal = XST_SUCCESS;
				xil_printf("Mounted fatfs on flash successfully, after file system creation!\r\n");
			}
			else /* mount fails */
			{
				xil_printf("Mounting fatfs on flash after file system creation failed!\r\n");
			}
		}
	}

	return retVal;
}

/*
 * =====================================================================================
 * Global Function Definitions
 * =====================================================================================
 */

/**
 * @brief  Main entry point of the program.
 *
 * This function serves as the main entry point for the program execution. It initializes necessary
 * peripherals, sets up required system configurations, and manages the primary control flow of the application.
 * The main loop runs the core logic of the system, handling tasks such as communication, data processing,
 * and state management.
 *
 * @param  None
 * @retval 0                Indicates successful program execution.
 */
int main(void)
{
	/* EVSE Main application flow starts from here */
	xil_printf( "\r\nFreertos main APP Started!\r\n" );

	xil_printf( "\r\nAPP_VERSION_INFO : %s\r\n\r\n", APP_VERSION_INFO);

	u8 retVal;
	u8 flagsInfo[5] = {0};

	/* Initialize all the required peripherals */
	retVal = initPeripherals();
	if(retVal != XST_SUCCESS) {
		xil_printf("Peripherals Initialization Failed!\r\n");

		/* Perform the EVSE System soft reset */
		EVSE_PerformSoftReset();
	}

	/* Create all the required Mutex locks */
	retVal = createMutexLocks();
	if(retVal != XST_SUCCESS) {
		xil_printf("Required Mutex Locks creation Failed!\r\n");

		/* Perform the EVSE System soft reset */
		EVSE_PerformSoftReset();
	}

	/* Create all the required Timers */
	retVal = createTimers();
	if(retVal != XST_SUCCESS) {
		xil_printf("Required Timers creation Failed!\r\n");

		/* Perform the EVSE System soft reset */
		EVSE_PerformSoftReset();
	}

	/* Mount fatfs file system on external qspi flash */
	retVal = initFatFs();
	if(retVal != XST_SUCCESS) {
		xil_printf("Fat file system Initialization Failed!\r\n");

		/* Perform the EVSE System soft reset */
		EVSE_PerformSoftReset();
	}

	/* Print Welcome greetings on the LCD */
	lcdPrintGreetingsMessage();

	/* Perform the System Self Test functionality and update the status on the LCD */
	retVal = checkSystemHealth();
	if(retVal != XST_SUCCESS) {
		xil_printf("System Self Test Failed!\r\n\n");
		lcdPrintSystemSelfTestFailMessage();

		/* Perform the EVSE System hard reset */
		EVSE_PerformHardReset();
	}
	else {
		xil_printf("System Self Test Passed!\r\n\n");
		lcdPrintSystemSelfTestPassMessage();
	}

	/* After the OTA if Application and Bit stream successfully
	 * running then update the previous flags with the current flag
	 */

	// Wait for the QSPI mutex
	if (xSemaphoreTake(xQSPIMutex, portMAX_DELAY) == pdTRUE) {
		/* Configure QSPI options including Quad IO Mode */
		XQspiPs_SetClkPrescaler(&g_qspiInstance, XQSPIPS_CLK_PRESCALE_8);

		/* Read the flags information from the QSPI storage */
		retVal = qspiRead(&g_qspiInstance, (u8 *)&flagsInfo, QSPI_ADDR_STATUS_FLAGS, 5);
		if (retVal == XST_SUCCESS) {
			xil_printf("PB : %d\r\nPA : %d\r\nCB : %d\r\nCA : %d\r\nRETRY : %d\r\n",
					flagsInfo[0], flagsInfo[1], flagsInfo[2], flagsInfo[3], flagsInfo[4]);

			/* If the Previous flags is not same as the current flags then either OTA or fallback is happened */
			if ((flagsInfo[0] != flagsInfo[2]) || (flagsInfo[1] != flagsInfo[3])) {
				/* Change the PB with the CB */
				flagsInfo[0] = flagsInfo[2];
				/* Change the PA with the CA */
				flagsInfo[1] = flagsInfo[3];
				/* Set the Retry with 0 */
				flagsInfo[4] = 0;

				xil_printf("PB : %d\r\nPA : %d\r\nCB : %d\r\nCA : %d\r\nRETRY : %d\r\n",
						flagsInfo[0], flagsInfo[1], flagsInfo[2], flagsInfo[3], flagsInfo[4]);

				/* Write the flags information into the QSPI storage */
				retVal = qspiWrite(&g_qspiInstance, (u8 *)&flagsInfo, QSPI_ADDR_STATUS_FLAGS, 5);
				if (retVal == XST_SUCCESS) {
					xil_printf("QSPI Status flags updated successfully!\r\n");
				}
				else {
					xil_printf("QSPI Status flags writing failed!\r\n");
				}
			}
			/* In the Normal boot attempt also reset the retry flag */
			else {
				/* Set the Retry with 0 */
				flagsInfo[4] = 0;

				xil_printf("PB : %d\r\nPA : %d\r\nCB : %d\r\nCA : %d\r\nRETRY : %d\r\n",
						flagsInfo[0], flagsInfo[1], flagsInfo[2], flagsInfo[3], flagsInfo[4]);

				/* Write the flags information into the QSPI storage */
				retVal = qspiWrite(&g_qspiInstance, (u8 *)&flagsInfo, QSPI_ADDR_STATUS_FLAGS, 5);
				if (retVal == XST_SUCCESS) {
					xil_printf("QSPI Status flags updated successfully!\r\n");
				}
				else {
					xil_printf("QSPI Status flags writing failed!\r\n");
				}
			}
		}
		else {
			xil_printf("QSPI Status flags reading failed!\r\n");
		}

		// Release the mutex
		xSemaphoreGive(xQSPIMutex);
	}

	/* Print System Booting on the LCD */
	lcdPrintSystemBootingMessage();

	/* Retrieve the Serial Number informations */
	retVal = getProductSerialNumbers();
	if(retVal != XST_SUCCESS) {
		xil_printf("Serial Numbers retrieve operation Failed!\r\n");

		/* Perform the EVSE System hard reset */
		EVSE_PerformHardReset();
	}

	/* Create all the necessary required tasks */
	createEvseSystemTasks();
	xil_printf("All required tasks created successfully!\r\n");

	/* Start the tasks and timer running. */
	vTaskStartScheduler();

	while(1);
}
