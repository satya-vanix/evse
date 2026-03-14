/*
 * =====================================================================================
 * File Name:    gpioControl.c
 * Author:       Vanix Technologies PVT. LTD.
 * Date Created: 2025-02-20
 * Description:  Implementation of GPIO driver APIs for Zynq-7000 Artix.
 *               Handles GPIO initialization, configuration, I/O, and interrupts.
 *
 * Revision History:
 * Version 1.2 - 2025-02-26 - Refined error handling, improved readability, added de-initialization.
 * =====================================================================================
 */

/*
 * =====================================================================================
 * Include Headers
 * =====================================================================================
 */

#include "gpioControl.h"

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
 * @brief Initializes the GPIO module.
 */
int initGpio(GpioModule *pGpioModule, u16 DeviceId) {
    if (!pGpioModule) {
    	xil_printf("ERROR: GPIO module pointer is NULL.\r\n");
    	return GPIO_FAILURE;
    }

    XGpio_Config *Config = XGpio_LookupConfig(DeviceId);
    if (!Config) {
        xil_printf("ERROR: GPIO LookupConfig failed for Device ID: %d\r\n", DeviceId);
        return GPIO_FAILURE;
    }

    if (XGpio_CfgInitialize(&pGpioModule->Instance, Config, Config->BaseAddress) != XST_SUCCESS) {
        xil_printf("ERROR: GPIO CfgInitialize failed for Device ID: %d\r\n", DeviceId);
        return GPIO_FAILURE;
    }

    pGpioModule->IsInitialized = 1;
    pGpioModule->DeviceId = DeviceId;
    xil_printf("GPIO Initialized Successfully for Device ID: %d\r\n", DeviceId);

    return GPIO_SUCCESS;
}

/**
 * @brief Configures a GPIO pin as input or output.
 */
int configureGpioPin(GpioModule *pGpioModule, u32 Channel, u32 Pin, GpioDirection Direction) {
    if (!pGpioModule || !pGpioModule->IsInitialized) {
    	xil_printf("ERROR: GPIO module is not initialized.\r\n");
    	return GPIO_NOT_INITIALIZED;
    }

    if (Channel < 1 || Channel > GPIO_MAX_CHANNELS) {
    	xil_printf("ERROR: Invalid GPIO channel: %d\r\n", Channel);
    	return GPIO_INVALID_PARAM;
    }

    if (Pin >= GPIO_MAX_PINS_PER_CHANNEL) {
    	xil_printf("ERROR: Invalid GPIO pin: %d\r\n", Pin);
    	return GPIO_INVALID_PARAM;
    }

    u32 PinMask = 1 << Pin;
    u32 CurrentDirection = XGpio_GetDataDirection(&pGpioModule->Instance, Channel);

    if (Direction == GPIO_DIRECTION_INPUT) {
        CurrentDirection |= PinMask;
    } else {
        CurrentDirection &= ~PinMask;
    }

    XGpio_SetDataDirection(&pGpioModule->Instance, Channel, CurrentDirection);
    xil_printf("GPIO Channel %d, Pin %d configured as %s\r\n",
                   Channel, Pin, (Direction == GPIO_DIRECTION_INPUT) ? "INPUT" : "OUTPUT");

    return GPIO_SUCCESS;
}

/**
 * @brief Writes a value to a GPIO pin.
 */
int writeGpioPin(GpioModule *pGpioModule, u32 Channel, u32 Pin, GpioPinState Value) {
    if (!pGpioModule || !pGpioModule->IsInitialized) {
    	xil_printf("ERROR: GPIO module is not initialized.\r\n");
        return GPIO_NOT_INITIALIZED;
    }

    if (Channel < 1 || Channel > GPIO_MAX_CHANNELS) {
    	xil_printf("ERROR: Invalid GPIO channel: %d\r\n", Channel);
    	return GPIO_INVALID_PARAM;
    }

    if (Pin >= GPIO_MAX_PINS_PER_CHANNEL) {
    	xil_printf("ERROR: Invalid GPIO pin: %d\r\n", Pin);
    	return GPIO_INVALID_PARAM;
    }

    u32 PinMask = 1 << Pin;
    u32 CurrentValue = XGpio_DiscreteRead(&pGpioModule->Instance, Channel);

    // Update the pin value
    if (Value == GPIO_PIN_HIGH) {
        CurrentValue |= PinMask;
    } else {
        CurrentValue &= ~PinMask;
    }

    XGpio_DiscreteWrite(&pGpioModule->Instance, Channel, CurrentValue);
    xil_printf("GPIO Channel %d, Pin %d set to %s\r\n",
                   Channel, Pin, (Value == GPIO_PIN_HIGH) ? "HIGH" : "LOW");

    return GPIO_SUCCESS;
}


/**
 * @brief Reads the value from a GPIO pin.
 */
GpioPinState readGpioPin(GpioModule *pGpioModule, u32 Channel, u32 Pin) {
    if (!pGpioModule || !pGpioModule->IsInitialized) {
    	xil_printf("ERROR: GPIO module is not initialized.\r\n");
        return GPIO_NOT_INITIALIZED;  // Ensure module is initialized before reading
    }

    if (Channel < 1 || Channel > GPIO_MAX_CHANNELS) {
    	xil_printf("ERROR: Invalid GPIO channel: %d\r\n", Channel);
    	return GPIO_INVALID_PARAM;
    }

    if (Pin >= GPIO_MAX_PINS_PER_CHANNEL) {
    	xil_printf("ERROR: Invalid GPIO pin: %d\r\n", Pin);
    	return GPIO_INVALID_PARAM;
    }

    u32 PinMask = 1 << Pin;
    u32 ReadValue = XGpio_DiscreteRead(&pGpioModule->Instance, Channel);
    xil_printf("GPIO Channel %d, Pin %d read as %s\r\n",
                   Channel, Pin, (ReadValue) ? "HIGH" : "LOW");

    return (ReadValue & PinMask) ? GPIO_PIN_HIGH : GPIO_PIN_LOW;
}

/**
 * @brief Reads the entire value from a GPIO channel.
 */
u32 readGpioChannel(GpioModule *pGpioModule, u32 Channel) {
    if (!pGpioModule || !pGpioModule->IsInitialized) {
    	xil_printf("ERROR: GPIO module is not initialized.\r\n");
        return 0;
    }

    if (Channel < 1 || Channel > GPIO_MAX_CHANNELS) {
    	xil_printf("ERROR: Invalid GPIO channel: %d\r\n", Channel);
    	return 0;
    }

    u32 ChannelValue = XGpio_DiscreteRead(&pGpioModule->Instance, Channel);
    xil_printf("GPIO Channel %d read complete value: 0x%08X\r\n", Channel, ChannelValue);

    return ChannelValue;
}

/**
 * @brief Deinitializes the GPIO module.
 */
int deInitGpio(GpioModule *pGpioModule) {
	if (!pGpioModule) {
		xil_printf("ERROR: GPIO module pointer is NULL.\r\n");
		return GPIO_FAILURE;
	}

	if (!pGpioModule->IsInitialized) {
		xil_printf("WARNING: GPIO module is already deinitialized.\r\n");
		return GPIO_SUCCESS;
	}

	// Disable all interrupts
	XGpio_InterruptDisable(&pGpioModule->Instance, XGPIO_IR_CH1_MASK | XGPIO_IR_CH2_MASK);
	XGpio_InterruptGlobalDisable(&pGpioModule->Instance);

	// Reset initialization flag
	pGpioModule->IsInitialized = 0;
	xil_printf("GPIO Deinitialized Successfully for Device ID: %d\r\n", pGpioModule->DeviceId);

	return GPIO_SUCCESS;
}

/**
 * @brief Initializes the GPIO interrupt system.
 */
int initGpioInterrupt(XScuGic *IntcInstancePtr, GpioModule *pGpioModule, u32 InterruptId,
                      Xil_ExceptionHandler Handler, void *CallbackRef) {
    if (!IntcInstancePtr || !pGpioModule) {
    	xil_printf("ERROR: Interrupt controller or GPIO module pointer is NULL.\r\n");
    	return GPIO_FAILURE;
    }

    XScuGic_Config *IntcConfig = XScuGic_LookupConfig(XPAR_SCUGIC_0_DEVICE_ID);
    if (!IntcConfig) {
    	xil_printf("ERROR: Interrupt controller lookup failed.\r\n");
    	return GPIO_FAILURE;
    }

    if (XScuGic_CfgInitialize(IntcInstancePtr, IntcConfig, IntcConfig->CpuBaseAddress) != GPIO_SUCCESS) {
    	xil_printf("ERROR: Interrupt controller initialization failed.\r\n");
    	return GPIO_FAILURE;
    }

    if (XScuGic_Connect(IntcInstancePtr, InterruptId, Handler, CallbackRef) != GPIO_SUCCESS) {
    	xil_printf("ERROR: Failed to connect interrupt handler.\r\n");
    	return GPIO_FAILURE;
    }

    XScuGic_Enable(IntcInstancePtr, InterruptId);
    Xil_ExceptionInit();
    Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_INT, (Xil_ExceptionHandler)XScuGic_InterruptHandler, IntcInstancePtr);
    Xil_ExceptionEnable();

    xil_printf("GPIO Interrupt Initialized Successfully for Interrupt ID: %d\r\n", InterruptId);
    return GPIO_SUCCESS;
}

/**
 * @brief Enables GPIO interrupt for a given mask.
 */
int enableGpioInterrupt(GpioModule *pGpioModule, u32 Mask) {
    if (!pGpioModule || !pGpioModule->IsInitialized) {
    	xil_printf("ERROR: GPIO module is not initialized.\r\n");
    	return GPIO_NOT_INITIALIZED;
    }

    XGpio_InterruptEnable(&pGpioModule->Instance, Mask);
    XGpio_InterruptGlobalEnable(&pGpioModule->Instance);

    xil_printf("GPIO Interrupt Enabled for Mask: 0x%X\r\n", Mask);
    return GPIO_SUCCESS;
}

/**
 * @brief Disables GPIO interrupt for a given mask.
 */
int disableGpioInterrupt(GpioModule *pGpioModule, u32 Mask) {
    if (!pGpioModule || !pGpioModule->IsInitialized) {
    	xil_printf("ERROR: GPIO module is not initialized.\r\n");
    	return GPIO_NOT_INITIALIZED;
    }

    XGpio_InterruptDisable(&pGpioModule->Instance, Mask);

    xil_printf("GPIO Interrupt Disabled for Mask: 0x%X\r\n", Mask);
    return GPIO_SUCCESS;
}

/**
 * @brief Clears GPIO interrupt for a given mask.
 */
int clearGpioInterrupt(GpioModule *pGpioModule, u32 Mask) {
    if (!pGpioModule || !pGpioModule->IsInitialized) {
    	xil_printf("ERROR: GPIO module is not initialized.\r\n");
    	return GPIO_NOT_INITIALIZED;
    }

    XGpio_InterruptClear(&pGpioModule->Instance, Mask);
    xil_printf("GPIO Interrupt Cleared for Mask: 0x%X\r\n", Mask);
    return GPIO_SUCCESS;
}
