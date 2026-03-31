#ifndef TETRIS_CLOCK16_H
#define TETRIS_CLOCK16_H

#include <Arduino.h>
#include <Adafruit_GFX.h>

namespace TetrisClock16 {

static const uint8_t CELL = 2;
static const uint8_t GLYPH_W = 6; // 5 pixels + 1 spacing column
static const uint8_t GLYPH_H = 7;

static const uint8_t DIGITS[10][GLYPH_H] = {
  {0b11111, 0b10001, 0b10011, 0b10101, 0b11001, 0b10001, 0b11111}, // 0
  {0b00100, 0b01100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110}, // 1
  {0b11111, 0b00001, 0b00001, 0b11111, 0b10000, 0b10000, 0b11111}, // 2
  {0b11111, 0b00001, 0b00001, 0b01111, 0b00001, 0b00001, 0b11111}, // 3
  {0b10001, 0b10001, 0b10001, 0b11111, 0b00001, 0b00001, 0b00001}, // 4
  {0b11111, 0b10000, 0b10000, 0b11111, 0b00001, 0b00001, 0b11111}, // 5
  {0b11111, 0b10000, 0b10000, 0b11111, 0b10001, 0b10001, 0b11111}, // 6
  {0b11111, 0b00001, 0b00001, 0b00010, 0b00100, 0b00100, 0b00100}, // 7
  {0b11111, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b11111}, // 8
  {0b11111, 0b10001, 0b10001, 0b11111, 0b00001, 0b00001, 0b11111}  // 9
};

inline bool getGlyph(char c, uint8_t rows[GLYPH_H]) {
  if (c >= '0' && c <= '9') {
    memcpy(rows, DIGITS[c - '0'], GLYPH_H);
    return true;
  }

  memset(rows, 0, GLYPH_H);
  return false;
}

inline uint8_t drawChar(Adafruit_GFX& gfx, int16_t x, int16_t y, char c, uint16_t color = SSD1306_WHITE) {
  uint8_t rows[GLYPH_H];
  getGlyph(c, rows);

  for (uint8_t row = 0; row < GLYPH_H; row++) {
    for (uint8_t col = 0; col < 5; col++) {
      if (rows[row] & (1 << (4 - col))) {
        gfx.fillRect(x + (col * CELL), y + (row * CELL), CELL, CELL, color);
      }
    }
  }

  return GLYPH_W * CELL;
}

inline uint16_t textWidth(const char* text) {
  if (!text) return 0;
  return strlen(text) * GLYPH_W * CELL;
}

inline void drawText(Adafruit_GFX& gfx, int16_t x, int16_t y, const char* text, uint16_t color = SSD1306_WHITE) {
  if (!text) return;
  while (*text) {
    x += drawChar(gfx, x, y, *text, color);
    text++;
  }
}

} // namespace TetrisClock16

#endif
