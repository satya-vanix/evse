/*
 * =====================================================================================
 * File Name:    espControl.c
 * Author:       Vanix Technologies PVT. LTD.
 * Date Created: 2025-02-17
 * Description:  This source file contains the implementation of the functions
 * that control the ESP32 AT commands communication module. These functions
 *               handle the initialization, configuration, and sending/receiving
 * of AT commands to communicate with the ESP32 Wi-Fi module.
 *
 *               The ESP32 module provides wireless connectivity for the
 * electric vehicle charging system, enabling remote monitoring and control
 *               through AT commands. This file includes functions to set up the
 *               Wi-Fi connection, send data, receive responses, and manage
 * error handling during the communication process.
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
#include "espControl.h"

/*
 * =====================================================================================
 * Global Variables
 * =====================================================================================
 */
bool g_isWifiConnected =
    FALSE; // Global flag to store the Wi-Fi connection status live data
bool g_networkConnectivity =
    FALSE; // Global flag to store the network connectivity status live data
bool g_networkConnectivityPrevious =
    FALSE; // Global flag to store previous value of network connectivity status
TaskHandle_t xESPTask; // ESP Task handler pointer

extern bool g_isOCPPServerConnected; // Global flag to store the OCPP server
                                     // connection status live data

/*
 * =====================================================================================
 * Static Global Variables
 * =====================================================================================
 */
/* Wi-Fi Credentials variables */
static char m_strWifiName[20] = WIFI_SSID;
static char m_strPassword[20] = WIFI_PWD;

/* Flags to track if LCD messages have been printed once */
static bool m_isWifiCredentialsPrinted = FALSE;
static bool m_isWifiFailedPrinted = FALSE;

/*
 * =====================================================================================
 * Static Function Definitions
 * =====================================================================================
 */

/**
 * @brief  Updates the Wi-Fi connection command sequence.
 *
 * This function updates the command sequence array used to send Wi-Fi
 * connection commands to the ESP32 module. It populates the provided array with
 * the necessary AT commands required for establishing a connection to the Wi-Fi
 * network.
 *
 * @param  connectToWifiCommandsSeq A 2D array containing the sequence of Wi-Fi
 * connection AT commands.
 * @retval None
 */
static void
updateWifiCommand(char connectToWifiCommandsSeq[WIFI_CONN_AT_CMD_ARRAY_ROW]
                                               [WIFI_CONN_AT_CMD_ARRAY_COL]) {
  // Before updating the array (show the initial command)
  xil_printf("Before update : %s\r\n", connectToWifiCommandsSeq[4]);

  // Use sprintf to format the SSID and password into the existing command array
  // entry
  snprintf(connectToWifiCommandsSeq[4], sizeof(connectToWifiCommandsSeq[4]),
           "AT+CWJAP=\"%s\",\"%s\"", m_strWifiName, m_strPassword);

  // After updating the array (show the updated command)
  xil_printf("After update : %s\r\n", connectToWifiCommandsSeq[4]);
}

/**
 * @brief  Checks the Wi-Fi connection status.
 *
 * This function verifies whether the system is currently connected to a Wi-Fi
 * network. It checks the connection status by querying the ESP32 module or
 * related components and returns the result of the connection state.
 *
 * @param  None
 * @retval XST_SUCCESS   Indicates that the system is successfully connected to
 * a Wi-Fi network.
 * @retval XST_FAILURE   Indicates that the system is not connected to any Wi-Fi
 * network.
 */
static u8 checkWiFiConnected(void) {
  // Variables to hold the extracted values
  int l_state; // Current Wi-Fi State -- If its value is 2 that means ESP is
               // connected with AP and have the IPv4 address
  char
      l_networkName[30]; // Assuming the network name won't exceed 30 characters

  char l_responseBuffer[30] = {0};
  char l_sendBuffer[15] = {0};
  u8 l_retVal;

  /* Clear the both send and response buffer data */
  memset(l_responseBuffer, 0, sizeof(l_responseBuffer));
  memset(l_sendBuffer, 0, sizeof(l_sendBuffer));

  /* Fill the send buffer data with the required AT command */
  strcpy(l_sendBuffer, "AT+CWSTATE?");

  // Wait for the AT mutex
  if (xSemaphoreTake(xATMutex, portMAX_DELAY) == pdTRUE) {
    /* Send the AT command to the ESP32 module and collect its response */
    l_retVal = SendUartATCommandWithResponse(
        l_sendBuffer, 60, 300, l_responseBuffer, sizeof(l_responseBuffer));

    if (l_retVal == XST_SUCCESS) {
      if (strstr(l_responseBuffer, "OK") == NULL) {
        xil_printf("Invalid Response of %s command : %s\r\n", l_sendBuffer,
                   l_responseBuffer);

        /* Update the global variable */
        g_isWifiConnected = FALSE;

        /* Clear the send buffer data */
        memset(l_sendBuffer, 0, sizeof(l_sendBuffer));
        /* Fill the send buffer data with the required AT command */
        strcpy(l_sendBuffer, "AT+CWQAP");

        l_retVal = SendUartATCommandWithResponse(
            l_sendBuffer, 60, 300, l_responseBuffer, sizeof(l_responseBuffer));
        if (l_retVal == XST_SUCCESS) {
          if (strstr(l_responseBuffer, "OK") == NULL) {
            xil_printf("Invalid Response of %s command : %s\r\n", l_sendBuffer,
                       l_responseBuffer);
          }
          // Release the AT mutex
          xSemaphoreGive(xATMutex);
          return XST_FAILURE;
        } else {
          /* Get the Timeout as AT Command response */
          xil_printf("AT command %s timeout occurred and got no response\r\n",
                     l_sendBuffer);
          // Release the AT mutex
          xSemaphoreGive(xATMutex);
          return XST_TIMEOUT;
        }
      } else {
        /* Extract the state and network name from the response buffer */
        sscanf(l_responseBuffer, "+CWSTATE:%d,\"%[^\"]\"", &l_state,
               l_networkName);
        xil_printf("Wi-Fi State : %d and Network Name : %s\r\n", l_state,
                   l_networkName);

        /* If state is not 2 or the Network name is not matched with the
         * configured SSID */
        if ((l_state != 2) || (strcmp(l_networkName, m_strWifiName) != 0)) {
          /* Need to connect with the configured Wi-Fi AP */
          xil_printf("Need to connect with the configured Wi-Fi AP\r\n");

          /* Update the global variable */
          g_isWifiConnected = FALSE;

          if (l_state != 0) {
            /* Clear the send buffer data */
            memset(l_sendBuffer, 0, sizeof(l_sendBuffer));
            /* Fill the send buffer data with the required AT command */
            strcpy(l_sendBuffer, "AT+CWQAP");

            l_retVal = SendUartATCommandWithResponse(l_sendBuffer, 60, 300,
                                                     l_responseBuffer,
                                                     sizeof(l_responseBuffer));
            if (l_retVal == XST_SUCCESS) {
              if (strstr(l_responseBuffer, "OK") == NULL) {
                xil_printf("Invalid Response of %s command : %s\r\n",
                           l_sendBuffer, l_responseBuffer);
              }
              // Release the AT mutex
              xSemaphoreGive(xATMutex);
              return XST_FAILURE;
            } else {
              /* Get the Timeout as AT Command response */
              xil_printf(
                  "AT command %s timeout occurred and got no response\r\n",
                  l_sendBuffer);
              // Release the AT mutex
              xSemaphoreGive(xATMutex);
              return XST_TIMEOUT;
            }
          } else {
            xil_printf(
                "Wi-Fi State is %d so no need to disconnect from AP!\r\n",
                l_state);
            // Release the AT mutex
            xSemaphoreGive(xATMutex);
            return XST_FAILURE;
          }
        } else {
          /* Device is already connected with the configured Wi-Fi AP */
          xil_printf(
              "Device is already connected with the configured Wi-Fi AP\r\n");
        }
      }
    } else {
      /* Get the Timeout as AT Command response */
      xil_printf("AT command %s timeout occurred and got no response\r\n",
                 l_sendBuffer);

      // Release the AT mutex
      xSemaphoreGive(xATMutex);

      return XST_TIMEOUT;
    }

    // Release the AT mutex
    xSemaphoreGive(xATMutex);
  }
  return XST_SUCCESS;
}

/*
 * =====================================================================================
 * Global Function Definitions
 * =====================================================================================
 */

/**
 * @brief  Initializes the ESP32 module.
 *
 * This function sets up the necessary configurations to initialize the ESP32
 * module, including configuring communication interfaces and ensuring the ESP32
 * is ready to receive and execute AT commands. It should be called before using
 * any other functions to interact with the ESP32.
 */
int InitESP(void) {
  int Status;

  Status =
      UartIntpInit(&IntcInstance, &UartInst, UART_DEVICE_ID, UART_IRPT_INTR);
  if (Status != XST_SUCCESS) {
    xil_printf("UART Intp Mode Init Failed\r\n");
    return XST_FAILURE;
  }
  xil_printf("Successfully Setup UART Intp Mode Init\r\n");
  return XST_SUCCESS;
}

/**
 * @brief  Connects the ESP32 module to a Wi-Fi network.
 *
 * This function sends the necessary AT commands to connect the ESP32 module to
 * a specified Wi-Fi network. It handles the connection process, including the
 * authentication and setup of network parameters. The function should be called
 * after initializing the ESP32 module.
 */
u8 ConnectToWifi(void) {
  u8 retVal = XST_SUCCESS;
  u8 loopCnt;

  char responseBuffer[BUFFER_SIZE] = {0};
  char sendBuffer[BUFFER_SIZE] = {0};

  /* AT Command sequence to connect the ESP32 module with the local Wi-Fi
   * network */
  char connectToWifiCommandsSeq
      [WIFI_CONN_AT_CMD_ARRAY_ROW][WIFI_CONN_AT_CMD_ARRAY_COL] = {
          {"AT+RESTORE"},  // Restore factory default settings of the module.
          {"AT"},          // Test AT startup.
          {"ATE0"},        // Configure AT commands echoing. -- Switch echo off.
          {"AT+CWMODE=1"}, // Set the Wi-Fi mode as STA(Station mode)
          {"AT+CWJAP=\"SSID\",\"PASSWORD\""}, // Connect to an AP.
          {"AT+CIPSTA?"} // Query the IP address of an ESP32 station.
      };

  /* Update the actual Wi-Fi credentials in the AT command string */
  updateWifiCommand(connectToWifiCommandsSeq);

  for (loopCnt = 0; loopCnt < WIFI_CONN_AT_CMD_ARRAY_ROW; loopCnt++) {
    /* Clear the both send and receive buffer data */
    memset(responseBuffer, 0, sizeof(responseBuffer));
    memset(sendBuffer, 0, sizeof(sendBuffer));

    /* Fill the send buffer data with the required command as per the sequence
     */
    strcpy(sendBuffer, connectToWifiCommandsSeq[loopCnt]);

    // Wait for the mutex
    if (xSemaphoreTake(xATMutex, portMAX_DELAY) == pdTRUE) {

      /* Send the AT command to the ESP32 module and collect its response */
      retVal = SendUartATCommandWithResponse(
          sendBuffer, 60, 300, responseBuffer, sizeof(responseBuffer));

      /* If the AT command is AT+RESTORE then we have to wait for some time so
       * that ESP module can restore its configuration to the factory default
       * settings and be ready to handle the next set of the commands.
       */
      if (strstr(sendBuffer, "AT+RESTORE") != NULL) {
        mssleep(2000);
        if (retVal == XST_SUCCESS) {
          if ((strstr(responseBuffer, "OK") == NULL) &&
              (strstr(responseBuffer, "AT+RESTORE") == NULL)) {
            xil_printf("Invalid Response of %s command : %s\r\n", sendBuffer,
                       responseBuffer);
            // Release the mutex
            xSemaphoreGive(xATMutex);
            return XST_FAILURE;
          }
        } else {
          /* Get the Timeout as AT Command response */
          xil_printf("AT command %s timeout occurred and got no response\r\n",
                     sendBuffer);
          // Release the AT mutex
          xSemaphoreGive(xATMutex);
          return XST_TIMEOUT;
        }
      }
      /* If the AT command is AT+CWJAP then we have to wait for some time so
       * that ESP module can connect with the provided Wi-Fi credentials and be
       * ready to handle the next set of the commands.
       */
      else if (strstr(sendBuffer, "AT+CWJAP") != NULL) {
        mssleep(2000);
        if (retVal == XST_SUCCESS) {
          if ((strstr(responseBuffer, "OK") == NULL) &&
              (strstr(responseBuffer, "CONNECTED") == NULL)) {
            xil_printf("Invalid Response of %s command : %s\r\n", sendBuffer,
                       responseBuffer);
            // Release the mutex
            xSemaphoreGive(xATMutex);
            return XST_FAILURE;
          }
        } else {
          /* Get the Timeout as AT Command response */
          xil_printf("AT command %s timeout occurred and got no response\r\n",
                     sendBuffer);
          // Release the AT mutex
          xSemaphoreGive(xATMutex);
          return XST_TIMEOUT;
        }
      } else if (retVal == XST_SUCCESS) {
        if (strstr(responseBuffer, "OK") == NULL) {
          xil_printf("Invalid Response of %s command : %s\r\n", sendBuffer,
                     responseBuffer);
          // Release the mutex
          xSemaphoreGive(xATMutex);
          return XST_FAILURE;
        }
      } else {
        /* Get the Timeout as AT Command response */
        xil_printf("AT command %s timeout occurred and got no response\r\n",
                   sendBuffer);
        // Release the AT mutex
        xSemaphoreGive(xATMutex);
        return XST_TIMEOUT;
      }

      // Release the mutex
      xSemaphoreGive(xATMutex);
    }
  }

  return XST_SUCCESS;
}

/**
 * @brief  Checks the network connectivity status.
 *
 * This function verifies whether the system has an active network connection,
 * either through Wi-Fi or another communication interface. It checks the
 * connection status by querying the relevant components, ensuring the system
 * can reach the network for further operations.
 */
u8 checkNetworkConnectivity(void) {
  // Variables to hold the extracted values
  int l_pingTime; // Ping response time

  char l_responseBuffer[30] = {0};
  char l_sendBuffer[20] = {0};
  u8 l_retVal;

  /* Clear the both send and response buffer data */
  memset(l_responseBuffer, 0, sizeof(l_responseBuffer));
  memset(l_sendBuffer, 0, sizeof(l_sendBuffer));

  /* Fill the send buffer data with the required AT command */
  strcpy(l_sendBuffer, "AT+PING=\"8.8.8.8\"");

  // Wait for the AT mutex
  if (xSemaphoreTake(xATMutex, portMAX_DELAY) == pdTRUE) {
    /* Send the AT command to the ESP32 module and collect its response */
    l_retVal = SendUartATCommandWithResponse(
        l_sendBuffer, 60, 300, l_responseBuffer, sizeof(l_responseBuffer));

    if (l_retVal == XST_SUCCESS) {
      if (strstr(l_responseBuffer, "OK") == NULL) {
        xil_printf("Invalid Response of %s command : %s\r\n", l_sendBuffer,
                   l_responseBuffer);

        /* Device has no network connectivity from the connected Wi-Fi AP */
        xil_printf("Device has no network connectivity from the connected "
                   "Wi-Fi AP\r\n");

        // Release the AT mutex
        xSemaphoreGive(xATMutex);

        return XST_FAILURE;
      } else {
        /* Extract the ping response time from the response buffer */
        sscanf(l_responseBuffer, "+PING:%d", &l_pingTime);
        xil_printf("Ping response time : %d\r\n", l_pingTime);

        /* If ping response time is 0 then it means there is no network
         * connectivity with the device */
        if (l_pingTime == 0) {
          /* Device has no network connectivity from the connected Wi-Fi AP */
          xil_printf("Device has no network connectivity from the connected "
                     "Wi-Fi AP\r\n");

          // Release the AT mutex
          xSemaphoreGive(xATMutex);

          return XST_FAILURE;
        } else {
          /* Network connectivity is working perfectly fine on the device */
          xil_printf("Network connectivity is working perfectly fine on the "
                     "device\r\n");
        }
      }
    } else {
      /* Get the Timeout as AT Command response */
      xil_printf("AT command %s timeout occurred and got no response\r\n",
                 l_sendBuffer);

      // Release the AT mutex
      xSemaphoreGive(xATMutex);

      return XST_TIMEOUT;
    }

    // Release the AT mutex
    xSemaphoreGive(xATMutex);
  }
  return XST_SUCCESS;
}

#if 0
/**
 * @brief  Sends charging data to the ESP32 module.
 *
 * This function sends the charging percentage data to the ESP32 module, typically to be transmitted
 * over a network or used for further processing. The provided charging percentage is sent as part
 * of the communication with the connected device.
 */
void SendChargingData(int nChargePercent)
{
	xil_printf( "Sending Data : %d\r\n",nChargePercent );
	char strChargingData[BUFFER_SIZE] = {0};
	char strChargingData_Command[BUFFER_SIZE] = {0};
	char strChargingData_api[BUFFER_SIZE] = {0};
	char strChargingData_param[BUFFER_SIZE] = {0};
	char strChargingData_jsondata[BUFFER_SIZE] = {0};
	char strChargingData_charge[BUFFER_SIZE] = {0};
	sprintf(strChargingData_Command,		 	"%s","AT+HTTPCLIENT=3,1,");
	sprintf(strChargingData_api,				"%s","\"https://evapp.vanixtechnologies.cloud/api/payload/add-payload-charging-state\",");
	sprintf(strChargingData_param,		 		"%s",",,1,");
	sprintf(strChargingData_jsondata,	 	"%s","\"{\\\"charging_state\\\":\\\"");
	sprintf(strChargingData_charge,	 		"%d%s",nChargePercent,"\\\"}\"");

	sprintf(strChargingData,"%s%s%s%s%s",
			strChargingData_Command,
			strChargingData_api,
			strChargingData_param,
			strChargingData_jsondata,
			//"\"{\\\"charging-data\\\":{\\\"charging_state\\\":\\\"55\\\"}}\"",
			strChargingData_charge
			);
	SendUartATCommand(strChargingData,200,2000);
	xil_printf( "Data Sent %d\r\n",nChargePercent);
}

/**
 * @brief  Sends charger connected information to the ESP32 module.
 *
 * This function sends the "charger connected" status information to the ESP32 module, indicating that
 * the charger has been successfully connected. This data is used to notify the ESP32 about the charger
 * connection status for further processing or communication with other systems.
 */
void SendChargingDataConnected(void)
{
	xil_printf( "Sending Data : Charger Connected\r\n");
	char strChargingData[BUFFER_SIZE] = {0};
	char strChargingData_Command[BUFFER_SIZE] = {0};
	char strChargingData_api[BUFFER_SIZE] = {0};
	char strChargingData_param[BUFFER_SIZE] = {0};
	char strChargingData_jsondata[BUFFER_SIZE] = {0};
	char strChargingData_chargerStatus[BUFFER_SIZE] = {0};
	sprintf(strChargingData_Command,		 	"%s","AT+HTTPCLIENT=3,1,");
	sprintf(strChargingData_api,				"%s","\"https://evapp.vanixtechnologies.cloud/api/payload/add-payload-charging-status\",");
	sprintf(strChargingData_param,		 		"%s",",,1,");
	sprintf(strChargingData_jsondata,	 	"%s","\"{\\\"charging_status\\\":\\\"");
	sprintf(strChargingData_chargerStatus,	 		"%s%s","connected","\\\"}}\"");

	sprintf(strChargingData,"%s%s%s%s%s",
			strChargingData_Command,
			strChargingData_api,
			strChargingData_param,
			strChargingData_jsondata,
			//{"charging-data":{"charger_status":"connected"}}
			strChargingData_chargerStatus
	);
	SendUartATCommand(strChargingData,200,2000);
	xil_printf( "Data Sent : Charger Connected\r\n");
}

/**
 * @brief  Sends charger disconnected information to the ESP32 module.
 *
 * This function sends the "charger disconnected" status information to the ESP32 module, indicating that
 * the charger has been disconnected. This data is used to notify the ESP32 about the charger disconnection
 * status for further processing or communication with other systems.
 */
void SendChargingDataDisconnected(void)
{
	xil_printf( "Sending Data : Charger Disconnected\r\n");
		char strChargingData[BUFFER_SIZE] = {0};
		char strChargingData_Command[BUFFER_SIZE] = {0};
		char strChargingData_api[BUFFER_SIZE] = {0};
		char strChargingData_param[BUFFER_SIZE] = {0};
		char strChargingData_jsondata[BUFFER_SIZE] = {0};
		char strChargingData_chargerStatus[BUFFER_SIZE] = {0};
		sprintf(strChargingData_Command,		 	"%s","AT+HTTPCLIENT=3,1,");
		sprintf(strChargingData_api,				"%s","\"https://evapp.vanixtechnologies.cloud/api/payload/add-payload\",");
		sprintf(strChargingData_param,		 		"%s",",,1,");
		sprintf(strChargingData_jsondata,	 	"%s","\"{\\\"charging_status\\\":\\\"");
		sprintf(strChargingData_chargerStatus,	 		"%s%s","disconnected","\\\"}}\"");

		sprintf(strChargingData,"%s%s%s%s%s",
				strChargingData_Command,
				strChargingData_api,
				strChargingData_param,
				strChargingData_jsondata,
				//{"charging-data":{"charger_status":"connected"}}
				strChargingData_chargerStatus
		);
		SendUartATCommand(strChargingData,200,2000);
		xil_printf( "Data Sent : Charger Disconnected\r\n");
}
#endif

/**
 * @brief  ESP32 task for handling communication and operations.
 *
 * This function is a FreeRTOS task dedicated to handling communication with the
 * ESP32 module, including sending and receiving AT commands, processing
 * responses, and managing ESP32-related operations. It runs as part of the
 * system's task scheduler to manage ESP32 interactions.
 */
void prvESPTask(void *pvParameters) {
  /* Delay for 5 seconds */
  const TickType_t x5second = INT_TO_TICKS(5 * DELAY_1_SECOND);

  /* Initialize UART with full configuration including interrupt setup */
  UartIntpInit(&IntcInstance, &UartInst, UART_DEVICE_ID, UART_IRPT_INTR);

  u8 retVal;

  while (1) {
    xil_printf("\t\t\t###   %s   ###\t\t\t\r\n", __func__);

    if (g_isOtaInProgress) {
      /* Delay for 5 second. */
      vTaskDelay(x5second);
    }

    /* Check if device is already connected with the configured SSID */
    retVal = checkWiFiConnected();
    /* In case Device is not connected with the configured SSID */
    if ((retVal != XST_SUCCESS) && (retVal != XST_TIMEOUT)) {
      xil_printf("ESP module is not connected with %s Wi-Fi network!\r\n",
                 m_strWifiName);

      /* Indicate that Wi-Fi is not working by turning off the Wi-Fi status LED
       */
      resetLedPin(LED_WIFI);
    }

    if (g_isWifiConnected == FALSE) {
      // Wait for the LCD mutex - Print only once
      if (!m_isWifiCredentialsPrinted &&
          (xSemaphoreTake(xLCDMutex, portMAX_DELAY) == pdTRUE)) {
        /* Print the Wi-Fi configuration details on the LCD module */
        lcdPrintWifiCredentialsMessage(m_strWifiName);

        // Release the mutex
        xSemaphoreGive(xLCDMutex);

        // Mark as printed
        m_isWifiCredentialsPrinted = TRUE;
      }

      /* Check if device is already connected with the configured SSID */
      retVal = checkWiFiConnected();
      /* In case Device is not connected with the configured SSID */
      if ((retVal != XST_SUCCESS) && (retVal != XST_TIMEOUT)) {
        /* Connect with the configured Wi-Fi network */
        retVal = ConnectToWifi();
      }

      /* If connection successfully established */
      if (retVal == XST_SUCCESS) {
        xil_printf(
            "ESP module successfully connected with %s Wi-Fi network!\r\n",
            m_strWifiName);
        /* Update the global variable */
        g_isWifiConnected = TRUE;

        /* Indicate that Wi-Fi is not working by turning on the Wi-Fi status LED
         */
        setLedPin(LED_WIFI);

        // Wait for the LCD mutex
        if (xSemaphoreTake(xLCDMutex, portMAX_DELAY) == pdTRUE) {
          /* Print the Wi-Fi connection details on the LCD module */
          lcdPrintWifiConnectionSuccessMessage();

          // Release the mutex
          xSemaphoreGive(xLCDMutex);
        }
      }
      /* If connection establishment failed */
      else {
        xil_printf("ESP module connection with %s Wi-Fi network failed!\r\n",
                   m_strWifiName);

        // Wait for the LCD mutex - Print only once
        if (!m_isWifiFailedPrinted &&
            (xSemaphoreTake(xLCDMutex, portMAX_DELAY) == pdTRUE)) {
          /* Print the Wi-Fi connection details on the LCD module */
          lcdPrintWifiConnectionFailMessage();

          // Release the mutex
          xSemaphoreGive(xLCDMutex);

          // Mark as printed
          m_isWifiFailedPrinted = TRUE;
        }
      }
    }
    if (g_isWifiConnected) {
      /* Check if device has the network connectivity */
      retVal = checkNetworkConnectivity();
      /* In case Device doesn't have the network connectivity */
      if (retVal != XST_SUCCESS) {
        /* Clear the network connectivity global flag */
        g_networkConnectivity = FALSE;

        if (g_networkConnectivityPrevious == TRUE) {
          // Wait for the LCD mutex
          if (xSemaphoreTake(xLCDMutex, portMAX_DELAY) == pdTRUE) {
            /* Print the details on the LCD module */
            lcdPrintOfflineModeConnectionLostMessage();

            // Release the mutex
            xSemaphoreGive(xLCDMutex);
          }
        }

        g_networkConnectivityPrevious = FALSE;

        /* TODO : In case of the Network Connectivity is not available for the
         * longer period of time so at that time forcefully disconnect from the
         * connected Wi-Fi router and send a reconnection request. */
      } else {
        /* Set the network connectivity global flag */
        g_networkConnectivity = TRUE;
        g_networkConnectivityPrevious = TRUE;
      }
    } else {
      g_networkConnectivity = FALSE;
      g_isOCPPServerConnected = FALSE;

      if (g_networkConnectivityPrevious == TRUE) {
        // Wait for the LCD mutex
        if (xSemaphoreTake(xLCDMutex, portMAX_DELAY) == pdTRUE) {
          /* Print the details on the LCD module */
          lcdPrintOfflineModeConnectionLostMessage();

          // Release the mutex
          xSemaphoreGive(xLCDMutex);
        }
      }

      g_networkConnectivityPrevious = FALSE;
    }

    /* Delay for 5 second. */
    vTaskDelay(x5second);
  }
}
