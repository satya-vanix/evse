/*
 * =====================================================================================
 * File Name:    rtcControl.c
 * Author:       Vanix Technologies PVT. LTD.
 * Date Created: 2025-04-15
 * Description:  This source file contains the implementation of functions required for
 *               handling Real-Time Clock (RTC) communication using the MCP7940N module.
 *               It manages I2C communication, timekeeping operations, and provides
 *               functions to set, retrieve, and synchronize time.
 *
 *               The file includes functions for BCD conversion, oscillator control,
 *               time configuration, and synchronization with an external NTP server.
 *               It ensures accurate timekeeping and supports seamless integration
 *               with embedded systems requiring real-time tracking.
 *
 * Revision History:
 * Version 1.0 - 2025-04-15 - Initial version.
 * =====================================================================================
 */


#include "rtcControl.h"


/*
 * =====================================================================================
 * Static Function Definitions
 * =====================================================================================
 */

/**
 * @brief Converts a Binary-Coded Decimal (BCD) value to a decimal value.
 *
 * @param val The BCD-encoded value (0x00 to 0x99).
 * @return The decimal representation of the BCD value.
 */
static uint8_t bcd_to_dec(uint8_t val) {
	return ((val >> 4) * 10) + (val & 0x0F);
}

/**
 * @brief Converts a decimal value to Binary-Coded Decimal (BCD) format.
 *
 * @param val The decimal value (0-99) to convert.
 * @return The BCD-encoded representation of the decimal value.
 */
static uint8_t dec_to_bcd(uint8_t val) {
	return ((val / 10) << 4) | (val % 10);
}

/**
 * @brief Checks if the RTC oscillator is running.
 *
 * This function reads the seconds register of the MCP7940N RTC to determine
 * if the oscillator is running by checking the ST (Start Oscillator) bit (bit 7).
 *
 * @param IicPtr Pointer to the I2C instance used for communication.
 * @return XST_SUCCESS if the oscillator is running, XST_FAILURE otherwise.
 */
static int rtcIsOscillatorRunning(XIicPs *IicPtr)
{
	uint8_t reg = MCP7940N_REG_RTCSEC;
	uint8_t sec_reg;
	int status;

    if (IicPtr == NULL) {
        xil_printf("ERROR: NULL IicPtr\r\n");
        return XST_FAILURE;
    }

    // Read seconds register
    status = XIicPs_MasterSendPolled(IicPtr, &reg, 1, MCP7940N_I2C_ADDRESS);
    if (status != XST_SUCCESS) {
        xil_printf("ERROR: I2C write failed (%d)\r\n", status);
        return XST_FAILURE;
    }

    mssleep(500);

    status = XIicPs_MasterRecvPolled(IicPtr, &sec_reg, 1, MCP7940N_I2C_ADDRESS);
    if (status != XST_SUCCESS) {
        xil_printf("ERROR: I2C read failed (%d)\r\n", status);
        return XST_FAILURE;
    }

    mssleep(500);

    // Check if ST bit (bit 7) is set
    if (sec_reg & 0x80) {
        xil_printf("RTC Oscillator is running.\r\n");
        return XST_SUCCESS;
    } else {
        xil_printf("RTC Oscillator is NOT running!\r\n");
        return XST_FAILURE;
    }
}

/**
 * @brief Sets the RTC to 24-hour mode.
 *
 * This function modifies the hour register of the MCP7940N RTC to ensure
 * that it operates in 24-hour format by clearing the 12/24-hour mode bit (bit 6).
 *
 * @param IicPtr Pointer to the I2C instance used for communication.
 * @return XST_SUCCESS if the operation was successful, XST_FAILURE otherwise.
 */
int rtcSet24HourMode(XIicPs *IicPtr)
{
    uint8_t reg = MCP7940N_REG_RTCHOUR;
    uint8_t hour_reg;
    int status;

    // Read the current hour register
    status = XIicPs_MasterSendPolled(IicPtr, &reg, 1, MCP7940N_I2C_ADDRESS);
    if (status != XST_SUCCESS) return XST_FAILURE;

    mssleep(500);

    status = XIicPs_MasterRecvPolled(IicPtr, &hour_reg, 1, MCP7940N_I2C_ADDRESS);
    if (status != XST_SUCCESS) return XST_FAILURE;

    mssleep(500);

    // Ensure the 12/24-hour bit (bit 6) is cleared for 24-hour mode
    hour_reg &= ~0x40;

    uint8_t buffer[2] = {MCP7940N_REG_RTCHOUR, hour_reg};
    status = XIicPs_MasterSendPolled(IicPtr, buffer, 2, MCP7940N_I2C_ADDRESS);
    if (status != XST_SUCCESS) return XST_FAILURE;

    xil_printf("24-hour mode set.\r\n");
    return XST_SUCCESS;
}

/**
 * @brief Enables the oscillator of the MCP7940N RTC.
 *
 * This function sets the ST (Start) bit in the RTCSEC register (bit 7) to enable
 * the oscillator of the MCP7940N RTC. The function first reads the current value
 * of the RTCSEC register, modifies it to enable the oscillator, and writes it back.
 *
 * @param IicPtr Pointer to the I2C instance used for communication.
 * @return XST_SUCCESS if the oscillator was successfully enabled, XST_FAILURE otherwise.
 */
static int rtcEnableOscillator(XIicPs *IicPtr)
{
	uint8_t reg = MCP7940N_REG_RTCSEC;
	uint8_t sec_reg;
	int status;

	if (IicPtr == NULL) {
		xil_printf("ERROR: NULL IicPtr\r\n");
		return XST_FAILURE;
	}

	// Read current seconds register
	status = XIicPs_MasterSendPolled(IicPtr, &reg, 1, MCP7940N_I2C_ADDRESS);
	if (status != XST_SUCCESS) {
		xil_printf("ERROR: I2C write failed (%d)\r\n", status);
		return XST_FAILURE;
	}

	mssleep(500);

	status = XIicPs_MasterRecvPolled(IicPtr, &sec_reg, 1, MCP7940N_I2C_ADDRESS);
	if (status != XST_SUCCESS) {
		xil_printf("ERROR: I2C read failed (%d)\r\n", status);
		return XST_FAILURE;
	}

	mssleep(500);

	// Set ST bit (bit 7) to enable oscillator
	sec_reg |= 0x80;

	uint8_t buffer[2] = {MCP7940N_REG_RTCSEC, sec_reg};

	status = XIicPs_MasterSendPolled(IicPtr, buffer, 2, MCP7940N_I2C_ADDRESS);
	if (status != XST_SUCCESS) {
		xil_printf("ERROR: I2C write failed (%d)\r\n", status);
		return XST_FAILURE;
	}

	mssleep(500);

	xil_printf("RTC Oscillator Enabled Successfully!\r\n");
	return XST_SUCCESS;
}

/**
 * @brief Disables the oscillator of the MCP7940N RTC.
 *
 * This function clears the ST (Start) bit in the RTCSEC register (bit 7) to disable
 * the oscillator of the MCP7940N RTC. It first reads the current value of the RTCSEC
 * register, modifies it to disable the oscillator, and writes it back.
 *
 * @param IicPtr Pointer to the I2C instance used for communication.
 * @return XST_SUCCESS if the oscillator was successfully disabled, XST_FAILURE otherwise.
 */
static int rtcDisableOscillator(XIicPs *IicPtr)
{
	uint8_t reg = MCP7940N_REG_RTCSEC;
	uint8_t sec_reg;
	int status;

	if (IicPtr == NULL) {
		xil_printf("ERROR: NULL IicPtr\r\n");
		return XST_FAILURE;
	}

	// Read current seconds register
	status = XIicPs_MasterSendPolled(IicPtr, &reg, 1, MCP7940N_I2C_ADDRESS);
	if (status != XST_SUCCESS) {
		xil_printf("ERROR: I2C write failed (%d)\r\n", status);
		return XST_FAILURE;
	}

	mssleep(500);

	status = XIicPs_MasterRecvPolled(IicPtr, &sec_reg, 1, MCP7940N_I2C_ADDRESS);
	if (status != XST_SUCCESS) {
		xil_printf("ERROR: I2C read failed (%d)\r\n", status);
		return XST_FAILURE;
	}

	mssleep(500);

	// Clear ST bit (bit 7) to disable oscillator
	sec_reg &= ~0x80;

	uint8_t buffer[2] = {MCP7940N_REG_RTCSEC, sec_reg};

	status = XIicPs_MasterSendPolled(IicPtr, buffer, 2, MCP7940N_I2C_ADDRESS);
	if (status != XST_SUCCESS) {
		xil_printf("ERROR: I2C write failed (%d)\r\n", status);
		return XST_FAILURE;
	}

	mssleep(500);

	xil_printf("RTC Oscillator Disabled Successfully!\r\n");
	return XST_SUCCESS;
}

/*
 * =====================================================================================
 * Global Function Definitions
 * =====================================================================================
 */

/**
 * @brief Initializes the MCP7940N RTC module.
 *
 * This function checks whether the RTC oscillator is running. If not, it enables the oscillator.
 * It also ensures that the RTC is set to 24-hour mode.
 */
int rtcInit()
{
	int status;

	// Check if oscillator is already running
	status = rtcIsOscillatorRunning(&Iic0);
	if (status != XST_SUCCESS) {
		// Enable the RTC oscillator if not running
		status = rtcEnableOscillator(&Iic0);
		if (status != XST_SUCCESS) {
			xil_printf("ERROR: RTC Oscillator Enable Failed!\r\n");
			return XST_FAILURE;
		}
	} else {
		xil_printf("RTC Oscillator already running.\r\n");
	}

	// Ensure 24-hour mode is set
	status = rtcSet24HourMode(&Iic0);
	if (status != XST_SUCCESS) {
		xil_printf("WARNING: Failed to set 24-hour mode.\r\n");
	}

	xil_printf("RTC Initialized Successfully!\r\n");
	return XST_SUCCESS;
}

/**
 * @brief Performs a self-test on the MCP7940N RTC.
 *
 * This function checks the communication with the RTC by reading the seconds register.
 * It ensures that the retrieved value is within the valid range (0-59).
 */
int rtcSelfTest() {
	uint8_t reg = MCP7940N_REG_RTCSEC; // Address of seconds register
	uint8_t sec_reg = 0;
	uint8_t seconds;
	int status;

	// Write the register address to set the read pointer
	status = XIicPs_MasterSendPolled(&Iic0, &reg, 1, MCP7940N_I2C_ADDRESS);
	if (status != XST_SUCCESS) {
		xil_printf("RTC Self-Test Failed. I2C Write Error.\r\n");
		return XST_FAILURE;
	}

	mssleep(500);

	// Read one byte from the seconds register
	status = XIicPs_MasterRecvPolled(&Iic0, &sec_reg, 1, MCP7940N_I2C_ADDRESS);
	if (status != XST_SUCCESS) {
		xil_printf("RTC Self-Test Failed. I2C Read Error.\r\n");
		return XST_FAILURE;
	}

	mssleep(500);

	// Mask out the oscillator bit (bit 7) and convert from BCD
	seconds = ((sec_reg & 0x70) >> 4) * 10 + (sec_reg & 0x0F);

	// Validate if the value is within expected range (0-59)
	if (seconds > 59) {
		xil_printf("RTC Self-Test Failed. Invalid Seconds Value: 0x%02X (%d)\r\n", sec_reg, seconds);
		return XST_FAILURE;
	}

	xil_printf("RTC Self-Test Passed. Seconds Register: %02d\r\n", seconds);
	return XST_SUCCESS;
}

/**
 * @brief Synchronizes the RTC with the current date and time from an NTP server via ESP32.
 *
 * This function configures the ESP32 to use an NTP server, retrieves the current time,
 * parses the response, and updates the RTC module accordingly.
 */
int rtcSyncDateTime() {
    char responseBuffer[64] = {0};
    RTC_DateTime rtc;
    char dayOfWeek[4], monthStr[4];
    int fullYear;  // Temporary variable for the full 4-digit year
    int status;
    static const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
    		"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    static const char *weekdays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

    // Wait for the AT mutex
    if (xSemaphoreTake(xATMutex, portMAX_DELAY) == pdTRUE) {
    	char *ntpConfigCmd = "AT+CIPSNTPCFG=1,0,\"pool.ntp.org\"";
    	char *ntpTimeCmd = "AT+CIPSNTPTIME?";

        /* Send AT Command to Configure the NTP Server */
        status = SendUartATCommandWithResponse(ntpConfigCmd, strlen(ntpConfigCmd),
                                               5000, responseBuffer, sizeof(responseBuffer));

        if (status == XST_SUCCESS) {
            /* Check if the response contains "OK" and "+TIME_UPDATED" */
            if (strstr(responseBuffer, "OK") != NULL) {
                xil_printf("NTP configured successfully.\r\n");

                /* Reset the response buffer for the next command */
                memset(responseBuffer, 0, sizeof(responseBuffer));

                /* Send AT Command to Get Time from ESP32 */
                status = SendUartATCommandWithResponse(ntpTimeCmd, strlen(ntpTimeCmd),
                                                       1000, responseBuffer, sizeof(responseBuffer));

                if (status == XST_SUCCESS) {
                    /* Check if the response contains "+CIPSNTPTIME:" */
                    if (strstr(responseBuffer, "+CIPSNTPTIME:") != NULL) {
                        xil_printf("NTP time fetched successfully: %s\r\n", responseBuffer);
                    } else {
                        xil_printf("ERROR: Invalid NTP time response: %s\r\n", responseBuffer);
                        xSemaphoreGive(xATMutex);
                        return XST_FAILURE;
                    }
                } else {
                    xil_printf("ERROR: Timeout occurred while fetching NTP time.\r\n");
                    xSemaphoreGive(xATMutex);
                    return XST_TIMEOUT;
                }
            } else {
                xil_printf("ERROR: Invalid response for NTP config: %s\r\n", responseBuffer);
                xSemaphoreGive(xATMutex);
                return XST_FAILURE;
            }
        } else {
            xil_printf("ERROR: Timeout occurred during NTP configuration.\r\n");
            xSemaphoreGive(xATMutex);
            return XST_TIMEOUT;
        }

        // Release the AT mutex after all operations
        xSemaphoreGive(xATMutex);
    }

    char *timeLine = strstr(responseBuffer, "+CIPSNTPTIME:");
    if (timeLine == NULL) {
        xil_printf("ERROR: Time info not found in response\r\n");
        return XST_FAILURE;
    }

    if (sscanf(timeLine, "+CIPSNTPTIME:%3s %3s %hhu %hhu:%hhu:%hhu %d",
               dayOfWeek, monthStr, &rtc.date, &rtc.hours, &rtc.minutes, &rtc.seconds, &fullYear) != 7) {
        xil_printf("ERROR: Failed to parse time response\r\n");
        return XST_FAILURE;
    }

    // Convert fullYear properly
    if (fullYear >= 2000) {
        rtc.year = fullYear - 2000;  // 2025 -> 25, 2033 -> 33
    } else {
        rtc.year = fullYear - 1900;
    }

    // Convert month string to integer
    rtc.month = 0;
    for (int i = 0; i < 12; i++) {
        if (strncmp(monthStr, months[i], 3) == 0) {
            rtc.month = i + 1;
            break;
        }
    }
    if (rtc.month == 0) {
        xil_printf("ERROR: Invalid month in time response (%s)\r\n", monthStr);
        return XST_FAILURE;
    }

    // Convert dayOfWeek string to integer (1=Monday, 7=Sunday)
    rtc.day = 0;
    for (int i = 0; i < 7; i++) {
        if (strncmp(dayOfWeek, weekdays[i], 3) == 0) {
            rtc.day = (i == 0) ? 7 : i; // Convert Sunday to 7, rest remain same
            break;
        }
    }
    if (rtc.day == 0) {
        xil_printf("ERROR: Invalid day in time response (%s)\r\n", dayOfWeek);
        return XST_FAILURE;
    }

    // Update RTC with the fetched time
    return rtcSetDateTime(&rtc);
}


/**
 * @brief Reads the current date and time from the RTC and formats it as an ISO 8601 string.
 *
 * This function retrieves the time from the MCP7940N RTC over I2C, converts
 * the BCD-encoded values to decimal, and formats the result into an ISO 8601
 * string (YYYY-MM-DDTHH:MM:SSZ).
 */
int rtcGetDateTime(char *rtcTimeStr, size_t bufferSize) {
    uint8_t reg = MCP7940N_REG_RTCSEC;
    uint8_t data[7];
    int status;

    // Write register address
    status = XIicPs_MasterSendPolled(&Iic0, &reg, 1, MCP7940N_I2C_ADDRESS);
    if (status != XST_SUCCESS) {
        xil_printf("ERROR: RTC Read Failed (Write Step)!\r\n");
        return XST_FAILURE;
    }

    // Read time data
    status = XIicPs_MasterRecvPolled(&Iic0, data, 7, MCP7940N_I2C_ADDRESS);
    if (status != XST_SUCCESS) {
        xil_printf("ERROR: RTC Read Failed!\r\n");
        return XST_FAILURE;
    }

    // Convert BCD to Decimal
    RTC_DateTime rtc;
    rtc.seconds = bcd_to_dec(data[0] & 0x7F);
    rtc.minutes = bcd_to_dec(data[1] & 0x7F);
    rtc.hours   = bcd_to_dec(data[2] & 0x3F);
    rtc.day     = bcd_to_dec(data[3] & 0x07);
    rtc.date    = bcd_to_dec(data[4] & 0x3F);
    rtc.month   = bcd_to_dec(data[5] & 0x1F);
    rtc.year    = bcd_to_dec(data[6]);  // Convert 2-digit year to 4-digit

    // Format time as "YYYY-MM-DDTHH:MM:SSZ"
    snprintf(rtcTimeStr, bufferSize, "%04d-%02d-%02dT%02d:%02d:%02dZ",
             2000 + rtc.year, rtc.month, rtc.date,
             rtc.hours, rtc.minutes, rtc.seconds);

    return XST_SUCCESS;
}

/**
 * @brief Sets the date and time on the MCP7940N RTC.
 *
 * This function writes the provided date and time to the RTC over I2C.
 * The oscillator is enabled automatically if needed.
 */
int rtcSetDateTime(RTC_DateTime *rtc) {
    uint8_t data[8];

    data[0] = MCP7940N_REG_RTCSEC;    // Start register (Seconds)
    data[1] = dec_to_bcd(rtc->seconds) | 0x80;  // Enable oscillator
    data[2] = dec_to_bcd(rtc->minutes);
    data[3] = dec_to_bcd(rtc->hours);
    data[4] = dec_to_bcd(rtc->day) | 0x08;  // Ensure oscillator runs
    data[5] = dec_to_bcd(rtc->date);
    data[6] = dec_to_bcd(rtc->month);
    data[7] = dec_to_bcd(rtc->year);

    // Write all date-time registers in one transaction
    int status = XIicPs_MasterSendPolled(&Iic0, data, 8, MCP7940N_I2C_ADDRESS);
    if (status != XST_SUCCESS) {
        xil_printf("RTC Write Failed!\r\n");
        return XST_FAILURE;
    }

    xil_printf("RTC Time Set Successfully!\r\n");
    return XST_SUCCESS;
}
