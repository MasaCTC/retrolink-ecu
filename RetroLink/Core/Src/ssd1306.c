/*
 * ssd1306.c — SSD1306 OLED driver implementation
 *
 * How the SSD1306 works:
 * The display has 128x64 pixels organized into 8 "pages" of 8 rows each.
 * Each byte you send controls 8 vertical pixels in one column.
 * Bit 0 = top pixel, Bit 7 = bottom pixel of that 8-pixel column.
 *
 * We keep a full copy of the screen in RAM (the framebuffer).
 * Drawing functions modify the buffer, then UpdateScreen() sends
 * the entire buffer to the display over I2C.
 */

#include "ssd1306.h"
#include <string.h>

/* Private variables */
static I2C_HandleTypeDef *ssd1306_i2c;  // Pointer to the I2C handle
static uint8_t ssd1306_buffer[SSD1306_WIDTH * SSD1306_HEIGHT / 8];  // 1024 bytes
static uint8_t ssd1306_cursorX = 0;
static uint8_t ssd1306_cursorY = 0;

/* Send a single command byte to the display */
static void ssd1306_WriteCommand(uint8_t command)
{
    /* I2C protocol for SSD1306:
     * First byte after address = control byte
     * 0x00 = next byte is a command
     * 0x40 = next bytes are display data */
    uint8_t buf[2] = {0x00, command};
    HAL_I2C_Master_Transmit(ssd1306_i2c, SSD1306_I2C_ADDR << 1, buf, 2, 100);
}

/* Initialize the SSD1306 display */
void ssd1306_Init(I2C_HandleTypeDef *hi2c)
{
    ssd1306_i2c = hi2c;

    /* Wait for the display to power up */
    HAL_Delay(100);

    /* Initialization sequence — these commands configure the display
     * hardware. The values come from the SSD1306 datasheet. */

    ssd1306_WriteCommand(0xAE);  // Display OFF during setup

    ssd1306_WriteCommand(0x20);  // Set memory addressing mode
    ssd1306_WriteCommand(0x00);  // Horizontal addressing (auto-wrap)

    ssd1306_WriteCommand(0xB0);  // Set page start address to 0

    ssd1306_WriteCommand(0xC8);  // COM output scan direction: remapped
    ssd1306_WriteCommand(0x00);  // Set low column address to 0
    ssd1306_WriteCommand(0x10);  // Set high column address to 0

    ssd1306_WriteCommand(0x40);  // Set display start line to 0

    ssd1306_WriteCommand(0x81);  // Set contrast
    ssd1306_WriteCommand(0xFF);  // Maximum brightness

    ssd1306_WriteCommand(0xA1);  // Segment remap: column 127 = SEG0

    ssd1306_WriteCommand(0xA6);  // Normal display (not inverted)

    ssd1306_WriteCommand(0xA8);  // Set multiplex ratio
    ssd1306_WriteCommand(0x3F);  // 64 rows (0x3F = 63)

    ssd1306_WriteCommand(0xA4);  // Display follows RAM content

    ssd1306_WriteCommand(0xD3);  // Set display offset
    ssd1306_WriteCommand(0x00);  // No offset

    ssd1306_WriteCommand(0xD5);  // Set display clock divide ratio
    ssd1306_WriteCommand(0xF0);  // Max frequency, no divide

    ssd1306_WriteCommand(0xD9);  // Set pre-charge period
    ssd1306_WriteCommand(0x22);  // Phase 1: 2 clocks, Phase 2: 2 clocks

    ssd1306_WriteCommand(0xDA);  // Set COM pins hardware config
    ssd1306_WriteCommand(0x12);  // Alternative COM pin config

    ssd1306_WriteCommand(0xDB);  // Set VCOMH deselect level
    ssd1306_WriteCommand(0x20);  // ~0.77 x VCC

    ssd1306_WriteCommand(0x8D);  // Enable charge pump
    ssd1306_WriteCommand(0x14);  // Charge pump ON

    ssd1306_WriteCommand(0xAF);  // Display ON

    /* Clear the framebuffer and push to display */
    ssd1306_Fill(SSD1306_COLOR_BLACK);
    ssd1306_UpdateScreen();
}

/* Push the entire framebuffer to the display over I2C */
void ssd1306_UpdateScreen(void)
{
    for (uint8_t page = 0; page < 8; page++)
    {
        ssd1306_WriteCommand(0xB0 + page);  // Set page address
        ssd1306_WriteCommand(0x00);          // Set low column to 0
        ssd1306_WriteCommand(0x10);          // Set high column to 0

        /* Send one page (128 bytes) of display data.
         * 0x40 prefix tells the SSD1306 "these are data bytes, not commands" */
        uint8_t buf[129];
        buf[0] = 0x40;  // Data mode
        memcpy(&buf[1], &ssd1306_buffer[page * SSD1306_WIDTH], SSD1306_WIDTH);
        HAL_I2C_Master_Transmit(ssd1306_i2c, SSD1306_I2C_ADDR << 1, buf, 129, 100);
    }
}

/* Fill the entire framebuffer with one color */
void ssd1306_Fill(SSD1306_COLOR color)
{
    memset(ssd1306_buffer, (color == SSD1306_COLOR_WHITE) ? 0xFF : 0x00,
           sizeof(ssd1306_buffer));
}

/* Set a single pixel in the framebuffer */
void ssd1306_DrawPixel(uint8_t x, uint8_t y, SSD1306_COLOR color)
{
    if (x >= SSD1306_WIDTH || y >= SSD1306_HEIGHT) return;

    /* Each byte covers 8 vertical pixels.
     * y / 8 = which page (row of bytes)
     * y % 8 = which bit within that byte */
    if (color == SSD1306_COLOR_WHITE)
        ssd1306_buffer[x + (y / 8) * SSD1306_WIDTH] |= (1 << (y % 8));
    else
        ssd1306_buffer[x + (y / 8) * SSD1306_WIDTH] &= ~(1 << (y % 8));
}

/* Set cursor position for text */
void ssd1306_SetCursor(uint8_t x, uint8_t y)
{
    ssd1306_cursorX = x;
    ssd1306_cursorY = y;
}

/* Write a single character using the specified font */
char ssd1306_WriteChar(char ch, SSD1306_Font font, SSD1306_COLOR color)
{
    /* Only printable ASCII (32-126) */
    if (ch < 32 || ch > 126) return 0;

    /* Check if character fits on screen */
    if (ssd1306_cursorX + font.width > SSD1306_WIDTH ||
        ssd1306_cursorY + font.height > SSD1306_HEIGHT)
        return 0;

    /* Font data layout: each character = font.height uint16_t values.
     * Each uint16_t is one row of the character.
     * Bit 15 = leftmost pixel, bit 14 = next, etc.
     * We check each bit and draw the corresponding pixel. */
    uint32_t charOffset = (ch - 32) * font.height;

    for (uint8_t row = 0; row < font.height; row++)
    {
        uint16_t rowData = font.data[charOffset + row];
        for (uint8_t col = 0; col < font.width; col++)
        {
            /* Test bit from MSB side: bit 15 = col 0, bit 14 = col 1, etc. */
            if (rowData & (0x8000 >> col))
                ssd1306_DrawPixel(ssd1306_cursorX + col, ssd1306_cursorY + row, color);
            else
                ssd1306_DrawPixel(ssd1306_cursorX + col, ssd1306_cursorY + row,
                                  (SSD1306_COLOR)!color);
        }
    }

    ssd1306_cursorX += font.width;
    return ch;
}

/* Write a string */
char ssd1306_WriteString(const char *str, SSD1306_Font font, SSD1306_COLOR color)
{
    while (*str)
    {
        if (ssd1306_WriteChar(*str, font, color) != *str)
            return *str;
        str++;
    }
    return *str;
}

/* Draw a horizontal line */
void ssd1306_DrawHLine(uint8_t x, uint8_t y, uint8_t width, SSD1306_COLOR color)
{
    for (uint8_t i = 0; i < width; i++)
        ssd1306_DrawPixel(x + i, y, color);
}
