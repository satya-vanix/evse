/*
 * =====================================================================================
 * File Name:    iicControl.c
 * Author:       Vanix Technologies PVT. LTD.
 * Date Created: 2025-02-14
 * Description:  This source file contains the implementations for the functions
 *               and constants used in the I2C module control.
 *
 * Revision History:
 * Version 1.0 - 2025-02-14 - Initial version.
 * =====================================================================================
 */

/*
 * =====================================================================================
 * Include Headers
 * =====================================================================================
 */
#include "iicControl.h"

/*
 * =====================================================================================
 * Global Variables
 * =====================================================================================
 */
XIicPs Iic;
XIicPs Iic0;

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
 * @brief  Performs a self-test on the I2C interface.
 *
 * This function executes a self-test on the I2C interface to verify its functionality and
 * ensure that communication with the device identified by `DeviceId` is working correctly.
 * The self-test typically involves sending and receiving test data over the I2C bus.
 *
 * @param  DeviceId  		The unique identifier of the I2C device to be tested.
 * @retval XST_SUCCESS      If the self-test is successful and the device is functioning correctly.
 * @retval XST_FAILURE      If the self-test fails or an error is encountered.
 */
static int IicPsSelfTestExample(u16 DeviceId)
{
	int Status;
	XIicPs_Config *Config;

	/*
	 * Initialize the IIC driver so that it's ready to use
	 * Look up the configuration in the config table, then initialize it.
	 */
	Config = XIicPs_LookupConfig(DeviceId);
	if (NULL == Config) {
		return XST_FAILURE;
	}

	Status = XIicPs_CfgInitialize(&Iic, Config, Config->BaseAddress);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}

	/*
	 * Perform a self-test.
	 */
	Status = XIicPs_SelfTest(&Iic);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}

	/*
	 * Set the IIC serial clock rate.
	 */
	XIicPs_SetSClk(&Iic, IIC_SCLK_RATE);

	return XST_SUCCESS;
}

/**
 * @brief  Performs a self-test on the I2C0 interface.
 *
 * This function executes a self-test on the I2C0 interface to verify its functionality and
 * ensure that communication with the device identified by `DeviceId` is working correctly.
 * The self-test typically involves sending and receiving test data over the I2C bus.
 *
 * @param  DeviceId            The unique identifier of the I2C device to be tested.
 * @retval XST_SUCCESS      If the self-test is successful and the device is functioning correctly.
 * @retval XST_FAILURE      If the self-test fails or an error is encountered.
 */
static int Iic0PsSelfTestExample(u16 DeviceId)
{
	int Status;
	XIicPs_Config *Config;

	/*
	 * Initialize the IIC driver so that it's ready to use
	 * Look up the configuration in the config table, then initialize it.
	 */
	Config = XIicPs_LookupConfig(DeviceId);
	if (NULL == Config) {
		return XST_FAILURE;
	}

	Status = XIicPs_CfgInitialize(&Iic0, Config, Config->BaseAddress);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}

	/*
	 * Perform a self-test.
	 */
	Status = XIicPs_SelfTest(&Iic0);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}

	/*
	 * Set the IIC serial clock rate.
	 */
	XIicPs_SetSClk(&Iic0, IIC_SCLK_RATE);

	return XST_SUCCESS;
}

/*
 * =====================================================================================
 * Global Function Definitions
 * =====================================================================================
 */

/**
 * @brief  Initializes the I2C interface for a specified device.
 */
int iic_init(u16 DeviceId)
{
	if (DeviceId == IIC0_DEVICE_ID) {
		return Iic0PsSelfTestExample(DeviceId);
	}
	else if (DeviceId == IIC_DEVICE_ID){
		return IicPsSelfTestExample(DeviceId);
	}
	else {
		xil_printf("Invalid DeviceId :- %u\r\n", DeviceId);
		return XST_FAILURE;
	}
}
