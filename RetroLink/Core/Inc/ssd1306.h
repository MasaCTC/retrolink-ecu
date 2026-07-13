/*
 * ssd1306.h — Minimal SSD1306 OLED driver for STM32 HAL I2C
 *
 * This driver maintains a 1024-byte framebuffer in RAM.
 * You draw to the buffer, then call ssd1306_UpdateScreen()
 * to push it all to the display at once.
 *
 * The display is 128 pixels wide x 64 pixels tall.
 * Each byte in the buffer represents 8 vertical pixels (one "page").
 */

#ifndef SSD1306_H
#define SSD1306_H

#include "main.h"

/* I2C address — most SSD1306 modules use 0x3C.
 * If your display doesn't respond, try 0x3D. */
#define SSD1306_I2C_ADDR  0x3C

/* Display dimensions */
#define SSD1306_WIDTH   128
#define SSD1306_HEIGHT  64

/* Colors */
typedef enum {
    SSD1306_COLOR_BLACK = 0x00,  // Pixel off
    SSD1306_COLOR_WHITE = 0x01   // Pixel on
} SSD1306_COLOR;

/* Font structure — each character is a fixed-width bitmap.
 * Data is stored as uint16_t rows: one uint16_t per row,
 * MSB (bit 15) = leftmost pixel column. */
typedef struct {
    uint8_t width;           // Character width in pixels
    uint8_t height;          // Character height in pixels
    const uint16_t *data;    // Pointer to font bitmap data (uint16_t per row)
} SSD1306_Font;

/* Available fonts (defined in ssd1306_fonts.c) */
extern const SSD1306_Font Font_7x10;
extern const SSD1306_Font Font_11x18;

/* Initialize the display — call once at startup */
void ssd1306_Init(I2C_HandleTypeDef *hi2c);

/* Push the framebuffer to the display */
void ssd1306_UpdateScreen(void);

/* Clear the entire framebuffer (all pixels off) */
void ssd1306_Fill(SSD1306_COLOR color);

/* Set a single pixel */
void ssd1306_DrawPixel(uint8_t x, uint8_t y, SSD1306_COLOR color);

/* Set the cursor position for text writing */
void ssd1306_SetCursor(uint8_t x, uint8_t y);

/* Write a single character at the current cursor position */
char ssd1306_WriteChar(char ch, SSD1306_Font font, SSD1306_COLOR color);

/* Write a string starting at the current cursor position */
char ssd1306_WriteString(const char *str, SSD1306_Font font, SSD1306_COLOR color);

/* Draw a horizontal line */
void ssd1306_DrawHLine(uint8_t x, uint8_t y, uint8_t width, SSD1306_COLOR color);

#endif /* SSD1306_H */
