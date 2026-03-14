/*
 * =====================================================================================
 * File Name:    cpControl.c
 * Author:       Vanix Technologies PVT. LTD.
 * Date Created: 2025-02-12
 * Description:  This source file contains the implementation of the functions that
 *               control the CP (Control Pilot) signal management for the electric
 *               vehicle charging process. These functions handle the generation,
 *               monitoring, and regulation of the CP signal based on the charging
 *               requirements and communication protocol.
 *
 *               The CP signal plays a vital role in communication between the
 *               charging station and the electric vehicle, ensuring safe and
 *               efficient charging by regulating the signal's voltage and current
 *               levels.
 *
 * Revision History:
 * Version 1.0 - 2025-02-12 - Initial version.
 * =====================================================================================
 */

/*
 * =====================================================================================
 * Include Headers
 * =====================================================================================
 */
#include "cpControl.h"

/*
 * =====================================================================================
 * Macros / Constants
 * =====================================================================================
 */
#define CP_MODULE_DEBUG		1

#define SIMULATION 			0

/*
 * =====================================================================================
 * Global Variables
 * =====================================================================================
 */
TaskHandle_t xCPTask;						// CP Task handler pointer
bool g_isDeviceFaulted = FALSE;				// Global flag to track the Device Fault status

char g_bitFirmwareVersionStr[6] = {0};		// Bit stream firmware version string

/*
 * =====================================================================================
 * Static Global Variables
 * =====================================================================================
 */
static u16 g_bitFirmwareVersion;		//Bit Stream firmware version
static CPState_e g_CPState;				//Control Pilot Pin state
static PPState_e g_PPState;				//Control Pilot Pin state
static u16 g_voltageValue;			//Voltage value (scaled from PL)
static u16 g_currentValue;			//current value (scaled from PL)
static u8 g_earthFaultValue;			//Earth Fault value
static u8 g_relayStateValue;			//Relay State Value
static bool g_relayFaultStatus;			//Relay Fault Status
static bool g_gfciFaultStatus;			//GFCI Fault Status
static bool g_voltageFaultStatus;		//Voltage Fault Status
static bool g_currentFaultStatus;		//Current Fault Status
static bool g_overallFaultStatus;		//Overall Fault Status

//static GpioModule g_axGpio1Module;		//AXI GPIO 1 module instance
//static u16 g_gpio1Channel1Value;		//AXI GPIO 1 Channel 1 value (16-bit) - cp_pk
//static u16 g_gpio1Channel2Value;		//AXI GPIO 1 Channel 2 value (16-bit) - ADC ch3

static const char *g_controlPilotStates[] = {
		"Invalid Control Pilot State",
		"CP_STATE_EVSE_IDLE",					//State A	--	Voltage 12
		"CP_STATE_VEHICLE_CONNECTED",			//State B	--	Voltage 9
		"CP_STATE_CHARGING_INPROGRESS",			//State C	--	Voltage 6
		"CP_STATE_CHARGING_WITH_VENTILATION",	//State D	--	Voltage 3
		"CP_STATE_EVSE_DISCONNECTED",			//State E	--	Voltage 0
		"CP_STATE_EVSE_ERROR"					//State F	--	Voltage -12
};

/*
 * =====================================================================================
 * Static Function Definitions
 * =====================================================================================
 */
#if 0
/**
 * @brief Converts binary data to a decimal value based on a custom encoding format.
 *
 * This function takes a 16-bit binary input, extracts specific bits to determine
 * the decimal value, and applies a custom encoding scheme to return the corresponding
 * decimal value. The lower 12 bits represent the main data, the upper 3 bits act
 * as a multiplier, and the sign bit determines whether the value is positive or negative.
 *
 * @param p_binaryData A 16-bit unsigned integer representing the binary data to be converted.
 *
 * @return Xint16 The corresponding decimal value after conversion.
 */
static Xint16 ConvertBinaryDataToDecimalValue(u16 p_binaryData)
{
	u16 l_lower12BitsData, l_upper3BitsData;
	Xint16 l_result;
	u8 l_signBit;

	// Extract the lower 12 bits (bits 0 to 11) for decimal value
	l_lower12BitsData = p_binaryData & 0x0FFF;	// Mask the lower 12 bits (0x0FFF is 0000 1111 1111 1111)

	// Extract bits 12 to 14 for multiplier
	l_upper3BitsData = (p_binaryData >> 12) & 0x07;	// Shift right by 12 bits and mask the last 3 bits (0x07 is 0000 0111)

	// Extract the signed bit (bit 15)
	l_signBit = (p_binaryData >> 15) & 0x01; // Check the highest bit (bit 15)

	// Positive number (sign bit = 0)
	if (l_signBit == 0) {
		// Calculate the result for positive number
		l_result = (l_upper3BitsData * 4096) + l_lower12BitsData;
	}
	// Negative number (sign bit = 1)
	else {
		l_upper3BitsData = l_upper3BitsData - 1; // Subtract 1 from upper 3 bits data

		// 1's Complement the result (invert the bits)
		l_upper3BitsData = ~l_upper3BitsData & 0x07; // Invert the bits of the upper 3 bits data and mask with 0x07 to ensure it stays 3 bits long

		// Calculate the result for negative number
		l_result = -((l_upper3BitsData * 4096) + l_lower12BitsData); // Negate the result to make it negative
	}

	// return the result
	return l_result;
}
#endif

/**
 * @brief Reads the voltage register value.
 *
 * This function is used to read the value from the voltage register.
 * It does not take any parameters and returns a 16-bit unsigned value
 * representing the voltage register data.
 *
 * @return u16 The value read from the voltage register.
 */
static u16 ReadVoltageRegister(void)
{
	return Xil_In16(ADDR_VOLTAGE_REGISTER);
}

/**
 * @brief Reads the current register value.
 *
 * This function is used to read the value from the current register.
 * It does not take any parameters and returns a 16-bit unsigned value
 * representing the current register data.
 *
 * @return u16 The value read from the current register.
 */
static u16 ReadCurrentRegister(void)
{
	return Xil_In16(ADDR_CURRENT_REGISTER);
}

/**
 * @brief Reads the earth fault register value.
 *
 * This function is used to read the value from the earth fault register.
 * It does not take any parameters and returns a 16-bit unsigned value
 * representing the earth fault register data.
 *
 * @return u16 The value read from the earth fault register.
 */
static u16 ReadEarthFaultRegister(void)
{
	return Xil_In16(ADDR_EARTH_FAULT_REGISTER);
}

/**
 * @brief Reads the control pilot register value.
 *
 * This function is used to read the value from the control pilot register.
 * It does not take any parameters and returns a 16-bit unsigned value
 * representing the control pilot register data.
 *
 * @return u16 The value read from the control pilot register.
 */
static u16 ReadControlPilotRegister(void)
{
	return Xil_In16(ADDR_CONTROL_PILOT_REGISTER);
}

/**
 * @brief Reads the Proximity pilot register value.
 *
 * This function is used to read the value from the Proximity pilot register.
 * It does not take any parameters and returns a 16-bit unsigned value
 * representing the Proximity pilot register data.
 *
 * @return u16 The value read from the Proximity pilot register.
 */
static u16 ReadProximityPilotRegister(void)
{
	return Xil_In16(ADDR_PROXIMITY_PILOT_REGISTER);
}

/**
 * @brief Reads the Relay Fault Indication register value.
 *
 * This function is used to read the value from the Relay Fault Indication register.
 * It does not take any parameters and returns a 16-bit unsigned value representing
 * the fault status of the relay.
 *
 * @return u16 The value read from the Relay Fault Indication register.
 */
static u16 ReadRelayFaultIndicationRegister(void)
{
	return Xil_In16(ADDR_RELAY_FAULT_INDICATION_REGISTER);
}

/**
 * @brief Reads the GFCI Fault Indication register value.
 *
 * This function is used to read the value from the GFCI (Ground Fault Circuit Interrupter)
 * Fault Indication register. It provides information about any detected ground faults
 * in the system.
 *
 * @return u16 The value read from the GFCI Fault Indication register.
 */
static u16 ReadGFCIFaultIndicationRegister(void)
{
	return Xil_In16(ADDR_GFCI_FAULT_INDICATION_REGISTER);
}

/**
 * @brief Reads the Voltage Fault Indication register value.
 *
 * This function is used to read the value from the Voltage Fault Indication register.
 * It provides information about any detected overvoltage or undervoltage conditions
 * in the system that may impact safe operation.
 *
 * @return u16 The value read from the Voltage Fault Indication register.
 */
static u16 ReadVoltageFaultIndicationRegister(void)
{
	return Xil_In16(ADDR_VOLTAGE_FAULT_INDICATION_REGISTER);
}

/**
 * @brief Reads the Current Fault Indication register value.
 *
 * This function is used to read the value from the Current Fault Indication register.
 * It provides information about any detected overcurrent or abnormal current conditions
 * in the system that may affect safety or normal operation.
 *
 * @return u16 The value read from the Current Fault Indication register.
 */
static u16 ReadCurrentFaultIndicationRegister(void)
{
	return Xil_In16(ADDR_CURRENT_FAULT_INDICATION_REGISTER);
}

/**
 * @brief Reads the Overall Fault Indication register value.
 *
 * This function is used to read the value from the Overall Fault Indication register.
 * It provides a consolidated status of all fault conditions detected in the system,
 * including voltage, current, relay, GFCI, ADC calibration, and other safety-related faults.
 *
 * @return u16 The value read from the Overall Fault Indication register.
 */
static u16 ReadOverallFaultIndicationRegister(void)
{
	return Xil_In16(ADDR_OVERALL_FAULT_INDICATION_REGISTER);
}

/**
 * @brief Reads the Relay State register value.
 *
 * This function is used to read the value from the Relay State register.
 * It provides the current operational state of the relay, which is essential
 * for monitoring and controlling the power delivery to the EV.
 *
 * @return u16 The value read from the Relay State register.
 */
static u16 ReadRelayStateRegister(void)
{
	return Xil_In16(ADDR_RELAY_STATE_REGISTER);
}

/**
 * @brief Reads the BIT firmware version register value.
 *
 * This function is used to read the value from the Built-In Test (BIT) firmware version register.
 * It retrieves the firmware version of the BIT module, which can be used for diagnostics and
 * version tracking purposes.
 *
 * @return u16 The value read from the BIT firmware version register.
 */
static u16 ReadBitFirmwareVersionRegister(void)
{
	return Xil_In16(ADDR_BIT_STREAM_FIRMWARE_VERSION);
}

/**
 * @brief Parses the firmware version from a 16-bit encoded value.
 *
 * This function takes a 16-bit encoded firmware version value and extracts the major, minor,
 * and patch version components. It returns a structured representation of the firmware version
 * for easier interpretation and display.
 *
 * @param  version  The 16-bit encoded firmware version.
 * @retval FirmwareVersion_t Structure containing the parsed major, minor, and patch version.
 */
static FirmwareVersion_t ParseFirmwareVersion(u16 version)
{
    FirmwareVersion_t fw;

    fw.major = EXTRACT_MAJOR(version);
    fw.minor = EXTRACT_MINOR(version);
    fw.patch = EXTRACT_PATCH(version);

    return fw;
}

/**
 * @brief Converts a firmware version structure to a human-readable string.
 *
 * This function takes a FirmwareVersion_t structure containing the major, minor, and patch version
 * numbers and formats it into a null-terminated string in the format "v<major>.<minor>.<patch>".
 *
 * @param  fw         The firmware version structure to convert.
 * @param  outputStr  Pointer to the character array where the formatted string will be stored.
 *                    The buffer should be large enough to hold the resulting string.
 * @retval None
 */
void FirmwareVersionToString(FirmwareVersion_t fw, char *outputStr)
{
    sprintf(outputStr, "%u.%u.%u", fw.major, fw.minor, fw.patch);
    outputStr[5] = '\0';
}

/*
 * =====================================================================================
 * Global Function Definitions
 * =====================================================================================
 */

/**
 * @brief Initializes the Control Pilot (CP).
 */
u8 InitCP(void)
{
	FirmwareVersion_t l_version = {0};
	//int l_status;

	/* Initialize AXI GPIO 1 module */
	//l_status = initGpio(&g_axGpio1Module, AXI_GPIO_1_DEVICE_ID);
	//if (l_status != GPIO_SUCCESS) {
	//	xil_printf("ERROR: Failed to initialize AXI GPIO 1 module\r\n");
	//	return XST_FAILURE;
	//}

	/* Configure both channels as input (Channel 1: 16-bit cp_pk, Channel 2: 16-bit ADC ch3) */
	// Configure all 16 bits of Channel 1 as input (cp_pk)
	//for (u32 pin = 0; pin < 16; pin++) {
	//	configureGpioPin(&g_axGpio1Module, 1, pin, GPIO_DIRECTION_INPUT);
	//}

	// Configure all 16 bits of Channel 2 as input (ADC ch3)
	//for (u32 pin = 0; pin < 16; pin++) {
	//	configureGpioPin(&g_axGpio1Module, 2, pin, GPIO_DIRECTION_INPUT);
	//}

	/* Read initial values from AXI GPIO 1 */
	//g_gpio1Channel1Value = (u16)(readGpioChannel(&g_axGpio1Module, 1) & 0xFFFF);  // cp_pk
	//g_gpio1Channel2Value = (u16)(readGpioChannel(&g_axGpio1Module, 2) & 0xFFFF);  // ADC ch3

	/* Read all the PL register information to get the latest status between the EV and EVSE */
	g_CPState = ReadControlPilotRegister();
	g_PPState = ReadProximityPilotRegister();
	g_voltageValue = ReadVoltageRegister();
	g_currentValue = ReadCurrentRegister();
	g_earthFaultValue = ReadEarthFaultRegister();
	g_relayStateValue = ReadRelayStateRegister();
	g_relayFaultStatus = ReadRelayFaultIndicationRegister();
	g_gfciFaultStatus = ReadGFCIFaultIndicationRegister();
	g_voltageFaultStatus = ReadVoltageFaultIndicationRegister();
	g_currentFaultStatus = ReadCurrentFaultIndicationRegister();
	g_overallFaultStatus = ReadOverallFaultIndicationRegister();
	g_bitFirmwareVersion = ReadBitFirmwareVersionRegister();

#if CP_MODULE_DEBUG
	xil_printf("Bit Firmware Version : %d\r\n"
			"Voltage Magnitude : %d\r\n"
			"Current Magnitude : %d\r\n"
			"Relay State : %d\r\n"
			"CP State : %d\r\n"
			"Relay Fault Status : %d\r\n"
			"Voltage Fault Status : %d\r\n"
			"Current Fault Status : %d\r\n"
			"GFCI Fault Status : %d\r\n"
			"Overall Fault Status : %d\r\n",
			//"AXI GPIO 1 Channel 1 (cp_pk) : %d\r\n"
			//"AXI GPIO 1 Channel 2 (ADC ch3) : %d\r\n",
			g_bitFirmwareVersion, g_voltageValue, g_currentValue,
			g_relayStateValue, g_CPState, g_relayFaultStatus,
			g_voltageFaultStatus, g_currentFaultStatus, g_gfciFaultStatus,
			g_overallFaultStatus);
			//g_overallFaultStatus, g_gpio1Channel1Value, g_gpio1Channel2Value);
#endif

	l_version = ParseFirmwareVersion(g_bitFirmwareVersion);
	FirmwareVersionToString(l_version, g_bitFirmwareVersionStr);
	xil_printf("g_bitFirmwareVersionStr : %s\r\n", g_bitFirmwareVersionStr);

#if SIMULATION
	g_relayFaultStatus = FALSE;
	g_voltageFaultStatus = FALSE;
	g_currentFaultStatus = FALSE;
	g_overallFaultStatus = FALSE;
#endif

	/* Based on the Fault variables status update the device fault global flag */
	if (g_overallFaultStatus) {
		/* Set the Device fault flag */
		g_isDeviceFaulted = TRUE;

		/* Indicate ERROR by turning on the ERROR status LED */
		setLedPin(LED_ERROR);
	}
	else {
		/* Reset the Device fault flag */
		g_isDeviceFaulted = FALSE;

		/* Reset ERROR by turning off the ERROR status LED */
		resetLedPin(LED_ERROR);
	}

	return XST_SUCCESS;
}

/**
 * @brief Retrieves the Control Pilot (CP) state information.
 */
CPState_e GetCPStateInfo(void)
{
	return g_CPState;
}

/**
 * @brief Retrieves the Proximity Pilot (CP) state information.
 */
PPState_e GetPPStateInfo(void)
{
	return g_PPState;
}

/**
 * @brief Retrieves the voltage information.
 */
u16 GetVolatgeInfo(void)
{
	return g_voltageValue;
}

/**
 * @brief Retrieves the current information.
 */
u16 GetCurrentInfo(void)
{
	return g_currentValue;
}

/**
 * @brief Retrieves information about earth fault conditions.
 */
u8 GetEarthFaultInfo(void)
{
	return g_earthFaultValue;
}

/**
 * @brief Retrieves information about relay fault conditions.
 */
bool GetRelayFaultInfo(void)
{
	return g_relayFaultStatus;
}

/**
 * @brief Retrieves information about GFCI fault conditions.
 */
bool GetGFCIFaultInfo(void)
{
	return g_gfciFaultStatus;
}

/**
 * @brief Retrieves information about voltage fault conditions.
 */
bool GetVoltageFaultInfo(void)
{
	return g_voltageFaultStatus;
}

/**
 * @brief Retrieves information about current fault conditions.
 */
bool GetCurrentFaultInfo(void)
{
	return g_currentFaultStatus;
}

/**
 * @brief Retrieves information about overall fault conditions.
 */
bool GetOverallFaultInfo(void)
{
	return g_overallFaultStatus;
}

/**
 * @brief Retrieves the current state of the relay.
 */
u8 GetRelayStateInfo(void)
{
	return g_relayStateValue;
}

/**
 * @brief Retrieves the firmware version information from the Bit Firmware Version register.
 */
u16 GetBitFirmwareVersionInfo(void)
{
	return g_bitFirmwareVersion;
}

/**
 * @brief  Retrieves a string representation of the Control Pilot (CP) state.
 *
 * This function converts a given CP state enumeration value into a human-readable
 * string describing the current state of the Control Pilot. This is useful for
 * debugging and logging purposes.
 */
const char * GetCPStateInfoString(CPState_e p_CPState)
{
	if (p_CPState < 0 || p_CPState >= sizeof(g_controlPilotStates) / sizeof(g_controlPilotStates[0])) {
		return "Invalid Control Pilot State";
	}
	return g_controlPilotStates[p_CPState];
}

///**
// * @brief Retrieves the AXI GPIO 1 Channel 1 value (16-bit cp_pk).
// */
//u16 GetGpio1Channel1Value(void)
//{
//	return g_gpio1Channel1Value;
//}
//
///**
// * @brief Retrieves the AXI GPIO 1 Channel 2 value (16-bit ADC ch3).
// */
//u16 GetGpio1Channel2Value(void)
//{
//	return g_gpio1Channel2Value;
//}

/**
 * @brief Sets the user authentication status to the PL (Programmable Logic).
 *
 * This function writes the authentication status to the PL register, allowing
 * the hardware to be aware of the current authentication state. This can be used
 * to control hardware-level access or enable/disable certain features based on
 * user authentication.
 *
 * @param  status Authentication status (1 = Authenticated, 0 = Not Authenticated)
 * @retval None
 */
void SetAuthenticationStatus(u8 status)
{
	/* Write the authentication status to the PL register */
	Xil_Out32(ADDR_AUTHENTICATION_STATUS_REGISTER, (u32)(status & 0x01));
	xil_printf("Authentication status written to PL: %d\r\n", status);
}

/**
 * @brief  Sets the power ON status in the PL register.
 *
 * This function writes the power ON status to the PL register, allowing
 * the hardware to control power-related operations. This can be used
 * to enable/disable power to the charging circuit or other hardware components.
 *
 * @param  status Power ON status (1 = Power ON, 0 = Power OFF)
 * @retval None
 */
void SetPowerOnStatus(u8 status)
{
	/* Write the power ON status to the PL register */
	Xil_Out32(ADDR_POWER_ON_STATUS_REGISTER, (u32)(status & 0x01));
	xil_printf("Power ON status written to PL: %d\r\n", status);
}

/**
 * @brief  CP (Control Processor) task for handling system operations.
 *
 * This function is a FreeRTOS task designed to manage the control processor operations, such as
 * executing control logic, processing commands, and interfacing with other system components.
 * The task is responsible for ensuring that the control processor's functions run smoothly within
 * the system's task scheduler.
 */
void prvCPTask( void *pvParameters )
{
	/* Delay for 500ms - Fast safety monitoring for CP state, faults, and relay status */
	const TickType_t x500ms = INT_TO_TICKS( DELAY_1_SECOND / 2 );
	//const TickType_t x1second = INT_TO_TICKS( DELAY_1_SECOND );      // Option: 1-second delay
	//const TickType_t x3second = INT_TO_TICKS( 3 * DELAY_1_SECOND );  // Old: 3-second delay

	while(1)
	{
		xil_printf("\t\t\t###   %s   ###\t\t\t\r\n", __func__);

		if (g_isOtaInProgress) {
			/* Delay for 500ms during OTA */
			vTaskDelay( x500ms );
			//vTaskDelay( x1second );  // Option: 1-second delay
			//vTaskDelay( x3second );  // Old: 3-second delay
		}

		/* Read AXI GPIO 1 values from both channels */
		//g_gpio1Channel1Value = (u16)(readGpioChannel(&g_axGpio1Module, 1) & 0xFFFF);  // cp_pk
		//g_gpio1Channel2Value = (u16)(readGpioChannel(&g_axGpio1Module, 2) & 0xFFFF);  // ADC ch3

		/* Read all the PL register information to get the latest status between the EV and EVSE */
		g_CPState = ReadControlPilotRegister();
		g_PPState = ReadProximityPilotRegister();
		g_voltageValue = ReadVoltageRegister();
		g_currentValue = ReadCurrentRegister();
		g_earthFaultValue = ReadEarthFaultRegister();
		g_relayStateValue = ReadRelayStateRegister();
		g_relayFaultStatus = ReadRelayFaultIndicationRegister();
		g_gfciFaultStatus = ReadGFCIFaultIndicationRegister();
		g_voltageFaultStatus = ReadVoltageFaultIndicationRegister();
		g_currentFaultStatus = ReadCurrentFaultIndicationRegister();
		g_overallFaultStatus = ReadOverallFaultIndicationRegister();
		g_bitFirmwareVersion = ReadBitFirmwareVersionRegister();

#if CP_MODULE_DEBUG
		xil_printf("Bit Firmware Version : %d\r\n"
				"Voltage Magnitude : %d\r\n"
				"Current Magnitude : %d\r\n"
				"Relay State : %d\r\n"
				"CP State : %d\r\n"
				"Relay Fault Status : %d\r\n"
				"Voltage Fault Status : %d\r\n"
				"Current Fault Status : %d\r\n"
				"GFCI Fault Status : %d\r\n"
				"Overall Fault Status : %d\r\n",
				//"AXI GPIO 1 Channel 1 (cp_pk) : %d\r\n"
				//"AXI GPIO 1 Channel 2 (ADC ch3) : %d\r\n",
				g_bitFirmwareVersion, g_voltageValue, g_currentValue,
				g_relayStateValue, g_CPState, g_relayFaultStatus,
				g_voltageFaultStatus, g_currentFaultStatus, g_gfciFaultStatus,
				g_overallFaultStatus);
				//g_overallFaultStatus, g_gpio1Channel1Value, g_gpio1Channel2Value);
#endif

#if SIMULATION
		g_relayFaultStatus = FALSE;
		g_voltageFaultStatus = FALSE;
		g_currentFaultStatus = FALSE;
		g_overallFaultStatus = FALSE;
#endif

		/* Based on the Fault variables status update the device fault global flag */
		if (g_overallFaultStatus) {
			/* Set the Device fault flag */
			g_isDeviceFaulted = TRUE;

			/* Indicate ERROR by turning on the ERROR status LED */
			setLedPin(LED_ERROR);
		}
		else {
			/* Reset the Device fault flag */
			g_isDeviceFaulted = FALSE;

			/* Reset ERROR by turning off the ERROR status LED */
			resetLedPin(LED_ERROR);
		}

		/* All the PL register information read completed and stored in the global variables */
		xil_printf("CP task : All the PL registers read completed and data stored in the variables!\r\n");

		/* Delay for 500ms - Fast polling for safety-critical monitoring */
		vTaskDelay( x500ms );
		//vTaskDelay( x1second );  // Option: 1-second delay
		//vTaskDelay( x3second );  // Old: 3-second delay
	}
}
