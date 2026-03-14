/*
 * =====================================================================================
 * File Name:    qspiControl.c
 * Author:       Vanix Technologies PVT. LTD.
 * Date Created: 2025-03-25
 * Description:  This header file provides a comprehensive and generic QSPI wrapper
 *               module for the Xilinx Zynq-7000 platform. It is designed to support
 *               robust and secure QSPI Flash operations.
 *
 *               Features:
 *               - QSPI Initialization & Configuration
 *               - Secure Read, Write, and Erase operations
 *               - Status polling and error handling
 *               - Support for Sector, Block, and Chip erase
 *               - Generic flash compatibility without vendor-specific commands
 *               - Quad-SPI mode support
 *               - Automatic write enable handling
 *               - Enhanced debugging with logging
 *
 * Revision History:
 * Version 1.0 - 2025-03-25 - Initial Implementations
 * =====================================================================================
 */

/*
 * =====================================================================================
 * Include Headers
 * =====================================================================================
 */

#include "qspiControl.h"
#include "xil_printf.h"
#include "sleep.h"
#include <string.h>
#include <stdlib.h>

/*
 * =====================================================================================
 * Static Variables
 * =====================================================================================
 */

static int EraseInProgress = 0;

/*
 * =====================================================================================
 * Static Function Definitions
 * =====================================================================================
 */

/**
 * @brief Reads a status register from the QSPI flash memory.
 *
 * This function sends a read status register command and retrieves the value of
 * the specified status register.
 *
 * @param QspiInstancePtr Pointer to the QSPI instance.
 * @param cmd Command byte specifying which status register to read.
 * @param status Pointer to store the retrieved status register value.
 *
 * @return
 *         - QSPI_SUCCESS if the status register is successfully read.
 *         - QSPI_FAILURE if the transfer operation fails.
 */
static int qspiReadStatusRegister(XQspiPs *QspiInstancePtr, uint8_t cmd, uint8_t *status)
{
    uint8_t readCmd[] = { cmd, 0 };
    uint8_t statusReg[2];
    if (XQspiPs_PolledTransfer(QspiInstancePtr, readCmd, statusReg, sizeof(readCmd)) != XST_SUCCESS) {
        xil_printf("ERROR: Status Register Read failed!\r\n");
        return QSPI_FAILURE;
    }
    *status = statusReg[1];
    return QSPI_SUCCESS;
}

/**
 * @brief Writes a value to a status register in the QSPI flash memory.
 *
 * This function sends a write status register command to modify the specified
 * status register in the QSPI flash memory.
 *
 * @param QspiInstancePtr Pointer to the QSPI instance.
 * @param reg Command byte specifying which status register to write.
 * @param data The value to be written to the status register.
 *
 * @return
 *         - QSPI_SUCCESS if the status register is successfully written.
 *         - QSPI_INVALID_PARAM if QspiInstancePtr is NULL.
 *         - QSPI_FAILURE if the transfer operation fails.
 */
static int qspiWriteStatusRegister(XQspiPs *QspiInstancePtr, uint8_t reg, uint8_t data)
{
    if (QspiInstancePtr == NULL) {
        xil_printf("ERROR: Invalid QSPI instance for Write Status Register.\r\n");
        return QSPI_INVALID_PARAM;
    }

    uint8_t cmd_buf[2] = {reg, data};
    int ret = XQspiPs_PolledTransfer(QspiInstancePtr, cmd_buf, NULL, 2);
    return (ret == XST_SUCCESS) ? QSPI_SUCCESS : QSPI_FAILURE;
}

/**
 * @brief Enables write operations on the QSPI flash memory.
 *
 * This function sends the Write Enable command to the QSPI flash memory and verifies
 * that the Write Enable Latch (WEL) bit is set in the status register.
 *
 * @param QspiInstancePtr Pointer to the QSPI instance.
 *
 * @return
 *         - QSPI_SUCCESS if the Write Enable command is successfully sent and WEL bit is set.
 *         - QSPI_INVALID_PARAM if QspiInstancePtr is NULL.
 *         - QSPI_FAILURE if the command fails or WEL bit is not set.
 */
static int qspiWriteEnable(XQspiPs *QspiInstancePtr)
{
    int Status;
    uint8_t writeEnableCmd = WRITE_ENABLE_CMD;
    uint8_t StatusReg = 0;
    int Retry = 0;

    if (QspiInstancePtr == NULL) {
        xil_printf("ERROR: Null QSPI instance in Write Enable.\r\n");
        return QSPI_INVALID_PARAM;
    }

    // Send Write Enable command
    Status = XQspiPs_PolledTransfer(QspiInstancePtr, &writeEnableCmd, NULL, sizeof(writeEnableCmd));
    if (Status != XST_SUCCESS) {
        xil_printf("ERROR: Write Enable command failed!\r\n");
        return QSPI_FAILURE;
    }

    // Verify WEL bit set (Bit 1)
    do {
        Status = qspiReadStatusRegister(QspiInstancePtr, READ_STATUS_CMD, &StatusReg);
        if (Status != QSPI_SUCCESS) {
            xil_printf("ERROR: Failed to read status reg after Write Enable!\r\n");
            return QSPI_FAILURE;
        }

        if (StatusReg & 0x02) { // WEL bit set
#ifdef QSPI_DEBUG
            xil_printf("INFO: Write Enable Successful. WEL=1 (Status: 0x%02X)\r\n", StatusReg);
#endif

            return QSPI_SUCCESS;
        }
        Retry++;
    } while (Retry < 10);

    xil_printf("ERROR: WEL bit not set after Write Enable!\r\n");
    return QSPI_FAILURE;
}

/**
 * @brief Disables write operations on the QSPI flash memory.
 *
 * This function sends the Write Disable command to the QSPI flash memory,
 * ensuring that the device is protected from unintended write operations.
 *
 * @param QspiInstancePtr Pointer to the QSPI instance.
 *
 * @return
 *         - QSPI_SUCCESS if the Write Disable command is successfully sent.
 *         - QSPI_INVALID_PARAM if QspiInstancePtr is NULL.
 *         - QSPI_FAILURE if the command fails.
 */
static int qspiWriteDisable(XQspiPs *QspiInstancePtr)
{
    if (QspiInstancePtr == NULL) {
        xil_printf("ERROR: Invalid QSPI instance for Write Disable.\r\n");
        return QSPI_INVALID_PARAM;
    }

    uint8_t cmd = WRITE_DISABLE_CMD;
    if (XQspiPs_PolledTransfer(QspiInstancePtr, &cmd, NULL, 1) != XST_SUCCESS) {
        xil_printf("ERROR: Write Disable command failed!\r\n");
        return QSPI_FAILURE;
    }
    return QSPI_SUCCESS;
}

/**
 * @brief Waits for the QSPI flash memory write operation to complete.
 *
 * This function repeatedly checks the Write-In-Progress (WIP) bit in the status register
 * until it is cleared, indicating that the write operation has completed.
 *
 * @param QspiInstancePtr Pointer to the QSPI instance.
 *
 * @return
 *         - QSPI_SUCCESS if the write operation completes successfully.
 *         - QSPI_FAILURE if reading the status register fails or timeout occurs.
 */
static int qspiWaitForWriteEnd(XQspiPs *QspiInstancePtr)
{
    uint8_t StatusReg;
    int Status;
    int Retry = 0;

    do {
        Status = qspiReadStatusRegister(QspiInstancePtr, READ_STATUS_CMD, &StatusReg);
        if (Status != QSPI_SUCCESS) {
            xil_printf("ERROR: Failed to read status register!\r\n");
            return QSPI_FAILURE;
        }
        Retry++;
        if (Retry > 1000000) {
            xil_printf("ERROR: Timeout waiting for WIP to clear!\r\n");
            return QSPI_FAILURE;
        }
    } while (StatusReg & 0x01); // WIP bit is Bit 0

    return QSPI_SUCCESS;
}

/**
 * @brief Enables Quad Mode on the QSPI flash memory.
 *
 * This function reads the JEDEC ID to verify the correct flash device,
 * checks the status register for the Quad Enable (QE) bit, and sets it
 * if it is not already enabled.
 *
 * @param QspiInstancePtr Pointer to the QSPI instance.
 *
 * @return
 *         - QSPI_SUCCESS if Quad Mode is successfully enabled or already enabled.
 *         - QSPI_INVALID_PARAM if QspiInstancePtr is NULL or the JEDEC ID does not match.
 *         - QSPI_FAILURE if any operation fails, such as reading/writing registers.
 */
static int qspiQuadEnable(XQspiPs *QspiInstancePtr)
{
    int Status;
    uint8_t statusReg = 0;
    uint8_t manufacturerID[3] = {0};

    if (QspiInstancePtr == NULL) {
        xil_printf("ERROR: Invalid QSPI instance pointer!\r\n");
        return QSPI_INVALID_PARAM;
    }

    /* Read JEDEC ID to ensure flash is correct */
    Status = qspiReadID(QspiInstancePtr, manufacturerID);
    if (Status != QSPI_SUCCESS) {
        xil_printf("ERROR: Failed to read JEDEC ID!\r\n");
        return QSPI_FAILURE;
    }

    if (manufacturerID[0] != 0xEF || manufacturerID[1] != 0x40 || manufacturerID[2] != 0x18) {
        xil_printf("ERROR: JEDEC ID does not match W25Q128JV!\r\n");
        return QSPI_INVALID_PARAM;
    }

    /* Read Status Register */
    Status = qspiReadStatusRegister(QspiInstancePtr, READ_STATUS_REG_2_CMD, &statusReg);
    if (Status != QSPI_SUCCESS) {
        xil_printf("ERROR: Failed to read Status Register 2!\r\n");
        return QSPI_FAILURE;
    }

    xil_printf("INFO: Current Status Register 2: 0x%02X\r\n", statusReg);

    /* Check if Quad Enable (QE) bit is set */
    if (statusReg & 0x02) {
        xil_printf("INFO: Quad Mode already enabled.\r\n");
        return QSPI_SUCCESS;
    }

    /* Enable Write */
    Status = qspiWriteEnable(QspiInstancePtr);
    if (Status != QSPI_SUCCESS) {
        xil_printf("ERROR: Write Enable failed!\r\n");
        return QSPI_FAILURE;
    }

    /* Set QE Bit */
    statusReg |= 0x02; // QE bit is bit 1

    Status = qspiWriteStatusRegister(QspiInstancePtr, WRITE_STATUS_CMD, statusReg);
    if (Status != QSPI_SUCCESS) {
        xil_printf("ERROR: Failed to write Status Register 2!\r\n");
        return QSPI_FAILURE;
    }

    /* Wait for completion */
    Status = qspiWaitForWriteEnd(QspiInstancePtr);
    if (Status != QSPI_SUCCESS) {
        xil_printf("ERROR: Timeout waiting after QE set!\r\n");
        return QSPI_FAILURE;
    }

    xil_printf("INFO: Quad Mode successfully enabled!\r\n");
    return QSPI_SUCCESS;
}

static int qspiEraseCore(XQspiPs *QspiInstancePtr, uint8_t EraseCmd, uint32_t Address, uint32_t BytesToErase)
{
    int Status;
    u8 CmdBuffer[4];

    if (QspiInstancePtr == NULL) {
        xil_printf("ERROR: Invalid QSPI instance pointer.\r\n");
        return QSPI_INVALID_PARAM;
    }

    if (EraseCmd == CHIP_ERASE_CMD) {
#ifdef QSPI_DEBUG
        xil_printf("qspiEraseCore: Initiating Chip Erase...\r\n");
#endif
        Status = qspiWaitForWriteEnd(QspiInstancePtr);
        if (Status != XST_SUCCESS) return Status;

        Status = qspiWriteEnable(QspiInstancePtr);
        if (Status != QSPI_SUCCESS) return Status;

        CmdBuffer[0] = CHIP_ERASE_CMD;
        Status = XQspiPs_PolledTransfer(QspiInstancePtr, CmdBuffer, NULL, 1);
        if (Status != XST_SUCCESS) {
            xil_printf("ERROR: Chip Erase command failed!\r\n");
            return Status;
        }

        Status = qspiWaitForWriteEnd(QspiInstancePtr);
        if (Status != XST_SUCCESS) {
            xil_printf("ERROR: Timeout waiting for Chip Erase completion!\r\n");
            return Status;
        }

#ifdef QSPI_DEBUG
        xil_printf("qspiEraseCore: Chip Erase completed successfully.\r\n");
#endif
        return QSPI_SUCCESS;
    }

    if (Address >= QSPI_FLASH_SIZE) {
        xil_printf("ERROR: Address 0x%08X exceeds flash size!\r\n", Address);
        return QSPI_INVALID_PARAM;
    }

    uint32_t EraseSize = 0;
    if (EraseCmd == SECTOR_ERASE_CMD) EraseSize = QSPI_SECTOR_SIZE;
    else if (EraseCmd == BLOCK_ERASE_CMD) EraseSize = QSPI_BLOCK_SIZE;
    else if (EraseCmd == PAGE_ERASE_CMD) EraseSize = QSPI_PAGE_SIZE;

    if (EraseSize == 0) {
        xil_printf("ERROR: Unsupported erase command 0x%02X\r\n", EraseCmd);
        return QSPI_INVALID_PARAM;
    }

    if ((Address % EraseSize) != 0) {
        xil_printf("ERROR: Address 0x%08X not aligned to erase size %u bytes!\r\n", Address, EraseSize);
        return QSPI_INVALID_PARAM;
    }

#ifdef QSPI_DEBUG
    xil_printf("qspiEraseCore: Erasing at 0x%08X (Size=%u)\r\n", Address, BytesToErase);
#endif

    Status = qspiWaitForWriteEnd(QspiInstancePtr);
    if (Status != XST_SUCCESS) return Status;

    Status = qspiWriteEnable(QspiInstancePtr);
    if (Status != QSPI_SUCCESS) return Status;

    // Prepare erase command + 3-byte address
    CmdBuffer[0] = EraseCmd;
    CmdBuffer[1] = (Address >> 16) & 0xFF;
    CmdBuffer[2] = (Address >> 8) & 0xFF;
    CmdBuffer[3] = Address & 0xFF;

    // Perform erase operation
    Status = XQspiPs_PolledTransfer(QspiInstancePtr, CmdBuffer, NULL, 4);
    if (Status != XST_SUCCESS) {
        xil_printf("ERROR: Erase command 0x%02X failed at 0x%08X\r\n", EraseCmd, Address);
        return Status;
    }

    Status = qspiWaitForWriteEnd(QspiInstancePtr);
    if (Status != XST_SUCCESS) {
        xil_printf("ERROR: Timeout waiting for erase at 0x%08X\r\n", Address);
        return Status;
    }
#ifdef QSPI_DEBUG
    xil_printf("qspiEraseCore: Erase successful at 0x%08X\r\n", Address);
#endif
    return QSPI_SUCCESS;
}

static int qspiWriteCore(XQspiPs *QspiInstancePtr, uint32_t Address, uint8_t *pBuffer, uint32_t ByteCount)
{
    int Status;
    u8 CmdBuffer[4];

    if (QspiInstancePtr == NULL || pBuffer == NULL || ByteCount == 0) {
        xil_printf("ERROR: Invalid parameters for qspiWriteCore.\r\n");
        return QSPI_INVALID_PARAM;
    }

    /* Wait until flash is ready */
    Status = qspiWaitForWriteEnd(QspiInstancePtr);
    if (Status != XST_SUCCESS) return Status;

    /* Write Enable */
    Status = qspiWriteEnable(QspiInstancePtr);
    if (Status != XST_SUCCESS) return Status;

    /* Prepare Page Program command + 3-byte address */
    CmdBuffer[0] = PAGE_PROGRAM_CMD;  // 0x02
    CmdBuffer[1] = (Address >> 16) & 0xFF;
    CmdBuffer[2] = (Address >> 8) & 0xFF;
    CmdBuffer[3] = Address & 0xFF;

    /* Merge command + data into buffer */
    u8 TempBuffer[4 + QSPI_PAGE_SIZE]; // Max = page size
    memcpy(TempBuffer, CmdBuffer, 4);
    memcpy(&TempBuffer[4], pBuffer, ByteCount);

    /* Transfer */
    Status = XQspiPs_PolledTransfer(QspiInstancePtr, TempBuffer, NULL, 4 + ByteCount);
    if (Status != XST_SUCCESS) return Status;

    /* Wait for completion */
    Status = qspiWaitForWriteEnd(QspiInstancePtr);
    if (Status != XST_SUCCESS) return Status;

#ifdef QSPI_DEBUG
    xil_printf("qspiWriteCore: Writing at Addr=0x%08X, Bytes=%d\r\n", Address, ByteCount);
#endif

    return QSPI_SUCCESS;
}

static int qspiReadCore(XQspiPs *QspiInstancePtr, uint32_t Address, uint8_t *pBuffer, uint32_t ByteCount)
{
    int Status;
    u8 CmdBuffer[4];  // Command + 3-byte address
    u8 LocalReadBuffer[QSPI_PAGE_SIZE + 4]; // Enough for full page

    if (QspiInstancePtr == NULL || pBuffer == NULL || ByteCount == 0) {
        xil_printf("ERROR: Invalid parameters for qspiReadCore.\r\n");
        return QSPI_INVALID_PARAM;
    }

#ifdef QSPI_DEBUG
    xil_printf("qspiReadCore: Addr=0x%X, Bytes=%d\r\n", Address, ByteCount);
#endif

    // Prepare normal Read command (0x03) - no dummy cycles
    CmdBuffer[0] = READ_DATA_CMD; // 0x03
    CmdBuffer[1] = (Address >> 16) & 0xFF;
    CmdBuffer[2] = (Address >> 8) & 0xFF;
    CmdBuffer[3] = Address & 0xFF;

    // Total transfer: cmd + addr + data
    Status = XQspiPs_PolledTransfer(QspiInstancePtr, CmdBuffer, LocalReadBuffer, 4 + ByteCount);
    if (Status != XST_SUCCESS) {
        xil_printf("ERROR: QSPI Read Transfer failed!\r\n");
        return Status;
    }

    // Copy received data (skip the command and address bytes)
    memcpy(pBuffer, &LocalReadBuffer[4], ByteCount);

#ifdef QSPI_DEBUG
    xil_printf("qspiReadCore: Read Successful\r\n");
#endif

    return QSPI_SUCCESS;
}

/**
 * @brief Writes a page of data to the QSPI flash memory.
 *
 * This function writes up to a page size of data to the QSPI flash memory, ensuring
 * that the write does not cross a page boundary. If the requested write size exceeds
 * the remaining space in the page, it is adjusted accordingly.
 *
 * @param QspiInstancePtr Pointer to the QSPI instance.
 * @param pBuffer Pointer to the data buffer containing the bytes to be written.
 * @param Address The starting address in the flash memory where data will be written.
 * @param NumByteToWrite_up_to_PageSize Number of bytes to write, limited to the page boundary.
 *
 * @return QSPI_SUCCESS on success, or an error code on failure.
 */
static int qspiWritePage(XQspiPs *QspiInstancePtr, uint8_t *pBuffer, uint32_t Address, uint32_t NumByteToWrite_up_to_PageSize)
{
    if (QspiInstancePtr == NULL || pBuffer == NULL) {
        xil_printf("ERROR: Invalid parameters for qspiWritePage.\r\n");
        return QSPI_INVALID_PARAM;
    }

    // Ensure write size does not exceed page boundary
    uint32_t PageBoundary = ((Address / QSPI_PAGE_SIZE) + 1) * QSPI_PAGE_SIZE;
    uint32_t BytesLeftInPage = PageBoundary - Address;
    if (NumByteToWrite_up_to_PageSize > BytesLeftInPage) {
        NumByteToWrite_up_to_PageSize = BytesLeftInPage;
    }

#ifdef QSPI_DEBUG
    xil_printf("qspiWritePage: Addr=0x%08X, WriteBytes=%d\r\n", Address, NumByteToWrite_up_to_PageSize);
#endif

    return qspiWriteCore(QspiInstancePtr, Address, pBuffer, NumByteToWrite_up_to_PageSize);
}

/**
 * @brief Writes a sector of data to the QSPI flash memory.
 *
 * This function writes data to a QSPI sector, ensuring that the write process
 * does not exceed sector boundaries. It writes data page by page within the sector.
 *
 * @param QspiInstancePtr Pointer to the QSPI instance.
 * @param pBuffer Pointer to the data buffer containing the bytes to be written.
 * @param Address The starting address in the flash memory where data will be written.
 * @param NumByteToWrite Number of bytes to write within the sector.
 *
 * @return QSPI_SUCCESS on success, or an error code on failure.
 */
int qspiWriteSector(XQspiPs *QspiInstancePtr, uint8_t *pBuffer, uint32_t Address, uint32_t NumByteToWrite)
{
    uint32_t StartPage;
    int32_t BytesRemaining;
    int Status;

    if (QspiInstancePtr == NULL || pBuffer == NULL) {
        xil_printf("ERROR: Invalid parameters for qspiWriteSector.\r\n");
        return QSPI_INVALID_PARAM;
    }

#ifdef QSPI_DEBUG
    xil_printf("qspiWriteSector: StartAddr=0x%08X, TotalWriteBytes=%d\r\n", Address, NumByteToWrite);
#endif

    uint32_t CurrentAddress = Address;
    BytesRemaining = NumByteToWrite;

    while (BytesRemaining > 0) {
        uint32_t SectorBoundary = (CurrentAddress / QSPI_SECTOR_SIZE) * QSPI_SECTOR_SIZE;
        uint32_t BytesLeftInSector = QSPI_SECTOR_SIZE - (CurrentAddress - SectorBoundary);
        uint32_t WriteBytesNow = (BytesRemaining < BytesLeftInSector) ? BytesRemaining : BytesLeftInSector;

#ifdef QSPI_DEBUG
        xil_printf("Writing SectorAddr=0x%08X, Bytes=%d\r\n", CurrentAddress, WriteBytesNow);
#endif

        // Start writing page-wise
        StartPage = CurrentAddress / QSPI_PAGE_SIZE;
        int32_t BytesInSectorRemaining = WriteBytesNow;
        uint8_t *LocalBufferPtr = pBuffer;

        do {
            uint32_t PageBoundary = (StartPage + 1) * QSPI_PAGE_SIZE;
            uint32_t BytesLeftInPage = PageBoundary - CurrentAddress;
            uint32_t BytesToWriteNow = (BytesInSectorRemaining < BytesLeftInPage) ? BytesInSectorRemaining : BytesLeftInPage;

            Status = qspiWritePage(QspiInstancePtr, LocalBufferPtr, CurrentAddress, BytesToWriteNow);
            if (Status != XST_SUCCESS) return Status;

            LocalBufferPtr += BytesToWriteNow;
            BytesInSectorRemaining -= BytesToWriteNow;
            CurrentAddress += BytesToWriteNow;
            StartPage = CurrentAddress / QSPI_PAGE_SIZE;
        } while (BytesInSectorRemaining > 0);

        // Move to next sector
        BytesRemaining -= WriteBytesNow;
        pBuffer += WriteBytesNow;
        CurrentAddress += WriteBytesNow;
    }

#ifdef QSPI_DEBUG
    xil_printf("qspiWriteSector: Sector Write Complete.\r\n");
#endif

    return QSPI_SUCCESS;
}

/**
 * @brief Writes a block of data to the QSPI flash memory.
 *
 * This function writes data to a QSPI block, ensuring that the write process
 * does not exceed block boundaries. It writes data page by page within the block.
 *
 * @param QspiInstancePtr Pointer to the QSPI instance.
 * @param pBuffer Pointer to the data buffer containing the bytes to be written.
 * @param Address The starting address in the flash memory where data will be written.
 * @param NumByteToWrite Number of bytes to write within the block.
 *
 * @return QSPI_SUCCESS on success, or an error code on failure.
 */
static int qspiWriteBlock(XQspiPs *QspiInstancePtr, uint8_t *pBuffer, uint32_t Address, uint32_t NumByteToWrite)
{
    uint32_t StartPage;
    int32_t BytesRemaining;
    int Status;

    if (QspiInstancePtr == NULL || pBuffer == NULL) {
        xil_printf("ERROR: Invalid parameters for qspiWriteBlock.\r\n");
        return QSPI_INVALID_PARAM;
    }

#ifdef QSPI_DEBUG
    xil_printf("qspiWriteBlock: StartAddr=0x%08X, TotalWriteBytes=%d\r\n", Address, NumByteToWrite);
#endif

    uint32_t CurrentAddress = Address;
    BytesRemaining = NumByteToWrite;

    while (BytesRemaining > 0) {
        uint32_t BlockBoundary = (CurrentAddress / QSPI_BLOCK_SIZE) * QSPI_BLOCK_SIZE;
        uint32_t BytesLeftInBlock = QSPI_BLOCK_SIZE - (CurrentAddress - BlockBoundary);
        uint32_t WriteBytesNow = (BytesRemaining < BytesLeftInBlock) ? BytesRemaining : BytesLeftInBlock;

#ifdef QSPI_DEBUG
        xil_printf("Writing BlockAddr=0x%08X, Bytes=%d\r\n", CurrentAddress, WriteBytesNow);
#endif

        // Start writing page-wise
        StartPage = CurrentAddress / QSPI_PAGE_SIZE;
        int32_t BytesInBlockRemaining = WriteBytesNow;
        uint8_t *LocalBufferPtr = pBuffer;

        do {
            uint32_t PageBoundary = (StartPage + 1) * QSPI_PAGE_SIZE;
            uint32_t BytesLeftInPage = PageBoundary - CurrentAddress;
            uint32_t BytesToWriteNow = (BytesInBlockRemaining < BytesLeftInPage) ? BytesInBlockRemaining : BytesLeftInPage;

            Status = qspiWritePage(QspiInstancePtr, LocalBufferPtr, CurrentAddress, BytesToWriteNow);
            if (Status != XST_SUCCESS) return Status;

            LocalBufferPtr += BytesToWriteNow;
            BytesInBlockRemaining -= BytesToWriteNow;
            CurrentAddress += BytesToWriteNow;
            StartPage = CurrentAddress / QSPI_PAGE_SIZE;
        } while (BytesInBlockRemaining > 0);

        // Move to next block
        BytesRemaining -= WriteBytesNow;
        pBuffer += WriteBytesNow;
        CurrentAddress += WriteBytesNow;
    }

#ifdef QSPI_DEBUG
    xil_printf("qspiWriteBlock: Block Write Complete.\r\n");
#endif

    return QSPI_SUCCESS;
}

/**
 * @brief Reads a page of data from the QSPI flash memory.
 *
 * This function reads data from a QSPI page while ensuring that
 * the read operation does not exceed the page boundary.
 *
 * @param QspiInstancePtr Pointer to the QSPI instance.
 * @param pBuffer Pointer to the buffer where the read data will be stored.
 * @param Address The starting address in the flash memory from where data will be read.
 * @param NumByteToRead Number of bytes to read within the page.
 *
 * @return QSPI_SUCCESS on success, or an error code on failure.
 */
static int qspiReadPage(XQspiPs *QspiInstancePtr, uint8_t *pBuffer, uint32_t Address, uint32_t NumByteToRead)
{
    if (QspiInstancePtr == NULL || pBuffer == NULL) {
        xil_printf("ERROR: Invalid parameters for qspiReadPage.\r\n");
        return QSPI_INVALID_PARAM;
    }

    // Ensure we do not exceed page boundary
    uint32_t PageBoundary = ((Address / QSPI_PAGE_SIZE) + 1) * QSPI_PAGE_SIZE;
    if ((Address + NumByteToRead) > PageBoundary) {
        NumByteToRead = PageBoundary - Address;
    }

#ifdef QSPI_DEBUG
    xil_printf("qspiReadPage: Address=0x%08X, ReadBytes=%d\r\n", Address, NumByteToRead);
#endif

    return qspiReadCore(QspiInstancePtr, Address, pBuffer, NumByteToRead);
}

/**
 * @brief Reads a sector of data from the QSPI flash memory.
 *
 * This function reads data from a QSPI sector in a page-wise manner, ensuring that
 * reads do not exceed sector boundaries.
 *
 * @param QspiInstancePtr Pointer to the QSPI instance.
 * @param pBuffer Pointer to the buffer where the read data will be stored.
 * @param Address The starting address in the flash memory from where data will be read.
 * @param NumByteToRead Number of bytes to read within the sector.
 *
 * @return QSPI_SUCCESS on success, or an error code on failure.
 */
static int qspiReadSector(XQspiPs *QspiInstancePtr, uint8_t *pBuffer, uint32_t Address, uint32_t NumByteToRead)
{
	uint32_t StartPage;
	int32_t BytesRemaining;
	int Status;

	if (QspiInstancePtr == NULL || pBuffer == NULL) {
		xil_printf("ERROR: Invalid parameters for qspiReadSectorWo.\r\n");
		return QSPI_INVALID_PARAM;
	}

#ifdef QSPI_DEBUG
	xil_printf("qspiReadSector: StartAddr=0x%08X, TotalReadBytes=%d\r\n", Address, NumByteToRead);
#endif

	uint32_t CurrentAddress = Address;
	BytesRemaining = NumByteToRead;

	while (BytesRemaining > 0) {
		uint32_t SectorBoundary = (CurrentAddress / QSPI_SECTOR_SIZE) * QSPI_SECTOR_SIZE;
		uint32_t BytesLeftInSector = QSPI_SECTOR_SIZE - (CurrentAddress - SectorBoundary);
		uint32_t ReadBytesNow = (BytesRemaining < BytesLeftInSector) ? BytesRemaining : BytesLeftInSector;

#ifdef QSPI_DEBUG
		xil_printf("Reading SectorAddr=0x%08X, Bytes=%d\r\n", CurrentAddress, ReadBytesNow);
#endif
		// Start writing page-wise
		StartPage = CurrentAddress / QSPI_PAGE_SIZE;
		int32_t BytesInSectorRemaining = ReadBytesNow;
		uint8_t *LocalBufferPtr = pBuffer;

		do {
			uint32_t PageBoundary = (StartPage + 1) * QSPI_PAGE_SIZE;
			uint32_t BytesLeftInPage = PageBoundary - CurrentAddress;
			uint32_t BytesToReadNow = (BytesInSectorRemaining < BytesLeftInPage) ? BytesInSectorRemaining : BytesLeftInPage;

			Status = qspiReadPage(QspiInstancePtr, LocalBufferPtr, CurrentAddress, BytesToReadNow);
			if (Status != XST_SUCCESS) return Status;

			LocalBufferPtr += BytesToReadNow;
			BytesInSectorRemaining -= BytesToReadNow;
			CurrentAddress += BytesToReadNow;
			StartPage = CurrentAddress / QSPI_PAGE_SIZE;
		} while (BytesInSectorRemaining > 0);

		// Move to next sector
		BytesRemaining -= ReadBytesNow;
		pBuffer += ReadBytesNow;
		CurrentAddress += ReadBytesNow;
	}

#ifdef QSPI_DEBUG
	xil_printf("qspiWriteSector: Sector Write Complete.\r\n");
#endif

    return QSPI_SUCCESS;
}

/**
 * @brief Reads a block of data from the QSPI flash memory.
 *
 * This function reads data from a QSPI block in a page-wise manner, ensuring that
 * reads do not exceed block boundaries.
 *
 * @param QspiInstancePtr Pointer to the QSPI instance.
 * @param pBuffer Pointer to the buffer where the read data will be stored.
 * @param Address The starting address in the flash memory from where data will be read.
 * @param NumByteToRead Number of bytes to read within the block.
 *
 * @return QSPI_SUCCESS on success, or an error code on failure.
 */
static int qspiReadBlock(XQspiPs *QspiInstancePtr, uint8_t *pBuffer, uint32_t Address, uint32_t NumByteToRead)
{
	uint32_t StartPage;
	int32_t BytesRemaining;
	int Status;

	if (QspiInstancePtr == NULL || pBuffer == NULL) {
		xil_printf("ERROR: Invalid parameters for qspiReadBlockWo.\r\n");
		return QSPI_INVALID_PARAM;
	}

#ifdef QSPI_DEBUG
	xil_printf("qspiReadBlock: StartAddr=0x%08X, TotalReadBytes=%d\r\n", Address, NumByteToRead);
#endif

	uint32_t CurrentAddress = Address;
	BytesRemaining = NumByteToRead;

	while (BytesRemaining > 0) {
		uint32_t BlockBoundary = (CurrentAddress / QSPI_BLOCK_SIZE) * QSPI_BLOCK_SIZE;
		uint32_t BytesLeftInBlock = QSPI_BLOCK_SIZE - (CurrentAddress - BlockBoundary);
		uint32_t ReadBytesNow = (BytesRemaining < BytesLeftInBlock) ? BytesRemaining : BytesLeftInBlock;

#ifdef QSPI_DEBUG
		xil_printf("Reading BlockAddr=0x%08X, Bytes=%d\r\n", CurrentAddress, ReadBytesNow);
#endif

		// Start writing page-wise
		StartPage = CurrentAddress / QSPI_PAGE_SIZE;
		int32_t BytesInBlockRemaining = ReadBytesNow;
		uint8_t *LocalBufferPtr = pBuffer;

		do {
			uint32_t PageBoundary = (StartPage + 1) * QSPI_PAGE_SIZE;
			uint32_t BytesLeftInPage = PageBoundary - CurrentAddress;
			uint32_t BytesToReadNow = (BytesInBlockRemaining < BytesLeftInPage) ? BytesInBlockRemaining : BytesLeftInPage;

			Status = qspiReadPage(QspiInstancePtr, LocalBufferPtr, CurrentAddress, BytesToReadNow);
			if (Status != XST_SUCCESS) return Status;

			LocalBufferPtr += BytesToReadNow;
			BytesInBlockRemaining -= BytesToReadNow;
			CurrentAddress += BytesToReadNow;
			StartPage = CurrentAddress / QSPI_PAGE_SIZE;
		} while (BytesInBlockRemaining > 0);

		// Move to next Block
		BytesRemaining -= ReadBytesNow;
		pBuffer += ReadBytesNow;
		CurrentAddress += ReadBytesNow;
	}

#ifdef QSPI_DEBUG
	xil_printf("qspiWriteBlock: Block Write Complete.\r\n");
#endif

	return QSPI_SUCCESS;
}

/*
 * =====================================================================================
 * Global Function Definitions
 * =====================================================================================
 */

/**
 * @brief Initializes the QSPI Flash controller.
 *
 * This function initializes the QSPI controller by looking up its configuration,
 * configuring options, setting the clock prescaler, and enabling Quad I/O mode.
 */
int qspiInit(XQspiPs *QspiInstancePtr, uint16_t QspiDeviceId)
{
    XQspiPs_Config *QspiConfig;
    int Status;

    if (QspiInstancePtr == NULL) {
        xil_printf("ERROR: QSPI instance pointer is NULL.\r\n");
        return QSPI_INVALID_PARAM;
    }

#ifdef QSPI_DEBUG
    xil_printf("INFO: Initializing QSPI Flash (Device ID: %d)...\r\n", QspiDeviceId);
#endif

    /* Look up the QSPI configuration */
    QspiConfig = XQspiPs_LookupConfig(QspiDeviceId);
    if (QspiConfig == NULL) {
        xil_printf("ERROR: QSPI LookupConfig failed for Device ID: %d\r\n", QspiDeviceId);
        return QSPI_FAILURE;
    }

    /* Initialize the QSPI controller */
    Status = XQspiPs_CfgInitialize(QspiInstancePtr, QspiConfig, QspiConfig->BaseAddress);
    if (Status != XST_SUCCESS) {
        xil_printf("ERROR: QSPI Initialization failed (Status: %d)\r\n", Status);
        return QSPI_FAILURE;
    }

    xil_printf("INFO: Setting QSPI options...\r\n");

    /* Configure QSPI options including Quad IO Mode */
    XQspiPs_SetOptions(QspiInstancePtr,
                        XQSPIPS_MANUAL_START_OPTION |
                        XQSPIPS_FORCE_SSELECT_OPTION |
                        XQSPIPS_HOLD_B_DRIVE_OPTION);

    XQspiPs_SetClkPrescaler(QspiInstancePtr, XQSPIPS_CLK_PRESCALE_8);
    XQspiPs_SetSlaveSelect(QspiInstancePtr);

    /* Confirm controller options */
    uint32_t opts = XQspiPs_GetOptions(QspiInstancePtr);
    if (opts)
        xil_printf("INFO: Controller Quad IO Mode ACTIVE.\r\n");
    else
        xil_printf("WARNING: Controller Quad IO Mode NOT ACTIVE!\r\n");

    /* Enable Quad Mode */
    Status = qspiQuadEnable(QspiInstancePtr);
    if (Status != QSPI_SUCCESS) {
        xil_printf("ERROR: Failed to Enable QuadMode\r\n");
        return XST_FAILURE;
    }

    xil_printf("INFO: QSPI Init Completed\r\n");

    return QSPI_SUCCESS;
}


/**
 * @brief Performs a self-test on the QSPI controller.
 *
 * This function runs the internal self-test provided by the Xilinx QSPI driver
 * to verify the integrity and proper functioning of the QSPI controller.
 */
int qspiSelfTest(XQspiPs *QspiInstancePtr)
{
    if (QspiInstancePtr == NULL) {
        xil_printf("ERROR: Invalid QSPI instance for self-test.\r\n");
        return QSPI_INVALID_PARAM;
    }

    int Status = XQspiPs_SelfTest(QspiInstancePtr);
    if (Status != XST_SUCCESS) {
        xil_printf("ERROR: Xilinx QSPI internal self-test failed.\r\n");
        return QSPI_FAILURE;
    }
    else {
    	xil_printf("INFO: QSPI Selftest Passed..\r\n");
    }

    return QSPI_SUCCESS;
}

/**
 * @brief Reads the JEDEC ID from the QSPI flash memory.
 *
 * This function sends the READ ID command to the QSPI flash and retrieves
 * the manufacturer ID, memory type, and memory capacity.
 */
int qspiReadID(XQspiPs *QspiInstancePtr, uint8_t *idBuffer)
{
    if (QspiInstancePtr == NULL || idBuffer == NULL) {
        xil_printf("ERROR: Invalid parameters for QSPI_ReadID.\r\n");
        return QSPI_INVALID_PARAM;
    }

    uint8_t CmdBuffer[4] = { READ_ID_CMD, 0x00, 0x00, 0x00 }; // 1 command + 3 dummy
    int Status;

    /* Transfer: Same buffer for TX & RX */
    Status = XQspiPs_PolledTransfer(QspiInstancePtr, CmdBuffer, CmdBuffer, sizeof(CmdBuffer));
    if (Status != XST_SUCCESS) {
        xil_printf("ERROR: Failed to read JEDEC ID.\r\n");
        return QSPI_FAILURE;
    }

    /* Copy JEDEC ID (starts after first byte) */
    idBuffer[0] = CmdBuffer[1];  // Manufacturer ID
    idBuffer[1] = CmdBuffer[2];  // Memory Type
    idBuffer[2] = CmdBuffer[3];  // Capacity

    xil_printf("[QSPI] JEDEC ID: 0x%02X 0x%02X 0x%02X\r\n", idBuffer[0], idBuffer[1], idBuffer[2]);

    return QSPI_SUCCESS;
}

/**
 * @brief Reads data from the QSPI flash memory.
 *
 * This function reads a specified number of bytes from the QSPI flash memory
 * starting at the given address. It determines the appropriate read method
 * (block, sector, or page) based on alignment and size constraints.
 */
int qspiRead(XQspiPs *QspiInstancePtr, uint8_t *pBuffer, uint32_t Address, uint32_t BytesToRead)
{
    if (QspiInstancePtr == NULL || pBuffer == NULL) {
        xil_printf("ERROR: Invalid parameters for qspiRead. QspiInstancePtr or pBuffer is NULL.\r\n");
        return QSPI_INVALID_PARAM;
    }

    if (Address >= QSPI_FLASH_SIZE || (Address + BytesToRead) > QSPI_FLASH_SIZE) {
        xil_printf("ERROR: Address (0x%08X) or BytesToRead (%u) exceeds flash size (0x%08X).\r\n", Address, BytesToRead, QSPI_FLASH_SIZE);
        return QSPI_INVALID_PARAM;
    }

    uint32_t CurrentAddress = Address;
    uint32_t BytesRemaining = BytesToRead;
    int Status;
    uint32_t BytesToReadNow;

    if (BytesRemaining >= QSPI_BLOCK_SIZE && (CurrentAddress % QSPI_BLOCK_SIZE) == 0) {
        // Read block-wise
        BytesToReadNow = QSPI_BLOCK_SIZE;
        Status = qspiReadBlock(QspiInstancePtr, pBuffer, CurrentAddress, BytesToReadNow);
    } else if (BytesRemaining >= QSPI_SECTOR_SIZE && (CurrentAddress % QSPI_SECTOR_SIZE) == 0) {
        // Read sector-wise
        BytesToReadNow = QSPI_SECTOR_SIZE;
        Status = qspiReadSector(QspiInstancePtr, pBuffer, CurrentAddress, BytesToReadNow);
    } else {
        // Read page-wise
        while (BytesRemaining > 0) {
        	uint32_t PageOffset = CurrentAddress % QSPI_PAGE_SIZE;
        	uint32_t PageBoundary = QSPI_PAGE_SIZE - PageOffset;
        	uint32_t BytesToReadNow = (BytesRemaining < PageBoundary) ? BytesRemaining : PageBoundary;

#ifdef QSPI_DEBUG
        	xil_printf("Reading %u bytes at address 0x%08X\r\n", BytesToReadNow, CurrentAddress);
#endif

        	Status = qspiReadCore(QspiInstancePtr, CurrentAddress, pBuffer, BytesToReadNow);
        	if (Status != XST_SUCCESS) {
        		xil_printf("ERROR: Read failed at address 0x%08X\r\n", CurrentAddress);
        		return Status;
        	}

        	// Update
        	pBuffer += BytesToReadNow;
        	BytesRemaining -= BytesToReadNow;
        	CurrentAddress += BytesToReadNow;
        }
    }

    if (Status != XST_SUCCESS) {
        xil_printf("ERROR: Read operation failed at address 0x%08X\r\n", CurrentAddress);
        return Status;
    }

#ifdef QSPI_DEBUG
    xil_printf("qspiRead: Read operation completed successfully.\r\n");
#endif

    return QSPI_SUCCESS;
}

/**
 * @brief Writes data to the QSPI flash memory.
 *
 * This function writes data to the QSPI flash memory at the specified address.
 * It first erases the required memory region before writing, ensuring data integrity.
 * It selects the appropriate write method based on the alignment and size of the data.
 */
int qspiWrite(XQspiPs *QspiInstancePtr, uint8_t *pBuffer, uint32_t Address, uint32_t BytesToWrite)
{
    if (QspiInstancePtr == NULL || pBuffer == NULL) {
        xil_printf("ERROR: Invalid parameters for qspiWrite. QspiInstancePtr or pBuffer is NULL.\r\n");
        return QSPI_INVALID_PARAM;
    }

    if (Address >= QSPI_FLASH_SIZE || (Address + BytesToWrite) > QSPI_FLASH_SIZE) {
        xil_printf("ERROR: Address (0x%08X) or BytesToWrite (%u) exceeds flash size (0x%08X).\r\n", Address, BytesToWrite, QSPI_FLASH_SIZE);
        return QSPI_INVALID_PARAM;
    }

    uint32_t CurrentAddress = Address;
    uint32_t BytesRemaining = BytesToWrite;
    int Status;

    if (!EraseInProgress) { // Avoid recursive erase calls
    	Status = qspiErase(QspiInstancePtr, Address, BytesToWrite);
    	if (Status != XST_SUCCESS) {
    		xil_printf("ERROR: Erase failed at address 0x%08X\r\n", Address);
    		return Status;
    	}
    }

    if (BytesRemaining >= QSPI_BLOCK_SIZE && (CurrentAddress % QSPI_BLOCK_SIZE) == 0) {
        Status = qspiWriteBlock(QspiInstancePtr, pBuffer, CurrentAddress, BytesToWrite);
    } else if (BytesRemaining >= QSPI_SECTOR_SIZE && (CurrentAddress % QSPI_SECTOR_SIZE) == 0) {
        Status = qspiWriteSector(QspiInstancePtr, pBuffer, CurrentAddress, BytesToWrite);
    } else {
        // Write page-wise
        while (BytesRemaining > 0) {
        	uint32_t PageOffset = CurrentAddress % QSPI_PAGE_SIZE;
        	uint32_t PageBoundary = QSPI_PAGE_SIZE - PageOffset;
        	uint32_t BytesToWriteNow = (BytesRemaining < PageBoundary) ? BytesRemaining : PageBoundary;

#ifdef QSPI_DEBUG
        	xil_printf("Writing %u bytes at address 0x%08X\r\n", BytesToWriteNow, CurrentAddress);
#endif

        	Status = qspiWriteCore(QspiInstancePtr, CurrentAddress, pBuffer, BytesToWriteNow);
        	if (Status != XST_SUCCESS) {
        		xil_printf("ERROR: Write failed at address 0x%08X\r\n", CurrentAddress);
        		return Status;
        	}

        	// Update
        	pBuffer += BytesToWriteNow;
        	BytesRemaining -= BytesToWriteNow;
        	CurrentAddress += BytesToWriteNow;
        }
    }

    if (Status != XST_SUCCESS) {
        xil_printf("ERROR: Write operation failed at address 0x%08X\r\n", CurrentAddress);
        return Status;
    }

#ifdef QSPI_DEBUG
    xil_printf("qspiWrite: Write operation completed successfully.\r\n");
#endif

    return QSPI_SUCCESS;
}

/**
 * @brief Erases the entire QSPI flash memory chip.
 *
 * This function performs a full-chip erase by issuing the chip erase command.
 * Since chip erase does not require an address, the address parameter is set to 0.
 */
int qspiEraseChip(XQspiPs *QspiInstancePtr)
{
    return qspiEraseCore(QspiInstancePtr, CHIP_ERASE_CMD, 0, 0); // No address
}

/**
 * @brief Erases a specified range of the QSPI flash memory.
 *
 * This function determines the optimal erase type (page, sector, or block) based on the
 * alignment and size of the requested erase operation. If a partial erase is required,
 * it performs a read-modify-write cycle to preserve unaffected data.
 */
int qspiErase(XQspiPs *QspiInstancePtr, uint32_t Address, uint32_t BytesToErase)
{
    if (QspiInstancePtr == NULL || BytesToErase == 0 ||
        Address >= QSPI_FLASH_SIZE || (Address + BytesToErase) > QSPI_FLASH_SIZE) {
        xil_printf("ERROR: Invalid parameters in qspiErase. Address=0x%08X, BytesToErase=%u\r\n", Address, BytesToErase);
        return QSPI_INVALID_PARAM;
    }

    uint32_t CurrentAddress = Address;
    uint32_t BytesRemaining = BytesToErase;
    int Status;

    EraseInProgress = 1;
    while (BytesRemaining > 0) {
        uint32_t SectorStartAddress = CurrentAddress - (CurrentAddress % QSPI_SECTOR_SIZE);
        uint32_t BlockStartAddress = CurrentAddress - (CurrentAddress % QSPI_BLOCK_SIZE);
        uint32_t PageStartAddress = CurrentAddress - (CurrentAddress % QSPI_PAGE_SIZE);
        uint32_t OffsetInSector = CurrentAddress % QSPI_SECTOR_SIZE;
        uint32_t OffsetInBlock = CurrentAddress % QSPI_BLOCK_SIZE;
        uint32_t OffsetInPage = CurrentAddress % QSPI_PAGE_SIZE;
        uint32_t BytesAvailableInSector = QSPI_SECTOR_SIZE - OffsetInSector;
        uint32_t BytesAvailableInBlock = QSPI_BLOCK_SIZE - OffsetInBlock;
        uint32_t BytesToEraseNow = BytesRemaining;

        if (BytesToEraseNow > BytesAvailableInBlock) {
        	BytesToEraseNow = BytesAvailableInBlock;
        }
        else if (BytesToEraseNow > BytesAvailableInSector) {
            BytesToEraseNow = BytesAvailableInSector;
        }

#ifdef QSPI_DEBUG
        xil_printf("Processing sector at 0x%08X, Offset=%u, BytesToErase=%u\r\n",
                   SectorStartAddress, OffsetInSector, BytesToEraseNow);
#endif

        if (OffsetInSector == 0 && BytesToEraseNow == QSPI_SECTOR_SIZE) {
            // Entire sector aligned, erase directly

#ifdef QSPI_DEBUG
            xil_printf("Erasing full sector at 0x%08X\r\n", SectorStartAddress);
#endif

            Status = qspiEraseCore(QspiInstancePtr, SECTOR_ERASE_CMD, SectorStartAddress, QSPI_SECTOR_SIZE);
            if (Status != XST_SUCCESS) {
                xil_printf("ERROR: Sector erase failed at 0x%08X\r\n", SectorStartAddress);
                return XST_FAILURE;
            }
        }
        else if (OffsetInBlock == 0 && BytesToEraseNow == QSPI_BLOCK_SIZE) {
        	// Entire Block aligned, erase directly
#ifdef QSPI_DEBUG
        	xil_printf("Erasing full block at 0x%08X\r\n", BlockStartAddress);
#endif
        	Status = qspiEraseCore(QspiInstancePtr, BLOCK_ERASE_CMD, BlockStartAddress, QSPI_BLOCK_SIZE);
        	if (Status != XST_SUCCESS) {
        		xil_printf("ERROR: Block erase failed at 0x%08X\r\n", BlockStartAddress);
        		return XST_FAILURE;
        	}
        }
        else if (OffsetInPage == 0 && BytesToEraseNow == QSPI_PAGE_SIZE) {
        	// Entire Page aligned, erase directly
#ifdef QSPI_DEBUG
        	xil_printf("Erasing full page at 0x%08X\r\n", PageStartAddress);
#endif

        	Status = qspiEraseCore(QspiInstancePtr, PAGE_ERASE_CMD, PageStartAddress, QSPI_PAGE_SIZE);
        	if (Status != XST_SUCCESS) {
        		xil_printf("ERROR: Page erase failed at 0x%08X\r\n", PageStartAddress);
        		return XST_FAILURE;
        	}
        }
        else {
#ifdef QSPI_DEBUG
        	xil_printf("Partial erase at 0x%08X\r\n", CurrentAddress);
#endif

            uint32_t EraseSize, EraseStartAddress, Offset, EraseCmd;
            static uint8_t Buffer[QSPI_BLOCK_SIZE];

        	// Handle Partial Erase (Sector, Block, Page)
        	if (BytesRemaining <= QSPI_SECTOR_SIZE && BytesRemaining >= QSPI_PAGE_SIZE) {
        		EraseSize = QSPI_SECTOR_SIZE;
        		EraseStartAddress = SectorStartAddress;
        		Offset = OffsetInSector;
        		EraseCmd = SECTOR_ERASE_CMD;
        	} else if (BytesRemaining <= QSPI_BLOCK_SIZE && BytesRemaining >= QSPI_SECTOR_SIZE) {
        		EraseSize = QSPI_BLOCK_SIZE;
        		EraseStartAddress = BlockStartAddress;
        		Offset = OffsetInBlock;
        		EraseCmd = BLOCK_ERASE_CMD;
        	} else {
        		EraseSize = QSPI_PAGE_SIZE;
        		EraseStartAddress = PageStartAddress;
        		Offset = OffsetInPage;
        		EraseCmd = PAGE_ERASE_CMD;
        	}

            Status = qspiRead(QspiInstancePtr, Buffer, EraseStartAddress, EraseSize);
            if (Status != XST_SUCCESS) {
                xil_printf("ERROR: Failed to read at 0x%08X\r\n", EraseStartAddress);
                return XST_FAILURE;
            }

            memset(Buffer + Offset, 0xFF, BytesToEraseNow);

            Status = qspiEraseCore(QspiInstancePtr, EraseCmd, EraseStartAddress, EraseSize);
            if (Status != XST_SUCCESS) {
                xil_printf("ERROR: erase failed at 0x%08X\r\n", EraseStartAddress);
                return XST_FAILURE;
            }

            Status = qspiWrite(QspiInstancePtr, Buffer, EraseStartAddress, EraseSize);
            if (Status != XST_SUCCESS) {
                xil_printf("ERROR: Failed to restore at 0x%08X\r\n", EraseStartAddress);
                return XST_FAILURE;
            }
        }

        CurrentAddress += BytesToEraseNow;
        BytesRemaining -= BytesToEraseNow;
    }

    EraseInProgress = 0;

#ifdef QSPI_DEBUG
    xil_printf("qspiErase: erase completed successfully.\r\n");
#endif

    return QSPI_SUCCESS;
}

/**
 * @brief Deinitializes the QSPI module.
 *
 * This function resets the QSPI instance and clears internal states, ensuring
 * the QSPI device is safely deinitialized before reuse.
 */
int qspiDeInit(XQspiPs *QspiInstancePtr) {
	if (QspiInstancePtr == NULL) {
		xil_printf("ERROR: QSPI instance pointer is NULL.\r\n");
		return QSPI_INVALID_PARAM;
	}

	xil_printf("INFO: Deinitializing QSPI...\r\n");

	/* Ensure the device is deselected */
	XQspiPs_SetSlaveSelect(QspiInstancePtr);

	/* Reset the QSPI controller */
	XQspiPs_Reset(QspiInstancePtr);

	/* Clear any configured options */
	XQspiPs_SetOptions(QspiInstancePtr, 0);

	/* Set the clock prescaler to a default safe value */
	XQspiPs_SetClkPrescaler(QspiInstancePtr, XQSPIPS_CLK_PRESCALE_8);

	xil_printf("INFO: QSPI Deinitialized successfully.\r\n");

	return QSPI_SUCCESS;
}
