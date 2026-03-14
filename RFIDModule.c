/*
 * =====================================================================================
 * File Name:    RFIDModule.c
 * Author:       Vanix Technologies PVT. LTD.
 * Date Created: 2025-03-17
 * Description:  This source file contains the implementation of functions required for
 *               managing the RFID module of the Electric Vehicle Supply Equipment (EVSE)
 *               charger. It handles RFID card authentication, user identification, and
 *               secure access control for the charging system.
 *
 *               The file includes functions for initializing the RFID module, reading
 *               RFID tags, verifying authentication credentials, and processing RFID-based
 *               transactions. It ensures reliable and secure communication between the
 *               RFID hardware and the EVSE system.
 *
 * Revision History:
 * Version 1.0 - 2025-03-17 - Initial version.
 * =====================================================================================
 */

/*
 * =====================================================================================
 * Include Headers
 * =====================================================================================
 */
#include "RFIDModule.h"

/*
 * =====================================================================================
 * Global Variables
 * =====================================================================================
 */
TaskHandle_t xRFIDTask;					// RFID Task handler pointer

NfcTag	g_nfcTagInfo;					// Global Variable to store the detected tag info
char g_nfcTagStr[9] = {0};				// Global Variable to store the detected tag info in string format
char g_authenticationTimestamp[30];		// Global Variable to store the timestamp when authentication attempted
bool g_isUserAuthenticated = FALSE; 	// Global flag to store the User authentication live data
bool g_isTagDetected = FALSE; 			// Global flag to store the Tag detected live data

/*
 * =====================================================================================
 * Static Global Variables
 * =====================================================================================
 */
static uint8_t lastUID[UID_LENGTH] = {0}; 	// To store the last maintainer UID

/*
 * =====================================================================================
 * Static Function Definitions
 * =====================================================================================
 */

/**
 * @brief Converts NFC tag UID to a string representation.
 *
 * This function takes the UID of an NFC tag and converts it into a human-readable
 * string format. The resulting string is stored in the provided buffer.
 *
 * @param  uid        Pointer to the NFC tag UID data.
 * @param  nfcTagStr  Pointer to the buffer where the converted string will be stored.
 * @param  uidLength  Length of the UID data in bytes.
 * @retval None
 */
static void convertNfcTagInfoToString(const u8 *uid, char *nfcTagStr, u8 uidLength)
{
	u8 loopCnt = 0;

	/* Convert the detected Tag information into the string format */
	if (uid != NULL) {
		for (loopCnt = 0; loopCnt < uidLength; loopCnt++) {
			sprintf(nfcTagStr + (loopCnt * 2), "%02X", uid[loopCnt]);
		}
		nfcTagStr[uidLength * 2] = '\0'; 	// Null-terminate the string
	}
	xil_printf("\r\nRFID / NFC Tag String : %s\r\n\n", nfcTagStr);
}

/*
 * =====================================================================================
 * Global Function Definitions
 * =====================================================================================
 */

/**
 * @brief  FreeRTOS task for handling RFID operations.
 *
 * This function is a FreeRTOS task dedicated to managing RFID-related operations,
 * including reading RFID tags, processing authentication, and communicating
 * with other system components. It runs as part of the system's task scheduler
 * to ensure efficient handling of RFID interactions.
 */
void prvRFIDTask( void *pvParameters )
{
	/* Delay for 2 seconds */
	const TickType_t x2second = INT_TO_TICKS( 2 * DELAY_1_SECOND );

	/* Initialize UART with full configuration including interrupt setup */
	UartIntpInit(&IntcInstance, &UartInst, UART_DEVICE_ID, UART_IRPT_INTR);

	/* Suspending the task as of now and wait for its resumption */
	vTaskSuspend(NULL);

	u8 retVal;

	while(1)
	{
		xil_printf("\t\t\t###   %s   ###\t\t\t\r\n", __func__);

		/* Take the NFC status variable */
		NFC_Status l_status;

		/* Reset the RFID/NFC tag Information */
		memset(&g_nfcTagInfo, 0, sizeof(g_nfcTagInfo));

		// Wait for the RFID mutex
		if (xSemaphoreTake(xLCDMutex, portMAX_DELAY) == pdTRUE) {
			/* Read the RFID/NFC tag or card */
			l_status =  readNFC(&g_nfcTagInfo);

			/* If RFID/NFC tag or card reading successful */
			if (l_status == NFC_SUCCESS) {
				/* If card read successfully that means tag was detected */
				g_isTagDetected = TRUE;
			}

			// Release the mutex
			xSemaphoreGive(xLCDMutex);
		}

		/* If RFID/NFC tag or card reading successful */
		if (g_isTagDetected) {

			/* If Tag is detected then we have to update the authentication attempted timestamp */
			rtcGetDateTime(g_authenticationTimestamp, sizeof(g_authenticationTimestamp));

			/* TODO : Also log the event into the session memory */

			/* Convert the detected Tag information into the string format */
			convertNfcTagInfoToString(g_nfcTagInfo._uid, (char *)&g_nfcTagStr, g_nfcTagInfo._uidLength);

			/* Create the JSON object for the Payload structure type */
			char *RFIDLogJSONStr = NULL;
			cJSON *RFIDLogJSON = cJSON_CreateObject();
			cJSON_AddStringToObject(RFIDLogJSON, "timestamp", g_authenticationTimestamp);
			cJSON_AddStringToObject(RFIDLogJSON, "idTag", g_nfcTagStr);

			/* validate the Tag info from the local database for Maintainer */
			l_status = VerifyMaintainerDB(g_nfcTagInfo._uid, g_nfcTagInfo._uidLength);

			/* If Maintainer authentication is success */
			if (l_status == NFC_SUCCESS) {
				cJSON_AddStringToObject(RFIDLogJSON, "usertype", "maintainer");

				if(!g_isMaintainerAuthenticated) {
					/* Set the Global Maintainer RFID authenticated flag */
					g_isMaintainerAuthenticated = TRUE;
					memcpy(lastUID, g_nfcTagInfo._uid, g_nfcTagInfo._uidLength);

					cJSON_AddStringToObject(RFIDLogJSON, "status", "authorized");

					// Wait for the LCD mutex
					if (xSemaphoreTake(xLCDMutex, portMAX_DELAY) == pdTRUE) {
						xil_printf("Maintainer authentication status : SUCCESS!\r\n");
						/* Print the details on the LCD module */
						lcdPrintMaintenanceModeActivatedMessage();

						// Release the mutex
						xSemaphoreGive(xLCDMutex);
					}
				}
				else if (memcmp(lastUID, g_nfcTagInfo._uid, g_nfcTagInfo._uidLength) == 0) {
					/* Set the Global Maintainer RFID authenticated flag */
					g_isMaintainerAuthenticated = FALSE;

					cJSON_AddStringToObject(RFIDLogJSON, "status", "authorized");

					//clear lastUID data
					memset(lastUID, 0, UID_LENGTH);

					// Wait for the LCD mutex
					if (xSemaphoreTake(xLCDMutex, portMAX_DELAY) == pdTRUE) {
						xil_printf("MAINTENANCE MODE DEACTIVATED!\r\n");

						/* Print the details on the LCD module */
						lcdPrintMaintenanceModeExitedMessage();

						// Release the mutex
						xSemaphoreGive(xLCDMutex);
					}
				}
				else {
					xil_printf("ACCESS DENIED: Another maintainer is in session!\r\n");

					cJSON_AddStringToObject(RFIDLogJSON, "status", "unauthorized");
				}
			}
			/* If there is maintainer mode activated then skip user authentication part */
			else {

				cJSON_AddStringToObject(RFIDLogJSON, "usertype", "user");

				/* Need to authenticate the read tag information based on the device mode */
				if (!g_networkConnectivity || !g_isOCPPServerConnected) {
					/* Device is in offline mode so need to validate the Tag info from the local database */
					l_status = VerifyUserDB(g_nfcTagInfo._uid, g_nfcTagInfo._uidLength);

					strcpy(g_AuthorizeRequest.idTag, g_nfcTagStr);

					/* If user authentication is success */
					if (l_status == NFC_SUCCESS) {
						/* Set the Global User RFID authenticated flag */
						g_isUserAuthenticated = TRUE;

						/* Indicate that RFID authenticated by turning on the RFID status LED */
						setLedPin(LED_RFID);

						cJSON_AddStringToObject(RFIDLogJSON, "status", "authorized");

						/* Check if the Session timer active or not */
						if( xTimerIsTimerActive( xSessionTimer ) == pdFALSE ) {
							/* Reset the Session Timer */
							xTimerReset( xSessionTimer, 0);

							/* Start the Session timer */
							if (xTimerStart(xSessionTimer, 0) != pdPASS) {
								/* If failed to start the timer */
								xil_printf("Failed to start Session Timer!\r\n");
							}
							else {
								/* If timer successfully started */
								xil_printf("Session Timer started!\r\n");
							}
						}

						// Wait for the LCD mutex
						if (xSemaphoreTake(xLCDMutex, portMAX_DELAY) == pdTRUE) {
							xil_printf("User authentication status : SUCCESS!\r\n");
							/* Print the details on the LCD module */
							lcdPrintOfflineModeRfidAuthPassMessage();

							// Release the mutex
							xSemaphoreGive(xLCDMutex);
						}

						/* Now is success scenario we have set the global flag, printed the message on LCD module
						 * and also started the session timer so now no need to keep running this task */

						/* Suspending the task as of now and wait for its resumption */
						vTaskSuspend(NULL);
					}
					/* If user authentication is failed */
					else if (l_status == NFC_ERROR_AUTH_FAILED) {
						/* Set the Global User RFID authenticated flag */
						g_isUserAuthenticated = FALSE;

						/* Indicate that RFID not authenticated by turning OFF the RFID status LED */
						resetLedPin(LED_RFID);

						cJSON_AddStringToObject(RFIDLogJSON, "status", "unauthorized");

						// Wait for the LCD mutex
						if (xSemaphoreTake(xLCDMutex, portMAX_DELAY) == pdTRUE) {
							xil_printf("User authentication status : FAILED!\r\n");
							/* Print the details on the LCD module */
							lcdPrintOfflineModeRfidAuthFailMessage();

							// Release the mutex
							xSemaphoreGive(xLCDMutex);
						}
					}
				}
				else {
					/* Device is online mode so need to validate the Tag info from OCPP cloud database */
					// Wait for the LCD mutex
					if (xSemaphoreTake(xLCDMutex, portMAX_DELAY) == pdTRUE) {
						/* Print the details on the LCD module */
						lcdPrintTagDetectedAndAuthorizingMessage();

						// Release the mutex
						xSemaphoreGive(xLCDMutex);
					}

					/* Sending the Authorize OCPP message */
					if(g_networkConnectivity && g_isOCPPServerConnected) {
						xil_printf("Sending OCPP Authorize messages!\r\n");

						/* Update the Authorize structure fields to send the message */
						g_AuthorizeRequest.serialNumbers = &g_ProductSerialNumbers;
						strcpy(g_AuthorizeRequest.idTag, g_nfcTagStr);

						retVal = OCPPSendAuthorizeMessage(&g_AuthorizeRequest);
						if (retVal == XST_SUCCESS) {
							/* If message sending part success then check its response if it is accepted or not */
							if (strcmp(g_AuthorizeResponse.status, "Accepted") == 0) {
								xil_printf("OCPP Authorize message sent successfully!\r\n");
								g_isUserAuthenticated = TRUE;

								/* Indicate that RFID authenticated by turning on the RFID status LED */
								setLedPin(LED_RFID);

								cJSON_AddStringToObject(RFIDLogJSON, "status", "authorized");

								/* Check if the Session timer active or not */
								if( xTimerIsTimerActive( xSessionTimer ) == pdFALSE ) {
									/* Reset the Session Timer */
									xTimerReset( xSessionTimer, 0);

									/* Start the Session timer */
									if (xTimerStart(xSessionTimer, 0) != pdPASS) {
										/* If failed to start the timer */
										xil_printf("Failed to start Session Timer!\r\n");
									}
									else {
										/* If timer successfully started */
										xil_printf("Session Timer started!\r\n");
									}
								}

								// Wait for the LCD mutex
								if (xSemaphoreTake(xLCDMutex, portMAX_DELAY) == pdTRUE) {
									xil_printf("User authentication status : SUCCESS!\r\n");
									/* Print the details on the LCD module */
									lcdPrintRfidAuthPassMessage();

									// Release the mutex
									xSemaphoreGive(xLCDMutex);
								}

								/* Now is success scenario we have set the global flag, printed the message on LCD module
								 * and also started the session timer so now no need to keep running this task */

//								/* Suspending the task as of now and wait for its resumption */
//								vTaskSuspend(NULL);
							}
							else {
								xil_printf("OCPP Authorize message sent successfully But got response as Rejected!\r\n");
								g_isUserAuthenticated = FALSE;

								/* Indicate that RFID not authenticated by turning OFF the RFID status LED */
								resetLedPin(LED_RFID);

								cJSON_AddStringToObject(RFIDLogJSON, "status", "unauthorized");

								// Wait for the LCD mutex
								if (xSemaphoreTake(xLCDMutex, portMAX_DELAY) == pdTRUE) {
									xil_printf("User authentication status : FAILED!\r\n");
									/* Print the details on the LCD module */
									lcdPrintRfidAuthFailMessage();

									// Release the mutex
									xSemaphoreGive(xLCDMutex);
								}
							}
						}
						else {
							xil_printf("OCPP Authorize message sending failed!\r\n");
							g_isUserAuthenticated = FALSE;

							/* Indicate that RFID not authenticated by turning OFF the RFID status LED */
							resetLedPin(LED_RFID);

							cJSON_AddStringToObject(RFIDLogJSON, "status", "unauthorized");
						}
					}
				}
			}

			/* Create the final Payload string by converting the JSON object into string format */
			RFIDLogJSONStr = cJSON_PrintUnformatted(RFIDLogJSON);

			/* Once Payload final string is generated then free the JSON object memory */
			cJSON_Delete(RFIDLogJSON);

			retVal = flashAddMsg(FILETYPE_RFID_LOG,0,RFIDLogJSONStr);
			if(retVal == XST_FAILURE)
			{
				xil_printf("RFIDLogJSON Saving msg into memory failed, Error %d\r\n",retVal);
			}

			free(RFIDLogJSONStr);

			if( g_isUserAuthenticated == TRUE )
			{
				/* Suspending the task as of now and wait for its resumption */
				vTaskSuspend(NULL);
			}
		}

		/* Reset the Tag detected flag */
		g_isTagDetected = FALSE;

		/* Delay for 2 second. */
		vTaskDelay( x2second );
	}
}

/**
 * @brief  Checks for user authentication via RFID or HTTP API.
 *
 * This function attempts two authentication methods:
 * 1. RFID card detection using PN532 module (via IRQ polling)
 * 2. HTTP API check to backend server if no RFID card detected
 *
 * For RFID: Performs complete authentication flow with all original logic
 * For HTTP API: Simple authorization check, sets user authenticated flag
 *
 * @param  None
 * @retval u8 Returns XST_SUCCESS if authentication successful, XST_FAILURE otherwise.
 */
u8 CheckUserAuthentication(void)
{
	u16 irqPinStatus;
	u8 retVal = XST_FAILURE;
	cJSON *responseJSON = NULL;
	NFC_Status l_status;
	char *RFIDLogJSONStr = NULL;
	cJSON *RFIDLogJSON = NULL;
	
	xil_printf("Checking user authentication...\r\n");
	
	/* Reset the RFID/NFC tag Information */
	memset(&g_nfcTagInfo, 0, sizeof(g_nfcTagInfo));
	g_isTagDetected = FALSE;
	
	/* Step 1: Check IRQ pin first - maybe card is already detected */
	irqPinStatus = ReadPN532_IRQ_Register();
	xil_printf("Initial IRQ Status: %d (0=card detected, 1=no card)\r\n", irqPinStatus);
	
	/* If IRQ is HIGH (no card), send InListPassiveTarget command to search */
	if (irqPinStatus == 1) {
		xil_printf("No card detected yet, sending InListPassiveTarget command...\r\n");
		if (nfc_sendInListPassiveTargetCommand() != XST_SUCCESS) {
			xil_printf("Failed to send InListPassiveTarget command!\r\n");
		}
		
		/* Small delay to allow PN532 to search for card */
		vTaskDelay(INT_TO_TICKS(100));
		
		/* Check IRQ pin again after command */
		irqPinStatus = ReadPN532_IRQ_Register();
		xil_printf("IRQ Status after command: %d (0=card detected, 1=no card)\r\n", irqPinStatus);
	}
	else {
		xil_printf("Card already detected (IRQ is LOW), reading directly...\r\n");
	}
	
	/* =========================================================================
	 * PATH 1: RFID CARD DETECTED - Use complete original authentication logic
	 * =========================================================================*/
	if (irqPinStatus == 0) {
		xil_printf("RFID card detected via IRQ!\r\n");
		
		// Wait for the RFID mutex
		if (xSemaphoreTake(xLCDMutex, portMAX_DELAY) == pdTRUE) {
			/* Read the response and card data - DON'T send command again! */
			if (nfc_readPassiveTargetResponse()) {
				/* UID extracted, now read full card data */
				g_nfcTagInfo = nfc_read();
				
				if (g_nfcTagInfo._uid != NULL && g_nfcTagInfo._uidLength > 0) {
					/* If card read successfully that means tag was detected */
					g_isTagDetected = TRUE;
				}
			}
			
			// Release the mutex
			xSemaphoreGive(xLCDMutex);
		}
	}
	/* =========================================================================
	 * PATH 2: NO RFID CARD - Check HTTP API (Simple authorization only)
	 * =========================================================================*/
	/*else {
		xil_printf("No RFID card detected. Checking HTTP API...\r\n");
		
		 Check if network is available
		if (!g_networkConnectivity) {
			xil_printf("No network connectivity for HTTP API check\r\n");
			return XST_FAILURE;
		}
		
		 Send HTTP GET request to the API
		// Wait for the AT mutex
		if (xSemaphoreTake(xATMutex, portMAX_DELAY) == pdTRUE) {
			 Use static buffer to avoid heap allocation failures
			static char httpResponseBuffer[512];
			memset(httpResponseBuffer, 0, sizeof(httpResponseBuffer));
			
			 Prepare HTTP GET AT command with MAC address as query parameter
			char HTTPCommandStr[256];
			snprintf(HTTPCommandStr, sizeof(HTTPCommandStr), 
					"AT+HTTPCGET=\"%s?eps32MACAddress=%s\"", 
					OCPP_MSG_CHECK_AUTHORIZE_URL, g_ProductSerialNumbers.Esp32MACAddress);
			
			xil_printf("HTTP GET Command: %s\r\n", HTTPCommandStr);
			
			 Send AT command for HTTP GET
			retVal = SendUartATCommandWithResponse(HTTPCommandStr, 60, 5000, httpResponseBuffer, sizeof(httpResponseBuffer));
			
			if (retVal == XST_SUCCESS && (strstr(httpResponseBuffer, "OK") != NULL || strstr(httpResponseBuffer, "+HTTPCGET") != NULL)) {
				xil_printf("HTTP GET Response: %s\r\n", httpResponseBuffer);
				
				 Find JSON in response (starts with '{')
				char *jsonStart = strchr(httpResponseBuffer, '{');
					if (jsonStart == NULL) {
						xil_printf("No JSON found in response\r\n");
						retVal = XST_FAILURE;
					} else {
						 Parse JSON response
						responseJSON = cJSON_Parse(jsonStart);
						if (responseJSON != NULL) {
						cJSON *returnItem = cJSON_GetObjectItem(responseJSON, "return");
						
						if (returnItem != NULL && cJSON_IsBool(returnItem)) {
							if (cJSON_IsTrue(returnItem)) {
								xil_printf("HTTP API authentication successful!\r\n");
								
								 Get idTag from response
								cJSON *idTagItem = cJSON_GetObjectItem(responseJSON, "idTag");
								if (idTagItem != NULL && cJSON_IsString(idTagItem)) {
									 Store idTag
									strncpy(g_nfcTagStr, idTagItem->valuestring, sizeof(g_nfcTagStr) - 1);
									g_nfcTagStr[sizeof(g_nfcTagStr) - 1] = '\0';
									
									xil_printf("Authenticated with idTag: %s\r\n", g_nfcTagStr);
									
									 Simple HTTP API authorization - just set authenticated flag
									g_isUserAuthenticated = TRUE;
									
									 Indicate that authenticated by turning on the RFID status LED
									setLedPin(LED_RFID);
									
									// Wait for the LCD mutex
									if (xSemaphoreTake(xLCDMutex, portMAX_DELAY) == pdTRUE) {
										xil_printf("HTTP API User authentication : SUCCESS!\r\n");
										lcdPrintRfidAuthPassMessage();
										xSemaphoreGive(xLCDMutex);
									}
									
									retVal = XST_SUCCESS;
								}
								else {
									xil_printf("No idTag found in response\r\n");
									g_isUserAuthenticated = FALSE;
									resetLedPin(LED_RFID);
									retVal = XST_FAILURE;
								}
							}
							else {
								xil_printf("HTTP API returned false - not authorized\r\n");
								g_isUserAuthenticated = FALSE;
								resetLedPin(LED_RFID);
								retVal = XST_FAILURE;
							}
						}
						else {
							xil_printf("Invalid response format\r\n");
							g_isUserAuthenticated = FALSE;
							retVal = XST_FAILURE;
						}
						
						cJSON_Delete(responseJSON);
						}
						else {
							xil_printf("Failed to parse JSON response\r\n");
							g_isUserAuthenticated = FALSE;
							retVal = XST_FAILURE;
						}
					}
				}
				else {
				xil_printf("HTTP request failed or no SEND OK\r\n");
				g_isUserAuthenticated = FALSE;
				retVal = XST_FAILURE;
			}
			
			xSemaphoreGive(xATMutex);
		}
		
		 HTTP API path done - return result
		return retVal;
	}
	*/
	/* =========================================================================
	 * AUTHENTICATION PROCESSING - Works for both RFID and HTTP API methods
	 * =========================================================================*/
	
	/* If Tag is detected (either via RFID or HTTP API) */
	if (g_isTagDetected) {
		
		/* If Tag is detected then we have to update the authentication attempted timestamp */
		rtcGetDateTime(g_authenticationTimestamp, sizeof(g_authenticationTimestamp));
		
		/* TODO : Also log the event into the session memory */
		
		/* Convert the detected Tag information into the string format (only for RFID) */
		if (g_nfcTagInfo._uid != NULL && g_nfcTagInfo._uidLength > 0) {
			convertNfcTagInfoToString(g_nfcTagInfo._uid, (char *)&g_nfcTagStr, g_nfcTagInfo._uidLength);
		}
		
		/* Create the JSON object for the Payload structure type */
		RFIDLogJSON = cJSON_CreateObject();
		cJSON_AddStringToObject(RFIDLogJSON, "timestamp", g_authenticationTimestamp);
		cJSON_AddStringToObject(RFIDLogJSON, "idTag", g_nfcTagStr);
		
		/* validate the Tag info from the local database for Maintainer (only for RFID) */
		if (g_nfcTagInfo._uid != NULL && g_nfcTagInfo._uidLength > 0) {
			l_status = VerifyMaintainerDB(g_nfcTagInfo._uid, g_nfcTagInfo._uidLength);
		}
		else {
			l_status = NFC_ERROR_AUTH_FAILED;
		}
		
		/* If Maintainer authentication is success */
		if (l_status == NFC_SUCCESS) {
			cJSON_AddStringToObject(RFIDLogJSON, "usertype", "maintainer");
			
			if(!g_isMaintainerAuthenticated) {
				/* Set the Global Maintainer RFID authenticated flag */
				g_isMaintainerAuthenticated = TRUE;
				memcpy(lastUID, g_nfcTagInfo._uid, g_nfcTagInfo._uidLength);
				
				cJSON_AddStringToObject(RFIDLogJSON, "status", "authorized");
				
				// Wait for the LCD mutex
				if (xSemaphoreTake(xLCDMutex, portMAX_DELAY) == pdTRUE) {
					xil_printf("Maintainer authentication status : SUCCESS!\r\n");
					/* Print the details on the LCD module */
					lcdPrintMaintenanceModeActivatedMessage();
					
					// Release the mutex
					xSemaphoreGive(xLCDMutex);
				}
				
				retVal = XST_SUCCESS;
			}
			else if (memcmp(lastUID, g_nfcTagInfo._uid, g_nfcTagInfo._uidLength) == 0) {
				/* Set the Global Maintainer RFID authenticated flag */
				g_isMaintainerAuthenticated = FALSE;
				
				cJSON_AddStringToObject(RFIDLogJSON, "status", "authorized");
				
				//clear lastUID data
				memset(lastUID, 0, UID_LENGTH);
				
				// Wait for the LCD mutex
				if (xSemaphoreTake(xLCDMutex, portMAX_DELAY) == pdTRUE) {
					xil_printf("MAINTENANCE MODE DEACTIVATED!\r\n");
					
					/* Print the details on the LCD module */
					lcdPrintMaintenanceModeExitedMessage();
					
					// Release the mutex
					xSemaphoreGive(xLCDMutex);
				}
				
				retVal = XST_SUCCESS;
			}
			else {
				xil_printf("ACCESS DENIED: Another maintainer is in session!\r\n");
				
				cJSON_AddStringToObject(RFIDLogJSON, "status", "unauthorized");
				retVal = XST_FAILURE;
			}
		}
		/* If there is maintainer mode activated then skip user authentication part */
		else {
			
			cJSON_AddStringToObject(RFIDLogJSON, "usertype", "user");
			
			/* Need to authenticate the read tag information based on the device mode */
			if (!g_networkConnectivity || !g_isOCPPServerConnected) {
				/* Device is in offline mode so need to validate the Tag info from the local database */
				/* Only validate RFID cards in offline mode, skip HTTP API method */
				if (g_nfcTagInfo._uid != NULL && g_nfcTagInfo._uidLength > 0) {
					l_status = VerifyUserDB(g_nfcTagInfo._uid, g_nfcTagInfo._uidLength);
				}
				else {
					l_status = NFC_ERROR_AUTH_FAILED;
				}
				
				strcpy(g_AuthorizeRequest.idTag, g_nfcTagStr);
				
				/* If user authentication is success */
				if (l_status == NFC_SUCCESS) {
					/* Set the Global User RFID authenticated flag */
					g_isUserAuthenticated = TRUE;
					
					/* Indicate that RFID authenticated by turning on the RFID status LED */
					setLedPin(LED_RFID);
					
					cJSON_AddStringToObject(RFIDLogJSON, "status", "authorized");
					
					/* Check if the Session timer active or not */
					if( xTimerIsTimerActive( xSessionTimer ) == pdFALSE ) {
						/* Reset the Session Timer */
						xTimerReset( xSessionTimer, 0);
						
						/* Start the Session timer */
						if (xTimerStart(xSessionTimer, 0) != pdPASS) {
							/* If failed to start the timer */
							xil_printf("Failed to start Session Timer!\r\n");
						}
						else {
							/* If timer successfully started */
							xil_printf("Session Timer started!\r\n");
						}
					}
					
					// Wait for the LCD mutex
					if (xSemaphoreTake(xLCDMutex, portMAX_DELAY) == pdTRUE) {
						xil_printf("User authentication status : SUCCESS!\r\n");
						/* Print the details on the LCD module */
						lcdPrintOfflineModeRfidAuthPassMessage();
						
						// Release the mutex
						xSemaphoreGive(xLCDMutex);
					}
					
					retVal = XST_SUCCESS;
				}
				/* If user authentication is failed */
				else if (l_status == NFC_ERROR_AUTH_FAILED) {
					/* Set the Global User RFID authenticated flag */
					g_isUserAuthenticated = FALSE;
					
					/* Indicate that RFID not authenticated by turning OFF the RFID status LED */
					resetLedPin(LED_RFID);
					
					cJSON_AddStringToObject(RFIDLogJSON, "status", "unauthorized");
					
					// Wait for the LCD mutex
					if (xSemaphoreTake(xLCDMutex, portMAX_DELAY) == pdTRUE) {
						xil_printf("User authentication status : FAILED!\r\n");
						/* Print the details on the LCD module */
						lcdPrintOfflineModeRfidAuthFailMessage();
						
						// Release the mutex
						xSemaphoreGive(xLCDMutex);
					}
					
					retVal = XST_FAILURE;
				}
			}
			else {
				/* Device is online mode so need to validate the Tag info from OCPP cloud database */
				// Wait for the LCD mutex
				if (xSemaphoreTake(xLCDMutex, portMAX_DELAY) == pdTRUE) {
					/* Print the details on the LCD module */
					lcdPrintTagDetectedAndAuthorizingMessage();
					
					// Release the mutex
					xSemaphoreGive(xLCDMutex);
				}
				
				/* Sending the Authorize OCPP message */
				if(g_networkConnectivity && g_isOCPPServerConnected) {
					xil_printf("Sending OCPP Authorize messages!\r\n");
					
					/* Update the Authorize structure fields to send the message */
					g_AuthorizeRequest.serialNumbers = &g_ProductSerialNumbers;
					strcpy(g_AuthorizeRequest.idTag, g_nfcTagStr);
					
					retVal = OCPPSendAuthorizeMessage(&g_AuthorizeRequest);
					if (retVal == XST_SUCCESS) {
						/* If message sending part success then check its response if it is accepted or not */
						if (strcmp(g_AuthorizeResponse.status, "Accepted") == 0) {
							xil_printf("OCPP Authorize message sent successfully!\r\n");
							g_isUserAuthenticated = TRUE;
							
							/* Indicate that RFID authenticated by turning on the RFID status LED */
							setLedPin(LED_RFID);
							
							cJSON_AddStringToObject(RFIDLogJSON, "status", "authorized");
							
							/* Check if the Session timer active or not */
							if( xTimerIsTimerActive( xSessionTimer ) == pdFALSE ) {
								/* Reset the Session Timer */
								xTimerReset( xSessionTimer, 0);
								
								/* Start the Session timer */
								if (xTimerStart(xSessionTimer, 0) != pdPASS) {
									/* If failed to start the timer */
									xil_printf("Failed to start Session Timer!\r\n");
								}
								else {
									/* If timer successfully started */
									xil_printf("Session Timer started!\r\n");
								}
							}
							
							// Wait for the LCD mutex
							if (xSemaphoreTake(xLCDMutex, portMAX_DELAY) == pdTRUE) {
								xil_printf("User authentication status : SUCCESS!\r\n");
								/* Print the details on the LCD module */
								lcdPrintRfidAuthPassMessage();
								
								// Release the mutex
								xSemaphoreGive(xLCDMutex);
							}
							
							retVal = XST_SUCCESS;
						}
						else {
							xil_printf("OCPP Authorize message sent successfully But got response as Rejected!\r\n");
							g_isUserAuthenticated = FALSE;
							
							/* Indicate that RFID not authenticated by turning OFF the RFID status LED */
							resetLedPin(LED_RFID);
							
							cJSON_AddStringToObject(RFIDLogJSON, "status", "unauthorized");
							
							// Wait for the LCD mutex
							if (xSemaphoreTake(xLCDMutex, portMAX_DELAY) == pdTRUE) {
								xil_printf("User authentication status : FAILED!\r\n");
								/* Print the details on the LCD module */
								lcdPrintRfidAuthFailMessage();
								
								// Release the mutex
								xSemaphoreGive(xLCDMutex);
							}
							
							retVal = XST_FAILURE;
						}
					}
					else {
						xil_printf("OCPP Authorize message sending failed!\r\n");
						g_isUserAuthenticated = FALSE;
						
						/* Indicate that RFID not authenticated by turning OFF the RFID status LED */
						resetLedPin(LED_RFID);
						
						cJSON_AddStringToObject(RFIDLogJSON, "status", "unauthorized");
						
						retVal = XST_FAILURE;
					}
				}
			}
		}
		
		/* Create the final Payload string by converting the JSON object into string format */
		RFIDLogJSONStr = cJSON_PrintUnformatted(RFIDLogJSON);
		
		/* Once Payload final string is generated then free the JSON object memory */
		cJSON_Delete(RFIDLogJSON);
		
		/* Save log to flash - use separate variable to not overwrite authentication result */
		u8 flashResult = flashAddMsg(FILETYPE_RFID_LOG,0,RFIDLogJSONStr);
		if(flashResult == XST_FAILURE)
		{
			xil_printf("RFIDLogJSON Saving msg into memory failed, Error %d\r\n",flashResult);
		}
		
		free(RFIDLogJSONStr);
	}
	
	/* Reset the Tag detected flag */
	g_isTagDetected = FALSE;
	
	return retVal;
}
