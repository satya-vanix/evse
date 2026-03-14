/*
 * =====================================================================================
 * File Name:    ocpp.c
 * Author:       Vanix Technologies PVT. LTD.
 * Date Created: 2025-03-11
 * Description:  This source file contains the implementation of functions required for
 *               handling Open Charge Point Protocol (OCPP) communication in the
 *               Electric Vehicle Supply Equipment (EVSE) charger. It manages the
 *               exchange of OCPP messages, WebSocket communication, and protocol
 *               state transitions.
 *
 *               The file includes functions for JSON message parsing, event handling,
 *               and interaction with the central OCPP server. It ensures seamless
 *               interoperability between the EVSE charger and backend systems,
 *               supporting remote monitoring, transaction processing, and firmware
 *               updates.
 *
 * Revision History:
 * Version 1.0 - 2025-03-11 - Initial version.
 * =====================================================================================
 */

/*
 * =====================================================================================
 * Include Headers
 * =====================================================================================
 */
#include "ocpp.h"
#include "evseMainApp.h"

/*
 * =====================================================================================
 * Global Variables
 * =====================================================================================
 */
TaskHandle_t xOCPPTask;					// OCPP Task handler pointer
TimerHandle_t xOCPPTimer;  				// OCPP Timer handle
bool g_isOCPPServerConnected;			// Global flag to store the OCPP server connection status live data
bool g_isOCPPServerConnectedPrevious;	// Global flag to store previous value of the OCPP server connection flag
bool g_isOCPPBootNotificationSent;		// Global flag to store the OCPP Boot Notification message status live data
u16 g_HeartBeatInterval;				// Heart Beat interval time
int g_CurrentSessionId = 0;				// Global variable to store the current session id
int g_OfflineSessionId = 0;				// Global variable to store the current session id in progress in offline mode

/* Declaring the all OCPP messages structure as global to be used in any module whenever needed and initializing it with NULL */
ProductSerialNumbers_t		g_ProductSerialNumbers		= {"NA", "NA", "", "NA", "NA"};
BootNotificationRequest_t 	g_BootNotificationRequest 	= {"", "", "", "NA", "NA"};
BootNotificationResponse_t 	g_BootNotificationResponse 	= {"", 0};
HeartBeatRsponse_t 			g_HeartBeatResponse 		= {""};
StatusNotificationRequest_t g_StatusNotificationRequest = {"NA", "", "", ""};
AuthorizeRequest_t 			g_AuthorizeRequest 			= {""};
AuthorizeResponse_t 		g_AuthorizeResponse 		= {"", "", ""};
StartTransactionRequest_t	g_StartTransactionRequest	= {"NA", "", "", 0, "NA"};
StartTransactionResponse_t	g_StartTransactionResponse	= {0, ""};
StopTransactionRequest_t	g_StopTransactionRequest	= {0, "", "", "NA", ""};
StopTransactionResponse_t	g_StopTransactionResponse	= {""};
MeterValuesRequest_t		g_MeterValuesRequest		= {"NA", 0, "",
															{"NA", "Energy.Active.Import.Register", "kWh"},
															{"", "Voltage", "V"},
															{"", "Current.Import", "A"}
														  };

extern EVSEStateMachineStatus_e g_EVSECurrentState;	/* Global variable to track the EVSE current state status */

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
 * @brief  Callback function for the OCPP timer.
 *
 * This function is triggered when the OCPP timer expires. It handles periodic
 * tasks related to OCPP communication, such as sending heartbeat messages,
 * monitoring server connectivity, or performing scheduled updates.
 *
 * @param  xTimer Handle to the timer that triggered the callback.
 * @retval None
 */
static void vOCPPTimerCallback(TimerHandle_t xTimer)
{
	xil_printf("\t\t\t!!!   %s   !!!\t\t\t\r\n", __func__);

	u8 retVal;

	/* If Boot Notification message was not sent successfully due to any reason then AT every 5 minutes
	 * OCPP Boot Notification message will be sent from here. If network connectivity is available and
	 * backend OCPP server is connected then only try to send the message. */

	if (!g_isOCPPBootNotificationSent && g_networkConnectivity && !g_isOtaInProgress)
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
}

/*
 * =====================================================================================
 * Global Function Definitions
 * =====================================================================================
 */

/**
 * @brief  Initializes the OCPP (Open Charge Point Protocol) module.
 */
u8 InitOCPP(void)
{
	/* Initialize the Global variables */
	g_isOCPPServerConnected = FALSE;
	g_isOCPPBootNotificationSent = FALSE;
	g_HeartBeatInterval = OCPP_HEART_BEAT_INTERVAL_DEFAULT_TIME;

	/* Everything is perfect then return SUCCESS */
	return XST_SUCCESS;
}

/**
 * @brief  Deinitializes the OCPP (Open Charge Point Protocol) module.
 */
u8 DeinitOCPP(void)
{
	/* Everything is perfect then return SUCCESS */
	return XST_SUCCESS;
}

/**
 * @brief  Sends a Boot Notification message to the OCPP central server.
 */
u8 OCPPSendBootNotificationMessage(BootNotificationRequest_t *msg)
{
	u8 retVal;
	int payloadLength = 0;
	char HTTPCommandStr[BUFFER_SIZE] = {0};
	char ResponseBufferStr[BUFFER_SIZE] = {0};
	cJSON *BootNotificationRequestJSON = NULL;
	char *BootNotificationRequestStr = NULL;
	cJSON *BootNotificationResponseJSON = NULL;
	char *BootNotificationResponseStr = NULL;

	/* If Payload message is NULL then return failure to calling API */
	if(!msg) {
		return XST_FAILURE;
	}

	/* Create the JSON object for the Payload structure type */
	BootNotificationRequestJSON = cJSON_CreateObject();

	/* Update the created JSON object to fill its key values */
	cJSON_AddStringToObject(BootNotificationRequestJSON, "chargePointModel", msg->chargePointModel);
	cJSON_AddStringToObject(BootNotificationRequestJSON, "chargePointVendor", msg->chargePointVendor);
	cJSON_AddStringToObject(BootNotificationRequestJSON, "firmwareVersion", msg->firmwareVersion);
	cJSON_AddStringToObject(BootNotificationRequestJSON, "meterType", msg->meterType);
	cJSON_AddStringToObject(BootNotificationRequestJSON, "meterSerialNumber", msg->meterSerialNumber);
	cJSON_AddStringToObject(BootNotificationRequestJSON, "chargePointSerialNumber", msg->serialNumbers->EVSESerialNumber);
	cJSON_AddStringToObject(BootNotificationRequestJSON, "fpgaId", msg->serialNumbers->FpgaID);
	cJSON_AddStringToObject(BootNotificationRequestJSON, "eps32MACAddress", msg->serialNumbers->Esp32MACAddress);
//	cJSON_AddStringToObject(BootNotificationRequestJSON, "ethernetMACAddress", msg->serialNumbers->EthMACAddress);
	cJSON_AddStringToObject(BootNotificationRequestJSON, "ec200MACAddress", msg->serialNumbers->EC200MACAddress);

	/* Create the final Payload string by converting the JSON object into string format */
	BootNotificationRequestStr = cJSON_PrintUnformatted(BootNotificationRequestJSON);

	/* Once Payload final string is generated then free the JSON object memory */
	cJSON_Delete(BootNotificationRequestJSON);

	xil_printf("BootNotificationRequestStr : %s\r\n", BootNotificationRequestStr);

	/* Calculate the Payload message length using the final created Payload string */
	payloadLength = strlen(BootNotificationRequestStr);

	/* Create the HTTP POST AT command string */
	snprintf(HTTPCommandStr, BUFFER_SIZE, "AT+HTTPCPOST=\"%s\",%d,1,\"content-type: application/json\"", OCPP_MSG_BOOT_NOTIFICTION_URL, payloadLength);

	xil_printf("BootNotificationRequest Message command : %s\r\n", HTTPCommandStr);

	// Wait for the AT mutex
	if (xSemaphoreTake(xATMutex, portMAX_DELAY) == pdTRUE) {
		/* Send the AT command to the ESP32 module and collect its response */
		retVal = SendUartATCommandWithResponse(HTTPCommandStr, 60, 1000, ResponseBufferStr, BUFFER_SIZE);

		if (retVal == XST_SUCCESS) {
			/* If HTTP POST AT command response is valid then proceed further */
			if(strstr(ResponseBufferStr, ">") != NULL) {
				/* Reset the response buffer to collect the next command response */
				memset(ResponseBufferStr, 0, BUFFER_SIZE);

				/* Send the Paylod string to the ESP32 module and collect its response */
				retVal = SendUartATCommandWithResponse(BootNotificationRequestStr, 60, 3000, ResponseBufferStr, BUFFER_SIZE);

				if (retVal == XST_SUCCESS) {
					/* If Payload final string send part completed then free that resource as well */
					free(BootNotificationRequestStr);

					/* If Payload command response is valid then proceed further */
					if (strstr(ResponseBufferStr, "SEND OK") != NULL) {
						/* ============= Payload server response handling started here ===================*/
						BootNotificationResponseStr = strchr(ResponseBufferStr, '{');

						if (!BootNotificationResponseStr) {
							xil_printf("No JSON string found in response!\r\n");

							// Release the AT mutex
							xSemaphoreGive(xATMutex);

							/* Return the failure status for Payload string response */
							return XST_FAILURE;
						}

						/* If JSON string is foudn in response */
						BootNotificationResponseJSON = cJSON_Parse(BootNotificationResponseStr);

						if (!BootNotificationResponseJSON) {
							xil_printf("Error in parsing response JSON!\r\n");

							// Release the AT mutex
							xSemaphoreGive(xATMutex);

							/* Return the failure status for response JSON parsing */
							return XST_FAILURE;
						}

						xil_printf("Response JSON parsed successfully!\r\n");

						cJSON * statusJSON = cJSON_GetObjectItem(BootNotificationResponseJSON, "status");
						cJSON * heartBeatIntervalJSON = cJSON_GetObjectItem(BootNotificationResponseJSON, "interval");

						/* Extract the status information */
						if (statusJSON && cJSON_IsString(statusJSON)) {
							strncpy(g_BootNotificationResponse.status,
									statusJSON->valuestring, sizeof(g_BootNotificationResponse.status));
							xil_printf("g_BootNotificationResponse.status : %s\r\n", g_BootNotificationResponse.status);
						}

						/* Extract the Heart beat interval information */
						if (heartBeatIntervalJSON && cJSON_IsNumber(heartBeatIntervalJSON)) {
							g_BootNotificationResponse.heartBeatInterval = heartBeatIntervalJSON->valueint;
							xil_printf("g_BootNotificationResponse.heartBeatInterval : %d\r\n", g_BootNotificationResponse.heartBeatInterval);

							/* Update the Heart Beat interval global variable */
							g_HeartBeatInterval = g_BootNotificationResponse.heartBeatInterval;
                            /* force fixed 10s interval */
                            g_HeartBeatInterval = OCPP_HEART_BEAT_INTERVAL_DEFAULT_TIME;
						}

						/* Free the allocated resources */
						cJSON_Delete(BootNotificationResponseJSON);

						/* ============= Payload server response handling exited here ===================*/
					}
					else {
						/* Received the invalid response for the Payload string command */
						xil_printf("Received the invalid response : %s\r\n", ResponseBufferStr);

						// Release the AT mutex
						xSemaphoreGive(xATMutex);

						/* Return the failure status in case of the invalid response of Payload string response */
						return XST_FAILURE;
					}
				}
				else {
					/* Get the Timeout as Payload string response */
					xil_printf("Timeout occurred : %s\r\n", BootNotificationRequestStr);

					// Free allocated resources
					free(BootNotificationRequestStr);

					// Release the AT mutex
					xSemaphoreGive(xATMutex);

					/* Return the Time out status in case of Payload string response time out occurred */
					return XST_TIMEOUT;
				}
			}
			else {
				/* Received the invalid response for the HTTP POST AT command */
				xil_printf("Received the invalid response : %s\r\n", ResponseBufferStr);

				// Free allocated resources
				free(BootNotificationRequestStr);

				// Release the AT mutex
				xSemaphoreGive(xATMutex);

				/* Return the failure status in case of the invalid response of AT command */
				return XST_FAILURE;
			}
		}
		else {
			/* Get the Timeout as AT Command response */
			xil_printf("Timeout occurred : %s\r\n", HTTPCommandStr);

			// Free allocated resources
			free(BootNotificationRequestStr);

			// Release the AT mutex
			xSemaphoreGive(xATMutex);

			/* Return the Time out status in case of the AT command time out occurred */
			return XST_TIMEOUT;
		}

		// Release the AT mutex
		xSemaphoreGive(xATMutex);
	}

	/* Everything is perfect then return SUCCESS */
	return XST_SUCCESS;
}

/**
 * @brief  Sends a Heartbeat message to the OCPP central server.
 */
u8 OCPPSendHeartBeatMessage(void)
{
	u8 retVal;
	int payloadLength = 0;
	char HTTPCommandStr[BUFFER_SIZE] = {0};
	char ResponseBufferStr[BUFFER_SIZE] = {0};
	cJSON *HeartBeatRequestJSON = NULL;
	char *HeartBeatRequestStr = NULL;
	cJSON *HeartBeatResponseJSON = NULL;
	char *HeartBeatResponseStr = NULL;

	/* Create the JSON object for the Payload structure type */
	HeartBeatRequestJSON = cJSON_CreateObject();

	/* Create the final Payload string by converting the JSON object into string format */
	HeartBeatRequestStr = cJSON_PrintUnformatted(HeartBeatRequestJSON);

	/* Once Payload final string is generated then free the JSON object memory */
	cJSON_Delete(HeartBeatRequestJSON);

	xil_printf("HeartBeatRequestStr : %s\r\n", HeartBeatRequestStr);

	/* Calculate the Payload message length using the final created Payload string */
	payloadLength = strlen(HeartBeatRequestStr);

	/* Create the HTTP POST AT command string */
	snprintf(HTTPCommandStr, BUFFER_SIZE, "AT+HTTPCPOST=\"%s\",%d,1,\"content-type: application/json\"", OCPP_MSG_HEART_BEAT_URL, payloadLength);

	xil_printf("HeartBeatRequest Message command : %s\r\n", HTTPCommandStr);

	// Wait for the AT mutex
	if (xSemaphoreTake(xATMutex, portMAX_DELAY) == pdTRUE) {
		/* Send the AT command to the ESP32 module and collect its response */
		retVal = SendUartATCommandWithResponse(HTTPCommandStr, 60, 1000, ResponseBufferStr, BUFFER_SIZE);

		if (retVal == XST_SUCCESS) {
			/* If HTTP POST AT command response is valid then proceed further */
			if(strstr(ResponseBufferStr, ">") != NULL) {
				/* Reset the response buffer to collect the next command response */
				memset(ResponseBufferStr, 0, BUFFER_SIZE);

				/* Send the Paylod string to the ESP32 module and collect its response */
				retVal = SendUartATCommandWithResponse(HeartBeatRequestStr, 60, 2000, ResponseBufferStr, BUFFER_SIZE);

				if (retVal == XST_SUCCESS) {
					/* If Payload final string send part completed then free that resource as well */
					free(HeartBeatRequestStr);

					/* If Payload command response is valid then proceed further */
					if (strstr(ResponseBufferStr, "SEND OK") != NULL) {
						/* ============= Payload server response handling started here ===================*/
						HeartBeatResponseStr = strchr(ResponseBufferStr, '{');

						if (!HeartBeatResponseStr) {
							xil_printf("No JSON string found in response!\r\n");

							// Release the AT mutex
							xSemaphoreGive(xATMutex);

							/* Return the failure status for Payload string response */
							return XST_FAILURE;
						}

						/* If JSON string is foudn in response */
						HeartBeatResponseJSON = cJSON_Parse(HeartBeatResponseStr);

						if (!HeartBeatResponseJSON) {
							xil_printf("Error in parsing response JSON!\r\n");

							// Release the AT mutex
							xSemaphoreGive(xATMutex);

							/* Return the failure status for response JSON parsing */
							return XST_FAILURE;
						}

						xil_printf("Response JSON parsed successfully!\r\n");

						cJSON * currentTimeJSON = cJSON_GetObjectItem(HeartBeatResponseJSON, "currentTime");

						/* Extract the Current time information */
						if (currentTimeJSON && cJSON_IsString(currentTimeJSON)) {
							strncpy(g_HeartBeatResponse.currentTime,
									currentTimeJSON->valuestring, sizeof(g_HeartBeatResponse.currentTime));
							xil_printf("g_HeartBeatResponse.currentTime : %s\r\n", g_HeartBeatResponse.currentTime);
						}

						/* Free the allocated resources */
						cJSON_Delete(HeartBeatResponseJSON);

						/* ============= Payload server response handling exited here ===================*/
					}
					else {
						/* Received the invalid response for the Payload string command */
						xil_printf("Received the invalid response : %s\r\n", ResponseBufferStr);

						// Release the AT mutex
						xSemaphoreGive(xATMutex);

						/* Return the failure status in case of the invalid response of Payload string response */
						return XST_FAILURE;
					}
				}
				else {
					/* Get the Timeout as Payload string response */
					xil_printf("Timeout occurred : %s\r\n", HeartBeatRequestStr);

					// Free allocated resources
					free(HeartBeatRequestStr);

					// Release the AT mutex
					xSemaphoreGive(xATMutex);

					/* Return the Time out status in case of Payload string response time out occurred */
					return XST_TIMEOUT;
				}
			}
			else {
				/* Received the invalid response for the HTTP POST AT command */
				xil_printf("Received the invalid response : %s\r\n", ResponseBufferStr);

				// Free allocated resources
				free(HeartBeatRequestStr);

				// Release the AT mutex
				xSemaphoreGive(xATMutex);

				/* Return the failure status in case of the invalid response of AT command */
				return XST_FAILURE;
			}
		}
		else {
			/* Get the Timeout as AT Command response */
			xil_printf("Timeout occurred : %s\r\n", HTTPCommandStr);

			// Free allocated resources
			free(HeartBeatRequestStr);

			// Release the AT mutex
			xSemaphoreGive(xATMutex);

			/* Return the Time out status in case of the AT command time out occurred */
			return XST_TIMEOUT;
		}

		// Release the AT mutex
		xSemaphoreGive(xATMutex);
	}

	/* Everything is perfect then return SUCCESS */
	return XST_SUCCESS;
}

/**
 * @brief  Sends a Status Notification message to the OCPP central server.
 */
u8 OCPPSendStatusNotificationMessage(int msgtype, StatusNotificationRequest_t *msg, char *offlinemsg)
{
	u8 retVal;
	int payloadLength = 0;
	char HTTPCommandStr[BUFFER_SIZE] = {0};
	char ResponseBufferStr[BUFFER_SIZE] = {0};
	cJSON *StatusNotificationRequestJSON = NULL;
	char *StatusNotificationRequestStr = NULL;

	if(msgtype == MSG_TYPE_ONLINE)
	{
		/* If Payload message is NULL then return failure to calling API */
		if(!msg) {
			return XST_FAILURE;
		}

		/* Create the JSON object for the Payload structure type */
		StatusNotificationRequestJSON = cJSON_CreateObject();

		/* Update the created JSON object to fill its key values */
		cJSON_AddStringToObject(StatusNotificationRequestJSON, "chargePointSerialNumber", msg->serialNumbers->EVSESerialNumber);
		cJSON_AddStringToObject(StatusNotificationRequestJSON, "fpgaId", msg->serialNumbers->FpgaID);
		cJSON_AddStringToObject(StatusNotificationRequestJSON, "eps32MACAddress", msg->serialNumbers->Esp32MACAddress);
	//	cJSON_AddStringToObject(StatusNotificationRequestJSON, "ethernetMACAddress", msg->serialNumbers->EthMACAddress);
		cJSON_AddStringToObject(StatusNotificationRequestJSON, "ec200MACAddress", msg->serialNumbers->EC200MACAddress);
		cJSON_AddStringToObject(StatusNotificationRequestJSON, "connectorId", msg->connectorId);
		cJSON_AddStringToObject(StatusNotificationRequestJSON, "errorCode", msg->errorCode);
		cJSON_AddStringToObject(StatusNotificationRequestJSON, "status", msg->status);
		cJSON_AddStringToObject(StatusNotificationRequestJSON, "timestamp", msg->timeStamp);

		/* Create the final Payload string by converting the JSON object into string format */
		StatusNotificationRequestStr = cJSON_PrintUnformatted(StatusNotificationRequestJSON);

		/* Once Payload final string is generated then free the JSON object memory */
		cJSON_Delete(StatusNotificationRequestJSON);

		/* Check if device is Online */
		if (!g_isOCPPServerConnected) 
		{
			xil_printf("StatusNotificationRequestStr : %s\r\n", StatusNotificationRequestStr);

			xil_printf("StatusNotificationRequest Device offline, Saving msg into memory as general\r\n");
			retVal = flashAddMsg(FILETYPE_GENERAL_STATUS,g_CurrentSessionId,StatusNotificationRequestStr);
			if(retVal == XST_FAILURE)
			{
				xil_printf("StatusNotificationRequest Saving msg into memory failed, Error %d\r\n",retVal);
			}
		
				free(StatusNotificationRequestStr);
			return XST_NO_ACCESS;
		}
	}
	else if(msgtype == MSG_TYPE_OFFLINE)
	{
		/* If Payload message is NULL then return failure to calling API */
		if(!offlinemsg) {
			return XST_FAILURE;
		}

		StatusNotificationRequestStr = offlinemsg;
	}
	else
	{
		xil_printf("Invalid msg type value : %d\r\n", msgtype);
		return XST_FAILURE;
	}

	xil_printf("StatusNotificationRequestStr : %s\r\n", StatusNotificationRequestStr);

	/* Calculate the Payload message length using the final created Payload string */
	payloadLength = strlen(StatusNotificationRequestStr);

	/* Create the HTTP POST AT command string */
	snprintf(HTTPCommandStr, BUFFER_SIZE, "AT+HTTPCPOST=\"%s\",%d,1,\"content-type: application/json\"", OCPP_MSG_STATUS_NOTIFICATION_URL, payloadLength);

	xil_printf("StatusNotificationRequest Message command : %s\r\n", HTTPCommandStr);

	// Wait for the AT mutex
	if (xSemaphoreTake(xATMutex, portMAX_DELAY) == pdTRUE) {
		/* Send the AT command to the ESP32 module and collect its response */
		retVal = SendUartATCommandWithResponse(HTTPCommandStr, 60, 1000, ResponseBufferStr, BUFFER_SIZE);

		if (retVal == XST_SUCCESS) {
			/* If HTTP POST AT command response is valid then proceed further */
			if(strstr(ResponseBufferStr, ">") != NULL) {
				/* Reset the response buffer to collect the next command response */
				memset(ResponseBufferStr, 0, BUFFER_SIZE);

				/* Send the Paylod string to the ESP32 module and collect its response */
				retVal = SendUartATCommandWithResponse(StatusNotificationRequestStr, 60, 3000, ResponseBufferStr, BUFFER_SIZE);

				if (retVal == XST_SUCCESS) {
					/* If Payload final string send part completed then free that resource as well */
					free(StatusNotificationRequestStr);

					/* If Payload command response is valid then proceed further */
					if (strstr(ResponseBufferStr, "SEND OK") != NULL) {
						/* ============= Payload server response handling started here ===================*/
						/* Here in OCPP Status Notification Request message response not contains any informative data
						 * so no need to process the response buffer */
						xil_printf("OCPP Status Notification message sent successfully!\r\n");
						/* ============= Payload server response handling exited here ===================*/
					}
					else {
						/* Received the invalid response for the Payload string command */
						xil_printf("Received the invalid response : %s\r\n", ResponseBufferStr);

						// Release the AT mutex
						xSemaphoreGive(xATMutex);

						/* Return the failure status in case of the invalid response of Payload string response */
						return XST_FAILURE;
					}
				}
				else {
					/* Get the Timeout as Payload string response */
					xil_printf("Timeout occurred : %s\r\n", StatusNotificationRequestStr);

					/* Save into Memory as Sending failed */
					xil_printf("StatusNotificationRequest Device offline, Saving msg into memory as general\r\n");
					retVal = flashAddMsg(FILETYPE_GENERAL_STATUS,g_CurrentSessionId,StatusNotificationRequestStr);
					if(retVal == XST_FAILURE)
					{
						xil_printf("StatusNotificationRequest Saving msg into memory failed, Error %d\r\n",retVal);
					}

					// Free allocated resources
					free(StatusNotificationRequestStr);

					// Release the AT mutex
					xSemaphoreGive(xATMutex);

					/* Return the Time out status in case of Payload string response time out occurred */
					return XST_TIMEOUT;
				}
			}
			else {
				/* Received the invalid response for the HTTP POST AT command */
				xil_printf("Received the invalid response : %s\r\n", ResponseBufferStr);

				/* Save into Memory as Sending failed */
				xil_printf("StatusNotificationRequest Device offline, Saving msg into memory as general\r\n");
				retVal = flashAddMsg(FILETYPE_GENERAL_STATUS,g_CurrentSessionId,StatusNotificationRequestStr);
				if(retVal == XST_FAILURE)
				{
					xil_printf("StatusNotificationRequest Saving msg into memory failed, Error %d\r\n",retVal);
				}

				// Free allocated resources
				free(StatusNotificationRequestStr);

				// Release the AT mutex
				xSemaphoreGive(xATMutex);

				/* Return the failure status in case of the invalid response of AT command */
				return XST_FAILURE;
			}
		}
		else {
			/* Get the Timeout as AT Command response */
			xil_printf("Timeout occurred : %s\r\n", HTTPCommandStr);

			/* Save into Memory as Sending failed */
			xil_printf("StatusNotificationRequest Device offline, Saving msg into memory as general\r\n");
			retVal = flashAddMsg(FILETYPE_GENERAL_STATUS,g_CurrentSessionId,StatusNotificationRequestStr);
			if(retVal == XST_FAILURE)
			{
				xil_printf("StatusNotificationRequest Saving msg into memory failed, Error %d\r\n",retVal);
			}

			// Free allocated resources
			free(StatusNotificationRequestStr);

			// Release the AT mutex
			xSemaphoreGive(xATMutex);

			/* Return the Time out status in case of the AT command time out occurred */
			return XST_NO_ACCESS;
		}

		// Release the AT mutex
		xSemaphoreGive(xATMutex);
	}

	/* Everything is perfect then return SUCCESS */
	return XST_SUCCESS;
}

/**
 * @brief  Sends an Authorize message to the OCPP central server.
 */
u8 OCPPSendAuthorizeMessage(AuthorizeRequest_t *msg)
{
	u8 retVal;
	int payloadLength = 0;
	char HTTPCommandStr[BUFFER_SIZE] = {0};
	char ResponseBufferStr[BUFFER_SIZE] = {0};
	cJSON *AuthorizeRequestJSON = NULL;
	char *AuthorizeRequestStr = NULL;
	cJSON *AuthorizeResponseJSON = NULL;
	char *AuthorizeResponseStr = NULL;

	/* If Payload message is NULL then return failure to calling API */
	if(!msg) {
		return XST_FAILURE;
	}

	/* Create the JSON object for the Payload structure type */
	AuthorizeRequestJSON = cJSON_CreateObject();

	/* Update the created JSON object to fill its key values */
	cJSON_AddStringToObject(AuthorizeRequestJSON, "chargePointSerialNumber", msg->serialNumbers->EVSESerialNumber);
	cJSON_AddStringToObject(AuthorizeRequestJSON, "fpgaId", msg->serialNumbers->FpgaID);
	cJSON_AddStringToObject(AuthorizeRequestJSON, "eps32MACAddress", msg->serialNumbers->Esp32MACAddress);
//	cJSON_AddStringToObject(AuthorizeRequestJSON, "ethernetMACAddress", msg->serialNumbers->EthMACAddress);
	cJSON_AddStringToObject(AuthorizeRequestJSON, "ec200MACAddress", msg->serialNumbers->EC200MACAddress);
	cJSON_AddStringToObject(AuthorizeRequestJSON, "idTag", msg->idTag);

	/* Create the final Payload string by converting the JSON object into string format */
	AuthorizeRequestStr = cJSON_PrintUnformatted(AuthorizeRequestJSON);

	/* Once Payload final string is generated then free the JSON object memory */
	cJSON_Delete(AuthorizeRequestJSON);

	xil_printf("AuthorizeRequestStr : %s\r\n", AuthorizeRequestStr);

	/* Calculate the Payload message length using the final created Payload string */
	payloadLength = strlen(AuthorizeRequestStr);

	/* Create the HTTP POST AT command string */
	snprintf(HTTPCommandStr, BUFFER_SIZE, "AT+HTTPCPOST=\"%s\",%d,1,\"content-type: application/json\"", OCPP_MSG_AUTHORIZE_URL, payloadLength);

	xil_printf("AuthorizeRequest Message command : %s\r\n", HTTPCommandStr);

	// Wait for the AT mutex
	if (xSemaphoreTake(xATMutex, portMAX_DELAY) == pdTRUE) {
		/* Send the AT command to the ESP32 module and collect its response */
		retVal = SendUartATCommandWithResponse(HTTPCommandStr, 60, 1000, ResponseBufferStr, BUFFER_SIZE);

		if (retVal == XST_SUCCESS) {
			/* If HTTP POST AT command response is valid then proceed further */
			if(strstr(ResponseBufferStr, ">") != NULL) {
				/* Reset the response buffer to collect the next command response */
				memset(ResponseBufferStr, 0, BUFFER_SIZE);

				/* Send the Paylod string to the ESP32 module and collect its response */
				retVal = SendUartATCommandWithResponse(AuthorizeRequestStr, 60, 3000, ResponseBufferStr, BUFFER_SIZE);

				if (retVal == XST_SUCCESS) {
					/* If Payload final string send part completed then free that resource as well */
					free(AuthorizeRequestStr);

					/* If Payload command response is valid then proceed further */
					if (strstr(ResponseBufferStr, "SEND OK") != NULL) {
						/* ============= Payload server response handling started here ===================*/
						AuthorizeResponseStr = strchr(ResponseBufferStr, '{');

						if (!AuthorizeResponseStr) {
							xil_printf("No JSON string found in response!\r\n");

							// Release the AT mutex
							xSemaphoreGive(xATMutex);

							/* Return the failure status for Payload string response */
							return XST_FAILURE;
						}

						/* If JSON string is foudn in response */
						AuthorizeResponseJSON = cJSON_Parse(AuthorizeResponseStr);

						if (!AuthorizeResponseJSON) {
							xil_printf("Error in parsing response JSON!\r\n");

							// Release the AT mutex
							xSemaphoreGive(xATMutex);

							/* Return the failure status for response JSON parsing */
							return XST_FAILURE;
						}

						xil_printf("Response JSON parsed successfully!\r\n");

						cJSON * idTagInfoJSON = cJSON_GetObjectItem(AuthorizeResponseJSON, "idTagInfo");

						/* Extract the ID tag information */
						if (idTagInfoJSON && cJSON_IsObject(idTagInfoJSON)) {
							cJSON * statusJSON = cJSON_GetObjectItem(idTagInfoJSON, "status");
							cJSON * expiryDateJSON = cJSON_GetObjectItem(idTagInfoJSON, "expiryDate");
							cJSON * parentIdTagJSON = cJSON_GetObjectItem(idTagInfoJSON, "parentIdTag");

							/* Extract the status information */
							if (statusJSON && cJSON_IsString(statusJSON)) {
								strncpy(g_AuthorizeResponse.status,
										statusJSON->valuestring, sizeof(g_AuthorizeResponse.status));
								xil_printf("g_AuthorizeResponse.status : %s\r\n", g_AuthorizeResponse.status);
							}

							/* Extract the Tag expiry date information */
							if (expiryDateJSON && cJSON_IsString(expiryDateJSON)) {
								strncpy(g_AuthorizeResponse.expiryDate,
										expiryDateJSON->valuestring, sizeof(g_AuthorizeResponse.expiryDate));
								xil_printf("g_AuthorizeResponse.expiryDate : %s\r\n", g_AuthorizeResponse.expiryDate);
							}

							/* Extract the Parent ID Tag information */
							if (parentIdTagJSON && cJSON_IsString(parentIdTagJSON)) {
								strncpy(g_AuthorizeResponse.parentIdTag,
										parentIdTagJSON->valuestring, sizeof(g_AuthorizeResponse.parentIdTag));
								xil_printf("g_AuthorizeResponse.parentIdTag : %s\r\n", g_AuthorizeResponse.parentIdTag);
							}
						}

						/* Free the allocated resources */
						cJSON_Delete(AuthorizeResponseJSON);

						/* ============= Payload server response handling exited here ===================*/
					}
					else {
						/* Received the invalid response for the Payload string command */
						xil_printf("Received the invalid response : %s\r\n", ResponseBufferStr);

						// Release the AT mutex
						xSemaphoreGive(xATMutex);

						/* Return the failure status in case of the invalid response of Payload string response */
						return XST_FAILURE;
					}
				}
				else {
					/* Get the Timeout as Payload string response */
					xil_printf("Timeout occurred : %s\r\n", AuthorizeRequestStr);

					// Free allocated resources
					free(AuthorizeRequestStr);

					// Release the AT mutex
					xSemaphoreGive(xATMutex);

					/* Return the Time out status in case of Payload string response time out occurred */
					return XST_TIMEOUT;
				}
			}
			else {
				/* Received the invalid response for the HTTP POST AT command */
				xil_printf("Received the invalid response : %s\r\n", ResponseBufferStr);

				// Free allocated resources
				free(AuthorizeRequestStr);

				// Release the AT mutex
				xSemaphoreGive(xATMutex);

				/* Return the failure status in case of the invalid response of AT command */
				return XST_FAILURE;
			}
		}
		else {
			/* Get the Timeout as AT Command response */
			xil_printf("Timeout occurred : %s\r\n", HTTPCommandStr);

			// Free allocated resources
			free(AuthorizeRequestStr);

			// Release the AT mutex
			xSemaphoreGive(xATMutex);

			/* Return the Time out status in case of the AT command time out occurred */
			return XST_TIMEOUT;
		}

		// Release the AT mutex
		xSemaphoreGive(xATMutex);
	}

	/* Everything is perfect then return SUCCESS */
	return XST_SUCCESS;
}

/**
 * @brief  Sends a Start Transaction message to the OCPP central server.
 */
u8 OCPPSendStartTransactionMessage(int msgtype, StartTransactionRequest_t *msg, char *offlinemsg)
{
	u8 retVal;
	int payloadLength = 0;
	char HTTPCommandStr[BUFFER_SIZE] = {0};
	char ResponseBufferStr[BUFFER_SIZE] = {0};
	cJSON *StartTransactionRequestJSON = NULL;
	char *StartTransactionRequestStr = NULL;
	cJSON *StartTransactionResponseJSON = NULL;
	char *StartTransactionResponseStr = NULL;
	int SessionID = 0;

	if(msgtype == MSG_TYPE_ONLINE)
	{
		/* If Payload message is NULL then return failure to calling API */
		if(!msg) {
			return XST_FAILURE;
		}

		/* Create the JSON object for the Payload structure type */
		StartTransactionRequestJSON = cJSON_CreateObject();

		/* Update the created JSON object to fill its key values */
		cJSON_AddStringToObject(StartTransactionRequestJSON, "chargePointSerialNumber", msg->serialNumbers->EVSESerialNumber);
		cJSON_AddStringToObject(StartTransactionRequestJSON, "fpgaId", msg->serialNumbers->FpgaID);
		cJSON_AddStringToObject(StartTransactionRequestJSON, "eps32MACAddress", msg->serialNumbers->Esp32MACAddress);
	//	cJSON_AddStringToObject(StartTransactionRequestJSON, "ethernetMACAddress", msg->serialNumbers->EthMACAddress);
		cJSON_AddStringToObject(StartTransactionRequestJSON, "ec200MACAddress", msg->serialNumbers->EC200MACAddress);
		cJSON_AddStringToObject(StartTransactionRequestJSON, "connectorId", msg->connectorId);
		cJSON_AddStringToObject(StartTransactionRequestJSON, "idTag", msg->idTag);
		cJSON_AddStringToObject(StartTransactionRequestJSON, "timestamp", msg->timeStamp);
		cJSON_AddNumberToObject(StartTransactionRequestJSON, "transactionId", msg->transactionId);
		cJSON_AddStringToObject(StartTransactionRequestJSON, "meterStart", msg->meterStart);

		/* Create the final Payload string by converting the JSON object into string format */
		StartTransactionRequestStr = cJSON_PrintUnformatted(StartTransactionRequestJSON);

		/* Once Payload final string is generated then free the JSON object memory */
		cJSON_Delete(StartTransactionRequestJSON);

		SessionID = g_CurrentSessionId;

		/* Check if device is Online */
		if (!g_isOCPPServerConnected) 
		{	
			xil_printf("StartTransactionRequestStr : %s\r\n", StartTransactionRequestStr);
			xil_printf("StartTransactionRequest Device offline, Saving msg into memory with SessionId %d\r\n",SessionID);
			retVal = flashAddMsg(FILETYPE_TRANSACTION_START,SessionID,StartTransactionRequestStr);
			if(retVal == XST_FAILURE)
			{
				xil_printf("StartTransactionRequest Saving msg into memory failed, Error %d\r\n",retVal);
			}

			free(StartTransactionRequestStr);
			return XST_NO_ACCESS;
		}
	}
	else if(msgtype == MSG_TYPE_OFFLINE)
	{
		/* If Payload message is NULL then return failure to calling API */
		if(!offlinemsg) {
			return XST_FAILURE;
		}

		SessionID = g_OfflineSessionId;
		StartTransactionRequestStr = offlinemsg;
	}
	else
	{
		xil_printf("Invalid msg type value : %d\r\n", msgtype);
		return XST_FAILURE;
	}

	xil_printf("StartTransactionRequestStr : %s\r\n", StartTransactionRequestStr);

	/* Calculate the Payload message length using the final created Payload string */
	payloadLength = strlen(StartTransactionRequestStr);

	/* Create the HTTP POST AT command string */
	snprintf(HTTPCommandStr, BUFFER_SIZE, "AT+HTTPCPOST=\"%s\",%d,1,\"content-type: application/json\"", OCPP_MSG_START_TRANSACTION_URL, payloadLength);

	xil_printf("StartTransactionRequest Message command : %s\r\n", HTTPCommandStr);

	// Wait for the AT mutex
	if (xSemaphoreTake(xATMutex, portMAX_DELAY) == pdTRUE) {
		/* Send the AT command to the ESP32 module and collect its response */
		retVal = SendUartATCommandWithResponse(HTTPCommandStr, 60, 1000, ResponseBufferStr, BUFFER_SIZE);

		if (retVal == XST_SUCCESS) {
			/* If HTTP POST AT command response is valid then proceed further */
			if(strstr(ResponseBufferStr, ">") != NULL) {
				/* Reset the response buffer to collect the next command response */
				memset(ResponseBufferStr, 0, BUFFER_SIZE);

				/* Send the Paylod string to the ESP32 module and collect its response */
				retVal = SendUartATCommandWithResponse(StartTransactionRequestStr, 60, 3000, ResponseBufferStr, BUFFER_SIZE);

				if (retVal == XST_SUCCESS) {
					/* If Payload final string send part completed then free that resource as well */
					free(StartTransactionRequestStr);

					/* If Payload command response is valid then proceed further */
					if (strstr(ResponseBufferStr, "SEND OK") != NULL) {
						/* ============= Payload server response handling started here ===================*/
						StartTransactionResponseStr = strchr(ResponseBufferStr, '{');

						if (!StartTransactionResponseStr) {
							xil_printf("No JSON string found in response!\r\n");

							// Release the AT mutex
							xSemaphoreGive(xATMutex);

							/* Return the failure status for Payload string response */
							return XST_FAILURE;
						}

						/* If JSON string is foudn in response */
						StartTransactionResponseJSON = cJSON_Parse(StartTransactionResponseStr);

						if (!StartTransactionResponseJSON) {
							xil_printf("Error in parsing response JSON!\r\n");

							// Release the AT mutex
							xSemaphoreGive(xATMutex);

							/* Return the failure status for response JSON parsing */
							return XST_FAILURE;
						}

						xil_printf("Response JSON parsed successfully!\r\n");

						cJSON * transactionIdJSON = cJSON_GetObjectItem(StartTransactionResponseJSON, "transactionId");
						cJSON * idTagInfoJSON = cJSON_GetObjectItem(StartTransactionResponseJSON, "idTagInfo");

						/* Extract the Transaction ID information */
						if (transactionIdJSON && cJSON_IsNumber(transactionIdJSON)) {
							g_StartTransactionResponse.transactionId = transactionIdJSON->valueint;
							xil_printf("g_StartTransactionResponse.transactionId : %d\r\n", g_StartTransactionResponse.transactionId);
						}

						/* Extract the ID tag information */
						if (idTagInfoJSON && cJSON_IsObject(idTagInfoJSON)) {
							cJSON * statusJSON = cJSON_GetObjectItem(idTagInfoJSON, "status");

							/* Extract the status information */
							if (statusJSON && cJSON_IsString(statusJSON)) {
								strncpy(g_StartTransactionResponse.idTagInfoStatus,
										statusJSON->valuestring, sizeof(g_StartTransactionResponse.idTagInfoStatus));
								xil_printf("g_StartTransactionResponse.idTagInfoStatus : %s\r\n", g_StartTransactionResponse.idTagInfoStatus);
							}
						}

						/* Free the allocated resources */
						cJSON_Delete(StartTransactionResponseJSON);

						/* ============= Payload server response handling exited here ===================*/
					}
					else {
						/* Received the invalid response for the Payload string command */
						xil_printf("Received the invalid response : %s\r\n", ResponseBufferStr);

						// Release the AT mutex
						xSemaphoreGive(xATMutex);

						/* Return the failure status in case of the invalid response of Payload string response */
						return XST_FAILURE;
					}
				}
				else {
					/* Get the Timeout as Payload string response */
					xil_printf("Timeout occurred : %s\r\n", StartTransactionRequestStr);

					/* Save into Memory as Sending failed */
					xil_printf("StartTransactionRequest Device offline, Saving msg into memory with SessionId %d\r\n",SessionID);
					retVal = flashAddMsg(FILETYPE_TRANSACTION_START,SessionID,StartTransactionRequestStr);
					if(retVal == XST_FAILURE)
					{
						xil_printf("StartTransactionRequest Saving msg into memory failed, Error %d\r\n",retVal);
					}

					// Free allocated resources
					free(StartTransactionRequestStr);

					// Release the AT mutex
					xSemaphoreGive(xATMutex);

					/* Return the Time out status in case of Payload string response time out occurred */
					return XST_NO_ACCESS;
				}
			}
			else {
				/* Received the invalid response for the HTTP POST AT command */
				xil_printf("Received the invalid response : %s\r\n", ResponseBufferStr);

				/* Save into Memory as Sending failed */
				xil_printf("StartTransactionRequest Device offline, Saving msg into memory with SessionId %d\r\n",SessionID);
				retVal = flashAddMsg(FILETYPE_TRANSACTION_START,SessionID,StartTransactionRequestStr);
				if(retVal == XST_FAILURE)
				{
					xil_printf("StartTransactionRequest Saving msg into memory failed, Error %d\r\n",retVal);
				}

				// Free allocated resources
				free(StartTransactionRequestStr);

				// Release the AT mutex
				xSemaphoreGive(xATMutex);

				/* Return the failure status in case of the invalid response of AT command */
				return XST_NO_ACCESS;
			}
		}
		else {
			/* Get the Timeout as AT Command response */
			xil_printf("Timeout occurred : %s\r\n", HTTPCommandStr);

			/* Save into Memory as Sending failed */
			xil_printf("StartTransactionRequest Device offline, Saving msg into memory with SessionId %d\r\n",SessionID);
			retVal = flashAddMsg(FILETYPE_TRANSACTION_START,SessionID,StartTransactionRequestStr);
			if(retVal == XST_FAILURE)
			{
				xil_printf("StartTransactionRequest Saving msg into memory failed, Error %d\r\n",retVal);
			}

			// Free allocated resources
			free(StartTransactionRequestStr);

			// Release the AT mutex
			xSemaphoreGive(xATMutex);

			/* Return the Time out status in case of the AT command time out occurred */
			return XST_NO_ACCESS;
		}

		// Release the AT mutex
		xSemaphoreGive(xATMutex);
	}

	/* Everything is perfect then return SUCCESS */
	return XST_SUCCESS;
}

/**
 * @brief  Sends a Stop Transaction message to the OCPP central server.
 */
u8 OCPPSendStopTransactionMessage(int msgtype, StopTransactionRequest_t *msg, char *offlinemsg)
{
	u8 retVal;
	int payloadLength = 0;
	char HTTPCommandStr[BUFFER_SIZE] = {0};
	char ResponseBufferStr[BUFFER_SIZE] = {0};
	cJSON *StopTransactionRequestJSON = NULL;
	char *StopTransactionRequestStr = NULL;
	cJSON *StopTransactionResponseJSON = NULL;
	char *StopTransactionResponseStr = NULL;
	int SessionID = 0;

	if(msgtype == MSG_TYPE_ONLINE)
	{
		/* If Payload message is NULL then return failure to calling API */
		if(!msg) {
			return XST_FAILURE;
		}

		/* Create the JSON object for the Payload structure type */
		StopTransactionRequestJSON = cJSON_CreateObject();

		/* Update the created JSON object to fill its key values */
		cJSON_AddStringToObject(StopTransactionRequestJSON, "chargePointSerialNumber", msg->serialNumbers->EVSESerialNumber);
		cJSON_AddStringToObject(StopTransactionRequestJSON, "fpgaId", msg->serialNumbers->FpgaID);
		cJSON_AddStringToObject(StopTransactionRequestJSON, "eps32MACAddress", msg->serialNumbers->Esp32MACAddress);
	//	cJSON_AddStringToObject(StopTransactionRequestJSON, "ethernetMACAddress", msg->serialNumbers->EthMACAddress);
		cJSON_AddStringToObject(StopTransactionRequestJSON, "ec200MACAddress", msg->serialNumbers->EC200MACAddress);
		cJSON_AddNumberToObject(StopTransactionRequestJSON, "transactionId", msg->transactionId);
		cJSON_AddStringToObject(StopTransactionRequestJSON, "idTag", msg->idTag);
		cJSON_AddStringToObject(StopTransactionRequestJSON, "timestamp", msg->timeStamp);
		cJSON_AddStringToObject(StopTransactionRequestJSON, "meterStop", msg->meterStop);
		cJSON_AddStringToObject(StopTransactionRequestJSON, "reason", msg->reason);

		/* Create the final Payload string by converting the JSON object into string format */
		StopTransactionRequestStr = cJSON_PrintUnformatted(StopTransactionRequestJSON);

		/* Once Payload final string is generated then free the JSON object memory */
		cJSON_Delete(StopTransactionRequestJSON);

		SessionID = g_CurrentSessionId;

		/* Check if device is Online */
		if (!g_isOCPPServerConnected) 
		{
			xil_printf("StopTransactionRequestStr : %s\r\n", StopTransactionRequestStr);
			xil_printf("StopTransactionRequest Device offline, Saving msg into memory with SessionId %d\r\n",SessionID);
			retVal = flashAddMsg(FILETYPE_TRANSACTION_STOP,SessionID,StopTransactionRequestStr);
			if(retVal == XST_FAILURE)
			{
				xil_printf("StopTransactionRequest Saving msg into memory failed, Error %d\r\n",retVal);
			}

			free(StopTransactionRequestStr);

			/* Return No Access error to indicate connectivity issue */
			return XST_NO_ACCESS;
		}
	}
	else if(msgtype == MSG_TYPE_OFFLINE)
	{
		/* If Payload message is NULL then return failure to calling API */
		if(!offlinemsg) {
			return XST_FAILURE;
		}

		SessionID = g_OfflineSessionId;
		StopTransactionRequestStr = offlinemsg;
	}
	else
	{
		xil_printf("Invalid msg type value : %d\r\n", msgtype);
		return XST_FAILURE;
	}

	xil_printf("StopTransactionRequestStr : %s\r\n", StopTransactionRequestStr);

	/* Calculate the Payload message length using the final created Payload string */
	payloadLength = strlen(StopTransactionRequestStr);

	/* Create the HTTP POST AT command string */
	snprintf(HTTPCommandStr, BUFFER_SIZE, "AT+HTTPCPOST=\"%s\",%d,1,\"content-type: application/json\"", OCPP_MSG_STOP_TRANSACTION_URL, payloadLength);

	xil_printf("StopTransactionRequest Message command : %s\r\n", HTTPCommandStr);

	// Wait for the AT mutex
	if (xSemaphoreTake(xATMutex, portMAX_DELAY) == pdTRUE) {
		/* Send the AT command to the ESP32 module and collect its response */
		retVal = SendUartATCommandWithResponse(HTTPCommandStr, 60, 1000, ResponseBufferStr, BUFFER_SIZE);

		if (retVal == XST_SUCCESS) {
			/* If HTTP POST AT command response is valid then proceed further */
			if(strstr(ResponseBufferStr, ">") != NULL) {
				/* Reset the response buffer to collect the next command response */
				memset(ResponseBufferStr, 0, BUFFER_SIZE);

				/* Send the Paylod string to the ESP32 module and collect its response */
				retVal = SendUartATCommandWithResponse(StopTransactionRequestStr, 60, 3000, ResponseBufferStr, BUFFER_SIZE);

				if (retVal == XST_SUCCESS) {
					/* If Payload final string send part completed then free that resource as well */
					free(StopTransactionRequestStr);

					/* If Payload command response is valid then proceed further */
					if (strstr(ResponseBufferStr, "SEND OK") != NULL) {
						/* ============= Payload server response handling started here ===================*/
						StopTransactionResponseStr = strchr(ResponseBufferStr, '{');

						if (!StopTransactionResponseStr) {
							xil_printf("No JSON string found in response!\r\n");

							// Release the AT mutex
							xSemaphoreGive(xATMutex);

							/* Return the failure status for Payload string response */
							return XST_FAILURE;
						}

						/* If JSON string is foudn in response */
						StopTransactionResponseJSON = cJSON_Parse(StopTransactionResponseStr);

						if (!StopTransactionResponseJSON) {
							xil_printf("Error in parsing response JSON!\r\n");

							// Release the AT mutex
							xSemaphoreGive(xATMutex);

							/* Return the failure status for response JSON parsing */
							return XST_FAILURE;
						}

						xil_printf("Response JSON parsed successfully!\r\n");

						cJSON * statusJSON = cJSON_GetObjectItem(StopTransactionResponseJSON, "status");

						/* Extract the status information */
						if (statusJSON && cJSON_IsString(statusJSON)) {
							strncpy(g_StopTransactionResponse.status,
									statusJSON->valuestring, sizeof(g_StopTransactionResponse.status));
							xil_printf("g_StopTransactionResponse.status : %s\r\n", g_StopTransactionResponse.status);
						}

						/* Free the allocated resources */
						cJSON_Delete(StopTransactionResponseJSON);

						/* ============= Payload server response handling exited here ===================*/
					}
					else {
						/* Received the invalid response for the Payload string command */
						xil_printf("Received the invalid response : %s\r\n", ResponseBufferStr);

						// Release the AT mutex
						xSemaphoreGive(xATMutex);

						/* Return the failure status in case of the invalid response of Payload string response */
						return XST_FAILURE;
					}
				}
				else {
					/* Get the Timeout as Payload string response */
					xil_printf("Timeout occurred : %s\r\n", StopTransactionRequestStr);

					/* Save into Memory as Sending failed */
					xil_printf("StopTransactionRequest Device offline, Saving msg into memory with SessionId %d\r\n",SessionID);
					retVal = flashAddMsg(FILETYPE_TRANSACTION_STOP,SessionID,StopTransactionRequestStr);
					if(retVal == XST_FAILURE)
					{
						xil_printf("StopTransactionRequest Saving msg into memory failed, Error %d\r\n",retVal);
					}

					// Free allocated resources
					free(StopTransactionRequestStr);

					// Release the AT mutex
					xSemaphoreGive(xATMutex);

					/* Return the Time out status in case of Payload string response time out occurred */
					/* Return No Access error to indicate connectivity issue */
					return XST_NO_ACCESS;
				}
			}
			else {
				/* Received the invalid response for the HTTP POST AT command */
				xil_printf("Received the invalid response : %s\r\n", ResponseBufferStr);

				/* Save into Memory as Sending failed */
				xil_printf("StopTransactionRequest Device offline, Saving msg into memory with SessionId %d\r\n",SessionID);
				retVal = flashAddMsg(FILETYPE_TRANSACTION_STOP,SessionID,StopTransactionRequestStr);
				if(retVal == XST_FAILURE)
				{
					xil_printf("StopTransactionRequest Saving msg into memory failed, Error %d\r\n",retVal);
				}

				// Free allocated resources
				free(StopTransactionRequestStr);

				// Release the AT mutex
				xSemaphoreGive(xATMutex);

				/* Return the failure status in case of the invalid response of AT command */
				return XST_NO_ACCESS;
			}
		}
		else {
			/* Get the Timeout as AT Command response */
			xil_printf("Timeout occurred : %s\r\n", HTTPCommandStr);

			/* Save into Memory as Sending failed */
			xil_printf("StopTransactionRequest Device offline, Saving msg into memory with SessionId %d\r\n",SessionID);
			retVal = flashAddMsg(FILETYPE_TRANSACTION_STOP,SessionID,StopTransactionRequestStr);
			if(retVal == XST_FAILURE)
			{
				xil_printf("StopTransactionRequest Saving msg into memory failed, Error %d\r\n",retVal);
			}

			// Free allocated resources
			free(StopTransactionRequestStr);

			// Release the AT mutex
			xSemaphoreGive(xATMutex);

			/* Return the Time out status in case of the AT command time out occurred */
			return XST_NO_ACCESS;
		}

		// Release the AT mutex
		xSemaphoreGive(xATMutex);
	}

	/* Everything is perfect then return SUCCESS */
	return XST_SUCCESS;
}

/**
 * @brief  Sends a Meter Values message to the OCPP central server.
 */
u8 OCPPSendMeterValuesMessage(int netmsgtype, MeterValuesMessageType_e msgType, MeterValuesRequest_t *msg, char *offlinemsgstr1,char *offlinemsgstr2,char *offlinemsgstr3)
{
	u8 retVal;
	int payloadLength = 0;
	char HTTPCommandStr[BUFFER_SIZE] = {0};
	char ResponseBufferStr[BUFFER_SIZE] = {0};
	cJSON *MeterValuesRequestJSON = NULL;
	char *MeterValuesRequestStr1 = NULL;
	char *MeterValuesRequestStr2 = NULL;
	char *MeterValuesRequestStr3 = NULL;
	int SessionID = 0;

	if(netmsgtype == MSG_TYPE_ONLINE)
	{
		/* If Payload message is NULL then return failure to calling API */
		if(!msg) {
			return XST_FAILURE;
		}

		/* ========================  Chunk 1 string creation ===============================*/

		/* Create the JSON object for the Payload structure type */
		MeterValuesRequestJSON = cJSON_CreateObject();

		/* Update the created JSON object to fill its key values */
		cJSON_AddNumberToObject(MeterValuesRequestJSON, "messageChunkNumber", 1);
		cJSON_AddStringToObject(MeterValuesRequestJSON, "chargePointSerialNumber", msg->serialNumbers->EVSESerialNumber);
		cJSON_AddStringToObject(MeterValuesRequestJSON, "fpgaId", msg->serialNumbers->FpgaID);
		cJSON_AddStringToObject(MeterValuesRequestJSON, "eps32MACAddress", msg->serialNumbers->Esp32MACAddress);
//		cJSON_AddStringToObject(MeterValuesRequestJSON, "ethernetMACAddress", msg->serialNumbers->EthMACAddress);
		cJSON_AddStringToObject(MeterValuesRequestJSON, "ec200MACAddress", msg->serialNumbers->EC200MACAddress);
		cJSON_AddStringToObject(MeterValuesRequestJSON, "connectorId", msg->connectorId);
		cJSON_AddNumberToObject(MeterValuesRequestJSON, "transactionId", msg->transactionId);
		cJSON_AddStringToObject(MeterValuesRequestJSON, "timestamp", msg->timeStamp);

		/* Create the final Payload string by converting the JSON object into string format */
		MeterValuesRequestStr1 = cJSON_PrintUnformatted(MeterValuesRequestJSON);

		/* Once Payload final string is generated then free the JSON object memory */
		cJSON_Delete(MeterValuesRequestJSON);

		/* ========================  Chunk 2 string creation ===============================*/

		/* Create the JSON object for the Payload structure type */
		MeterValuesRequestJSON = cJSON_CreateObject();

		/* Update the created JSON object to fill its key values */
		cJSON_AddNumberToObject(MeterValuesRequestJSON, "messageChunkNumber", 2);

		/* Create the Meter Value array */
		cJSON *MeterValueArray = cJSON_CreateArray();
		cJSON *MeterValueObject = cJSON_CreateObject();

		/* Add timestamp to Meter Value Object */
		cJSON_AddStringToObject(MeterValueObject, "timestamp", msg->timeStamp);

		/* Create the Sampled Value array */
		cJSON *SampledValueArray = cJSON_CreateArray();

		/* Add first Energy information data */
		cJSON *EnergyDataJSON = cJSON_CreateObject();
		cJSON_AddStringToObject(EnergyDataJSON, "value", msg->energyData.value);
		cJSON_AddStringToObject(EnergyDataJSON, "measurand", msg->energyData.measurand);
		cJSON_AddStringToObject(EnergyDataJSON, "unit", msg->energyData.unit);
		cJSON_AddItemToArray(SampledValueArray, EnergyDataJSON);

		/* If we are creating the Meter Values message for the charging progress update type then include Voltage information as well */
		if (msgType == METER_VALUES_PROGRESS_UPDATE) {
			/* Add second Voltage information data */
			cJSON *VoltageDataJSON = cJSON_CreateObject();
			cJSON_AddStringToObject(VoltageDataJSON, "value", msg->voltageData.value);
			cJSON_AddStringToObject(VoltageDataJSON, "measurand", msg->voltageData.measurand);
			cJSON_AddStringToObject(VoltageDataJSON, "unit", msg->voltageData.unit);
			cJSON_AddItemToArray(SampledValueArray, VoltageDataJSON);
		}

		/* Add Sampled Value array to Meter Value Object */
		cJSON_AddItemToObject(MeterValueObject, "sampledValue", SampledValueArray);

		/* Add Meter Value Object to Meter Value array */
		cJSON_AddItemToArray(MeterValueArray, MeterValueObject);

		/* Add Meter Value array to Meter Value Request JSON object */
		cJSON_AddItemToObject(MeterValuesRequestJSON, "meterValue", MeterValueArray);

		/* Create the final Payload string by converting the JSON object into string format */
		MeterValuesRequestStr2 = cJSON_PrintUnformatted(MeterValuesRequestJSON);

		/* Once Payload final string is generated then free the JSON object memory */
		cJSON_Delete(MeterValuesRequestJSON);

		/* ========================  Chunk 3 string creation ===============================*/

		/* If we are creating the Meter Values message for the charging progress update type then include Current information as well */
		if (msgType == METER_VALUES_PROGRESS_UPDATE) {
			/* Create the JSON object for the Payload structure type */
			MeterValuesRequestJSON = cJSON_CreateObject();

			/* Update the created JSON object to fill its key values */
			cJSON_AddNumberToObject(MeterValuesRequestJSON, "messageChunkNumber", 3);

			/* Create the Meter Value array */
			cJSON *MeterValueArray = cJSON_CreateArray();
			cJSON *MeterValueObject = cJSON_CreateObject();

			/* Add timestamp to Meter Value Object */
			cJSON_AddStringToObject(MeterValueObject, "timestamp", msg->timeStamp);

			/* Create the Sampled Value array */
			cJSON *SampledValueArray = cJSON_CreateArray();

			/* Add Current information data */
			cJSON *CurrentDataJSON = cJSON_CreateObject();
			cJSON_AddStringToObject(CurrentDataJSON, "value", msg->currentData.value);
			cJSON_AddStringToObject(CurrentDataJSON, "measurand", msg->currentData.measurand);
			cJSON_AddStringToObject(CurrentDataJSON, "unit", msg->currentData.unit);
			cJSON_AddItemToArray(SampledValueArray, CurrentDataJSON);

			/* Add Sampled Value array to Meter Value Object */
			cJSON_AddItemToObject(MeterValueObject, "sampledValue", SampledValueArray);

			/* Add Meter Value Object to Meter Value array */
			cJSON_AddItemToArray(MeterValueArray, MeterValueObject);

			/* Add Meter Value array to Meter Value Request JSON object */
			cJSON_AddItemToObject(MeterValuesRequestJSON, "meterValue", MeterValueArray);

			/* Create the final Payload string by converting the JSON object into string format */
			MeterValuesRequestStr3 = cJSON_PrintUnformatted(MeterValuesRequestJSON);

			/* Once Payload final string is generated then free the JSON object memory */
			cJSON_Delete(MeterValuesRequestJSON);
		}

		SessionID = g_CurrentSessionId;

		/* Check if device is Online */
		if (!g_isOCPPServerConnected) 
		{
			xil_printf("MeterValuesRequestStr1 : %s\r\n", MeterValuesRequestStr1);
			xil_printf("MeterValuesRequestStr2 : %s\r\n", MeterValuesRequestStr2);
			xil_printf("MeterValuesRequestStr3 : %s\r\n", MeterValuesRequestStr3);

			xil_printf("MeterValuesRequestStr Device offline, Saving msg into memory with SessionId %d\r\n",SessionID);
			retVal = flashAddMsg((msgType == METER_VALUES_PROGRESS_UPDATE ? FILETYPE_METERVAL_PROG_UPDATE : FILETYPE_METERVAL_SUMMARY_UPDATE),SessionID,MeterValuesRequestStr1);
			if(retVal == XST_FAILURE)
			{
				xil_printf("MeterValuesRequest Saving msg 1 into memory failed, Error %d\r\n",retVal);
			}

			retVal = flashAddMsg((msgType == METER_VALUES_PROGRESS_UPDATE ? FILETYPE_METERVAL_PROG_UPDATE : FILETYPE_METERVAL_SUMMARY_UPDATE),SessionID,MeterValuesRequestStr2);
			if(retVal == XST_FAILURE)
			{
				xil_printf("MeterValuesRequest Saving msg 2 into memory failed, Error %d\r\n",retVal);
			}

			retVal = flashAddMsg((msgType == METER_VALUES_PROGRESS_UPDATE ? FILETYPE_METERVAL_PROG_UPDATE : FILETYPE_METERVAL_SUMMARY_UPDATE),SessionID,MeterValuesRequestStr3);
			if(retVal == XST_FAILURE)
			{
				xil_printf("MeterValuesRequest Saving msg 3 into memory failed, Error %d\r\n",retVal);
			}

			free(MeterValuesRequestStr1);
			free(MeterValuesRequestStr2);
			free(MeterValuesRequestStr3);

			/* Return No Access error to indicate connectivity issue */
			return XST_NO_ACCESS;
		}
	}
	else if(netmsgtype == MSG_TYPE_OFFLINE)
	{
		/* If Payload message is NULL then return failure to calling API */
		if(!offlinemsgstr1 || !offlinemsgstr2 || ((msgType == METER_VALUES_PROGRESS_UPDATE) && !offlinemsgstr3)) {
			return XST_FAILURE;
		}

		SessionID = g_OfflineSessionId;
		MeterValuesRequestStr1 = offlinemsgstr1;
		MeterValuesRequestStr2 = offlinemsgstr2;
		MeterValuesRequestStr3 = offlinemsgstr3;

	}
	else
	{
		xil_printf("Invalid netmsg type value : %d\r\n", netmsgtype);
		return XST_FAILURE;
	}

	xil_printf("MeterValuesRequestStr1 : %s\r\n", MeterValuesRequestStr1);
	xil_printf("MeterValuesRequestStr2 : %s\r\n", MeterValuesRequestStr2);
	xil_printf("MeterValuesRequestStr3 : %s\r\n", MeterValuesRequestStr3);

	// Wait for the AT mutex
	if (xSemaphoreTake(xATMutex, portMAX_DELAY) == pdTRUE) {
		/* ========================  Sending Chunk 1 string to backend server ===============================*/

		/* Calculate the Payload message length using the final created Payload string */
		payloadLength = strlen(MeterValuesRequestStr1);

		/* Create the HTTP POST AT command string */
		if (msgType == METER_VALUES_PROGRESS_UPDATE) {
			snprintf(HTTPCommandStr, BUFFER_SIZE, "AT+HTTPCPOST=\"%s\",%d,1,\"content-type: application/json\"", OCPP_MSG_METER_VALUES_PROG_UPDATE_URL, payloadLength);
		}
		else if (msgType == METER_VALUES_SUMMARY_UPDATE) {
			snprintf(HTTPCommandStr, BUFFER_SIZE, "AT+HTTPCPOST=\"%s\",%d,1,\"content-type: application/json\"", OCPP_MSG_METER_VALUES_SUMM_UPDATE_URL, payloadLength);
		}

		xil_printf("MeterValuesRequest Message 1 command : %s\r\n", HTTPCommandStr);

		/* Send the AT command to the ESP32 module and collect its response */
		retVal = SendUartATCommandWithResponse(HTTPCommandStr, 60, 1000, ResponseBufferStr, BUFFER_SIZE);

		if (retVal == XST_TIMEOUT) {
			/* Get the Timeout as AT Command response */
			xil_printf("Timeout occurred : %s\r\n", HTTPCommandStr);

			xil_printf("MeterValuesRequestStr Device offline, Saving msg into memory with SessionId %d\r\n",SessionID);
			retVal = flashAddMsg((msgType == METER_VALUES_PROGRESS_UPDATE ? FILETYPE_METERVAL_PROG_UPDATE : FILETYPE_METERVAL_SUMMARY_UPDATE),SessionID,MeterValuesRequestStr1);
			if(retVal == XST_FAILURE)
			{
				xil_printf("MeterValuesRequest Saving msg 1 into memory failed, Error %d\r\n",retVal);
			}

			retVal = flashAddMsg((msgType == METER_VALUES_PROGRESS_UPDATE ? FILETYPE_METERVAL_PROG_UPDATE : FILETYPE_METERVAL_SUMMARY_UPDATE),SessionID,MeterValuesRequestStr2);
			if(retVal == XST_FAILURE)
			{
				xil_printf("MeterValuesRequest Saving msg 2 into memory failed, Error %d\r\n",retVal);
			}

			retVal = flashAddMsg((msgType == METER_VALUES_PROGRESS_UPDATE ? FILETYPE_METERVAL_PROG_UPDATE : FILETYPE_METERVAL_SUMMARY_UPDATE),SessionID,MeterValuesRequestStr3);
			if(retVal == XST_FAILURE)
			{
				xil_printf("MeterValuesRequest Saving msg 3 into memory failed, Error %d\r\n",retVal);
			}

			// Free allocated resources
			free(MeterValuesRequestStr1);
			free(MeterValuesRequestStr2);
			free(MeterValuesRequestStr3);

			// Release the AT mutex
			xSemaphoreGive(xATMutex);

			/* Return the Time out status in case of the AT command time out occurred */
			return XST_NO_ACCESS;
		}
		else if (retVal != XST_SUCCESS) {
			/* Received the invalid response for the HTTP POST AT command */
			xil_printf("Received the invalid response : %s\r\n", ResponseBufferStr);

			xil_printf("MeterValuesRequestStr Device offline, Saving msg into memory with SessionId %d\r\n",SessionID);
			retVal = flashAddMsg((msgType == METER_VALUES_PROGRESS_UPDATE ? FILETYPE_METERVAL_PROG_UPDATE : FILETYPE_METERVAL_SUMMARY_UPDATE),SessionID,MeterValuesRequestStr1);
			if(retVal == XST_FAILURE)
			{
				xil_printf("MeterValuesRequest Saving msg 1 into memory failed, Error %d\r\n",retVal);
			}

			retVal = flashAddMsg((msgType == METER_VALUES_PROGRESS_UPDATE ? FILETYPE_METERVAL_PROG_UPDATE : FILETYPE_METERVAL_SUMMARY_UPDATE),SessionID,MeterValuesRequestStr2);
			if(retVal == XST_FAILURE)
			{
				xil_printf("MeterValuesRequest Saving msg 2 into memory failed, Error %d\r\n",retVal);
			}

			retVal = flashAddMsg((msgType == METER_VALUES_PROGRESS_UPDATE ? FILETYPE_METERVAL_PROG_UPDATE : FILETYPE_METERVAL_SUMMARY_UPDATE),SessionID,MeterValuesRequestStr3);
			if(retVal == XST_FAILURE)
			{
				xil_printf("MeterValuesRequest Saving msg 3 into memory failed, Error %d\r\n",retVal);
			}

			// Free allocated resources
			free(MeterValuesRequestStr1);
			free(MeterValuesRequestStr2);
			free(MeterValuesRequestStr3);

			// Release the AT mutex
			xSemaphoreGive(xATMutex);

			/* Return the failure status in case of the invalid response of AT command */
			return XST_NO_ACCESS;
		}
		else if (retVal == XST_SUCCESS) {
			/* If HTTP POST AT command response is valid then proceed further */
			if(strstr(ResponseBufferStr, ">") != NULL) {
				/* Reset the response buffer to collect the next command response */
				memset(ResponseBufferStr, 0, BUFFER_SIZE);

				/* Send the Paylod string to the ESP32 module and collect its response */
				retVal = SendUartATCommandWithResponse(MeterValuesRequestStr1, 60, 3000, ResponseBufferStr, BUFFER_SIZE);

				if (retVal == XST_TIMEOUT) {
					/* Get the Timeout as Payload string response */
					xil_printf("Timeout occurred : %s\r\n", MeterValuesRequestStr1);

					xil_printf("MeterValuesRequestStr Device offline, Saving msg into memory with SessionId %d\r\n",SessionID);
					retVal = flashAddMsg((msgType == METER_VALUES_PROGRESS_UPDATE ? FILETYPE_METERVAL_PROG_UPDATE : FILETYPE_METERVAL_SUMMARY_UPDATE),SessionID,MeterValuesRequestStr1);
					if(retVal == XST_FAILURE)
					{
						xil_printf("MeterValuesRequest Saving msg 1 into memory failed, Error %d\r\n",retVal);
					}

					retVal = flashAddMsg((msgType == METER_VALUES_PROGRESS_UPDATE ? FILETYPE_METERVAL_PROG_UPDATE : FILETYPE_METERVAL_SUMMARY_UPDATE),SessionID,MeterValuesRequestStr2);
					if(retVal == XST_FAILURE)
					{
						xil_printf("MeterValuesRequest Saving msg 2 into memory failed, Error %d\r\n",retVal);
					}

					retVal = flashAddMsg((msgType == METER_VALUES_PROGRESS_UPDATE ? FILETYPE_METERVAL_PROG_UPDATE : FILETYPE_METERVAL_SUMMARY_UPDATE),SessionID,MeterValuesRequestStr3);
					if(retVal == XST_FAILURE)
					{
						xil_printf("MeterValuesRequest Saving msg 3 into memory failed, Error %d\r\n",retVal);
					}

					// Free allocated resources
					free(MeterValuesRequestStr1);
					free(MeterValuesRequestStr2);
					free(MeterValuesRequestStr3);

					// Release the AT mutex
					xSemaphoreGive(xATMutex);

					/* Return the Time out status in case of the AT command time out occurred */
					return XST_NO_ACCESS;
				}
				else if (retVal != XST_SUCCESS) {
					/* Received the invalid response for the Payload string command */
					xil_printf("Received the invalid response : %s\r\n", ResponseBufferStr);

					// Free allocated resources
					free(MeterValuesRequestStr1);
					free(MeterValuesRequestStr2);
					free(MeterValuesRequestStr3);

					// Release the AT mutex
					xSemaphoreGive(xATMutex);

					/* Return the failure status in case of the invalid response of AT command */
					return XST_FAILURE;
				}
				else if (retVal == XST_SUCCESS) {
					/* If Payload command response is valid then proceed further */
					if (strstr(ResponseBufferStr, "SEND OK") != NULL) {
						/* ============= Payload server response handling started here ===================*/
						/* Here in OCPP Meter Value Request message response not contains any informative data
						 * so no need to process the response buffer */
						xil_printf("OCPP Meter Values message 1 sent successfully!\r\n");
						/* ============= Payload server response handling exited here ===================*/
					}
				}
			}
		}

		/* ========================  Sending Chunk 2 string to backend server ===============================*/

		/* Reset the buffers and variables to handle the next chunks */
		memset(HTTPCommandStr, 0, BUFFER_SIZE);
		memset(ResponseBufferStr, 0, BUFFER_SIZE);
		payloadLength = 0;

		/* Calculate the Payload message length using the final created Payload string */
		payloadLength = strlen(MeterValuesRequestStr2);

		/* Create the HTTP POST AT command string */
		if (msgType == METER_VALUES_PROGRESS_UPDATE) {
			snprintf(HTTPCommandStr, BUFFER_SIZE, "AT+HTTPCPOST=\"%s\",%d,1,\"content-type: application/json\"", OCPP_MSG_METER_VALUES_PROG_UPDATE_URL, payloadLength);
		}
		else if (msgType == METER_VALUES_SUMMARY_UPDATE) {
			snprintf(HTTPCommandStr, BUFFER_SIZE, "AT+HTTPCPOST=\"%s\",%d,1,\"content-type: application/json\"", OCPP_MSG_METER_VALUES_SUMM_UPDATE_URL, payloadLength);
		}

		xil_printf("MeterValuesRequest Message 2 command : %s\r\n", HTTPCommandStr);

		/* Send the AT command to the ESP32 module and collect its response */
		retVal = SendUartATCommandWithResponse(HTTPCommandStr, 60, 1000, ResponseBufferStr, BUFFER_SIZE);

		if (retVal == XST_TIMEOUT) {
			/* Get the Timeout as AT Command response */
			xil_printf("Timeout occurred : %s\r\n", HTTPCommandStr);

			xil_printf("MeterValuesRequestStr Device offline, Saving msg into memory with SessionId %d\r\n",SessionID);
			retVal = flashAddMsg((msgType == METER_VALUES_PROGRESS_UPDATE ? FILETYPE_METERVAL_PROG_UPDATE : FILETYPE_METERVAL_SUMMARY_UPDATE),SessionID,MeterValuesRequestStr1);
			if(retVal == XST_FAILURE)
			{
				xil_printf("MeterValuesRequest Saving msg 1 into memory failed, Error %d\r\n",retVal);
			}

			retVal = flashAddMsg((msgType == METER_VALUES_PROGRESS_UPDATE ? FILETYPE_METERVAL_PROG_UPDATE : FILETYPE_METERVAL_SUMMARY_UPDATE),SessionID,MeterValuesRequestStr2);
			if(retVal == XST_FAILURE)
			{
				xil_printf("MeterValuesRequest Saving msg 2 into memory failed, Error %d\r\n",retVal);
			}

			retVal = flashAddMsg((msgType == METER_VALUES_PROGRESS_UPDATE ? FILETYPE_METERVAL_PROG_UPDATE : FILETYPE_METERVAL_SUMMARY_UPDATE),SessionID,MeterValuesRequestStr3);
			if(retVal == XST_FAILURE)
			{
				xil_printf("MeterValuesRequest Saving msg 3 into memory failed, Error %d\r\n",retVal);
			}

			// Free allocated resources
			free(MeterValuesRequestStr1);
			free(MeterValuesRequestStr2);
			free(MeterValuesRequestStr3);

			// Release the AT mutex
			xSemaphoreGive(xATMutex);

			/* Return the Time out status in case of the AT command time out occurred */
			return XST_NO_ACCESS;
		}
		else if (retVal != XST_SUCCESS) {
			/* Received the invalid response for the HTTP POST AT command */
			xil_printf("Received the invalid response : %s\r\n", ResponseBufferStr);

			xil_printf("MeterValuesRequestStr Device offline, Saving msg into memory with SessionId %d\r\n",SessionID);
			retVal = flashAddMsg((msgType == METER_VALUES_PROGRESS_UPDATE ? FILETYPE_METERVAL_PROG_UPDATE : FILETYPE_METERVAL_SUMMARY_UPDATE),SessionID,MeterValuesRequestStr1);
			if(retVal == XST_FAILURE)
			{
				xil_printf("MeterValuesRequest Saving msg 1 into memory failed, Error %d\r\n",retVal);
			}

			retVal = flashAddMsg((msgType == METER_VALUES_PROGRESS_UPDATE ? FILETYPE_METERVAL_PROG_UPDATE : FILETYPE_METERVAL_SUMMARY_UPDATE),SessionID,MeterValuesRequestStr2);
			if(retVal == XST_FAILURE)
			{
				xil_printf("MeterValuesRequest Saving msg 2 into memory failed, Error %d\r\n",retVal);
			}

			retVal = flashAddMsg((msgType == METER_VALUES_PROGRESS_UPDATE ? FILETYPE_METERVAL_PROG_UPDATE : FILETYPE_METERVAL_SUMMARY_UPDATE),SessionID,MeterValuesRequestStr3);
			if(retVal == XST_FAILURE)
			{
				xil_printf("MeterValuesRequest Saving msg 3 into memory failed, Error %d\r\n",retVal);
			}

			// Free allocated resources
			free(MeterValuesRequestStr1);
			free(MeterValuesRequestStr2);
			free(MeterValuesRequestStr3);

			// Release the AT mutex
			xSemaphoreGive(xATMutex);

			/* Return the failure status in case of the invalid response of AT command */
			return XST_NO_ACCESS;
		}
		else if (retVal == XST_SUCCESS) {
			/* If HTTP POST AT command response is valid then proceed further */
			if(strstr(ResponseBufferStr, ">") != NULL) {
				/* Reset the response buffer to collect the next command response */
				memset(ResponseBufferStr, 0, BUFFER_SIZE);

				/* Send the Paylod string to the ESP32 module and collect its response */
				retVal = SendUartATCommandWithResponse(MeterValuesRequestStr2, 60, 3000, ResponseBufferStr, BUFFER_SIZE);

				if (retVal == XST_TIMEOUT) {
					/* Get the Timeout as Payload string response */
					xil_printf("Timeout occurred : %s\r\n", MeterValuesRequestStr1);

					xil_printf("MeterValuesRequestStr Device offline, Saving msg into memory with SessionId %d\r\n",SessionID);
					retVal = flashAddMsg((msgType == METER_VALUES_PROGRESS_UPDATE ? FILETYPE_METERVAL_PROG_UPDATE : FILETYPE_METERVAL_SUMMARY_UPDATE),SessionID,MeterValuesRequestStr1);
					if(retVal == XST_FAILURE)
					{
						xil_printf("MeterValuesRequest Saving msg 1 into memory failed, Error %d\r\n",retVal);
					}

					retVal = flashAddMsg((msgType == METER_VALUES_PROGRESS_UPDATE ? FILETYPE_METERVAL_PROG_UPDATE : FILETYPE_METERVAL_SUMMARY_UPDATE),SessionID,MeterValuesRequestStr2);
					if(retVal == XST_FAILURE)
					{
						xil_printf("MeterValuesRequest Saving msg 2 into memory failed, Error %d\r\n",retVal);
					}

					retVal = flashAddMsg((msgType == METER_VALUES_PROGRESS_UPDATE ? FILETYPE_METERVAL_PROG_UPDATE : FILETYPE_METERVAL_SUMMARY_UPDATE),SessionID,MeterValuesRequestStr3);
					if(retVal == XST_FAILURE)
					{
						xil_printf("MeterValuesRequest Saving msg 3 into memory failed, Error %d\r\n",retVal);
					}

					// Free allocated resources
					free(MeterValuesRequestStr1);
					free(MeterValuesRequestStr2);
					free(MeterValuesRequestStr3);

					// Release the AT mutex
					xSemaphoreGive(xATMutex);

					/* Return the Time out status in case of the AT command time out occurred */
					return XST_NO_ACCESS;
				}
				else if (retVal != XST_SUCCESS) {
					/* Received the invalid response for the Payload string command */
					xil_printf("Received the invalid response : %s\r\n", ResponseBufferStr);

					// Free allocated resources
					free(MeterValuesRequestStr1);
					free(MeterValuesRequestStr2);
					free(MeterValuesRequestStr3);

					// Release the AT mutex
					xSemaphoreGive(xATMutex);

					/* Return the failure status in case of the invalid response of AT command */
					return XST_FAILURE;
				}
				else if (retVal == XST_SUCCESS) {
					/* If Payload command response is valid then proceed further */
					if (strstr(ResponseBufferStr, "SEND OK") != NULL) {
						/* ============= Payload server response handling started here ===================*/
						/* Here in OCPP Meter Value Request message response not contains any informative data
						 * so no need to process the response buffer */
						xil_printf("OCPP Meter Values message 2 sent successfully!\r\n");

						/* In case of message type is METER_VALUES_SUMMARY_UPDATE then it is the last string */
						if (msgType == METER_VALUES_SUMMARY_UPDATE) {
							// Free allocated resources
							free(MeterValuesRequestStr1);
							free(MeterValuesRequestStr2);
							free(MeterValuesRequestStr3);
						}

						/* ============= Payload server response handling exited here ===================*/
					}
				}
			}
		}

		/* If message type is METER_VALUES_PROGRESS_UPDATE then only send the 3rd chunk string */
		if (msgType == METER_VALUES_PROGRESS_UPDATE) {
			/* ========================  Sending Chunk 3 string to backend server ===============================*/

			/* Reset the buffers and variables to handle the next chunks */
			memset(HTTPCommandStr, 0, BUFFER_SIZE);
			memset(ResponseBufferStr, 0, BUFFER_SIZE);
			payloadLength = 0;

			/* Calculate the Payload message length using the final created Payload string */
			payloadLength = strlen(MeterValuesRequestStr3);

			/* Create the HTTP POST AT command string */
			if (msgType == METER_VALUES_PROGRESS_UPDATE) {
				snprintf(HTTPCommandStr, BUFFER_SIZE, "AT+HTTPCPOST=\"%s\",%d,1,\"content-type: application/json\"", OCPP_MSG_METER_VALUES_PROG_UPDATE_URL, payloadLength);
			}
			else if (msgType == METER_VALUES_SUMMARY_UPDATE) {
				snprintf(HTTPCommandStr, BUFFER_SIZE, "AT+HTTPCPOST=\"%s\",%d,1,\"content-type: application/json\"", OCPP_MSG_METER_VALUES_SUMM_UPDATE_URL, payloadLength);
			}

			xil_printf("MeterValuesRequest Message 3 command : %s\r\n", HTTPCommandStr);

			/* Send the AT command to the ESP32 module and collect its response */
			retVal = SendUartATCommandWithResponse(HTTPCommandStr, 60, 1000, ResponseBufferStr, BUFFER_SIZE);

			if (retVal == XST_TIMEOUT) {
				/* Get the Timeout as AT Command response */
				xil_printf("Timeout occurred : %s\r\n", HTTPCommandStr);

				xil_printf("MeterValuesRequestStr Device offline, Saving msg into memory with SessionId %d\r\n",SessionID);
				retVal = flashAddMsg((msgType == METER_VALUES_PROGRESS_UPDATE ? FILETYPE_METERVAL_PROG_UPDATE : FILETYPE_METERVAL_SUMMARY_UPDATE),SessionID,MeterValuesRequestStr1);
				if(retVal == XST_FAILURE)
				{
					xil_printf("MeterValuesRequest Saving msg 1 into memory failed, Error %d\r\n",retVal);
				}

				retVal = flashAddMsg((msgType == METER_VALUES_PROGRESS_UPDATE ? FILETYPE_METERVAL_PROG_UPDATE : FILETYPE_METERVAL_SUMMARY_UPDATE),SessionID,MeterValuesRequestStr2);
				if(retVal == XST_FAILURE)
				{
					xil_printf("MeterValuesRequest Saving msg 2 into memory failed, Error %d\r\n",retVal);
				}

				retVal = flashAddMsg((msgType == METER_VALUES_PROGRESS_UPDATE ? FILETYPE_METERVAL_PROG_UPDATE : FILETYPE_METERVAL_SUMMARY_UPDATE),SessionID,MeterValuesRequestStr3);
				if(retVal == XST_FAILURE)
				{
					xil_printf("MeterValuesRequest Saving msg 3 into memory failed, Error %d\r\n",retVal);
				}

				// Free allocated resources
				free(MeterValuesRequestStr1);
				free(MeterValuesRequestStr2);
				free(MeterValuesRequestStr3);

				// Release the AT mutex
				xSemaphoreGive(xATMutex);

				/* Return the Time out status in case of the AT command time out occurred */
				return XST_NO_ACCESS;
			}
			else if (retVal != XST_SUCCESS) {
				/* Received the invalid response for the HTTP POST AT command */
				xil_printf("Received the invalid response : %s\r\n", ResponseBufferStr);

				xil_printf("MeterValuesRequestStr Device offline, Saving msg into memory with SessionId %d\r\n",SessionID);
				retVal = flashAddMsg((msgType == METER_VALUES_PROGRESS_UPDATE ? FILETYPE_METERVAL_PROG_UPDATE : FILETYPE_METERVAL_SUMMARY_UPDATE),SessionID,MeterValuesRequestStr1);
				if(retVal == XST_FAILURE)
				{
					xil_printf("MeterValuesRequest Saving msg 1 into memory failed, Error %d\r\n",retVal);
				}

				retVal = flashAddMsg((msgType == METER_VALUES_PROGRESS_UPDATE ? FILETYPE_METERVAL_PROG_UPDATE : FILETYPE_METERVAL_SUMMARY_UPDATE),SessionID,MeterValuesRequestStr2);
				if(retVal == XST_FAILURE)
				{
					xil_printf("MeterValuesRequest Saving msg 2 into memory failed, Error %d\r\n",retVal);
				}

				retVal = flashAddMsg((msgType == METER_VALUES_PROGRESS_UPDATE ? FILETYPE_METERVAL_PROG_UPDATE : FILETYPE_METERVAL_SUMMARY_UPDATE),SessionID,MeterValuesRequestStr3);
				if(retVal == XST_FAILURE)
				{
					xil_printf("MeterValuesRequest Saving msg 3 into memory failed, Error %d\r\n",retVal);
				}

				// Free allocated resources
				free(MeterValuesRequestStr1);
				free(MeterValuesRequestStr2);
				free(MeterValuesRequestStr3);

				// Release the AT mutex
				xSemaphoreGive(xATMutex);

				/* Return the failure status in case of the invalid response of AT command */
				return XST_NO_ACCESS;
			}
			else if (retVal == XST_SUCCESS) {
				/* If HTTP POST AT command response is valid then proceed further */
				if(strstr(ResponseBufferStr, ">") != NULL) {
					/* Reset the response buffer to collect the next command response */
					memset(ResponseBufferStr, 0, BUFFER_SIZE);

					/* Send the Paylod string to the ESP32 module and collect its response */
					retVal = SendUartATCommandWithResponse(MeterValuesRequestStr3, 60, 3000, ResponseBufferStr, BUFFER_SIZE);

					if (retVal == XST_TIMEOUT) {
						/* Get the Timeout as Payload string response */
						xil_printf("Timeout occurred : %s\r\n", MeterValuesRequestStr1);

						xil_printf("MeterValuesRequestStr Device offline, Saving msg into memory with SessionId %d\r\n",SessionID);
						retVal = flashAddMsg((msgType == METER_VALUES_PROGRESS_UPDATE ? FILETYPE_METERVAL_PROG_UPDATE : FILETYPE_METERVAL_SUMMARY_UPDATE),SessionID,MeterValuesRequestStr1);
						if(retVal == XST_FAILURE)
						{
							xil_printf("MeterValuesRequest Saving msg 1 into memory failed, Error %d\r\n",retVal);
						}

						retVal = flashAddMsg((msgType == METER_VALUES_PROGRESS_UPDATE ? FILETYPE_METERVAL_PROG_UPDATE : FILETYPE_METERVAL_SUMMARY_UPDATE),SessionID,MeterValuesRequestStr2);
						if(retVal == XST_FAILURE)
						{
							xil_printf("MeterValuesRequest Saving msg 2 into memory failed, Error %d\r\n",retVal);
						}

						retVal = flashAddMsg((msgType == METER_VALUES_PROGRESS_UPDATE ? FILETYPE_METERVAL_PROG_UPDATE : FILETYPE_METERVAL_SUMMARY_UPDATE),SessionID,MeterValuesRequestStr3);
						if(retVal == XST_FAILURE)
						{
							xil_printf("MeterValuesRequest Saving msg 3 into memory failed, Error %d\r\n",retVal);
						}

						// Free allocated resources
						free(MeterValuesRequestStr1);
						free(MeterValuesRequestStr2);
						free(MeterValuesRequestStr3);

						// Release the AT mutex
						xSemaphoreGive(xATMutex);

						/* Return the Time out status in case of the AT command time out occurred */
						return XST_NO_ACCESS;
					}
					else if (retVal != XST_SUCCESS) {
						/* Received the invalid response for the Payload string command */
						xil_printf("Received the invalid response : %s\r\n", ResponseBufferStr);

						// Free allocated resources
						free(MeterValuesRequestStr1);
						free(MeterValuesRequestStr2);
						free(MeterValuesRequestStr3);

						// Release the AT mutex
						xSemaphoreGive(xATMutex);

						/* Return the failure status in case of the invalid response of AT command */
						return XST_FAILURE;
					}
					else if (retVal == XST_SUCCESS) {
						/* If Payload command response is valid then proceed further */
						if (strstr(ResponseBufferStr, "SEND OK") != NULL) {
							/* ============= Payload server response handling started here ===================*/
							/* Here in OCPP Meter Value Request message response not contains any informative data
							 * so no need to process the response buffer */
							xil_printf("OCPP Meter Values message 3 sent successfully!\r\n");

							// Free allocated resources
							free(MeterValuesRequestStr1);
							free(MeterValuesRequestStr2);
							free(MeterValuesRequestStr3);

							/* ============= Payload server response handling exited here ===================*/
						}
					}
				}
			}
		}

		// Release the AT mutex
		xSemaphoreGive(xATMutex);
	}

	/* Everything is perfect then return SUCCESS */
	return XST_SUCCESS;
}

/**
 * @brief  FreeRTOS task for handling OCPP communication.
 */
void prvOCPPTask( void *pvParameters )
{
	u8 retVal;

	/* Initialize UART with full configuration including interrupt setup */
	UartIntpInit(&IntcInstance, &UartInst, UART_DEVICE_ID, UART_IRPT_INTR);

	/* Create the timer as periodic for the OCPP connection retry */
	xOCPPTimer = xTimerCreate("OCPPTimer", OCPP_TIMER_PERIOD, pdTRUE, (void *)OCPP_TIMER_ID, vOCPPTimerCallback);
	if (xOCPPTimer != NULL) {
		xil_printf("OCPP Timer create successfully!\r\n");

		/* Check if the OCPP timer active or not */
		if( xTimerIsTimerActive( xOCPPTimer ) == pdFALSE ) {
			/* Start the OCPP timer */
			if (xTimerStart(xOCPPTimer, 0) != pdPASS) {
				/* If failed to start the timer */
				xil_printf("Failed to start OCPP Timer!\r\n");
			}
			else {
				/* If timer successfully started */
				xil_printf("OCPP Timer started!\r\n");
			}
		}
	}

	while(1)
	{
		xil_printf("\t\t\t###   %s   ###\t\t\t\r\n", __func__);

		if (g_isOtaInProgress) {
			/* Delay for 10 seconds  */
			vTaskDelay( INT_TO_TICKS( 10 * DELAY_1_SECOND ) );
		}

		if(g_isOCPPBootNotificationSent && g_networkConnectivity) {
			/* Send the OCPP Heart Beat Message to check the OCPP server connection */
			retVal = OCPPSendHeartBeatMessage();
			if (retVal == XST_SUCCESS) {
				/* Set the OCPP server connection flag */
				g_isOCPPServerConnected = TRUE;

				/* If Connectivity is restored */
				if(g_isOCPPServerConnectedPrevious == FALSE)
				{
					// Wait for the LCD mutex
					if (xSemaphoreTake(xLCDMutex, portMAX_DELAY) == pdTRUE)
					{
						/* Print the details on the LCD module */
						lcdPrintNetworkRestoredMessage();

						// Release the mutex
						xSemaphoreGive(xLCDMutex);
					}
				}

				/* Set the OCPP server previous connection flag */
				g_isOCPPServerConnectedPrevious = TRUE;
			}
			else {
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

		/* Delay for Heart Beat Interval seconds */
		vTaskDelay( INT_TO_TICKS( g_HeartBeatInterval * DELAY_1_SECOND ) );
	}
}
