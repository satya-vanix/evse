/*
 * =====================================================================================
 * File Name:    lcdControl.c
 * Author:       Vanix Technologies PVT. LTD.
 * Date Created: 2025-02-14
 * Description:  This source file contains the implementation of the functions that
 *               control the LCD (Liquid Crystal Display) module for the electric
 *               vehicle charging system. These functions handle the initialization,
 *               configuration, and updating of the LCD display based on the charging
 *               process and other system parameters.
 *
 *               The LCD module provides a user interface to display critical
 *               information such as charging status, voltage, current, and other
 *               real-time data during the electric vehicle charging operation.
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
#include "lcdControl.h"

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
static u8  _Addr = 0x27;						// LCD display IIC address
static u8  _cols = LCD_COL;						// LCD display number of columns
static u8  _rows = LCD_ROW;						// LCD display number of lines
static u8  _backlightval = LCD_NOBACKLIGHT;		// LCD display backlight value
static u8 _displayfunction = LCD_4BITMODE | LCD_1LINE | LCD_5x8DOTS;	// LCD display functionality configuration
static u8 _numlines;							// LCD display number of lines
static u8 _displaycontrol = 0;					// LCD display control
static u8 _displaymode = 0;						// LCD display mode
static u8 lines = 1;							// LCD display number of line default value

/*
 * =====================================================================================
 * Static Function Definitions
 * =====================================================================================
 */

/**
 * @brief  Sends data to the LCD through an I/O expander.
 *
 * This function writes the provided `_data` byte to the LCD via an I/O expander. It is used
 * when the LCD is interfaced through an external I/O expander, often in 4-bit mode to control
 * the data lines.
 *
 * @param  _data  The byte of data to be sent to the LCD through the expander.
 * @retval None
 */
static void lcd_expanderWrite(uint8_t _data)
{
	u8 data_t[1];

	data_t[0] = ((int)(_data) | _backlightval);

	u8 Status = XIicPs_MasterSendPolled(&(Iic), data_t, 1, IIC_SLAVE_ADDR_LCD);
	if (Status != XST_SUCCESS) {
		return;
	}
}

/**
 * @brief  Generates a pulse on the LCD's enable pin.
 *
 * This function triggers the enable pin of the LCD to latch data or commands. It is used
 * after sending data or command in either 4-bit or 8-bit mode to ensure the LCD correctly
 * processes the transmitted value.
 *
 * @param  _data  The data or command to be latched by the enable pulse.
 * @retval None
 */
static void lcd_pulseEnable(uint8_t _data)
{
	lcd_expanderWrite(_data | LCD_ENPIN);	// Enable high
//	usleep(10 * 1000);						// enable pulse must be >450ns
	usleep(3 * 1000);						// enable pulse must be >450ns

	lcd_expanderWrite(_data & ~LCD_ENPIN);	// Enable low
//	usleep(50 * 1000);						// commands need > 37us to settle
	usleep(500);							// commands need > 37us to settle
}

/**
 * @brief  Sends a 4-bit data or command to the LCD.
 *
 * This function sends the lower 4 bits of the provided `value` to the LCD. It is typically
 * used for communication in 4-bit mode, where only the high or low nibble (4 bits) is sent
 * at a time.
 *
 * @param  value  The byte whose lower 4 bits will be sent to the LCD.
 * @retval None
 */
static void lcd_write4bits(uint8_t value)
{
	lcd_expanderWrite(value);
	lcd_pulseEnable(value);
}

/**
 * @brief  Sends data or command to the LCD.
 *
 * This function sends either data or a command to the LCD, depending on the mode.
 * The `value` parameter holds the byte of data or command to be sent, and the `mode`
 * determines whether the operation is a data write or a command write.
 *
 * @param  value  The byte of data or command to be sent to the LCD.
 * @param  mode   The mode of the operation.
 *
 * @retval None
 */
static void lcd_send(uint8_t value, uint8_t mode)
{
	uint8_t highnib = value & 0xf0;
	uint8_t lownib = (value << 4) & 0xf0;
	lcd_write4bits((highnib) | mode);
	lcd_write4bits((lownib) | mode);
}

/**
 * @brief  Sends a command to the LCD.
 *
 * This function sends a command byte to the LCD to control its operation, such as
 * clearing the display, setting the cursor position, or changing the display mode.
 * The `value` parameter holds the command to be sent to the LCD.
 *
 * @param  value  The command byte to be sent to the LCD (e.g., clear, cursor move, etc.).
 * @retval None
 */
static void lcd_command(uint8_t value)
{
	lcd_send(value, 0);
}

/**
 * @brief  Sends a Display information command to the LCD.
 *
 * This function sends a display information command byte to the LCD to control its
 * operation, such as display on.
 *
 * @param  None
 * @retval None
 */
static void lcd_display(void)
{
	_displaycontrol |= LCD_DISPLAYON;
	lcd_command(LCD_DISPLAYCONTROL | _displaycontrol);
}

/**
 * @brief  Sends a 4-bit nibble to the LCD.
 *
 * This function writes the lower or upper nibble (4 bits) of the provided `Nibble` value
 * to the LCD. It is typically used for sending data or commands in 4-bit mode.
 *
 * @param  Nibble  The 4-bit data or command to be sent to the LCD.
 * @retval None
 */
static void lcd_Write_4Bit(unsigned char Nibble)
{
	// Get The RS Value To LSB OF Data
	u8 RS = 0;
	uint8_t cmd_t[2];

	Nibble |= RS;

	cmd_t[0] = Nibble | 0x04;
	cmd_t[1] = Nibble & 0xFB;

	u8 Status = XIicPs_MasterSendPolled(&(Iic), cmd_t, 2, IIC_SLAVE_ADDR_LCD);
	if (Status != XST_SUCCESS) {
		return ;
	}
}

/**
 * @brief  Sends data to the LCD in data mode.
 *
 * This function sends a single byte of data (usually a character) to the LCD. It is used
 * to send the data to be displayed on the screen when the LCD is in data mode.
 *
 * @param  data  		   The character or data byte to be sent to the LCD.
 * @retval XST_SUCCESS     If the data was successfully sent.
 * @retval XST_FAILURE     If there was an error while sending the data.
 */
static int lcd_send_data (char data)
{
	int Status = 0;
	uint16_t data_word = 0;
	uint8_t data_t[2];

	data_word = 0x08 | data;  // RS = 1

	data_t[0] = (data_word >> 8) & 0xFF;
	data_t[1] = (data_word) & 0xFF;

	Status = XIicPs_MasterSendPolled(&(Iic), data_t, 2, IIC_SLAVE_ADDR_LCD);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}

	return XST_SUCCESS;
}

/**
 * @brief  Sends a command to the LCD in command mode.
 *
 * This function sends a single byte command to the LCD. It is used to control the LCD’s
 * settings or functionality, such as clearing the screen or setting the cursor position.
 *
 * @param  cmd             The command byte to be sent to the LCD.
 * @retval XST_SUCCESS     If the data was successfully sent.
 * @retval XST_FAILURE     If there was an error while sending the data.
 */
static int lcd_send_cmd (char cmd)
{
	lcd_Write_4Bit(cmd & 0xF0);
	lcd_Write_4Bit((cmd << 4) & 0xF0);

	return XST_SUCCESS;
#if 0
	int Status = 0;
	uint16_t command_word = 0;
	uint8_t cmd_t[2];

	command_word = cmd;  // RS = 0

	cmd_t[0] = (command_word >> 8) & 0xFF;
	cmd_t[1] = (command_word) & 0xFF;

	Status = XIicPs_MasterSendPolled(&(Iic), cmd_t, 2, IIC_SLAVE_ADDR_LCD);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}

	return XST_SUCCESS;
#endif
}

/**
 * @brief  Sets the cursor position on the LCD.
 *
 * This function sets the cursor to the specified row and column on the LCD screen.
 * It is used to position the cursor before writing data or commands at the desired location.
 *
 * @param  row  The row number (0 or 1 for a 2-row LCD).
 * @param  col  The column number (0 to 15 for a 16-character LCD).
 * @retval None
 */
static void lcd_put_cur(int row, int col)
{
	switch (row)
	{
	case 0:
		col |= 0x80;
		break;
	case 1:
		col |= 0xC0;
		break;
	}

	lcd_send_cmd (col);
}

/**
 * @brief  Writes a byte of data or command to the LCD.
 *
 * This function sends a complete byte (8 bits) to the LCD, either as data or a command,
 * depending on the current mode. It is typically used for communication in 8-bit mode.
 *
 * @param  value  The byte of data or command to be written to the LCD.
 * @retval None
 */
static void lcd_write(uint8_t value)
{
	lcd_send(value, LCD_RSPIN);
}

/**
 * @brief  Initializes the LCD display with specified dimensions.
 *
 * This function initializes the LCD with the given number of columns and rows. It is
 * typically called at the beginning of the program to set up the LCD screen before
 * displaying any data.
 *
 * @param  cols   The number of columns (characters per row) of the LCD.
 * @param  lines  The number of lines (rows) of the LCD.
 * @retval None
 */
static void lcd_begin(uint8_t cols, uint8_t lines)
{
	if (lines > 1) {
		_displayfunction |= LCD_2LINE;
	}
	_numlines = lines;

	// SEE PAGE 45/46 FOR INITIALIZATION SPECIFICATION!
	// according to datasheet, we need at least 40ms after power rises above 2.7V
	// before sending commands. Arduino can turn on way befer 4.5V so we'll wait 50
	usleep(50 * 1000);

	// Now we pull both RS and R/W low to begin commands
	lcd_expanderWrite(_backlightval);	// reset expanderand turn backlight off (Bit 8 =1)
	usleep(10 * 1000);

	//put the LCD into 4 bit mode
	// this is according to the hitachi HD44780 datasheet
	// figure 24, pg 46

	// we start in 8bit mode, try to set 4 bit mode
	lcd_write4bits(0x03 << 4);
	usleep(5 * 1000); // wait min 4.1ms

	// second try
	lcd_write4bits(0x03 << 4);
	usleep(5 * 1000); // wait min 4.1ms

	// third go!
	lcd_write4bits(0x03 << 4);
	usleep(5 * 1000);

	// finally, set to 4-bit interface
	lcd_write4bits(0x02 << 4);

	// set # lines, font size, etc.
	lcd_command(LCD_FUNCTIONSET | _displayfunction);

	// turn the display on with no cursor or blinking default
	_displaycontrol = LCD_DISPLAYON | LCD_CURSOROFF | LCD_BLINKOFF;
	lcd_display();

	// clear it off
	lcd_clear();

	// Initialize to default text direction (for roman languages)
	_displaymode = LCD_ENTRYLEFT | LCD_ENTRYSHIFTDECREMENT;

	// set the entry mode
	lcd_command(LCD_ENTRYMODESET | _displaymode);

	lcd_home();
}

/*
 * =====================================================================================
 * Global Function Definitions
 * =====================================================================================
 */

/**
 * @brief  Initializes the LCD display.
 */
void lcd_init (void)
{
	lcd_begin(_cols, _rows);
	lcd_backlight();

	usleep(20 * 1000);
}

/**
  * @brief  Turns on the backlight of the LCD.
  */
void lcd_backlight(void)
{
	_backlightval = LCD_BACKLIGHT;
	lcd_expanderWrite(0);
}

/**
 * @brief  Moves the cursor to the home position.
 */
void lcd_home(void)
{
	lcd_command(LCD_RETURNHOME);  	// set cursor position to zero
	usleep(20 * 1000);  			// this command takes a long time!
}

/**
 * @brief  Clears the LCD display.
 */
void lcd_clear(void)
{
	lcd_command(LCD_CLEARDISPLAY);	// clear display, set cursor position to zero
	usleep(20 * 1000);  			// this command takes a long time!
}

/**
 * @brief  Displays a string on the LCD.
 */
void lcd_send_string(char *str)
{
	while (*str) lcd_write(*str++);
}

/**
 * @brief  Sets the cursor position on the LCD.
 */
void lcd_setCursor(uint8_t col, uint8_t row)
{
	int row_offsets[] = { 0x00, 0x40, 0x14, 0x54 };

	if ( row > _numlines ) {
		row = _numlines-1;    		// we count rows starting w/0
	}

	lcd_command(LCD_SETDDRAMADDR | (col + row_offsets[row]));
}

/**
 * @brief  Displays a string on the LCD with a left-scrolling effect.
 */
void lcd_send_string_with_left_scroll(char *str)
{
	int loopCnt;
    int str_len = strlen(str);

    if (str_len <= _cols) {
        // If the message fits on the screen, just display it normally
        lcd_send_string(str);
        return;
    }

    // Print only the first 16 characters initially
    for (loopCnt = 0; loopCnt < _cols; loopCnt++) {
        lcd_write(str[loopCnt]);
    }

    usleep(300);

    // Scroll the display to show the rest of the message
    for (loopCnt = 0; loopCnt < (str_len - _cols); loopCnt++) {
        usleep(300);
        lcd_command(LCD_CURSORSHIFT | LCD_DISPLAYMOVE | LCD_MOVELEFT);
        lcd_write(str[loopCnt + 16]);
    }

    usleep(300);
}

/**
 * @brief  Displays a string on the bottom line of the LCD with a left-scrolling effect.
 */
void lcd_send_string_with_left_scroll_bottom_line(char *str)
{
	int loopCnt;
    int str_len = strlen(str);

    if (str_len <= _cols) {
        // If the message fits on the screen, just display it normally
        lcd_put_cur(1, 0);
        lcd_send_string(str);
        return;
    }

    // Print only the first 16 characters initially
    lcd_put_cur(1, 0);
    for (loopCnt = 0; loopCnt < _cols; loopCnt++) {
        lcd_write(str[loopCnt]);
    }

    mssleep(1000);

    // Scroll the display to show the rest of the message
    for (loopCnt = 0; loopCnt <= (str_len - _cols); loopCnt++) {
        lcd_put_cur(1, 0);
        lcd_send_string(str + loopCnt);
    }
}

/**
 * @brief  Displays a string on the top line of the LCD with a left-scrolling effect.
 */
void lcd_send_string_with_left_scroll_top_line(char *str)
{
	int loopCnt;
    int str_len = strlen(str);

    if (str_len <= _cols) {
        // If the message fits on the screen, just display it normally
        lcd_put_cur(0, 0);
        lcd_send_string(str);
        return;
    }

    // Print only the first 16 characters initially
    lcd_put_cur(0, 0);
    for (loopCnt = 0; loopCnt < _cols; loopCnt++) {
        lcd_write(str[loopCnt]);
    }

    mssleep(1000);

    // Scroll the display to show the rest of the message
    for (loopCnt = 0; loopCnt <= (str_len - _cols); loopCnt++) {
        lcd_put_cur(0, 0);
        lcd_send_string(str + loopCnt);
    }
}
