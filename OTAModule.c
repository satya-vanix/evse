/*
 * =====================================================================================
 * File Name:    OTAModule.c
 * Author:       Vanix Technologies PVT. LTD.
 * Date Created: 2025-04-03
 * Description:  This source file contains the implementation of functions required for
 *               handling Over-The-Air (OTA) firmware updates in the Electric Vehicle
 *               Supply Equipment (EVSE) charger. It manages secure firmware download,
 *               verification, and installation to ensure the system remains up to date.
 *
 *               The file includes functions for establishing a connection with the
 *               update server, downloading firmware packages, verifying integrity,
 *               and executing the update process. It ensures minimal downtime and
 *               secure updates to maintain system reliability and security.
 *
 * Revision History:
 * Version 1.0 - 2025-04-03 - Initial version.
 * =====================================================================================
 */

/*
 * =====================================================================================
 * Include Headers
 * =====================================================================================
 */
#include "OTAModule.h"

/*
 * =====================================================================================
 * Global Variables
 * =====================================================================================
 */
bool g_isOtaInProgress;				/* Global variable to track the OTA In Progress status */
bool g_isOtaProcessCompleted;		/* Global variable to track the OTA process Completed status */

int g_OtaTotalRecvCount;			/* Global variable to Store the OTA chunk packet total receive bytes information */
int g_OtaExpectedTotalRecvCount;	/* Global variable to Store the OTA chunk packet expected total receive bytes information */

int g_OtaAppFileSize;				/* Global variable to Store the OTA Application file size information */
char g_OtaAppHashValue[33];			/* Global variable to Store the OTA Application md5sum information */
char g_OtaAppVersionValue[6];		/* Global variable to Store the OTA Application version information */
uint8_t g_OtaRecvAppHashValue[16];	/* Global variable to Store the Received OTA Application md5sum information */
uint8_t g_OtaReadAppHashValue[16];  /* Global variable to Store the Read OTA Application md5sum information */
int g_OtaBitFileSize;				/* Global variable to Store the OTA Bit stream file size information */
char g_OtaBitHashValue[33];			/* Global variable to Store the OTA Bit stream md5sum information */
char g_OtaBitVersionValue[6];		/* Global variable to Store the OTA Bit stream version information */
uint8_t g_OtaRecvBitHashValue[16];	/* Global variable to Store the Received OTA Bit stream md5sum information */
uint8_t g_OtaReadBitHashValue[16];	/* Global variable to Store the Read OTA Bit stream md5sum information */

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
 * @brief  Counts the number of digits in an integer.
 *
 * This function calculates and returns the number of decimal digits
 * present in the given integer. It works for both positive and negative
 * numbers by taking the absolute value.
 *
 * @param  number The integer whose digits are to be counted.
 * @retval int    The number of digits in the provided integer.
 */
static int count_digits (int number)
{
    int count = 0;

    // Handle negative numbers
    if (number < 0) {
        number = -number;
    }

    // Special case for 0
    if (number == 0) {
        return 1;
    }

    while (number != 0) {
        number /= 10;
        count++;
    }

    return count;
}

/*
 * =====================================================================================
 * Global Function Definitions
 * =====================================================================================
 */

/**
 * @brief  Initializes the OTA update module.
 */
u8 OtaInit(void)
{
	/* Initialize the Global variables and set it as a FALSE state */
	g_isOtaInProgress = FALSE;
	g_isOtaProcessCompleted = FALSE;

	g_OtaTotalRecvCount = 0;
	g_OtaAppFileSize = 0;
	g_OtaBitFileSize = 0;
	memset(g_OtaAppHashValue, 0, sizeof(g_OtaAppHashValue));
	memset(g_OtaBitHashValue, 0, sizeof(g_OtaBitHashValue));
	memset(g_OtaAppVersionValue, 0, sizeof(g_OtaAppVersionValue));
	memset(g_OtaBitVersionValue, 0, sizeof(g_OtaBitVersionValue));
	memset(g_OtaRecvAppHashValue, 0, sizeof(g_OtaRecvAppHashValue));
	memset(g_OtaRecvBitHashValue, 0, sizeof(g_OtaRecvBitHashValue));

	return XST_SUCCESS;
}

/**
 * @brief  Retrieves OTA file information including size, hash, and version.
 */
u8 OtaGetFileInfo(char *fileName, int *fileSize, char *fileHash, char *fileVersion)
{
	u8 retVal;
	const char *sizeKey = "\"file_size\":";
	const char *hashKey = "\"hash\":";
	const char *versionKey = "\"version\":";
	char ResponseBufferStr[BUFFER_SIZE] = {0};
	char SendBufferStr[BUFFER_SIZE] = {0};

	/* Check if the file name is valid pointer or not */
	if (!fileName) {
		xil_printf("Invalid File Name pointer!\r\n");
		/* If file name is not valid pointer then set all the fields as 0 / NULL and return failure */
		fileSize = 0;
		fileHash = '\0';
		fileVersion = '\0';
		return XST_FAILURE;
	}

	/* Reset the send buffer to fill new AT command which needs to be send */
	memset(SendBufferStr, 0, BUFFER_SIZE);
	/* Reset the response buffer to collect the next command response */
	memset(ResponseBufferStr, 0, BUFFER_SIZE);

	/* Fill the send buffer with the new command sequence */
	snprintf(SendBufferStr, BUFFER_SIZE, "AT+HTTPCGET=\"%s%s\"", OTA_GET_FILE_INFO_ENDPOINT, fileName);

	// Wait for the AT mutex
	if (xSemaphoreTake(xATMutex, portMAX_DELAY) == pdTRUE) {
		/* Send the AT command to the ESP32 module and collect its response */
		retVal = SendUartATCommandWithResponse(SendBufferStr, 60, 3000, ResponseBufferStr, BUFFER_SIZE);

		if (retVal == XST_SUCCESS) {
			/* If AT command response is valid then proceed further */
			if((strstr(ResponseBufferStr, "OK") != NULL) || (strstr(ResponseBufferStr, "+HTTPCGET") != NULL)) {
				/* Extract the file information from the response buffer */
				char *sizePos = strstr(ResponseBufferStr, sizeKey);
				char *hashPos = strstr(ResponseBufferStr, hashKey);
				char *versionPos = strstr(ResponseBufferStr, versionKey);

				if (sizePos != NULL) {
					/* Move pointer past the size key to the value */
					sizePos += strlen(sizeKey);
					/* Extract the integer value */
					sscanf(sizePos, "%d", fileSize);
				}
				xil_printf("File name %s size is : %d\r\n", fileName, *fileSize);

				if (hashPos != NULL) {
					/* Move pointer past the hash key to the value */
					hashPos += (strlen(hashKey) + 1);
					/* Extract the hash information */
					strncpy(fileHash, hashPos, 32);
					/* Null terminate the hash string */
					fileHash[32] = '\0';
				}
				xil_printf("File name %s hash is : %s\r\n", fileName, fileHash);

				if (versionPos != NULL) {
					/* Move pointer past the version key to the value */
					versionPos += (strlen(versionKey) + 1);
					/* Extract the hash information */
					strncpy(fileVersion, versionPos, 5);
					/* Null terminate the hash string */
					fileVersion[5] = '\0';
				}
				xil_printf("File name %s version is : %s\r\n", fileName, fileVersion);
			}
			else {
				/* Received the invalid response for the AT command */
				xil_printf("Received the invalid response : %s\r\n", ResponseBufferStr);

				// Release the AT mutex
				xSemaphoreGive(xATMutex);

				/* Return the failure status in case of the invalid response of AT command */
				return XST_FAILURE;
			}
		}
		else {
			/* Get the Timeout as AT Command response */
			xil_printf("Timeout occurred : %s\r\n", SendBufferStr);

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
 * @brief  Downloads the firmware file for OTA update.
 */
u8 OtaDownloadFirmwareFile(char *fileName, int fileSize)
{
	u8 retVal, retryCnt = 0;
	int payloadLength = 0;
	char HTTPCommandStr[BUFFER_SIZE] = {0};
	static char FirmwareResponseBufferStr[OTA_BUFFER_SIZE + 1024] = {0}; 	// Added the 1024 bytes extra to handle the extra header info
	static char FirmwareActualBuffer[OTA_BUFFER_SIZE] = {0};
	static char FirmwareReadBuffer[OTA_BUFFER_SIZE] = {0};
	cJSON *OtaFileHeaderJSON = NULL;
	char *OtaFileHeaderStr = NULL;
	int headerStartByte = 0, headerEndByte = 0, dataByte = 0;
	int receivedBytes = 0;
	bool l_packetDownloaded = FALSE;
	MD5_CTX ctx, ctx1;
	uint8_t currentAppPartition = 0;
	int newApplicationUpdateAddress = 0;

	/* Initialize MD5SUM context */
	MD5_Init(&ctx);

	/* Define the bytes number to download those much data from the backend server */
	int NoOfBytes = 4096;

	/* Check if the file name is valid pointer or not */
	if (!fileName) {
		xil_printf("Invalid File Name pointer!\r\n");
		/* If file name is not valid pointer then return failure */
		return XST_FAILURE;
	}

	// Wait for the QSPI mutex
	if (xSemaphoreTake(xQSPIMutex, portMAX_DELAY) == pdTRUE) {
		/* Configure QSPI options including Quad IO Mode */
		XQspiPs_SetClkPrescaler(&g_qspiInstance, XQSPIPS_CLK_PRESCALE_8);

		/* Read the current application partition number */
		retVal = qspiRead(&g_qspiInstance, &currentAppPartition, QSPI_ADDR_CURR_APPLICATION_STATUS_FLAG, 1);
		if (retVal == XST_SUCCESS) {
			xil_printf("currentAppPartition : %d\r\n", currentAppPartition);

			/* If current app partition is 1 then update the new application into partition 2 */
			if (currentAppPartition == 1) {
				newApplicationUpdateAddress = QSPI_ADDR_APPLICATION_2_PARTITION;
				xil_printf("newApplicationUpdateAddress : %08X\r\n", newApplicationUpdateAddress);
			}
			/* If current app partition is 2 then update the new application into partition 1 */
			else if (currentAppPartition == 2) {
				newApplicationUpdateAddress = QSPI_ADDR_APPLICATION_1_PARTITION;
				xil_printf("newApplicationUpdateAddress : %08X\r\n", newApplicationUpdateAddress);
			}
		}
		// Release the mutex
		xSemaphoreGive(xQSPIMutex);
	}

	// Wait for the AT mutex
	if (xSemaphoreTake(xATMutex, portMAX_DELAY) == pdTRUE) {
		/* Download the given file name from the backend server and store it into QSPI flash on respective location */
		for (receivedBytes = 0; receivedBytes < fileSize; receivedBytes += NoOfBytes) {
			/* Reset the packet download flag and OTA expected Total received count variable */
			l_packetDownloaded = FALSE;
			g_OtaExpectedTotalRecvCount = 0;
			dataByte = 0;
			memset(FirmwareActualBuffer, 0, sizeof(FirmwareActualBuffer));

			/* Header start byte information as previously received bytes */
			headerStartByte = receivedBytes;

			/* In case of the last packet it will just download the remaining bytes */
			if ((receivedBytes + NoOfBytes - 1) > fileSize) {
				/* For last packet header end byte will be equivalent to file size - 1 */
				headerEndByte = (fileSize - 1);
			}
			else {
				/* Header end byte will be equivalent to OTA buffer size - 1 */
				headerEndByte = (headerStartByte + NoOfBytes - 1);
			}

			/* Calculate the Total expected receive bytes to get the proper OTA chunk */

			/* Received information format is like:
			 * +HTTPCPOST:8192,						// Here for the +HTTPCPOST:<data-len>, --> 12 bytes + 2 bytes of CRLF + up to 4 bytes based on data-len digits
			 *										// Here 2 bytes of the CRLF
			 * SEND OK								// Here for the SEND OK --> 7 bytes + 2 bytes of CRLF
			 *
			 * So Overall required expected received bytes is like (12 + 7 + 6 = 25) 25 bytes static + 4 bytes dynamic will be added into the response header
			 */
			dataByte = (headerEndByte - headerStartByte + 1);
			g_OtaExpectedTotalRecvCount = (dataByte + 25 + (count_digits(dataByte)));
			xil_printf("g_OtaExpectedTotalRecvCount : %d\r\n", g_OtaExpectedTotalRecvCount);

			/* Create the JSON object for the Payload structure type */
			OtaFileHeaderJSON = cJSON_CreateObject();

			/* Update the created JSON object to fill its key values */
			cJSON_AddStringToObject(OtaFileHeaderJSON, "file_name", fileName);
			cJSON_AddNumberToObject(OtaFileHeaderJSON, "start_byte", headerStartByte);
			cJSON_AddNumberToObject(OtaFileHeaderJSON, "end_byte", headerEndByte);

			/* Create the final Payload string by converting the JSON object into string format */
			OtaFileHeaderStr = cJSON_PrintUnformatted(OtaFileHeaderJSON);

			/* Once Payload final string is generated then free the JSON object memory */
			cJSON_Delete(OtaFileHeaderJSON);

			for (retryCnt = 0; ((retryCnt < OTA_PKT_DOWNLAOD_MAX_RETRY) && (!l_packetDownloaded)); retryCnt++) {
				xil_printf("OtaFileHeaderStr : %s\r\n", OtaFileHeaderStr);

				/* Reset the response buffer to collect the next command response */
				memset(FirmwareResponseBufferStr, 0, sizeof(FirmwareResponseBufferStr));
				g_OtaTotalRecvCount = 0;

				/* Calculate the Payload message length using the final created Payload string */
				payloadLength = strlen(OtaFileHeaderStr);

				/* Create the HTTP POST AT command string */
				snprintf(HTTPCommandStr, BUFFER_SIZE, "AT+HTTPCPOST=\"%s\",%d,1,\"content-type: application/json\"", OTA_GET_FILE_ENDPOINT, payloadLength);

				xil_printf("OtaFileHeader Message command : %s\r\n", HTTPCommandStr);

				/* Send the AT command to the ESP32 module and collect its response */
				retVal = SendUartATCommandWithResponse(HTTPCommandStr, 60, 1000, FirmwareResponseBufferStr, sizeof(FirmwareResponseBufferStr));

				if (retVal == XST_SUCCESS) {
					/* If HTTP POST AT command response is valid then proceed further */
					if(strstr(FirmwareResponseBufferStr, ">") != NULL) {
						/* Reset the response buffer to collect the next command response */
						memset(FirmwareResponseBufferStr, 0, sizeof(FirmwareResponseBufferStr));

						/* Send the Paylod string to the ESP32 module and collect its response */
						retVal = SendUartATCommandWithResponse(OtaFileHeaderStr, 60, 3000, FirmwareResponseBufferStr, sizeof(FirmwareResponseBufferStr));

						if (retVal == XST_SUCCESS) {
							/* If Payload command response is valid then proceed further */
							if (g_OtaTotalRecvCount == g_OtaExpectedTotalRecvCount) {
								/* If expected length of response received then free resource as well */
								free(OtaFileHeaderStr);

								xil_printf("Requested chunk downloaded successfully!\r\n");

								/* Now extract the actual data information from the received response buffer */
								const char *comma_pos = strchr(FirmwareResponseBufferStr, ',');		// Locate the comma in the response buffer string
								if (comma_pos != NULL) {
									comma_pos++;													// Increase the one position to set the offset to the data first byte
									memcpy(FirmwareActualBuffer, comma_pos, dataByte);
									FirmwareActualBuffer[dataByte] = '\0';

									/* Once data is extracted then calculate its hash value and update the final hash context */
									MD5_Update(&ctx, (const uint8_t *)FirmwareActualBuffer, dataByte);
								}

								/* Write the extracted buffer actual data into the QSPI storage */
								// Wait for the QSPI mutex
								if (xSemaphoreTake(xQSPIMutex, portMAX_DELAY) == pdTRUE) {
									/* If the data chunk size is large than 4096 bytes then write it into the QSPI storage in multiple part */
									if(dataByte > 4096) {
										/* First write the half of the data bytes */
										retVal = qspiWrite(&g_qspiInstance, (uint8_t *)FirmwareActualBuffer, (newApplicationUpdateAddress + receivedBytes), (dataByte / 2));
										if (retVal == XST_SUCCESS) {
											/* Reset the Read buffer */
											memset(FirmwareReadBuffer, 0, sizeof(FirmwareReadBuffer));

											/* Read back the written data bytes from the storage */
											retVal = qspiRead(&g_qspiInstance, (uint8_t *)FirmwareReadBuffer, (newApplicationUpdateAddress + receivedBytes), (dataByte / 2));
											if (retVal == XST_SUCCESS) {
												if (memcmp(FirmwareActualBuffer, FirmwareReadBuffer, (dataByte / 2)) == 0) {
													xil_printf("Downloaded chunk partial half info written successfully into QSPI storage\r\n");
													/* If previous half data bytes written successfully then write the next half data bytes */
													retVal = qspiWrite(&g_qspiInstance, (uint8_t *)(FirmwareActualBuffer + (dataByte / 2)), (newApplicationUpdateAddress + receivedBytes + (dataByte / 2)), (dataByte / 2));
													if (retVal == XST_SUCCESS) {
														/* Reset the Read buffer */
														memset(FirmwareReadBuffer, 0, sizeof(FirmwareReadBuffer));

														/* Read back the written data bytes from the storage */
														retVal = qspiRead(&g_qspiInstance, (uint8_t *)FirmwareReadBuffer, (newApplicationUpdateAddress + receivedBytes + (dataByte / 2)), (dataByte / 2));
														if (retVal == XST_SUCCESS) {
															if (memcmp((FirmwareActualBuffer + (dataByte / 2)), FirmwareReadBuffer, (dataByte / 2)) == 0) {
																xil_printf("Downloaded chunk full info written successfully into QSPI storage\r\n");
																l_packetDownloaded = TRUE;	/* This is the final exit location of internal for loop so it confirms like data is available in storage */
															}
															else {
																xil_printf("[CRITICAL]: Failed to write downloaded chunk full info properly into QSPI storage\r\n");
															}
														}
														else {
															xil_printf("Failed to read back downloaded chunk full info from QSPI storage\r\n");
														}
													}
													else {

														xil_printf("Failed to write downloaded chunk full info into QSPI storage\r\n");
													}
												}
												else {
													xil_printf("[CRITICAL]: Failed to write downloaded chunk partial half info properly into QSPI storage\r\n");
												}
											}
											else {
												xil_printf("Failed to read back downloaded chunk partial half info from QSPI storage\r\n");
											}
										}
										else {
											xil_printf("Failed to write downloaded chunk partial half info into QSPI storage\r\n");
										}
									}
									/* If the data chunk size is less than 4096 bytes then write it into the QSPI storage in single shot */
									else {
										/* Write full buffer in single shot into QSPI storage */
										retVal = qspiWrite(&g_qspiInstance, (uint8_t *)FirmwareActualBuffer, (newApplicationUpdateAddress + receivedBytes), dataByte);
										if (retVal == XST_SUCCESS) {
											/* Reset the Read buffer */
											memset(FirmwareReadBuffer, 0, sizeof(FirmwareReadBuffer));

											/* Read back the written data bytes from the storage */
											retVal = qspiRead(&g_qspiInstance, (uint8_t *)FirmwareReadBuffer, (newApplicationUpdateAddress + receivedBytes), dataByte);
											if (retVal == XST_SUCCESS) {
												if (memcmp(FirmwareActualBuffer, FirmwareReadBuffer, dataByte) == 0) {
													xil_printf("Downloaded chunk written successfully into QSPI storage\r\n");
													l_packetDownloaded = TRUE;	/* This is the final exit location of internal for loop so it confirms like data is available in storage */
												}
												else {
													xil_printf("[CRITICAL]: Failed to write downloaded chunk properly into QSPI storage\r\n");
												}
											}
											else {
												xil_printf("Failed to read back downloaded chunk from QSPI storage\r\n");
											}
										}
										else {
											xil_printf("Failed to write downloaded chunk into QSPI storage\r\n");
										}
									}
									// Release the mutex
									xSemaphoreGive(xQSPIMutex);
								}
							}
							else {
								/* Received the invalid response for the Payload string command */
								xil_printf("Received the invalid response : %s\r\n", FirmwareResponseBufferStr);
							}
						}
						else {
							/* Get the Timeout as Payload string response */
							xil_printf("Timeout occurred : %s\r\n", OtaFileHeaderStr);
						}
					}
					else {
						/* Received the invalid response for the HTTP POST AT command */
						xil_printf("Received the invalid response : %s\r\n", FirmwareResponseBufferStr);
					}
				}
				else {
					/* Get the Timeout as AT Command response */
					xil_printf("Timeout occurred : %s\r\n", HTTPCommandStr);
				}
			}

			/* After the maximum number of the retry still the packet is not able to download then return the failure status */
			if ((retryCnt == OTA_PKT_DOWNLAOD_MAX_RETRY) && (!l_packetDownloaded)) {
				if (OtaFileHeaderStr != NULL) {
					// Free allocated resources
					free(OtaFileHeaderStr);
				}

				// Release the AT mutex
				xSemaphoreGive(xATMutex);

				if (retVal == XST_TIMEOUT) {
					/* Return the Time out status in case of the AT command time out occurred */
					return XST_TIMEOUT;
				}
				else {
					/* Return the failure status in case of the invalid response of AT command */
					return XST_FAILURE;
				}
			}

			/* In case of data not received properly then before retry to fetch it again wait for some time to allow some time to ESP to be in ready state */
			if(g_OtaTotalRecvCount < g_OtaExpectedTotalRecvCount) {
				mssleep(2000);
			}
		}
		// Release the AT mutex
		xSemaphoreGive(xATMutex);
	}

	/* Calculate the final hash value of the received buffer */
	MD5_Final(g_OtaRecvAppHashValue, &ctx);

	/* Again Read the full written image from the flash and calculate its md5sum for double confirmation */

	/* Assign the bytes number to read those much data from the storage */
	NoOfBytes = 4096;

	/* Initialize MD5SUM context */
	MD5_Init(&ctx1);

	// Wait for the QSPI mutex
	if (xSemaphoreTake(xQSPIMutex, portMAX_DELAY) == pdTRUE) {
		for (receivedBytes = 0; receivedBytes < fileSize; receivedBytes += NoOfBytes) {
			dataByte = 0;

			/* Header start byte information as previously received bytes */
			headerStartByte = receivedBytes;

			/* In case of the last packet it will just download the remaining bytes */
			if ((receivedBytes + NoOfBytes - 1) > fileSize) {
				/* For last packet header end byte will be equivalent to file size - 1 */
				headerEndByte = (fileSize - 1);
			}
			else {
				/* Header end byte will be equivalent to OTA buffer size - 1 */
				headerEndByte = (headerStartByte + NoOfBytes - 1);
			}

			dataByte = (headerEndByte - headerStartByte + 1);
//			xil_printf("dataByte : %d\r\n", dataByte);

			/* Reset the response buffer to collect the next command response */
			memset(FirmwareReadBuffer, 0, sizeof(FirmwareReadBuffer));

			/* Read data bytes from the storage */
			retVal = qspiRead(&g_qspiInstance, (uint8_t *)FirmwareReadBuffer, (newApplicationUpdateAddress + receivedBytes), dataByte);
			if (retVal == XST_SUCCESS) {
//				xil_printf("Successfully read data bytes from QSPI storage\r\n");
				/* Once data is read then calculate its hash value and update the final hash context */
				MD5_Update(&ctx1, (const uint8_t *)FirmwareReadBuffer, dataByte);
			}
			else {
				xil_printf("Failed to read data bytes from QSPI storage\r\n");
			}
		}

		// Release the mutex
		xSemaphoreGive(xQSPIMutex);
	}

	/* Calculate the final hash value of the Read buffer */
	MD5_Final(g_OtaReadAppHashValue, &ctx1);

	/* Everything is perfect then return SUCCESS */
	return XST_SUCCESS;
}

/**
 * @brief  Downloads a hardware-related OTA file.
 */
u8 OtaDownloadHardwareFile(char *fileName, int fileSize)
{
	u8 retVal, retryCnt = 0;
	int payloadLength = 0;
	char HTTPCommandStr[BUFFER_SIZE] = {0};
	static char HardwareResponseBufferStr[OTA_BUFFER_SIZE + 1024] = {0}; 	// Added the 1024 bytes extra to handle the extra header info
	static char HardwareActualBuffer[OTA_BUFFER_SIZE] = {0};
	static char HardwareReadBuffer[OTA_BUFFER_SIZE] = {0};
	cJSON *OtaFileHeaderJSON = NULL;
	char *OtaFileHeaderStr = NULL;
	int headerStartByte = 0, headerEndByte = 0, dataByte = 0;
	int receivedBytes = 0;
	bool l_packetDownloaded = FALSE;
	MD5_CTX ctx, ctx1;
	uint8_t currentBitPartition = 0;
	int newBitStreamUpdateAddress = 0;

	/* Initialize MD5SUM context */
	MD5_Init(&ctx);

	/* Define the bytes number to download those much data from the backend server */
	int NoOfBytes = 4096;

	/* Check if the file name is valid pointer or not */
	if (!fileName) {
		xil_printf("Invalid File Name pointer!\r\n");
		/* If file name is not valid pointer then return failure */
		return XST_FAILURE;
	}

	// Wait for the QSPI mutex
	if (xSemaphoreTake(xQSPIMutex, portMAX_DELAY) == pdTRUE) {
		/* Configure QSPI options including Quad IO Mode */
		XQspiPs_SetClkPrescaler(&g_qspiInstance, XQSPIPS_CLK_PRESCALE_8);

		/* Read the current bit stream partition number */
		retVal = qspiRead(&g_qspiInstance, &currentBitPartition, QSPI_ADDR_CURR_BIT_STREAM_STATUS_FLAG, 1);
		if (retVal == XST_SUCCESS) {
			xil_printf("currentBitPartition : %d\r\n", currentBitPartition);

			/* If current bit stream partition is 1 then update the new bit stream into partition 2 */
			if (currentBitPartition == 1) {
				newBitStreamUpdateAddress = QSPI_ADDR_BIT_STREAM_2_PARTITION;
				xil_printf("newBitStreamUpdateAddress : %08X\r\n", newBitStreamUpdateAddress);
			}
			/* If current bit stream partition is 2 then update the new bit stream into partition 1 */
			else if (currentBitPartition == 2) {
				newBitStreamUpdateAddress = QSPI_ADDR_BIT_STREAM_1_PARTITION;
				xil_printf("newBitStreamUpdateAddress : %08X\r\n", newBitStreamUpdateAddress);
			}
		}
		// Release the mutex
		xSemaphoreGive(xQSPIMutex);
	}

	// Wait for the AT mutex
	if (xSemaphoreTake(xATMutex, portMAX_DELAY) == pdTRUE) {
		/* Download the given file name from the backend server and store it into QSPI flash on respective location */
		for (receivedBytes = 0; receivedBytes < fileSize; receivedBytes += NoOfBytes) {
			/* Reset the packet download flag and OTA expected Total received count variable */
			l_packetDownloaded = FALSE;
			g_OtaExpectedTotalRecvCount = 0;
			dataByte = 0;
			memset(HardwareActualBuffer, 0, sizeof(HardwareActualBuffer));

			/* Header start byte information as previously received bytes */
			headerStartByte = receivedBytes;

			/* In case of the last packet it will just download the remaining bytes */
			if ((receivedBytes + NoOfBytes - 1) > fileSize) {
				/* For last packet header end byte will be equivalent to file size - 1 */
				headerEndByte = (fileSize - 1);
			}
			else {
				/* Header end byte will be equivalent to OTA buffer size - 1 */
				headerEndByte = (headerStartByte + NoOfBytes - 1);
			}

			/* Calculate the Total expected receive bytes to get the proper OTA chunk */

			/* Received information format is like:
			 * +HTTPCPOST:8192,						// Here for the +HTTPCPOST:<data-len>, --> 12 bytes + 2 bytes of CRLF + up to 4 bytes based on data-len digits
			 *										// Here 2 bytes of the CRLF
			 * SEND OK								// Here for the SEND OK --> 7 bytes + 2 bytes of CRLF
			 *
			 * So Overall required expected received bytes is like (12 + 7 + 6 = 25) 25 bytes static + 4 bytes dynamic will be added into the response header
			 */
			dataByte = (headerEndByte - headerStartByte + 1);
			g_OtaExpectedTotalRecvCount = (dataByte + 25 + (count_digits(dataByte)));
			xil_printf("g_OtaExpectedTotalRecvCount : %d\r\n", g_OtaExpectedTotalRecvCount);

			/* Create the JSON object for the Payload structure type */
			OtaFileHeaderJSON = cJSON_CreateObject();

			/* Update the created JSON object to fill its key values */
			cJSON_AddStringToObject(OtaFileHeaderJSON, "file_name", fileName);
			cJSON_AddNumberToObject(OtaFileHeaderJSON, "start_byte", headerStartByte);
			cJSON_AddNumberToObject(OtaFileHeaderJSON, "end_byte", headerEndByte);

			/* Create the final Payload string by converting the JSON object into string format */
			OtaFileHeaderStr = cJSON_PrintUnformatted(OtaFileHeaderJSON);

			/* Once Payload final string is generated then free the JSON object memory */
			cJSON_Delete(OtaFileHeaderJSON);

			for (retryCnt = 0; ((retryCnt < OTA_PKT_DOWNLAOD_MAX_RETRY) && (!l_packetDownloaded)); retryCnt++) {
				xil_printf("OtaFileHeaderStr : %s\r\n", OtaFileHeaderStr);

				/* Reset the response buffer to collect the next command response */
				memset(HardwareResponseBufferStr, 0, sizeof(HardwareResponseBufferStr));
				g_OtaTotalRecvCount = 0;

				/* Calculate the Payload message length using the final created Payload string */
				payloadLength = strlen(OtaFileHeaderStr);

				/* Create the HTTP POST AT command string */
				snprintf(HTTPCommandStr, BUFFER_SIZE, "AT+HTTPCPOST=\"%s\",%d,1,\"content-type: application/json\"", OTA_GET_FILE_ENDPOINT, payloadLength);

				xil_printf("OtaFileHeader Message command : %s\r\n", HTTPCommandStr);

				/* Send the AT command to the ESP32 module and collect its response */
				retVal = SendUartATCommandWithResponse(HTTPCommandStr, 60, 1000, HardwareResponseBufferStr, sizeof(HardwareResponseBufferStr));

				if (retVal == XST_SUCCESS) {
					/* If HTTP POST AT command response is valid then proceed further */
					if(strstr(HardwareResponseBufferStr, ">") != NULL) {
						/* Reset the response buffer to collect the next command response */
						memset(HardwareResponseBufferStr, 0, sizeof(HardwareResponseBufferStr));

						/* Send the Paylod string to the ESP32 module and collect its response */
						retVal = SendUartATCommandWithResponse(OtaFileHeaderStr, 60, 3000, HardwareResponseBufferStr, sizeof(HardwareResponseBufferStr));

						if (retVal == XST_SUCCESS) {
							/* If Payload command response is valid then proceed further */
							if (g_OtaTotalRecvCount == g_OtaExpectedTotalRecvCount) {
								/* If expected length of response received then free resource as well */
								free(OtaFileHeaderStr);

								xil_printf("Requested chunk downloaded successfully!\r\n");

								/* Now extract the actual data information from the received response buffer */
								const char *comma_pos = strchr(HardwareResponseBufferStr, ',');		// Locate the comma in the response buffer string
								if (comma_pos != NULL) {
									comma_pos++;													// Increase the one position to set the offset to the data first byte
									memcpy(HardwareActualBuffer, comma_pos, dataByte);
									HardwareActualBuffer[dataByte] = '\0';

									/* Once data is extracted then calculate its hash value and update the final hash context */
									MD5_Update(&ctx, (const uint8_t *)HardwareActualBuffer, dataByte);
								}

								/* Write the extracted buffer actual data into the QSPI storage */
								// Wait for the QSPI mutex
								if (xSemaphoreTake(xQSPIMutex, portMAX_DELAY) == pdTRUE) {
									/* If the data chunk size is large than 4096 bytes then write it into the QSPI storage in multiple part */
									if(dataByte > 4096) {
										/* First write the half of the data bytes */
										retVal = qspiWrite(&g_qspiInstance, (uint8_t *)HardwareActualBuffer, (newBitStreamUpdateAddress + receivedBytes), (dataByte / 2));
										if (retVal == XST_SUCCESS) {
											/* Reset the Read buffer */
											memset(HardwareReadBuffer, 0, sizeof(HardwareReadBuffer));

											/* Read back the written data bytes from the storage */
											retVal = qspiRead(&g_qspiInstance, (uint8_t *)HardwareReadBuffer, (newBitStreamUpdateAddress + receivedBytes), (dataByte / 2));
											if (retVal == XST_SUCCESS) {
												if (memcmp(HardwareActualBuffer, HardwareReadBuffer, (dataByte / 2)) == 0) {
													xil_printf("Downloaded chunk partial half info written successfully into QSPI storage\r\n");
													/* If previous half data bytes written successfully then write the next half data bytes */
													retVal = qspiWrite(&g_qspiInstance, (uint8_t *)(HardwareActualBuffer + (dataByte / 2)), (newBitStreamUpdateAddress + receivedBytes + (dataByte / 2)), (dataByte / 2));
													if (retVal == XST_SUCCESS) {
														/* Reset the Read buffer */
														memset(HardwareReadBuffer, 0, sizeof(HardwareReadBuffer));

														/* Read back the written data bytes from the storage */
														retVal = qspiRead(&g_qspiInstance, (uint8_t *)HardwareReadBuffer, (newBitStreamUpdateAddress + receivedBytes + (dataByte / 2)), (dataByte / 2));
														if (retVal == XST_SUCCESS) {
															if (memcmp((HardwareActualBuffer + (dataByte / 2)), HardwareReadBuffer, (dataByte / 2)) == 0) {
																xil_printf("Downloaded chunk full info written successfully into QSPI storage\r\n");
																l_packetDownloaded = TRUE;	/* This is the final exit location of internal for loop so it confirms like data is available in storage */
															}
															else {
																xil_printf("[CRITICAL]: Failed to write downloaded chunk full info properly into QSPI storage\r\n");
															}
														}
														else {
															xil_printf("Failed to read back downloaded chunk full info from QSPI storage\r\n");
														}
													}
													else {

														xil_printf("Failed to write downloaded chunk full info into QSPI storage\r\n");
													}
												}
												else {
													xil_printf("[CRITICAL]: Failed to write downloaded chunk partial half info properly into QSPI storage\r\n");
												}
											}
											else {
												xil_printf("Failed to read back downloaded chunk partial half info from QSPI storage\r\n");
											}
										}
										else {
											xil_printf("Failed to write downloaded chunk partial half info into QSPI storage\r\n");
										}
									}
									/* If the data chunk size is less than 4096 bytes then write it into the QSPI storage in single shot */
									else {
										/* Write full buffer in single shot into QSPI storage */
										retVal = qspiWrite(&g_qspiInstance, (uint8_t *)HardwareActualBuffer, (newBitStreamUpdateAddress + receivedBytes), dataByte);
										if (retVal == XST_SUCCESS) {
											/* Reset the Read buffer */
											memset(HardwareReadBuffer, 0, sizeof(HardwareReadBuffer));

											/* Read back the written data bytes from the storage */
											retVal = qspiRead(&g_qspiInstance, (uint8_t *)HardwareReadBuffer, (newBitStreamUpdateAddress + receivedBytes), dataByte);
											if (retVal == XST_SUCCESS) {
												if (memcmp(HardwareActualBuffer, HardwareReadBuffer, dataByte) == 0) {
													xil_printf("Downloaded chunk written successfully into QSPI storage\r\n");
													l_packetDownloaded = TRUE;	/* This is the final exit location of internal for loop so it confirms like data is available in storage */
												}
												else {
													xil_printf("[CRITICAL]: Failed to write downloaded chunk properly into QSPI storage\r\n");
												}
											}
											else {
												xil_printf("Failed to read back downloaded chunk from QSPI storage\r\n");
											}
										}
										else {
											xil_printf("Failed to write downloaded chunk into QSPI storage\r\n");
										}
									}
									// Release the mutex
									xSemaphoreGive(xQSPIMutex);
								}
							}
							else {
								/* Received the invalid response for the Payload string command */
								xil_printf("Received the invalid response : %s\r\n", HardwareResponseBufferStr);
							}
						}
						else {
							/* Get the Timeout as Payload string response */
							xil_printf("Timeout occurred : %s\r\n", OtaFileHeaderStr);
						}
					}
					else {
						/* Received the invalid response for the HTTP POST AT command */
						xil_printf("Received the invalid response : %s\r\n", HardwareResponseBufferStr);
					}
				}
				else {
					/* Get the Timeout as AT Command response */
					xil_printf("Timeout occurred : %s\r\n", HTTPCommandStr);
				}
			}

			/* After the maximum number of the retry still the packet is not able to download then return the failure status */
			if ((retryCnt == OTA_PKT_DOWNLAOD_MAX_RETRY) && (!l_packetDownloaded)) {
				if (OtaFileHeaderStr != NULL) {
					// Free allocated resources
					free(OtaFileHeaderStr);
				}

				// Release the AT mutex
				xSemaphoreGive(xATMutex);

				if (retVal == XST_TIMEOUT) {
					/* Return the Time out status in case of the AT command time out occurred */
					return XST_TIMEOUT;
				}
				else {
					/* Return the failure status in case of the invalid response of AT command */
					return XST_FAILURE;
				}
			}

			/* In case of data not received properly then before retry to fetch it again wait for some time to allow some time to ESP to be in ready state */
			if(g_OtaTotalRecvCount < g_OtaExpectedTotalRecvCount) {
				mssleep(2000);
			}
		}
		// Release the AT mutex
		xSemaphoreGive(xATMutex);
	}

	/* Calculate the final hash value of the received buffer */
	MD5_Final(g_OtaRecvBitHashValue, &ctx);

	/* Again Read the full written image from the flash and calculate its md5sum for double confirmation */

	/* Assign the bytes number to read those much data from the storage */
	NoOfBytes = 4096;

	/* Initialize MD5SUM context */
	MD5_Init(&ctx1);

	// Wait for the QSPI mutex
	if (xSemaphoreTake(xQSPIMutex, portMAX_DELAY) == pdTRUE) {
		for (receivedBytes = 0; receivedBytes < fileSize; receivedBytes += NoOfBytes) {
			dataByte = 0;

			/* Header start byte information as previously received bytes */
			headerStartByte = receivedBytes;

			/* In case of the last packet it will just download the remaining bytes */
			if ((receivedBytes + NoOfBytes - 1) > fileSize) {
				/* For last packet header end byte will be equivalent to file size - 1 */
				headerEndByte = (fileSize - 1);
			}
			else {
				/* Header end byte will be equivalent to OTA buffer size - 1 */
				headerEndByte = (headerStartByte + NoOfBytes - 1);
			}

			dataByte = (headerEndByte - headerStartByte + 1);
//			xil_printf("dataByte : %d\r\n", dataByte);

			/* Reset the response buffer to collect the next command response */
			memset(HardwareReadBuffer, 0, sizeof(HardwareReadBuffer));

			/* Read data bytes from the storage */
			retVal = qspiRead(&g_qspiInstance, (uint8_t *)HardwareReadBuffer, (newBitStreamUpdateAddress + receivedBytes), dataByte);
			if (retVal == XST_SUCCESS) {
//				xil_printf("Successfully read data bytes from QSPI storage\r\n");
				/* Once data is read then calculate its hash value and update the final hash context */
				MD5_Update(&ctx1, (const uint8_t *)HardwareReadBuffer, dataByte);
			}
			else {
				xil_printf("Failed to read data bytes from QSPI storage\r\n");
			}
		}

		// Release the mutex
		xSemaphoreGive(xQSPIMutex);
	}

	/* Calculate the final hash value of the Read buffer */
	MD5_Final(g_OtaReadBitHashValue, &ctx1);

	/* Everything is perfect then return SUCCESS */
	return XST_SUCCESS;
}
