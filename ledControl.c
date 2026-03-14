/*
 * =====================================================================================
 * File Name:    ledControl.c
 * Author:       Vanix Technologies PVT. LTD.
 * Date Created: 2025-02-20
 * Description:  This source file contains the implementation of the LED control module.
 *               It provides APIs to initialize, control, and deinitialize LEDs connected
 *               to GPIO pins.
 *
 * Revision History:
 * Version 1.0 - 2025-02-26 - Initial version.
 * =====================================================================================
 */

#include "ledControl.h"
#include "gpioControl.h"
#include <unistd.h> // For usleep function

/*
 * =====================================================================================
 * Global Variables
 * =====================================================================================
 */
GpioModule ledGpioModule;		// GPIO module instance for the LED module

/*
 * =====================================================================================
 * Static Variables
 * =====================================================================================
 */

static GpioModule *gpioModule = NULL; // Pointer to the GPIO module instance
static bool isInitialized = false;    // Module initialization status

// LED configuration for the 3 available LEDs
static const LedConfig ledConfigs[MAX_LEDS] = {
    {1, 0, "LED ERROR (L14)"},  // Channel 1, Pin 0 (L14)
    {1, 1, "LED WIFI  (M14)"},  // Channel 1, Pin 1 (M14)
    {1, 2, "LED RFID  (M15)"}   // Channel 1, Pin 2 (M15)
};

/*
 * =====================================================================================
 * Static Helper Functions
 * =====================================================================================
 */

/**
 * @brief Validates the LED enum value.
 */
static bool validateLed(Led led) {
    return (led >= LED_ERROR && led <= LED_RFID);
}

/**
 * @brief Validates the GPIO module and initialization status.
 */
static bool validateLedModule() {
    if (!gpioModule) {
        xil_printf("ERROR: GPIO module pointer is NULL.\r\n");
        return false;
    }

    if (!isInitialized) {
        xil_printf("ERROR: LED module is not initialized.\r\n");
        return false;
    }

    return true;
}

/*
 * =====================================================================================
 * Public Function Definitions
 * =====================================================================================
 */

/**
 * @brief Initializes the LED module.
 *
 * This function initializes the LED module and configures the GPIO pins for the LEDs.
 */
LedStatus initLed(GpioModule *pGpioModule, u16 DeviceId) {
    if (!pGpioModule) {
        xil_printf("ERROR: GPIO module pointer is NULL.\r\n");
        return LED_STATUS_FAILURE;
    }

    // Initialize the GPIO module
    if (initGpio(pGpioModule, DeviceId) != GPIO_SUCCESS) {
        xil_printf("ERROR: Failed to initialize GPIO module.\r\n");
        return LED_STATUS_HW_ERROR;
    }

    gpioModule = pGpioModule;

    // Configure all LED pins as outputs
    for (u32 i = 0; i < MAX_LEDS; i++) {
        if (configureGpioPin(gpioModule, ledConfigs[i].Channel, ledConfigs[i].Pin, GPIO_DIRECTION_OUTPUT) != GPIO_SUCCESS) {
            xil_printf("ERROR: Failed to configure %s.\r\n", ledConfigs[i].Name);
            return LED_STATUS_HW_ERROR;
        }
    }

    isInitialized = true;
    xil_printf("LED Module Initialized Successfully.\r\n");
    return LED_STATUS_SUCCESS;
}

/**
 * @brief Sets the specified LED to HIGH (turns ON the LED).
 *
 * This function turns ON the specified LED by setting its GPIO pin to HIGH.
 */
LedStatus setLedPin(Led led) {
    if (!validateLedModule()) {
        return LED_STATUS_NOT_INITIALIZED;
    }

    if (!validateLed(led)) {
        xil_printf("ERROR: Invalid LED: %d\r\n", led);
        return LED_STATUS_INVALID_PARAM;
    }

    if (writeGpioPin(gpioModule, ledConfigs[led].Channel, ledConfigs[led].Pin, GPIO_PIN_HIGH) != GPIO_SUCCESS) {
        xil_printf("ERROR: Failed to set %s.\r\n", ledConfigs[led].Name);
        return LED_STATUS_HW_ERROR;
    }

    xil_printf("%s turned ON.\r\n", ledConfigs[led].Name);
    return LED_STATUS_SUCCESS;
}

/**
 * @brief Resets the specified LED to LOW (turns OFF the LED).
 *
 * This function turns OFF the specified LED by setting its GPIO pin to LOW.
 */
LedStatus resetLedPin(Led led) {
    if (!validateLedModule()) {
        return LED_STATUS_NOT_INITIALIZED;
    }

    if (!validateLed(led)) {
        xil_printf("ERROR: Invalid LED: %d\r\n", led);
        return LED_STATUS_INVALID_PARAM;
    }

    if (writeGpioPin(gpioModule, ledConfigs[led].Channel, ledConfigs[led].Pin, GPIO_PIN_LOW) != GPIO_SUCCESS) {
        xil_printf("ERROR: Failed to reset %s.\r\n", ledConfigs[led].Name);
        return LED_STATUS_HW_ERROR;
    }

    xil_printf("%s turned OFF.\r\n", ledConfigs[led].Name);
    return LED_STATUS_SUCCESS;
}

/**
 * @brief Toggles the specified LED state (ON to OFF or OFF to ON).
 *
 * This function toggles the state of the specified LED.
 */
LedStatus toggleLedPin(Led led) {
    if (!validateLedModule()) {
        return LED_STATUS_NOT_INITIALIZED;
    }

    if (!validateLed(led)) {
        xil_printf("ERROR: Invalid LED: %d\r\n", led);
        return LED_STATUS_INVALID_PARAM;
    }

    GpioPinState currentState = readGpioPin(gpioModule, ledConfigs[led].Channel, ledConfigs[led].Pin);
    if (currentState == GPIO_PIN_HIGH) {
        return resetLedPin(led);
    } else {
        return setLedPin(led);
    }
}

/**
 * @brief Deinitializes the LED module.
 *
 * This function deinitializes the LED module and turns off all LEDs.
 */
LedStatus deInitLed() {
    if (!validateLedModule()) {
        xil_printf("WARNING: LED module is already deinitialized.\r\n");
        return LED_STATUS_SUCCESS;
    }

    // Turn off all LEDs before deinitializing
    for (u32 i = 0; i < MAX_LEDS; i++) {
        resetLedPin((Led)i);
    }

    gpioModule = NULL;
    isInitialized = false;

    xil_printf("LED Module Deinitialized Successfully.\r\n");
    return LED_STATUS_SUCCESS;
}
