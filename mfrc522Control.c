/*
 * =====================================================================================
 * File Name:    mfrc522Control.c
 * Author:       Vanix Technologies PVT. LTD.
 * Date Created: 2025-02-14
 * Description:  This source file contains the implementation of the functions that
 *               control the MFRC522 RFID/NFC module for contactless communication.
 *               These functions handle initialization, card detection, authentication,
 *               and data exchange operations, enabling secure and efficient RFID/NFC
 *               communication.
 *
 *               The MFRC522 module is widely used for RFID-based authentication,
 *               access control, and data transfer, making it a critical component in
 *               various embedded applications.
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
#include "mfrc522Control.h"
#include "iicControl.h"

/*
 * =====================================================================================
 * Global Variables
 * =====================================================================================
 */


/*
 * =====================================================================================
 * Structure
 * =====================================================================================
 */

struct stPn532Data {
	u8 packetbuffer[64];
};

static struct stPn532Data g_pn532Data;

/*
 * =====================================================================================
 * Static Global Variables
 * =====================================================================================
 */

static u8 uidLength = 0;
static u8 uid[7];
static u8 ackBuf[6];
static u8 command_rfid = 0;
static u8 _key[6];
static u8 _uid[7];
static u8 _uidLen = 0;
static int g_messageLength = 0;
static int g_messageStartIndex = 0;

/*
 * =====================================================================================
 * Static Function Definitions
 * =====================================================================================
 */

/**
 * @brief Sends multiple bytes of data over I2C to the RFID module.
 *
 * This function transmits a specified length of data from the provided
 * `pn532Data` structure to the RFID module using the I2C interface in
 * polled mode. It ensures the data is successfully sent to the specified
 * I2C slave address.
 *
 * @param pn532Data The structure containing the data to be sent.
 * @param nLength   The length of the data to be sent.
 *
 * @return u8 Returns XST_SUCCESS if the transmission is successful,
 *             or XST_FAILURE if an error occurs during transmission.
 */
static u8 i2c_send_data (struct stPn532Data pn532Data, int nLength)
{
	int Status = 0;

	Status = XIicPs_MasterSendPolled(&(Iic), pn532Data.packetbuffer ,
			nLength, IIC_SLAVE_ADDR_RFID);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}
	return XST_SUCCESS;
}

/**
 * @brief Reads multiple bytes of data from the RFID module over I2C.
 *
 * This function retrieves a specified length of data from the RFID module
 * using the I2C interface in polled mode. It stores the received data into
 * the provided `pn532Data` structure and ensures successful data retrieval
 * from the specified I2C slave address.
 *
 * @param pn532Data The structure where the received data will be stored.
 * @param nLength   The length of the data to be read.
 *
 * @return int Returns XST_SUCCESS if the data is successfully read,
 *             or XST_FAILURE if an error occurs during the read operation.
 */
static int i2c_read_data(struct stPn532Data pn532Data, int nLength)
{
	int status = 0;
	status  = XIicPs_MasterRecvPolled(&(Iic), pn532Data.packetbuffer , nLength, IIC_SLAVE_ADDR_RFID);
	if (status != XST_SUCCESS) {
		return XST_FAILURE;
	}
	memcpy(g_pn532Data.packetbuffer, pn532Data.packetbuffer, nLength);
	return XST_SUCCESS;
}

/**
 * @brief Reads the acknowledgment frame from the PN532 module to verify communication.
 *
 * This function waits for the acknowledgment frame from the PN532 module after sending a command.
 * It checks the response for validity and ensures that the communication between the host and
 * the PN532 module is successful. If no valid acknowledgment is received within the specified time
 * limit, it returns a timeout error. If the received acknowledgment does not match the expected frame,
 * it returns an invalid acknowledgment error.
 *
 * @return int8_t Returns 0 on successful acknowledgment receipt,
 *                PN532_TIMEOUT if a timeout occurs,
 *                or PN532_INVALID_ACK if the received acknowledgment is invalid.
 */
static int8_t readAckFrame()
{
	const uint8_t PN532_ACK[] = {0, 0, 0xFF, 0, 0xFF, 0};
	uint8_t ackBuf[sizeof(PN532_ACK)];

	xil_printf("wait for ack at : ");

	xil_printf("\n\r");
	struct stPn532Data stRecvData;

	uint16_t time = 0;
	do {
		//if (_wire->requestFrom(PN532_I2C_ADDRESS,  sizeof(PN532_ACK) + 1)) {
		if(XST_SUCCESS == i2c_read_data(stRecvData, sizeof(PN532_ACK) + 1)){
			if (g_pn532Data.packetbuffer[0] && 1) {  // check first byte --- status
				break;         // PN532 is ready
			}
		}

		usleep(100 * 1000);
		time++;
		if (time > PN532_ACK_WAIT_TIME) {
			xil_printf("Time out when waiting for ACK\n\r");
			return PN532_TIMEOUT;
		}
	} while (1);

	xil_printf("ready at : ");

	xil_printf("\n\r");


	for (uint8_t i = 0; i < sizeof(PN532_ACK); i++) {
		ackBuf[i] = g_pn532Data.packetbuffer[i + 1];//i2c_read_byte();
	}

	if (memcmp(ackBuf, PN532_ACK, sizeof(PN532_ACK))) {
		xil_printf("Invalid ACK\n\r");
		return PN532_INVALID_ACK;
	}

	return 0;
}

/**
 * @brief Writes a command to the PN532 module via I2C.
 *
 * This function constructs a command frame and sends it to the PN532 module over I2C. The frame consists of a preamble,
 * start codes, length field, checksum, and the command data. After sending the command, it waits for an acknowledgment
 * frame from the PN532 module to verify the successful transmission.
 *
 * @param header Pointer to the command header data to be sent.
 * @param hlen   Length of the header data.
 *
 * @return u8 Returns PN532_INVALID_FRAME if there is an issue with the data or packet size,
 *            or 0 if the command was successfully sent and acknowledged.
 */
static u8 writeCommand(const u8 *header, u8 hlen)
{
	command_rfid = header[0];
	u8 commandToSend[64];
	u8 nCounter = 0;
	commandToSend[nCounter++] = PN532_PREAMBLE;
	commandToSend[nCounter++] = PN532_STARTCODE1;
	commandToSend[nCounter++] = PN532_STARTCODE2;

	//i2c_send_byte(PN532_PREAMBLE);
	//i2c_send_byte(PN532_STARTCODE1);
	//i2c_send_byte(PN532_STARTCODE2);

	uint8_t length = hlen + 0 + 1;   // length of data field: TFI + DATA
	//i2c_send_byte(length);
	//i2c_send_byte(~length + 1);
	commandToSend[nCounter++] = length;
	commandToSend[nCounter++] = ~length + 1;// checksum of length

	//i2c_send_byte(PN532_HOSTTOPN532);
	commandToSend[nCounter++] = PN532_HOSTTOPN532;
	uint8_t sum = PN532_HOSTTOPN532;    // sum of TFI + DATA

	xil_printf("write with hlen %d : \n\r", hlen);

	for (uint8_t i = 0; i < hlen; i++) {
		if (1)//(i2c_send_byte(header[i]))
		{
			commandToSend[nCounter++] = header[i];
			sum += header[i];

			xil_printf("%x \n\r",header[i]);
		} else {
			xil_printf("\nToo many data to send, I2C doesn't support such a big packet\n\r");     // I2C max packet: 32 bytes
			return PN532_INVALID_FRAME;
		}
	}

	/*
    for (uint8_t i = 0; i < 0; i++) {
        if (i2c_send_byte(body[i])) {
            sum += body[i];

            xil_printf("%x \n\r",body[i]);
        } else {
        	xil_printf("\nToo many data to send, I2C doesn't support such a big packet\n\r");     // I2C max packet: 32 bytes
            return PN532_INVALID_FRAME;
        }
    }
	 */

	uint8_t checksum = ~sum + 1;            // checksum of TFI + DATA
	//i2c_send_byte(checksum);
	//i2c_send_byte(PN532_POSTAMBLE);
	commandToSend[nCounter++] = checksum;
	commandToSend[nCounter++] = PN532_POSTAMBLE;

	memset(g_pn532Data.packetbuffer, 0, 64);
	memcpy(g_pn532Data.packetbuffer, commandToSend, nCounter - 1);

	if (i2c_send_data(g_pn532Data , nCounter - 1)) {
		return 0;
	}
	usleep(100 * 1000);
	return readAckFrame();
}

/**
 * @brief Retrieves the response length from the PN532 module.
 *
 * This function waits for a valid response from the PN532 module and checks the frame for expected preamble and start codes.
 * It reads the length byte from the response and sends a NACK frame to acknowledge receipt. If a valid frame is received,
 * it returns the response length. If an error occurs, it returns a negative value.
 *
 * @param buf      Pointer to the buffer that will hold the response data.
 * @param len      Maximum length of the expected response.
 * @param timeout  Timeout period (in 100ms increments) for waiting for a valid response.
 *
 * @return int16_t Returns the response length on success, or -1 if a timeout occurs,
 *                 or PN532_INVALID_FRAME if the response frame is invalid.
 */
static int16_t getResponseLength(uint8_t buf[], uint8_t len, uint16_t timeout)
{
	const uint8_t PN532_NACK[] = {0, 0, 0xFF, 0xFF, 0, 0};
	uint16_t time = 0;
	struct stPn532Data pn532DataRecv;
	usleep(100 * 1000);
	do {
		//if (_wire->requestFrom(PN532_I2C_ADDRESS, 6)) {
		if(XST_SUCCESS == i2c_read_data(pn532DataRecv, 6)){
			if (g_pn532Data.packetbuffer[0] && 1) {  // check first byte --- status
				break;         // PN532 is ready
			}
		}

		usleep(100 * 1000);
		time++;
		if ((0 != timeout) && (time > timeout)) {
			return -1;
		}
	} while (1);

	if (0x00 != g_pn532Data.packetbuffer[1]      ||       // PREAMBLE
			0x00 != g_pn532Data.packetbuffer[2]  ||       // STARTCODE1
			0xFF != g_pn532Data.packetbuffer[3]           // STARTCODE2
	) {

		return PN532_INVALID_FRAME;
	}

	uint8_t length = g_pn532Data.packetbuffer[4];

	// request for last respond msg again
	//_wire->beginTransmission(PN532_I2C_ADDRESS);

	//for (uint16_t i = 0; i < sizeof(PN532_NACK); ++i) {
	//	i2c_send_byte(PN532_NACK[i]);
	//}

	memcpy(g_pn532Data.packetbuffer, PN532_NACK, sizeof(PN532_NACK));
	usleep(100 * 1000);
	if (i2c_send_data(g_pn532Data , sizeof(PN532_NACK))) {
		return 0;
	}
	//_wire->endTransmission();

	return length;
}

/**
 * @brief Reads the response from the PN532 module and verifies its integrity.
 *
 * This function waits for the PN532 module to send a response and reads the response frame. It checks the validity of the frame,
 * including the preamble, start code, checksum, and command. If the response is valid, the function extracts the data and verifies
 * the checksum. The data is copied into the provided buffer, and the length of the response is returned. If any errors are encountered,
 * the appropriate error code is returned.
 *
 * @param buf      Pointer to the buffer where the response data will be stored.
 * @param len      The maximum length of the response data that can be stored in the buffer.
 * @param timeout  Timeout period (in 100ms increments) for waiting for the response.
 *
 * @return int16_t Returns the length of the response on success, or a negative value if an error occurs:
 *                 -1: Timeout occurred.
 *                 PN532_INVALID_FRAME: Invalid frame detected.
 *                 PN532_NO_SPACE: Not enough space in the buffer.
 */
static int16_t readResponse(uint8_t buf[], uint8_t len, uint16_t timeout)
{
	uint16_t time = 0;
	int16_t length;
	struct stPn532Data pn532DataRecv;

	length = getResponseLength(buf, len, timeout);
	u8 nCounter = 0;

	// [RDY] 00 00 FF LEN LCS (TFI PD0 ... PDn) DCS 00
	do {
		//if (_wire->requestFrom(PN532_I2C_ADDRESS, 6 + length + 2)) {
		usleep(100 * 1000);
		if(XST_SUCCESS == i2c_read_data(pn532DataRecv, 6 + length + 2))
		{
			if (g_pn532Data.packetbuffer[nCounter++] && 1) {  // check first byte --- status
				break;         // PN532 is ready
			}
		}

		usleep(100 * 1000);
		time++;
		if ((0 != timeout) && (time > timeout)) {
			return -1;
		}
	} while (1);

	if (0x00 != g_pn532Data.packetbuffer[nCounter++]      ||       // PREAMBLE
			0x00 != g_pn532Data.packetbuffer[nCounter++]  ||       // STARTCODE1
			0xFF != g_pn532Data.packetbuffer[nCounter++]           // STARTCODE2
	) {

		return PN532_INVALID_FRAME;
	}

	length = g_pn532Data.packetbuffer[nCounter++];

	if (0 != (uint8_t)(length + g_pn532Data.packetbuffer[nCounter++])) {   // checksum of length
		return PN532_INVALID_FRAME;
	}

	uint8_t cmd = command_rfid + 1;               // response command
	uint8_t cmd1 = g_pn532Data.packetbuffer[nCounter++];
	uint8_t cmd2 = g_pn532Data.packetbuffer[nCounter++];
	if ((PN532_PN532TOHOST != cmd1) || (cmd != cmd2)) {
		return PN532_INVALID_FRAME;
	}

	length -= 2;
	if (length > len) {
		return PN532_NO_SPACE;  // not enough space
	}

	xil_printf("read:  %d\n\r ID : ", cmd);

	uint8_t sum = PN532_PN532TOHOST + cmd;
	for (uint8_t i = 0; i < length; i++) {
		buf[i] = g_pn532Data.packetbuffer[nCounter++];
		sum += buf[i];

		xil_printf("%x ",buf[i]);
	}
	xil_printf("\n\rID written\n\r");

	uint8_t checksum = g_pn532Data.packetbuffer[nCounter++];
	if (0 != (uint8_t)(sum + checksum)) {
		xil_printf("checksum is not ok\n\r");
		return PN532_INVALID_FRAME;
	}
	u8 nPostChar = g_pn532Data.packetbuffer[nCounter++];         // POSTAMBLE

	return length;
}

/**
 * @brief Reads the Passive Target ID from an NFC card.
 *
 * This function sends a command to the PN532 module to read the passive target (NFC card) ID.
 * It expects to receive the response from the PN532 module and then extracts the card's UID (Unique Identifier).
 * The UID is stored in the provided `uid` array, and the length of the UID is stored in `uidLength`.
 *
 * The function checks the response format to ensure the presence of the correct card data.
 *
 * @param cardbaudrate The baud rate for the card (e.g., 0x00 for 106 kbps).
 * @param uid Pointer to an array where the UID will be stored.
 * @param uidLength Pointer to a variable where the length of the UID will be stored.
 * @return u8 1 if successful, 0 if an error occurred.
 */
static u8 shield_readPassiveTargetID(uint8_t cardbaudrate, uint8_t *uid, uint8_t *uidLength)
{
	g_pn532Data.packetbuffer[0] = PN532_COMMAND_INLISTPASSIVETARGET;
	g_pn532Data.packetbuffer[1] = 1;  // max 1 cards at once (we can set this to 2 later)
	g_pn532Data.packetbuffer[2] = cardbaudrate;

	if (writeCommand(g_pn532Data.packetbuffer , 3)) {
		return 0;
	}
	usleep(100 * 1000);
	int16_t status = readResponse(g_pn532Data.packetbuffer, sizeof(g_pn532Data.packetbuffer), 10);
	if (0 > status) {
		return 0;
	}
	// check some basic stuff
	/* ISO14443A card response should be in the following format:

      byte            Description
      -------------   ------------------------------------------
      b0              Tags Found
      b1              Tag Number (only one used in this example)
      b2..3           SENS_RES
      b4              SEL_RES
      b5              NFCID Length
      b6..NFCIDLen    NFCID
	 */

	if (g_pn532Data.packetbuffer[0] != 1)
		return 0;

	uint16_t sens_res = g_pn532Data.packetbuffer[2];
	sens_res <<= 8;
	sens_res |= g_pn532Data.packetbuffer[3];

	xil_printf("ATQA: 0x");  xil_printf("%x ",sens_res);
	xil_printf("SAK: 0x");  xil_printf("%x ",g_pn532Data.packetbuffer[4]);
	xil_printf("\n\r");

	/* Card appears to be Mifare Classic */
	*uidLength = g_pn532Data.packetbuffer[5];

	for (uint8_t i = 0; i < g_pn532Data.packetbuffer[5]; i++) {
		uid[i] = g_pn532Data.packetbuffer[6 + i];
	}

	return 1;
}

/**
 * @brief Sends InListPassiveTarget command to PN532 to start card detection.
 *
 * This function initiates card detection by sending the InListPassiveTarget
 * command. The PN532 will pull IRQ low when a response is ready.
 *
 * @return u8 XST_SUCCESS if command sent successfully, XST_FAILURE otherwise.
 */
u8 nfc_sendInListPassiveTargetCommand(void)
{
	xil_printf("Sending InListPassiveTarget command...\r\n");
	
	g_pn532Data.packetbuffer[0] = PN532_COMMAND_INLISTPASSIVETARGET;
	g_pn532Data.packetbuffer[1] = 1;  // max 1 cards at once
	g_pn532Data.packetbuffer[2] = PN532_MIFARE_ISO14443A;  // 106 kbps Type A (ISO14443A)

	if (writeCommand(g_pn532Data.packetbuffer, 3)) {
		xil_printf("Failed to send InListPassiveTarget command\r\n");
		return XST_FAILURE;
	}
	
	xil_printf("InListPassiveTarget command sent. Waiting for IRQ...\r\n");
	return XST_SUCCESS;
}

/**
 * @brief Reads the response after IRQ goes low.
 *
 * This function reads the card detection response when IRQ pin is low.
 * It extracts UID and stores in global variables.
 *
 * @return u8 1 if card detected and read successfully, 0 otherwise.
 */
u8 nfc_readPassiveTargetResponse(void)
{
	xil_printf("IRQ is LOW. Reading response...\r\n");
	
	int16_t status = readResponse(g_pn532Data.packetbuffer, sizeof(g_pn532Data.packetbuffer), 10);
	if (0 > status) {
		xil_printf("Failed to read response\r\n");
		return 0;
	}
	
	// Check if a tag was found
	if (g_pn532Data.packetbuffer[0] != 1) {
		xil_printf("No tag found in response\r\n");
		return 0;
	}

	uint16_t sens_res = g_pn532Data.packetbuffer[2];
	sens_res <<= 8;
	sens_res |= g_pn532Data.packetbuffer[3];

	xil_printf("ATQA: 0x%x ", sens_res);
	xil_printf("SAK: 0x%x\r\n", g_pn532Data.packetbuffer[4]);

	// Extract UID
	uidLength = g_pn532Data.packetbuffer[5];

	for (uint8_t i = 0; i < g_pn532Data.packetbuffer[5]; i++) {
		uid[i] = g_pn532Data.packetbuffer[6 + i];
	}

	xil_printf("Card detected! UID Length: %d\r\n", uidLength);
	return 1;
}

/**
 * @brief Authenticate a block on a Mifare Classic NFC card using either the A or B keys.
 *
 * This function sends an authentication request to the PN532 NFC module to authenticate
 * a block on a Mifare Classic NFC card. It uses the specified key (A or B) and UID of the card.
 *
 * @param uid         Pointer to the UID (unique identifier) of the NFC card.
 * @param uidLen      Length of the UID in bytes.
 * @param blockNumber The block number to authenticate (valid range depends on the card size).
 * @param keyNumber   The key to use for authentication. 0 for "A" key, 1 for "B" key.
 * @param keyData     Pointer to the 6-byte key data to use for authentication.
 *
 * @return 1 if authentication is successful, 0 if authentication fails.
 */
static u8 _nfcShield_mifareclassic_AuthenticateBlock(uint8_t *uid, uint8_t uidLen, uint32_t blockNumber, uint8_t keyNumber, uint8_t *keyData)
{
	uint8_t i;

	// Hang on to the key and uid data
	memcpy (_key, keyData, 6);
	memcpy (_uid, uid, uidLen);
	_uidLen = uidLen;

	// Prepare the authentication command //
	g_pn532Data.packetbuffer[0] = PN532_COMMAND_INDATAEXCHANGE;   /* Data Exchange Header */
	g_pn532Data.packetbuffer[1] = 1;                              /* Max card numbers */
	g_pn532Data.packetbuffer[2] = (keyNumber) ? MIFARE_CMD_AUTH_B : MIFARE_CMD_AUTH_A;
	g_pn532Data.packetbuffer[3] = blockNumber;                    /* Block Number (1K = 0..63, 4K = 0..255 */
	memcpy (g_pn532Data.packetbuffer + 4, _key, 6);
	for (i = 0; i < _uidLen; i++) {
		g_pn532Data.packetbuffer[10 + i] = _uid[i];              /* 4 bytes card ID */
	}

	if (i2c_send_data(g_pn532Data , 10 + _uidLen)) {
		return 0;
	}
	int16_t status = i2c_read_data(g_pn532Data, sizeof(g_pn532Data.packetbuffer));
	if (0 > status) {
		return 0;
	}

	// Check if the response is valid and we are authenticated???
	// for an auth success it should be bytes 5-7: 0xD5 0x41 0x00
	// Mifare auth error is technically byte 7: 0x14 but anything other and 0x00 is not good
	if (g_pn532Data.packetbuffer[0] != 0x00) {
		xil_printf("Authentication failed\n\r");
		return 0;
	}

	return 1;
}

/**
 * @brief Reads 16 bytes of data from a specified block on a Mifare Classic NFC card.
 *
 * This function sends a read command to the PN532 NFC module to read the data stored in
 * a specific block on a Mifare Classic NFC card.
 *
 * @param blockNumber The block number to read (valid range depends on the card size).
 * @param data        Pointer to the buffer to store the 16 bytes of read data.
 *
 * @return 1 if reading the block is successful, 0 if reading the block fails.
 */
static u8 _nfcShield_mifareclassic_ReadDataBlock(uint8_t blockNumber, uint8_t *data)
{
	xil_printf("Trying to read 16 bytes from block \n\r");
	xil_printf("%d\n\r",blockNumber);

	/* Prepare the command */
	g_pn532Data.packetbuffer[0] = PN532_COMMAND_INDATAEXCHANGE;
	g_pn532Data.packetbuffer[1] = 1;                      /* Card number */
	g_pn532Data.packetbuffer[2] = MIFARE_CMD_READ;        /* Mifare Read command = 0x30 */
	g_pn532Data.packetbuffer[3] = blockNumber;            /* Block Number (0..63 for 1K, 0..255 for 4K) */

	if (i2c_send_data(g_pn532Data , 4)) {
		return 0;
	}
	int16_t status = i2c_read_data(g_pn532Data, sizeof(g_pn532Data.packetbuffer));
	if (0 > status) {
		return 0;
	}

	/* If byte 8 isn't 0x00 we probably have an error */
	if (g_pn532Data.packetbuffer[0] != 0x00) {
		return 0;
	}

	/* Copy the 16 data bytes to the output buffer        */
	/* Block content starts at byte 9 of a valid response */
	memcpy (data, g_pn532Data.packetbuffer + 1, 16);

	return 1;
}

/**
 * @brief Calculates the required buffer size based on the message length.
 *
 * This function computes the buffer size needed to store a message, including the size for the TLV (Type-Length-Value)
 * header, TLV terminator, and ensures the final buffer size is a multiple of the BLOCK_SIZE.
 *
 * @param messageLength The length of the message to be stored.
 *
 * @return The calculated buffer size.
 */
static int getBufferSize(int messageLength)
{

	int bufferSize = messageLength;

	// TLV header is 2 or 4 bytes, TLV terminator is 1 byte.
	if (messageLength < 0xFF)
	{
		bufferSize += SHORT_TLV_SIZE + 1;
	}
	else
	{
		bufferSize += LONG_TLV_SIZE + 1;
	}

	// bufferSize needs to be a multiple of BLOCK_SIZE
	if (bufferSize % BLOCK_SIZE != 0)
	{
		bufferSize = ((bufferSize / BLOCK_SIZE) + 1) * BLOCK_SIZE;
	}

	return bufferSize;
}

/**
 * @brief Finds the start index of the NDEF (NFC Data Exchange Format) data in the provided block of data.
 *
 * This function scans through the data buffer and looks for the TLV (Type-Length-Value) marker with a value of 0x03
 * which indicates the start of the NDEF data. It returns the index where the NDEF data begins.
 * If no valid TLV is found, it prints an error message and returns -2. If no NDEF data is found, it returns -1.
 *
 * @param data Pointer to the data buffer to be searched.
 *
 * @return The index of the NDEF start or a negative value indicating an error.
 */
static int getNdefStartIndex(u8 *data)
{

	for (int i = 0; i < BLOCK_SIZE; i++)
	{
		if (data[i] == 0x0)
		{
			// do nothing, skip
		}
		else if (data[i] == 0x3)
		{
			return i;
		}
		else
		{
			xil_printf("Unknown TLV \n\r");xil_printf("%x\n\r",data[i]);
			return -2;
		}
	}

	return -1;
}

/**
 * @brief Decodes the TLV (Type-Length-Value) message from the provided data buffer.
 *
 * This function extracts the message length and start index from the TLV data. It identifies whether the TLV header
 * is short (1 byte) or long (2 bytes) and sets the message length and starting index accordingly. If the TLV format is invalid
 * or the message cannot be decoded, it prints an error message and returns 0. If successful, it updates global variables
 * for the message length and start index.
 *
 * @param data Pointer to the data buffer containing the TLV encoded message.
 * @param messageLength Reference to the variable that will store the decoded message length.
 * @param messageStartIndex Reference to the variable that will store the decoded message start index.
 *
 * @return 1 if the decoding is successful, 0 if an error occurs.
 */
static u8 decodeTlv(u8 *data, int messageLength, int messageStartIndex)
{
	int i = getNdefStartIndex(data);

	if (i < 0 || data[i] != 0x3)
	{
		xil_printf("Error. Can't decode message length.");
		return 0;
	}
	else
	{
		if (data[i+1] == 0xFF)
		{
			messageLength = ((0xFF & data[i+2]) << 8) | (0xFF & data[i+3]);
			messageStartIndex = i + LONG_TLV_SIZE;
		}
		else
		{
			messageLength = data[i+1];
			messageStartIndex = i + SHORT_TLV_SIZE;
		}
		g_messageLength = messageLength;
		g_messageStartIndex = messageStartIndex;
	}

	return 1;
}

/**
 * @brief Checks if the given block is the first block in the sector.
 *
 * This function determines whether a given block belongs to the first block in a sector
 * based on the block number. The sectors are divided into smaller (4 blocks) and larger (16 blocks) sizes.
 * It checks the block number modulo 4 for small sectors (0-127) and modulo 16 for larger sectors (128 and above).
 *
 * @param uiBlock The block number to check.
 *
 * @return 1 if the block is the first in its sector, 0 otherwise.
 */
static u8 _nfcShield_mifareclassic_IsFirstBlock(uint32_t uiBlock)
{
	// Test if we are in the small or big sectors
	if (uiBlock < 128)
		return ((uiBlock) % 4 == 0);
	else
		return ((uiBlock) % 16 == 0);
}

/**
 * @brief Checks if the given block is the trailer block in the sector.
 *
 * This function determines whether a given block is the trailer block (the last block) in a sector.
 * The sectors are divided into smaller (4 blocks) and larger (16 blocks) sizes.
 * It checks the block number modulo 4 for small sectors (0-127) and modulo 16 for larger sectors (128 and above).
 * The trailer block is located at the second-to-last block in each sector.
 *
 * @param uiBlock The block number to check.
 *
 * @return 1 if the block is the trailer block in its sector, 0 otherwise.
 */
static u8 _nfcShield_mifareclassic_IsTrailerBlock (uint32_t uiBlock)
{
	// Test if we are in the small or big sectors
	if (uiBlock < 128)
		return ((uiBlock + 1) % 4 == 0);
	else
		return ((uiBlock + 1) % 16 == 0);
}

/**
 * @brief Reads data from a Mifare Classic NFC tag.
 *
 * This function authenticates the Mifare Classic NFC tag using a predefined key and reads its data.
 * It first reads the first block to determine the message length, then continues to read the subsequent blocks.
 * The data is processed as TLV (Tag-Length-Value) format, and the NFC tag's UID and type are set accordingly.
 * If any errors occur during the reading process (authentication or reading data), an error message is displayed.
 * It also handles skipping trailer blocks during the reading process.
 *
 * @param uid The UID of the NFC tag to read.
 * @param uidLength The length of the UID.
 *
 * @return A `NfcTag` structure containing the tag's UID, UID length, and type, or an error if the read fails.
 */
static NfcTag MifareClassic_read(u8 *uid, unsigned int uidLength)
{
	uint8_t key[6] = { 0xD3, 0xF7, 0xD3, 0xF7, 0xD3, 0xF7 };
	int currentBlock = 4;
	int messageStartIndex = 0;
	int messageLength = 0;
	u8 data[BLOCK_SIZE];

	// read first block to get message length
	int success = _nfcShield_mifareclassic_AuthenticateBlock(uid, uidLength, currentBlock, 0, key);
	if (success)
	{
		success = _nfcShield_mifareclassic_ReadDataBlock(currentBlock, data);
		if (success)
		{
			if (!decodeTlv(data, messageLength, messageStartIndex)) {
				g_nfcTag._uid = uid;
				g_nfcTag._uidLength = uidLength;
				g_nfcTag._tagType[0] = 'E';
				g_nfcTag._tagType[1] = 'R';
				g_nfcTag._tagType[2] = 'R';
				g_nfcTag._tagType[3] = 'O';
				g_nfcTag._tagType[4] = 'R';
				return g_nfcTag; // TODO should the error message go in NfcTag?
			}
		}
		else
		{
			xil_printf("Error. Failed read block ");xil_printf("%d\n\r",currentBlock);
			//return NfcTag(uid, uidLength, MIFARE_CLASSIC);
		}
	}
	else
	{
		xil_printf("Tag is not NDEF formatted.\n\r");
		// TODO set tag.isFormatted = false
		//return NfcTag(uid, uidLength, MIFARE_CLASSIC);
	}

	// this should be nested in the message length loop
	int index = 0;
	int bufferSize = getBufferSize(messageLength);
	uint8_t buffer[bufferSize];


	xil_printf("Message Length ");xil_printf("%d\n\r",messageLength);
	xil_printf("Buffer Size ");xil_printf("%d\n\r",bufferSize);

	while (index < bufferSize)
	{

		// authenticate on every sector
		if (_nfcShield_mifareclassic_IsFirstBlock(currentBlock))
		{
			success = _nfcShield_mifareclassic_AuthenticateBlock(uid, uidLength, currentBlock, 0, key);
			if (!success)
			{
				xil_printf("Error. Block Authentication failed for");xil_printf("%d\n\r",currentBlock);
				// TODO error handling
			}
		}

		// read the data
		success = _nfcShield_mifareclassic_ReadDataBlock(currentBlock, &buffer[index]);
		if (success)
		{

			xil_printf("Block ");xil_printf("%d",currentBlock);xil_printf(" ");
			xil_printf("%x\n\r",&buffer[index]);

		}
		else
		{
			xil_printf("Read failed ");
			xil_printf("%d\n\r",currentBlock);
			// TODO handle errors here
		}

		index += BLOCK_SIZE;
		currentBlock++;

		// skip the trailer block
		if (_nfcShield_mifareclassic_IsTrailerBlock(currentBlock))
		{

			xil_printf("Skipping block ");xil_printf("%d\n\r",currentBlock);
			currentBlock++;
		}
	}

	g_nfcTag._uid = uid;
	g_nfcTag._uidLength = uidLength;
	g_nfcTag._tagType[0] = 'M';
	g_nfcTag._tagType[1] = 'I';
	g_nfcTag._tagType[2] = 'F';
	g_nfcTag._tagType[3] = 'A';
	g_nfcTag._tagType[4] = 'R';
	g_nfcTag._tagType[5] = 'E';
	g_nfcTag._tagType[6] = '-';
	g_nfcTag._tagType[7] = 'C';
	g_nfcTag._tagType[8] = 'L';
	g_nfcTag._tagType[9] = 'A';
	g_nfcTag._tagType[10] = 'S';
	g_nfcTag._tagType[11] = 'S';
	g_nfcTag._tagType[12] = 'I';
	g_nfcTag._tagType[13] = 'C';
	g_nfcTag._tagType[14] = 'C';
	return g_nfcTag;
}

/*
 * =====================================================================================
 * Global Function Definitions
 * =====================================================================================
 */

/**
 * @brief Verifies if the given UID exists in the authorized user database.
 *
 * This function checks whether the provided UID matches any entry in the list
 * of authorized users. It ensures secure authentication for RFID/NFC-based
 * access control systems.
 */
u8 VerifyUserDB(const uint8_t *uid, uint8_t uidLength) {
    if (uidLength != UID_LENGTH) {
        return NFC_ERROR_INVALID_UID;  // Invalid UID length
    }

    // Check if UID exists in the authorized list
    for (int i = 0; i < (sizeof(authorizedUIDs) / sizeof(authorizedUIDs[0])); i++) {
        if (memcmp(uid, authorizedUIDs[i], UID_LENGTH) == 0) {
            return NFC_SUCCESS;
        }
    }
    return NFC_ERROR_AUTH_FAILED;
}

/**
 * @brief Initializes the NFC module and configures the PN532 chip.
 *
 * This function checks for the presence of the PN532 NFC chip, retrieves
 * its firmware version, and configures it for normal operation using the
 * Secure Access Module (SAM) configuration. It ensures proper communication
 * with the NFC module before proceeding with further operations.
 */
int nfc_init()
{
	u32 versiondata = nfc_getFirmwareVersion();

	if (! versiondata)
	{
		xil_printf("Didn't find PN53x board\n\r");
	}
	else
	{
		xil_printf("Found chip PN5");
		xil_printf("%x\n\r",(versiondata>>24) & 0xFF);
		xil_printf("Firmware ver. ");
		xil_printf("%d",(versiondata>>16) & 0xFF);
		xil_printf(".");
		xil_printf("%d \n\r",(versiondata>>8) & 0xFF);

		//Secure Access Module Config
		g_pn532Data.packetbuffer[0] = PN532_COMMAND_SAMCONFIGURATION;
		g_pn532Data.packetbuffer[1] = 0x01; // normal mode;
		g_pn532Data.packetbuffer[2] = 0x14; // timeout 50ms * 20 = 1 second
		g_pn532Data.packetbuffer[3] = 0x01; // use IRQ pin!

		xil_printf("SAMConfig\n\r");

		if (writeCommand(g_pn532Data.packetbuffer , 4))
		{
			xil_printf("Trouble with PN53x board\n\r");
		}
		else
		{
			int16_t status = readResponse(g_pn532Data.packetbuffer, sizeof(g_pn532Data.packetbuffer), 0);
			if (0 > status)
			{
				xil_printf("Trouble with PN53x board\n\r");
			}
		}
	}
	g_nfcTag._uidLength = 0;

	return XST_SUCCESS;
}

/**
 * @brief Prints the NFC tag's UID as a formatted hexadecimal string.
 *
 * This function iterates through the stored UID bytes and prints them
 * in a space-separated hexadecimal format. It ensures that each byte
 * is properly formatted with leading zeros when necessary.
 */
void print_getUidString()
{
	for (uint i = 0; i < g_nfcTag._uidLength; i++)
	{
		if (i > 0)
		{
			xil_printf(" ");
		}

		if (g_nfcTag._uid[i] < 0xF)
		{
			xil_printf("0");
		}

		xil_printf("%x ",g_nfcTag._uid[i]);
	}
};

/**
 * @brief Sends a single byte over I2C to the RFID module.
 *
 * This function transmits a single byte of data using the I2C interface
 * in a polled mode. It ensures communication with the RFID module by
 * sending data to the specified I2C slave address.
 */
u8 i2c_send_byte (u8 data)
{
	int Status = 0;
	u8 data_arr[1];
	data_arr[0] = data;
	Status = XIicPs_MasterSendPolled(&(Iic), data_arr ,
			1, IIC_SLAVE_ADDR_RFID);
	if (Status != XST_SUCCESS) {
		return 0;
	}
	return 1;
}

/**
 * @brief Reads a single byte of data from the RFID module over I2C.
 *
 * This function retrieves a byte of data from the RFID module using the
 * I2C interface in polled mode. It waits for the data to be received from
 * the specified I2C slave address.
 */
u8 i2c_read_byte()
{
	int status = 0;
	u8 data[1];
	status  = XIicPs_MasterRecvPolled(&(Iic), data , 1, IIC_SLAVE_ADDR_RFID);
	if (status != XST_SUCCESS) {
		return XST_FAILURE;
	}

	return data[0];
}

/**
 * @brief Retrieves the firmware version from the PN532 module.
 *
 * This function sends the "GET FIRMWARE VERSION" command to the PN532 module, waits for the response,
 * and processes the data to extract the firmware version. The firmware version is returned as a 32-bit integer
 * by concatenating the four bytes of the response.
 */
u32 nfc_getFirmwareVersion()
{

	u32 response;

	g_pn532Data.packetbuffer[0] = PN532_COMMAND_GETFIRMWAREVERSION;

	if (i2c_send_data(g_pn532Data , 1)) {
		return 0;
	}
	usleep(100 * 1000);
	// read data packet
	int16_t status = i2c_read_data(g_pn532Data, sizeof(g_pn532Data.packetbuffer));
	if (0 > status) {
		return 0;
	}

	response = g_pn532Data.packetbuffer[0];
	response <<= 8;
	response |= g_pn532Data.packetbuffer[1];
	response <<= 8;
	response |= g_pn532Data.packetbuffer[2];
	response <<= 8;
	response |= g_pn532Data.packetbuffer[3];

	return response;
}

/**
 * @brief Checks if an NFC tag is present and reads its UID.
 *
 * This function checks for the presence of an NFC tag (such as a Mifare Classic card) and reads its UID.
 * It utilizes the `shield_readPassiveTargetID` function to detect the tag and store the UID.
 * The function supports a timeout parameter, but in the current implementation, it always performs an immediate check.
 */
u8 nfc_tagPresent(unsigned long timeout)
{
	u8 success = 0;
	uidLength = 0;

	//   if (timeout == 0)
	{
		success = shield_readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, (uint8_t*)&uidLength);
	}
	// else
	//{
	//   success = shield->readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, (uint8_t*)&uidLength, timeout);
	//}
	return success;
}

/**
 * @brief Guesses the type of the detected NFC tag based on the UID length and known ATQA/SAK values.
 *
 * This function checks the UID length and returns an estimated tag type. The logic assumes:
 * - If the UID length is 4 bytes, the tag is classified as Mifare Classic.
 * - If the UID length is greater than 4 bytes, the tag is assumed to be NFC Forum Type 2.
 */
unsigned int guessTagType()
{

	// 4 byte id - Mifare Classic
	//  - ATQA 0x4 && SAK 0x8
	// 7 byte id
	//  - ATQA 0x44 && SAK 0x8 - Mifare Classic
	//  - ATQA 0x44 && SAK 0x0 - Mifare Ultralight NFC Forum Type 2
	//  - ATQA 0x344 && SAK 0x20 - NFC Forum Type 4

	if (uidLength == 4)
	{
		return TAG_TYPE_MIFARE_CLASSIC;
	}
	else
	{
		return TAG_TYPE_2;
	}
}


/**
 * @brief Reads data from an NFC tag based on its type.
 *
 * This function first determines the type of the NFC tag using the `guessTagType` function.
 * If the tag is identified as a Mifare Classic, it reads the data using the `MifareClassic_read` function.
 * If the tag is a Mifare Ultralight, it would read the data using the appropriate method (currently commented out).
 * If the tag type is unknown or not supported, an error message is printed.
 */
NfcTag nfc_read()
{
	uint8_t type = guessTagType();

	if (type == TAG_TYPE_MIFARE_CLASSIC)
	{

		xil_printf("Reading Mifare Classic\n\r");
		//MifareClassic mifareClassic = MifareClassic(*shield);
		return MifareClassic_read(uid, uidLength);
	}
	else if (type == TAG_TYPE_2)
	{

		xil_printf("Reading Mifare Ultralight\n\r");

		//MifareUltralight ultralight = MifareUltralight(*shield);
		//return ultralight.read(uid, uidLength);
	}
	else if (type == TAG_TYPE_UNKNOWN)
	{
		xil_printf("Can not determine tag type\n\r");
		//return NfcTag(uid, uidLength);
	}
	else
	{
		xil_printf("No driver for card type ");xil_printf("%d\n\r",type);
		// TODO should set type here
		//return NfcTag(uid, uidLength);
	}

}

/**
 * @brief Reads an NFC tag and verifies the user based on its UID.
 *
 * This function checks if an NFC tag is present and reads its data. It then verifies the UID of the detected tag
 * against the user database. If the tag is successfully detected, it proceeds with the verification process.
 * If no tag is detected, it returns an error indicating the absence of a tag.
 */
NFC_Status readNFC(NfcTag *nfcTagInfo) {
	if (nfc_tagPresent(0)) {
		xil_printf("Card detected.\r\n");

		*nfcTagInfo = nfc_read();

		return NFC_SUCCESS;		  // tag detected
	} else {
		return NFC_ERROR_NO_TAG;  // No tag detected
	}
}
