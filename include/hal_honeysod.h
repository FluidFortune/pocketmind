// Pisces Moon OS — PocketMind Edition
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// fluidfortune.com

#ifndef HAL_HONEYSOD_H
#define HAL_HONEYSOD_H

/**
 * HAL — Waveshare ESP32-S3-Touch-LCD-2.8
 * (HoneySod PocketMind target)
 *
 * Display:  ST7789 via 4-wire SPI
 * Touch:    CST328 via I2C
 * Audio:    PCM5101 decoder + onboard MIC
 * IMU:      QMI8658
 * RTC:      PCF85063
 * Storage:  MicroSD via SPI
 * Battery:  MX1.25 charging
 *
 * Pin assignments from Waveshare ESP32-S3-Touch-LCD-2.8C schematic
 */

// ─────────────────────────────────────────────────────────────────
// DISPLAY — ST7789 via SPI
// ─────────────────────────────────────────────────────────────────
#define LCD_DC          4
#define LCD_CS          5
#define LCD_SCK         6
#define LCD_MOSI        7
#define LCD_MISO        -1    // display only, no readback needed
#define LCD_RST         8
#define LCD_BL          45    // BL_PWM — GPIO45

#define LCD_WIDTH       240
#define LCD_HEIGHT      320

// ─────────────────────────────────────────────────────────────────
// TOUCH — CST328 via I2C
// ─────────────────────────────────────────────────────────────────
#define TOUCH_SDA       10
#define TOUCH_SCL       11
#define TOUCH_INT       9
#define TOUCH_RST       -1    // software reset
#define TOUCH_ADDR      0x1A  // CST328 default

// ─────────────────────────────────────────────────────────────────
// SD CARD — SPI (shared bus with display)
// ─────────────────────────────────────────────────────────────────
#define SD_CS           46
#define SD_MOSI         LCD_MOSI
#define SD_MISO         42
#define SD_SCK          LCD_SCK

// ─────────────────────────────────────────────────────────────────
// AUDIO — PCM5101 I2S decoder + onboard MIC
// ─────────────────────────────────────────────────────────────────
#define I2S_BCLK        15
#define I2S_LRCK        16
#define I2S_DOUT        17    // DAC out to PCM5101
#define I2S_DIN         18    // MIC in
#define MIC_GPIO        18    // onboard microphone data pin

// ─────────────────────────────────────────────────────────────────
// IMU — QMI8658 via I2C
// ─────────────────────────────────────────────────────────────────
#define IMU_SDA         TOUCH_SDA
#define IMU_SCL         TOUCH_SCL
#define IMU_ADDR        0x6B

// ─────────────────────────────────────────────────────────────────
// RTC — PCF85063 via I2C
// ─────────────────────────────────────────────────────────────────
#define RTC_ADDR        0x51

// ─────────────────────────────────────────────────────────────────
// BATTERY ADC
// ─────────────────────────────────────────────────────────────────
#define BAT_ADC         1     // GPIO1 via voltage divider

// ─────────────────────────────────────────────────────────────────
// UART
// ─────────────────────────────────────────────────────────────────
#define UART_TX         43
#define UART_RX         44

// ─────────────────────────────────────────────────────────────────
// ARDUINO GFX INIT PATTERN
//
// Arduino_DataBus *bus = new Arduino_ESP32SPI(
//     LCD_DC, LCD_CS, LCD_SCK, LCD_MOSI, LCD_MISO);
//
// Arduino_GFX *gfx = new Arduino_ST7789(
//     bus, LCD_RST, 0, true, LCD_WIDTH, LCD_HEIGHT);
//
// pinMode(LCD_BL, OUTPUT);
// analogWrite(LCD_BL, 255);
// gfx->begin();
// ─────────────────────────────────────────────────────────────────

#endif // HAL_HONEYSOD_H
