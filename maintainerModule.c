/*
 * =====================================================================================
 * File Name:    maintainerModule.c
 * Author:       Vanix Technologies PVT. LTD.
 * Date Created: 2025-04-07
 * Description:  This source file contains the implementation of functions used for
 *               maintainer-specific operations in the Electric Vehicle Supply Equipment
 *               (EVSE) charger. It enables diagnostic access, system verification,
 *               configuration management, and other critical maintenance activities.
 *
 *               The file includes functions for authenticating maintenance personnel,
 *               performing hardware and software diagnostics, applying configuration
 *               updates, and executing service tasks. It ensures secure and efficient
 *               maintenance of the EVSE system to support long-term reliability and
 *               performance.
 *
 * Revision History:
 * Version 1.0 - 2025-04-07 - Initial version.
 * =====================================================================================
 */

/*
 * =====================================================================================
 * Include Headers
 * =====================================================================================
 */
#include "maintainerModule.h"

/*
 * =====================================================================================
 * Global Variables
 * =====================================================================================
 */
bool g_isMaintainerAuthenticated = FALSE; 	// Global flag to store the Maintainer authentication live data

MaintenanceStatus_t g_maintenanceStatus = {0};

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
 * @brief Performs a soft reset of the EVSE system's processing logic.
 */
void EVSE_PerformSoftReset(void){
	// Unlock SLCR (System Level Control Register)
	Xil_Out32(SLCR_UNLOCK_ADDR, SLCR_UNLOCK_KEY);

	// Trigger soft reset
	Xil_Out32(SLCR_PS_RST_CTRL, 0x1);

	while(1);  // Wait for reset
}

/**
 * @brief Performs a hard reset of the entire EVSE system, including the FPGA logic.
 */
void EVSE_PerformHardReset(void){
	/* Delay for 1 seconds */
	mssleep(1000);

	// Unlock the SLCR
	Xil_Out32(SLCR_UNLOCK_ADDR, SLCR_UNLOCK_KEY);

	// Assert reset to all PL logic (all 4 interfaces)
	Xil_Out32(SLCR_PL_RST_CTRL, 0xF);
	mssleep(100);
	Xil_Out32(SLCR_PL_RST_CTRL, 0x0);

	/* Delay for 0.1 seconds */
	mssleep(100);

	// Trigger soft reset
	Xil_Out32(SLCR_PS_RST_CTRL, 0x1);
}

/**
 * @brief Verifies if the given UID exists in the authorized Maintainers database.
 */
u8 VerifyMaintainerDB(const uint8_t *uid, uint8_t uidLength) {
    if (uidLength != UID_LENGTH) {
        return NFC_ERROR_INVALID_UID;  // Invalid UID length
    }

    // Check if UID exists in the authorized list
    for (int i = 0; i < (sizeof(maintainerUIDs) / sizeof(maintainerUIDs[0])); i++) {
        if (memcmp(uid, maintainerUIDs[i], UID_LENGTH) == 0) {
            return NFC_SUCCESS;
        }
    }
    return NFC_ERROR_AUTH_FAILED;
}

/**
 * @brief Performs a relay hardware status check during maintenance mode.
 */
void Maintenance_CheckRelayStatus(void)
{

	if (GetRelayFaultInfo())
	{
		g_maintenanceStatus.relay = TRUE;
	}
	else {
		g_maintenanceStatus.relay = FALSE;
	}
}

/**
 * @brief Performs a network status check during maintenance mode.
 */
void Maintenance_CheckNetworkStatus(void)
{
	u8 retVal = checkNetworkConnectivity();
	if (retVal != XST_SUCCESS)
	{
		g_maintenanceStatus.network = TRUE;
	}
	else{
		g_maintenanceStatus.network = FALSE;
	}
}

/**
 * @brief Checks the voltage status during maintenance mode.
 */
void Maintenance_CheckVoltageStatus(void)
{

	if (GetVoltageFaultInfo())
	{
		g_maintenanceStatus.voltage = TRUE;
	}
	else {
		g_maintenanceStatus.voltage = FALSE;
	}
}

/**
 * @brief Checks the current status during maintenance mode.
 */
void Maintenance_CheckCurrentStatus(void)
{

	if (GetCurrentFaultInfo())
	{
		g_maintenanceStatus.current = TRUE;
	}
	else {
		g_maintenanceStatus.current = FALSE;
	}
}

/**
 * @brief Checks the GFCI status during maintenance mode.
 */
void Maintenance_CheckGFCIStatus(void)
{

	if (GetGFCIFaultInfo())
	{
		g_maintenanceStatus.gfci = TRUE;
	}
	else {
		g_maintenanceStatus.gfci = FALSE;
	}
}
