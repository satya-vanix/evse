/*
 * =====================================================================================
 * File Name:    lcdMessages.c
 * Author:       Vanix Technologies PVT. LTD.
 * Date Created: 2025-03-04
 * Description:  This source file contains the implementation of functions responsible
 *               for managing the LCD display of the Electric Vehicle Supply Equipment
 *               (EVSE) charger. It handles the rendering of status messages, user
 *               prompts, and charging process updates on the LCD screen.
 *
 *               The file includes functions for formatting and displaying text,
 *               updating the screen based on real-time data, and ensuring clear
 *               communication of essential information such as charging status,
 *               errors, and user instructions. It also provides mechanisms for
 *               refreshing the display and optimizing readability.
 *
 * Revision History:
 * Version 1.0 - 2025-03-04 - Initial version.
 * =====================================================================================
 */

/*
 * =====================================================================================
 * Include Headers
 * =====================================================================================
 */
#include "lcdMessages.h"

/*
 * =====================================================================================
 * Global Variables
 * =====================================================================================
 */

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

/*
 * =====================================================================================
 * Global Function Definitions
 * =====================================================================================
 */

/**
 * @brief  Prints a greeting message to the LCD display.
 */
void lcdPrintGreetingsMessage(void)
{
	lcd_clear();
	lcd_setCursor(6, 0);
	lcd_send_string("VANIX");
	lcd_setCursor(2, 1);
	lcd_send_string("TECHNOLOGIES");

	mssleep(500);

	lcd_clear();
	lcd_setCursor(3, 0);
	lcd_send_string("WELCOME TO");
	lcd_send_string_with_left_scroll_bottom_line("7.2kW AC CHARGER");

	mssleep(500);
}

/**
 * @brief  Prints the system booting message on the LCD display.
 */
void lcdPrintSystemBootingMessage(void)
{
	lcd_clear();
	lcd_setCursor(0, 0);
	lcd_send_string("BOOTING SYSTEM..");

	mssleep(500);
}

/**
 * @brief  Prints the system self-test pass message on the LCD display.
 */
void lcdPrintSystemSelfTestPassMessage(void)
{
	lcd_clear();
	lcd_setCursor(0, 0);
	lcd_send_string("SYSTEM SELF TEST");
	lcd_setCursor(0, 1);
	lcd_send_string("STATUS : PASS");

	mssleep(500);
}

/**
 * @brief  Prints the system self-test fail message on the LCD display.
 */
void lcdPrintSystemSelfTestFailMessage(void)
{
	lcd_clear();
	lcd_setCursor(0, 0);
	lcd_send_string("SYSTEM SELF TEST");
	lcd_setCursor(0, 1);
	lcd_send_string("STATUS : FAIL");

	mssleep(500);
}

/**
 * @brief  Prints the authentication pass message on the LCD display.
 */
void lcdPrintAuthPassMessage(void)
{
	lcd_clear();
	lcd_setCursor(0, 0);
	lcd_send_string("USER VERIFIED.");
	lcd_send_string_with_left_scroll_bottom_line("PLEASE PLUG IN YOUR VEHICLE.");

	mssleep(500);  // Fast state transition
}

/**
 * @brief  Prints the session timeout message on the LCD display.
 */
void lcdPrintSessionTimeoutMessage(void)
{
	lcd_clear();
	lcd_setCursor(0, 0);
	lcd_send_string("NO EV DETECTED.");
	lcd_send_string_with_left_scroll_bottom_line("PLEASE RESTART THE SESSION.");

	mssleep(500);
}

/**
 * @brief  Prints the RFID authentication pass message on the LCD display.
 */
void lcdPrintRfidAuthPassMessage(void)
{
	lcdPrintAuthPassMessage();
}

/**
 * @brief  Prints the RFID authentication fail message on the LCD display.
 */
void lcdPrintRfidAuthFailMessage(void)
{
	lcd_clear();
	lcd_setCursor(0, 0);
	lcd_send_string("INVALID RFID.");
	lcd_setCursor(0, 1);
	lcd_send_string("TRY AGAIN.");

	mssleep(500);
}

/**
 * @brief  Prints the Wi-Fi credentials message on the LCD display.
 */
void lcdPrintWifiCredentialsMessage(char *str)
{
	char lcdStringBuffer[30] = {0};

	memset(lcdStringBuffer, 0, 30);

	lcd_clear();
	lcd_setCursor(0, 0);
	lcd_send_string("WI-FI NETWORK");
	snprintf(lcdStringBuffer, 30, "SSID : %s", str);
	lcd_send_string_with_left_scroll_bottom_line(lcdStringBuffer);

	mssleep(500);
}

/**
 * @brief  Prints the Wi-Fi connection success message on the LCD display.
 */
void lcdPrintWifiConnectionSuccessMessage(void)
{
	lcd_clear();
	lcd_setCursor(0, 0);
	lcd_send_string("WI-FI CONNECTION");
	lcd_setCursor(4, 1);
	lcd_send_string("CONNECTED");

	mssleep(500);
}

/**
 * @brief  Prints the Wi-Fi connection failure message on the LCD display.
 */
void lcdPrintWifiConnectionFailMessage(void)
{
	lcd_clear();
	lcd_setCursor(0, 0);
	lcd_send_string("WI-FI CONNECTION");
	lcd_setCursor(4, 1);
	lcd_send_string("FAILED");

	mssleep(500);
}

/**
 * @brief  Prints the OTA update check message on the LCD display.
 */
void lcdPrintOtaUpdateCheckMessage(void)
{
	lcd_clear();
	lcd_setCursor(0, 0);
	lcd_send_string("CHECKING FOR");
	lcd_setCursor(0, 1);
	lcd_send_string("UPDATES.....");

	mssleep(500);
}

/**
 * @brief  Prints the OTA update found message on the LCD display.
 */
void lcdPrintOtaUpdateFoundMessage(void)
{
	lcd_clear();
	lcd_send_string_with_left_scroll_top_line("NEW FIRMWARE AVAILABLE.");
	lcd_setCursor(0, 1);
	lcd_send_string("UPDATING.....");

	mssleep(500);
}

/**
 * @brief  Prints the OTA update not found message on the LCD display.
 */
void lcdPrintOtaUpdateNotFoundMessage(void)
{
	lcd_clear();
	lcd_send_string_with_left_scroll_top_line("NO UPDATES FOUND.");
	lcd_setCursor(0, 1);
	lcd_send_string("CONTINUING BOOT.");

	mssleep(500);
}

/**
 * @brief  Prints the OTA update skip message on the LCD display.
 */
void lcdPrintOtaUpdateSkipMessage(void)
{
	lcd_clear();
	lcd_setCursor(0, 0);
	lcd_send_string("NO NETWORK.");
	lcd_send_string_with_left_scroll_bottom_line("SKIPPING OTA CHECK.");

	mssleep(500);
}

/**
 * @brief  Prints the OTA update success message on the LCD display.
 */
void lcdPrintOtaUpdateSuccessMessage(void)
{
	lcd_clear();
	lcd_send_string_with_left_scroll_top_line("FIRMWARE UPDATE INSTALLED SUCCESSFULLY.");
	lcd_setCursor(0, 1);
	lcd_send_string("REBOOTING.....");

	mssleep(500);
}

/**
 * @brief  Prints the OTA update failed message on the LCD display.
 */
void lcdPrintOtaUpdateFailedMessage(void)
{
	lcd_clear();
	lcd_send_string_with_left_scroll_top_line("FIRMWARE UPDATE FAILED.");
	lcd_setCursor(0, 1);
	lcd_send_string("RETRYING LATER.");

	mssleep(500);
}

/**
 * @brief  Prints the system ready state message on the LCD display.
 */
void lcdPrintSystemReadyStateMessage(void)
{
	lcd_clear();
//	lcd_send_string_with_left_scroll_top_line("WELCOME! TAP RFID / SCAN QR CODE");
	lcd_send_string_with_left_scroll_top_line("WELCOME! TAP RFID / USE MOBILE APP");
	lcd_send_string_with_left_scroll_bottom_line("TO START CHARGING.");

	mssleep(500);
}

/**
 * @brief  Prints the user authentication message on the LCD display.
 */
void lcdPrintUserAuthenticateMessage(void)
{
	lcd_clear();
	lcd_setCursor(2, 0);
	lcd_send_string("TAP RFID OR");
	lcd_setCursor(0, 1);
	lcd_send_string("TO START CHARGING.");

	mssleep(500);

	lcd_setCursor(0, 0);
	lcd_send_string("USE MOBILE APP");

	mssleep(500);
}

/**
 * @brief  Prints the tag detected and authorizing message on the LCD display.
 */
void lcdPrintTagDetectedAndAuthorizingMessage(void)
{
	lcd_clear();
	lcd_setCursor(0, 0);
	lcd_send_string("TAG DETECTED!");
	lcd_setCursor(0, 1);
	lcd_send_string("AUTHORIZING...");

	mssleep(500);
}

/**
 * @brief  Prints the EV disconnect before charging message on the LCD display.
 */
void lcdPrintEVDisconnectBeforeChargingMessage(void)
{
	lcd_clear();
	lcd_setCursor(0, 0);
	lcd_send_string("EV DISCONNECTED.");
	lcd_setCursor(0, 1);
	lcd_send_string("RESTART SESSION.");

	mssleep(500);
}

/**
 * @brief  Prints the mobile app authentication failure message on the LCD display.
 */
void lcdPrintAuthMobileAppFailMessage(void)
{
	lcd_clear();
	lcd_setCursor(0, 0);
	lcd_send_string("NO NETWORK.");
	lcd_setCursor(0, 1);
	lcd_send_string("USE RFID INSTEAD");

	mssleep(500);
}

/**
 * @brief  Prints the EV not plugged properly message on the LCD display.
 */
void lcdPrintEVNotPluggedProperlyMessage(void)
{
	lcd_clear();
	lcd_send_string_with_left_scroll_top_line("PLUG NOT INSERTED PROPERLY.");
	lcd_setCursor(0, 1);
	lcd_send_string("TRY AGAIN.");

	mssleep(500);
}

/**
 * @brief  Prints the EV not detected message on the LCD display.
 */
void lcdPrintEVNotDetectedMessage(void)
{
	lcd_clear();
	lcd_setCursor(0, 0);
	//lcd_send_string("NO EV DETECTED.");
	lcd_send_string("Connect your EV");
	lcd_setCursor(0, 1);
	lcd_send_string("WAITING.....");

	mssleep(500);  // Reduced from 3000ms - State A message
}

/**
 * @brief  Prints the EV waiting for charging message on the LCD display.
 */
void lcdPrintEVWaitingForChargingMessage(void)
{
	lcd_clear();
	lcd_setCursor(0, 0);
	lcd_send_string("EV CONNECTED.");
	lcd_send_string_with_left_scroll_bottom_line("WAITING FOR A CHARGE REQUEST...");

	mssleep(500);  // Reduced from 3000ms - State B message
}

/**
 * @brief  Prints the EV ready for charging message on the LCD display.
 */
void lcdPrintEVReadyForChargingMessage(void)
{
	lcd_clear();
	lcd_setCursor(0, 0);
	lcd_send_string("EV READY.");
	lcd_send_string_with_left_scroll_bottom_line("CHARGING WILL START SOON...");

	mssleep(500);  // Fast state transition
}

/**
 * @brief  Prints the safety check pass message on the LCD display.
 */
void lcdPrintSafetyCheckPassMessage(void)
{
	lcd_clear();
	lcd_send_string_with_left_scroll_top_line("SAFETY CHECKS COMPLETE.");
	lcd_setCursor(0, 1);
	lcd_send_string("POWERING ON.....");

	mssleep(500);  // Fast state transition
}

/**
 * @brief  Prints the safety check failure message on the LCD display.
 */
void lcdPrintSafetyCheckFailMessage(void)
{
	lcd_clear();
	lcd_setCursor(0, 0);
	lcd_send_string("SAFETY ERROR!");
	lcd_setCursor(0, 1);
	lcd_send_string("CHARGING STOPPED");

	mssleep(500);
}

/**
 * @brief  Prints the EV charging started message on the LCD display.
 */
void lcdPrintEVChargingStartedMessage(void)
{
	lcd_clear();
	lcd_setCursor(0, 0);
	lcd_send_string("CHARGING STARTED");
	lcd_setCursor(0, 1);
	lcd_send_string("ENJOY YOUR DRIVE");

	mssleep(500);  // Fast state transition
}

/**
 * @brief  Prints the EV communication error message on the LCD display.
 */
void lcdPrintEVCommunicationErrorMessage(void)
{
	lcd_clear();
	lcd_send_string_with_left_scroll_top_line("EV NOT COMMUNICATING.");
	lcd_setCursor(0, 1);
	lcd_send_string("CHECK CABLE.");

	mssleep(500);
}

/**
 * @brief  Prints the charging started message on the LCD display with duration.
 */
void lcdPrintChargingStartedMessage(u8 hours, u8 minutes)
{
	char lcdStringBuffer[30] = {0};

	memset(lcdStringBuffer, 0, 30);

	lcd_clear();
	lcd_setCursor(0, 0);
	lcd_send_string("CHARGING STARTED");
	snprintf(lcdStringBuffer, 30, "TIME ELAPSED: %02d:%02d", hours, minutes);
	lcd_send_string_with_left_scroll_bottom_line(lcdStringBuffer);

	mssleep(500);  // Reduced from 3000ms - Charging in progress update
}

/**
 * @brief  Prints the charging progress message on the LCD display.
 */
void lcdPrintChargingProgressMessage(u16 voltage, u16 current, u8 hours, u8 minutes)
{
	char lcdStringBuffer[30] = {0};

	memset(lcdStringBuffer, 0, 30);

	lcd_clear();
	lcd_send_string_with_left_scroll_top_line("CHARGING IN PROGRESS...");
	snprintf(lcdStringBuffer, 30, "CURRENT: %02dA | VOLTAGE: %02dV", current, voltage);
	lcd_send_string_with_left_scroll_bottom_line(lcdStringBuffer);

	mssleep(500);  // Fast update

	memset(lcdStringBuffer, 0, 30);
	snprintf(lcdStringBuffer, 30, "TIME ELAPSED: %02d:%02d mins", hours, minutes);
	lcd_send_string_with_left_scroll_bottom_line(lcdStringBuffer);

	mssleep(500);  // Fast update - Total 1s instead of 5s
}

/**
 * @brief  Displays a charging in-progress fault detected message on the LCD.
 */
void lcdPrintChargingInprogressFaultDetectedMessage(void)
{
	lcd_clear();
	lcd_setCursor(0, 0);
	lcd_send_string("FAULT DETECTED.");
	lcd_send_string_with_left_scroll_bottom_line("ENDING SESSION...");

	mssleep(500);
}

/**
 * @brief  Prints the fault message on the LCD display.
 */
void lcdPrintFaultMessage(void)
{
	lcd_clear();
	lcd_send_string_with_left_scroll_top_line("CHARGING ERROR DETECTED.");
	lcd_send_string_with_left_scroll_bottom_line("PLEASE CONTACT SUPPORT.");

	mssleep(500);
}

/**
 * @brief  Prints the EV unplugged message on the LCD display.
 */
void lcdPrintEVUnpluggedMessage(void)
{
	lcd_clear();
	lcd_setCursor(0, 0);
	lcd_send_string("EV UNPLUGGED");
	lcd_send_string_with_left_scroll_bottom_line("ENDING SESSION...");

	mssleep(500);
}

/**
 * @brief  Prints the session stopped by user message on the LCD display.
 */
void lcdPrintSessionStoppedByUserMessage(void)
{
	lcd_clear();
	lcd_send_string_with_left_scroll_top_line("SESSION STOPPED BY USER.");

	mssleep(500);
}

/**
 * @brief  Prints the EV full charge message on the LCD display.
 */
void lcdPrintEVFullChargeMessage(void)
{
	lcd_clear();
	lcd_send_string_with_left_scroll_top_line("CHARGING COMPLETE.");
	lcd_setCursor(0, 1);
	lcd_send_string("THANK YOU...!");

	mssleep(500);
}

/**
 * @brief  Prints the charging session summary message on the LCD display.
 */
void lcdPrintChargingSessionSummaryMessage(u8 hours, u8 minutes)
{
	char lcdStringBuffer[30] = {0};

	memset(lcdStringBuffer, 0, 30);

	lcd_clear();
	lcd_setCursor(0, 0);
	lcd_send_string_with_left_scroll_top_line("CHARGING COMPLETE!");
	lcd_setCursor(0, 1);
	lcd_send_string("----------------");

	mssleep(500);

	lcd_clear();
	lcd_setCursor(3, 0);
	lcd_send_string_with_left_scroll_top_line("TOTAL TIME");
	lcd_setCursor(0, 1);
	snprintf(lcdStringBuffer, 30, "%02d hr %02d mins", hours, minutes);
	lcd_send_string(lcdStringBuffer);

	mssleep(500);

	lcd_clear();
	lcd_send_string_with_left_scroll_top_line("ENERGY DELIVERED");
	lcd_setCursor(6, 1);
	lcd_send_string("NA");

	mssleep(500);

	lcd_clear();
	lcd_send_string_with_left_scroll_top_line("THANK YOU FOR USING VANIX EVESE!");

	mssleep(500);
}

/**
 * @brief  Prints the session restart message on the LCD display.
 */
void lcdPrintSessionRestartMessage(void)
{
	lcd_clear();
	lcd_setCursor(4, 0);
	lcd_send_string("READY FOR");
	lcd_setCursor(2, 1);
	lcd_send_string("NEW SESSION.");

	mssleep(500);

	lcd_clear();
	lcd_setCursor(0, 0);
	lcd_send_string("CHARGER READY!");
	lcd_send_string_with_left_scroll_bottom_line("TAP RFID TO START CHARGING.");

	mssleep(500);
}

/**
 * @brief  Prints the fault charging interrupted message on the LCD display.
 */
void lcdPrintFaultChargingInterruptedMessage(void)
{
	lcd_clear();
	lcd_send_string_with_left_scroll_top_line("CHARGING INTERRUPTED:");
	lcd_send_string_with_left_scroll_bottom_line("GROUND FAULT DETECTED. PLEASE CONTACT SUPPORT.");

	mssleep(500);
}

/**
 * @brief  Prints the system lock message on the LCD display.
 */
void lcdPrintSystemLockMessage(void)
{
	lcd_clear();
	lcd_setCursor(0, 0);
	lcd_send_string("SYSTEM LOCKED.");
	lcd_send_string_with_left_scroll_bottom_line("TECHNICIAN ACCESS REQUIRED.");

	mssleep(500);
}

/**
 * @brief  Prints the offline mode connection lost message on the LCD display.
 */
void lcdPrintOfflineModeConnectionLostMessage(void)
{
	lcd_clear();
	lcd_send_string_with_left_scroll_top_line("NETWORK CONNECTION LOST!");
	lcd_send_string_with_left_scroll_bottom_line("OFFLINE MODE ACTIVATED.");

	mssleep(500);

	lcd_send_string_with_left_scroll_bottom_line("CHARGING AVAILABLE.");

	mssleep(500);
}

/**
 * @brief  Prints the offline mode RFID authentication pass message on the LCD display.
 */
void lcdPrintOfflineModeRfidAuthPassMessage(void)
{
	lcd_clear();
	lcd_send_string_with_left_scroll_top_line("OFFLINE AUTHENTICATION SUCCESSFUL.");
	lcd_send_string_with_left_scroll_bottom_line("PLEASE PLUG IN YOUR VEHICLE.");

	mssleep(500);
}

/**
 * @brief  Prints the offline mode RFID authentication failure message on the LCD display.
 */
void lcdPrintOfflineModeRfidAuthFailMessage(void)
{
	lcd_clear();
	lcd_send_string_with_left_scroll_top_line("RFID NOT RECOGNIZED");
	lcd_send_string_with_left_scroll_bottom_line("TRY AGAIN OR CONTACT SUPPORT.");

	mssleep(500);
}

/**
 * @brief  Prints the network restored message on the LCD display.
 */
void lcdPrintNetworkRestoredMessage(void)
{
	lcd_clear();
	lcd_send_string_with_left_scroll_top_line("NETWORK CONNECTION RESTORED.");

	mssleep(500);
}

/**
 * @brief  Prints the offline session synced message on the LCD display.
 */
void lcdPrintOfflineSessionSyncedMessage(void)
{
	lcd_clear();
	lcd_setCursor(0, 0);
	lcd_send_string("ALL OFFLINE SESSIONS");
	lcd_setCursor(0, 1);
	lcd_send_string("SYNCED.");

	mssleep(500);
}

/**
 * @brief  Prints the new charging session message on the LCD display.
 */
void lcdPrintNewChargingSessionMessage(void)
{
	lcd_clear();
	lcd_setCursor(0, 0);
	lcd_send_string("READY FOR NEW");
	lcd_setCursor(0, 1);
	lcd_send_string("CHARGING SESSION");

	mssleep(500);
}

/**
 * @brief  Prints the maintenance mode activated message on the LCD display.
 */
void lcdPrintMaintenanceModeActivatedMessage(void)
{
	lcd_clear();
	lcd_send_string_with_left_scroll_top_line("MAINTENANCE MODE ACTIVATED.");
	lcd_send_string_with_left_scroll_bottom_line("TECHNICIAN ACCESS ONLY.");

	mssleep(500);
}

/**
 * @brief  Prints the diagnostics summary message on the LCD display.
 */
void lcdPrintDiagnosticsSummaryMessage(void)
{
	lcd_clear();
	lcd_send_string_with_left_scroll_top_line("DIAGNOSTIC SUMMARY:");

	if(g_maintenanceStatus.relay == FALSE){
		lcd_send_string_with_left_scroll_bottom_line("RELAY: PASS");
		mssleep(500);
	}
	else {
		lcd_send_string_with_left_scroll_bottom_line("RELAY: FAIL");
		mssleep(500);
	}

	if(g_maintenanceStatus.voltage == FALSE){
		lcd_send_string_with_left_scroll_bottom_line("VOLTAGE: PASS   ");
		mssleep(500);
	}
	else {
		lcd_send_string_with_left_scroll_bottom_line("VOLTAGE: FAIL   ");
		mssleep(500);
	}

	if(g_maintenanceStatus.current == FALSE){
		lcd_send_string_with_left_scroll_bottom_line("CURRENT: PASS   ");
		mssleep(500);
	}
	else {
		lcd_send_string_with_left_scroll_bottom_line("CURRENT: FAIL   ");
		mssleep(500);
	}

	if(g_maintenanceStatus.gfci == FALSE){
		lcd_send_string_with_left_scroll_bottom_line("GFCI: PASS      ");
		mssleep(500);
	}
	else {
		lcd_send_string_with_left_scroll_bottom_line("GFCI: FAIL      ");
		mssleep(500);
	}

	if(g_maintenanceStatus.network == FALSE){
		lcd_send_string_with_left_scroll_bottom_line("NETWORK: PASS   ");
		mssleep(500);
	}
	else {
		lcd_send_string_with_left_scroll_bottom_line("NETWORK: FAIL   ");
		mssleep(500);
	}

	if(!g_maintenanceStatus.relay && !g_maintenanceStatus.voltage &&
			!g_maintenanceStatus.current && !g_maintenanceStatus.gfci &&
			!g_maintenanceStatus.network) {
		lcd_send_string_with_left_scroll_bottom_line("ALL SYSTEMS OPERATIONAL.");
	}
	else {
		lcd_send_string_with_left_scroll_bottom_line("FAULT DETECTED.");
	}

	mssleep(500);
}

/**
 * @brief  Prints the system reset message on the LCD display.
 */
void lcdPrintSystemResetMessage(void)
{
	lcd_clear();
	lcd_send_string_with_left_scroll_top_line("SYSTEM RESET IN PROGRESS...");
	lcd_send_string_with_left_scroll_bottom_line("PLEASE WAIT.");

	mssleep(500);
}

/**
 * @brief  Prints the maintenance mode firmware update message on the LCD display.
 */
void lcdPrintMaintenanceModeFirmwareUpdateMessage(void)
{
	lcd_clear();
	lcd_send_string_with_left_scroll_top_line("UPDATING FIRMWARE...");
	lcd_send_string_with_left_scroll_bottom_line("DO NOT POWER OFF");

	mssleep(500);
}

/**
 * @brief  Prints the maintenance mode firmware update status message on the LCD display.
 */
void lcdPrintMaintenanceModeFirmwareUpdateStatusMessage(u8 majorVersion, u8 minorVersion, u8 patchVersion)
{
	char lcdStringBuffer[30] = {0};

	memset(lcdStringBuffer, 0, 30);

	lcd_clear();
	lcd_send_string_with_left_scroll_top_line("FIRMWARE UPDATED SUCCESSFULLY!");
	snprintf(lcdStringBuffer, 30, "VERSION: %d.%d.%d", majorVersion, minorVersion, patchVersion);
	lcd_send_string_with_left_scroll_bottom_line(lcdStringBuffer);

	mssleep(500);
}

/**
 * @brief  Prints the maintenance mode exited message on the LCD display.
 */
void lcdPrintMaintenanceModeExitedMessage(void)
{
	lcd_clear();
	lcd_send_string_with_left_scroll_top_line("MAINTENANCE COMPLETE.");
	lcd_send_string_with_left_scroll_bottom_line("SYSTEM READY FOR NEW SESSION.");

	mssleep(500);
}
