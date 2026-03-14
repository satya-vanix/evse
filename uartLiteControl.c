/*
 * =====================================================================================
 * File Name:    uartLiteControl.c
 * Author:       Vanix Technologies PVT. LTD.
 * Date Created: 2025-02-17
 * Description:  This source file contains the implementation of the functions that
 *               control the UART Lite (Universal Asynchronous Receiver/Transmitter)
 *               module for communication between the Xilinx PL (Programmable Logic)
 *               and other system components. These functions handle initialization,
 *               configuration, and data transmission/reception through the UART Lite
 *               interface.
 *
 *               The UART Lite module enables serial communication for transmitting
 *               and receiving data, which is crucial for system diagnostics, control,
 *               and monitoring during the operation of the electric vehicle charging system.
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
#include "uartLiteControl.h"

/*
 * =====================================================================================
 * Global Variables
 * =====================================================================================
 */
INTC IntcInstance;		/* The instance of the Interrupt Controller */
XUart UartInst;  		/* The instance of the UART Device */

// Legacy variable name for backward compatibility
#define UartLiteInst UartInst

/*
 * =====================================================================================
 * Static Global Variables
 * =====================================================================================
 */

/* Cloud Communication related buffers and count variables */
static char m_SendBuffer[BUFFER_SIZE] = {0};
static char m_RecvBuffer[BUFFER_SIZE] = {0};
static char m_OtaRecvBuffer[OTA_BUFFER_SIZE + 1024] = {0};	// Added the 1024 bytes extra to handle the extra header info
static int TotalSentCount = 0;
static int TotalRecvCount = 0;

/*
 * =====================================================================================
 * Static Function Definitions
 * =====================================================================================
 */

/*
 * =====================================================================================
 * Global Function Definitions
 * =====================================================================================
 */

/**
 * @brief  Initializes the UART interrupt.
 *
 * This function sets up the UART interrupt, including configuring the interrupt controller,
 * initializing the UART instance, and associating the appropriate interrupt ID. It should
 * be called to enable the UART interrupt functionality.
 */
int UartIntpInit(INTC *IntcInstancePtr, XUart *UartInstancePtr,
		u16 UartDeviceId, u16 UartIntrId)
{
	int Status;

#ifdef USE_UART_LITE
	Status = XUartLite_Initialize(UartInstancePtr, UartDeviceId);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}
	Status = XUartLite_SelfTest(UartInstancePtr);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}
	Status = UartSetupIntrSystem(IntcInstancePtr,
			UartInstancePtr,
			UartIntrId);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}

	XUartLite_SetRecvHandler(UartInstancePtr, UartRecvHandler,
			UartInstancePtr);
	XUartLite_EnableInterrupt(UartInstancePtr);

#elif defined(USE_UART_PS)
	XUartPs_Config *Config;

	// Initialize UART PS
	Config = XUartPs_LookupConfig(UartDeviceId);
	if (NULL == Config) {
		return XST_FAILURE;
	}

	Status = XUartPs_CfgInitialize(UartInstancePtr, Config, Config->BaseAddress);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}

	Status = XUartPs_SelfTest(UartInstancePtr);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}

	// Set baud rate (115200 is typical for ESP32)
	XUartPs_SetBaudRate(UartInstancePtr, 115200);

	// Setup interrupt system
	Status = UartSetupIntrSystem(IntcInstancePtr, UartInstancePtr, UartIntrId);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}

	// Set receive handler
	XUartPs_SetHandler(UartInstancePtr, (XUartPs_Handler)UartRecvHandler, UartInstancePtr);

	// Enable receive interrupt
	XUartPs_SetInterruptMask(UartInstancePtr, XUARTPS_IXR_RXOVR | XUARTPS_IXR_TOUT | XUARTPS_IXR_RXFULL);
#endif

	return XST_SUCCESS;
}

/**
 * @brief  UART send interrupt handler.
 *
 * This function handles the UART send interrupt, processing the event data and executing
 * any necessary callback functions. It is typically called when the UART transmit buffer
 * is empty or ready for more data to be sent.
 */
void UartSendHandler(void *CallBackRef, unsigned int EventData)
{
//	TotalSentCount = EventData;
}

/**
 * @brief  UART receive interrupt handler.
 *
 * This function handles the UART receive interrupt, processing the event data and executing
 * any necessary callback functions. It is typically called when the UART receive buffer
 * has data available to be read.
 */
#ifdef USE_UART_LITE
void UartRecvHandler(void *CallBackRef, unsigned int EventData)
{
	uint8_t data = 0;

	u8 i = XUartLite_Recv(&UartInst, (uint8_t *)&data, 1);
//	xil_printf("%c",data);

	if (i == 1) {
		if(g_isOtaInProgress) {
			m_OtaRecvBuffer[TotalRecvCount] = data;
		}
		else {
			m_RecvBuffer[TotalRecvCount] = data;
		}
		TotalRecvCount++;
	}
}
#elif defined(USE_UART_PS)
void UartRecvHandler(void *CallBackRef, u32 Event, unsigned int EventData)
{
	uint8_t data = 0;

	// For PS UART, EventData contains number of bytes available
	if (EventData > 0) {
		u8 i = XUartPs_Recv(&UartInst, (uint8_t *)&data, 1);
//		xil_printf("%c",data);

		if (i == 1) {
			if(g_isOtaInProgress) {
				m_OtaRecvBuffer[TotalRecvCount] = data;
			}
			else {
				m_RecvBuffer[TotalRecvCount] = data;
			}
			TotalRecvCount++;
		}
	}
}
#endif

/**
 * @brief  Sets up the UART interrupt system.
 *
 * This function configures the interrupt system to handle interrupts for the UART device,
 * including connecting the UART interrupt to the interrupt controller and enabling the
 * interrupt. It should be called to properly initialize the interrupt system for UART.
 */
int UartSetupIntrSystem(INTC *IntcInstancePtr, XUart *UartInstancePtr, u16 UartIntrId)
{
	int Status;

	XScuGic_Config *IntcConfig;

	/*
	 * Initialize the interrupt controller driver so that it is ready to
	 * use.
	 */
	IntcConfig = XScuGic_LookupConfig(INTC_DEVICE_ID);
	if (NULL == IntcConfig) {
		return XST_FAILURE;
	}

	Status = XScuGic_CfgInitialize(IntcInstancePtr, IntcConfig,
			IntcConfig->CpuBaseAddress);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}

	XScuGic_SetPriorityTriggerType(IntcInstancePtr, UartIntrId,
			0x40, 0x3);//A0

#ifdef USE_UART_LITE
	Status = XScuGic_Connect(IntcInstancePtr, UartIntrId,
			(Xil_ExceptionHandler)XUartLite_InterruptHandler,
			UartInstancePtr);
#elif defined(USE_UART_PS)
	Status = XScuGic_Connect(IntcInstancePtr, UartIntrId,
			(Xil_ExceptionHandler)XUartPs_InterruptHandler,
			UartInstancePtr);
#endif
	if (Status != XST_SUCCESS) {
		return Status;
	}

	XScuGic_Enable(IntcInstancePtr, UartIntrId);

	/*
	 * Initialize the exception table.
	 */
	Xil_ExceptionInit();

	/*
	 * Register the interrupt controller handler with the exception table.
	 */
	Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_INT,
			(Xil_ExceptionHandler)INTC_HANDLER,
			IntcInstancePtr);

	/*
	 * Enable exceptions.
	 */
	Xil_ExceptionEnable();

	return XST_SUCCESS;
}

/**
 * @brief  Disables the UART interrupt system.
 *
 * This function disables the UART interrupt system by disconnecting the UART interrupt
 * from the interrupt controller and disabling the interrupt. It should be called when interrupt
 * handling for the UART device is no longer needed.
 */
void UartDisableIntrSystem(INTC *IntcInstancePtr, u16 UartIntrId)
{
	XScuGic_Disable(IntcInstancePtr, UartIntrId);
	XScuGic_Disconnect(IntcInstancePtr, UartIntrId);
}

/**
 * @brief  Sends an AT command via UART to the ESP32 module.
 *
 * This function sends the specified AT command to the ESP32 module over UART, including a delay
 * after the command is sent if required. The command is sent as a string, and the function ensures
 * the correct length and formatting of the command before transmission.
 */
int SendUartATCommand(char* strCommand,int nLengthCommand, int nSendPostDelay)
{
	memset(m_RecvBuffer,0,BUFFER_SIZE);
	memset(m_SendBuffer,0,BUFFER_SIZE);
	TotalRecvCount = 0;
	TotalSentCount = 0;

	xil_printf("\r\nSending Command: %s\r\n", strCommand);

	/* Adding the CRLF suffix in the string in case of the AT commands */
	if (strstr(strCommand, "AT") != NULL) {
		sprintf(m_SendBuffer, "%s\r\n", strCommand);
	}
	/* For the normal cloud payloads no need of the CRLF suffix */
	else {
		sprintf(m_SendBuffer, "%s", strCommand);
	}

	u8 len = strlen(m_SendBuffer);

	for(uint8_t count = 0 ; count < len; count++)
	{
#ifdef USE_UART_LITE
		XUartLite_SendByte(UART_BASEADDR, m_SendBuffer[count]);
#elif defined(USE_UART_PS)
		XUartPs_SendByte(UART_BASEADDR, m_SendBuffer[count]);
#endif
		usleep(20 * 1000);
	}

	mssleep(nSendPostDelay);
	TotalSentCount = len;

	return TotalSentCount;
}

/**
 * @brief  Sends an AT command via UART and waits for a response.
 *
 * This function sends an AT command to the ESP32 module over UART and waits for a response. It
 * stores the response in the provided buffer, with a specified delay after sending the command.
 * The function ensures that the command is transmitted correctly and handles the response appropriately.
 */
int SendUartATCommandWithResponse(char* strCommand, int nLengthCommand, int nSendPostDelay,
		char* responseBuffer, int responseBufferSize)
{
	memset(m_RecvBuffer, 0, BUFFER_SIZE);
	memset(m_SendBuffer, 0, BUFFER_SIZE);
	memset(m_OtaRecvBuffer, 0, sizeof(m_OtaRecvBuffer));
	TotalRecvCount = 0;
	TotalSentCount = 0;

	xil_printf("\r\nSending Command: %s\r\n", strCommand);

	/* Adding the CRLF suffix in the string in case of the AT commands */
	if (strstr(strCommand, "AT") != NULL) {
		sprintf(m_SendBuffer, "%s\r\n", strCommand);
	}
	/* For the normal cloud payloads no need of the CRLF suffix */
	else {
		sprintf(m_SendBuffer, "%s", strCommand);
	}

	u8 len = strlen(m_SendBuffer);

	for (uint8_t count = 0; count < len; count++) {
#ifdef USE_UART_LITE
		XUartLite_SendByte(UART_BASEADDR, m_SendBuffer[count]);
#elif defined(USE_UART_PS)
		XUartPs_SendByte(UART_BASEADDR, m_SendBuffer[count]);
#endif
		usleep(20 * 1000);
	}

	// Wait for response
	int timeout = 5000; // 5 seconds timeout
	int time = 0;

	if (g_isOtaInProgress && (strstr(strCommand, "AT") == NULL)) {
		while ((TotalRecvCount != g_OtaExpectedTotalRecvCount) && time < timeout) {
			mssleep(10);
			time += 10;
		}
	}
	else {
		//mssleep(nSendPostDelay);
		timeout+=nSendPostDelay;
	}
	TotalSentCount = len;

#if 0
	time = 0;
	while (TotalRecvCount == 0 && time < timeout) {
		mssleep(10);
		time += 10;
	}

	// Copy received data to response buffer
	if (TotalRecvCount > 0) {
		int copySize = (TotalRecvCount < responseBufferSize - 1) ? TotalRecvCount : (responseBufferSize - 1);
		if(g_isOtaInProgress) {
			memcpy(responseBuffer, m_OtaRecvBuffer, copySize);
		}
		else {
			memcpy(responseBuffer, m_RecvBuffer, copySize);
		}
		responseBuffer[copySize] = '\0'; // Null-terminate

		if(!g_isOtaInProgress) {
			xil_printf("\r\nResponse: %s\r\n", responseBuffer);
		}
		else if(g_isOtaInProgress && (TotalRecvCount < BUFFER_SIZE)) {
			xil_printf("\r\nResponse: %s\r\n", responseBuffer);
			g_OtaTotalRecvCount = TotalRecvCount;
		}
		else {
			xil_printf("\r\nTotalRecvCount: %d\r\n", TotalRecvCount);
			g_OtaTotalRecvCount = TotalRecvCount;
		}

		return XST_SUCCESS;
	}
	else if (time >= timeout) {
		xil_printf("\r\nNo response from server within defined timeout\r\n");
		return XST_TIMEOUT;
	}
#endif

#if 1
	time = 0;
	while (time < timeout) 
	{
		// Copy received data to response buffer
		if (TotalRecvCount > 0) 
		{
			/* Check if Responce contains OK msg */
			if (((g_isOtaInProgress) && ((strstr(m_OtaRecvBuffer, "OK") != NULL) || (TotalRecvCount == g_OtaExpectedTotalRecvCount))) || (strstr(m_RecvBuffer, "OK") != NULL))
			{
				int copySize = (TotalRecvCount < responseBufferSize - 1) ? TotalRecvCount : (responseBufferSize - 1);
				if(g_isOtaInProgress) {
					memcpy(responseBuffer, m_OtaRecvBuffer, copySize);
				}
				else {
					memcpy(responseBuffer, m_RecvBuffer, copySize);
				}
				responseBuffer[copySize] = '\0'; // Null-terminate

				if(!g_isOtaInProgress) {
					xil_printf("\r\nResponse: %s\r\n", responseBuffer);
				}
				else if(g_isOtaInProgress && (TotalRecvCount < BUFFER_SIZE)) {
					xil_printf("\r\nResponse: %s\r\n", responseBuffer);
					g_OtaTotalRecvCount = TotalRecvCount;
				}
				else {
					xil_printf("\r\nTotalRecvCount: %d\r\n", TotalRecvCount);
					g_OtaTotalRecvCount = TotalRecvCount;
				}

				return XST_SUCCESS;
			}
			else if (((g_isOtaInProgress) && strstr(m_OtaRecvBuffer, "busy p...") != NULL) || (strstr(m_RecvBuffer, "busy p...") != NULL))
			{
				xil_printf("\r\nDevice busy!!\r\n");
				return XST_TIMEOUT;
			}

			/* Sleep in inc timeout counter */
			mssleep(1);
			time += 1;
		}
	}

	if (time >= timeout) {
		xil_printf("\r\nNo response from server within defined timeout\r\n");
		return XST_TIMEOUT;
	}
#endif
}
