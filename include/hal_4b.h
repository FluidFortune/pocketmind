// Pisces Moon OS — PocketMind Edition
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// fluidfortune.com

#ifndef HAL_4B_H
#define HAL_4B_H

/**
 * HARDWARE ABSTRACTION LAYER v1.2
 * Waveshare ESP32-S3-Touch-LCD-4B
 *
 * Display:  ST7701 480x480 RGB parallel (NOT QSPI)
 * Touch:    GT911 capacitive (I2C)
 * PMU:      AXP2101
 * RTC:      PCF85063A
 * IMU:      QMI8658 6-axis
 * Audio:    ES8311 codec (speaker) + ES7210 ADC (2x mic)
 * SD:       No onboard slot — expansion header only
 *
 * I2C BUSES (two separate buses):
 *   Wire  (GPIO47/48) → XCA9554 IO expander
 *   Wire1 (GPIO8/9)   → Touch, PMU, RTC, IMU
 *   Wire2 (GPIO19/20) → Audio codec (ES8311 + ES7210)
 *
 * AUDIO NOTES:
 *   ES8311 = speaker/headphone DAC + basic ADC
 *   ES7210 = dedicated 4-channel mic ADC (we use 2 mics)
 *   Both controlled via I2C on AUDIO_SDA/SCL
 *   Audio data flows via I2S bus
 */

// ─────────────────────────────────────────────────────────────────
// I2C — IO EXPANDER BUS
// Wire (GPIO47 SDA, GPIO48 SCL) → XCA9554 IO expander
// ─────────────────────────────────────────────────────────────────
#define EXPANDER_SDA    47
#define EXPANDER_SCL    48
#define EXPANDER_ADDR   0x20  // XCA9554SWSPI address

// IO expander pin assignments
#define EXPANDER_PIN_LCD_RST    5
#define EXPANDER_PIN_LCD_BL     6

// ─────────────────────────────────────────────────────────────────
// I2C — PERIPHERAL BUS
// Wire1 (GPIO8 SDA, GPIO9 SCL) → Touch, PMU, RTC, IMU
// ─────────────────────────────────────────────────────────────────
#define BOARD_SDA       8
#define BOARD_SCL       9

// ─────────────────────────────────────────────────────────────────
// I2C — AUDIO BUS
// Wire2 (GPIO19 SDA, GPIO20 SCL) → ES8311 + ES7210
// ─────────────────────────────────────────────────────────────────
#define AUDIO_SDA       19
#define AUDIO_SCL       20
#define ES8311_ADDR     0x18  // ES8311 I2C address
#define ES7210_ADDR     0x40  // ES7210 I2C address (A0=A1=0)
#define CODEC_CE        16    // ES8311 chip enable (active high)

// ─────────────────────────────────────────────────────────────────
// I2S — AUDIO DATA BUS
// Shared by ES8311 (speaker out) and ES7210 (mic in)
// From schematic signal names cross-referenced with ESP32-S3 pins
// ─────────────────────────────────────────────────────────────────
#define I2S_MCLK        6     // Master clock
#define I2S_SCLK        5     // Bit clock (BCLK)
#define I2S_LRCK        4     // Left/right clock (WS)
#define I2S_ASDOUT      7     // Data OUT from ES7210 → ESP32 (mic audio)
#define I2S_DSDIN       15    // Data IN to ES8311 → speaker

#define I2S_SAMPLE_RATE     16000   // 16kHz for voice
#define I2S_BITS            16      // 16-bit samples
#define I2S_CHANNELS        1       // Mono mic input

// ─────────────────────────────────────────────────────────────────
// DISPLAY — ST7701 via RGB Parallel Bus
// Arduino_ESP32RGBPanel + Arduino_RGB_Display
// Confirmed from official Waveshare 4B demo
// ─────────────────────────────────────────────────────────────────
#define LCD_DE          17
#define LCD_VSYNC       3
#define LCD_HSYNC       46
#define LCD_PCLK        9

// Blue channel (5 bits)
#define LCD_B0          10
#define LCD_B1          11
#define LCD_B2          12
#define LCD_B3          13
#define LCD_B4          14

// Green channel (6 bits)
#define LCD_G0          21
#define LCD_G1          8
#define LCD_G2          18
#define LCD_G3          45
#define LCD_G4          38
#define LCD_G5          39

// Red channel (5 bits)
#define LCD_R0          40
#define LCD_R1          41
#define LCD_R2          42
#define LCD_R3          2
#define LCD_R4          1

#define LCD_WIDTH       480
#define LCD_HEIGHT      480

// ─────────────────────────────────────────────────────────────────
// TOUCH — GT911 (via peripheral I2C bus Wire1)
// ─────────────────────────────────────────────────────────────────
#define TOUCH_SDA       BOARD_SDA
#define TOUCH_SCL       BOARD_SCL
#define TOUCH_INT       -1    // Not broken out
#define TOUCH_RST       -1    // Controlled via IO expander
#define TOUCH_ADDR      0x5D  // GT911 default

// ─────────────────────────────────────────────────────────────────
// SD CARD
// No onboard slot on 4B
// Expansion header (H3, 2.0mm pitch) has limited free GPIOs
// SD not currently supported on this target
// ─────────────────────────────────────────────────────────────────
#define SD_CS           -1
#define SD_MOSI         -1
#define SD_MISO         -1
#define SD_SCK          -1

// ─────────────────────────────────────────────────────────────────
// PMU — AXP2101 (via peripheral I2C bus)
// ─────────────────────────────────────────────────────────────────
#define PMU_ADDR        0x34

// ─────────────────────────────────────────────────────────────────
// RTC — PCF85063A (via peripheral I2C bus)
// ─────────────────────────────────────────────────────────────────
#define RTC_ADDR        0x51

// ─────────────────────────────────────────────────────────────────
// IMU — QMI8658 6-axis (via peripheral I2C bus)
// ─────────────────────────────────────────────────────────────────
#define IMU_ADDR        0x6B

// ─────────────────────────────────────────────────────────────────
// UART
// ─────────────────────────────────────────────────────────────────
#define UART_TX         43
#define UART_RX         44

// ─────────────────────────────────────────────────────────────────
// ARDUINO GFX INIT — confirmed from official Waveshare demo
//
// Wire.begin(EXPANDER_SDA, EXPANDER_SCL);
//
// Arduino_XCA9554SWSPI *expander = new Arduino_XCA9554SWSPI(
//     7, 0, 2, 1, &Wire, EXPANDER_ADDR);
//
// Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
//     LCD_DE, LCD_VSYNC, LCD_HSYNC, LCD_PCLK,
//     LCD_B0, LCD_B1, LCD_B2, LCD_B3, LCD_B4,
//     LCD_G0, LCD_G1, LCD_G2, LCD_G3, LCD_G4, LCD_G5,
//     LCD_R0, LCD_R1, LCD_R2, LCD_R3, LCD_R4,
//     1, 10, 8, 50,
//     1, 10, 8, 20);
//
// Arduino_GFX *gfx = new Arduino_RGB_Display(
//     LCD_WIDTH, LCD_HEIGHT, rgbpanel, 0, true,
//     expander, GFX_NOT_DEFINED,
//     st7701_type1_init_operations,
//     sizeof(st7701_type1_init_operations));
//
// expander->pinMode(EXPANDER_PIN_LCD_BL, OUTPUT);
// expander->pinMode(EXPANDER_PIN_LCD_RST, OUTPUT);
// expander->digitalWrite(EXPANDER_PIN_LCD_BL, LOW);
// expander->digitalWrite(EXPANDER_PIN_LCD_RST, LOW);
// delay(200);
// expander->digitalWrite(EXPANDER_PIN_LCD_RST, HIGH);
// delay(200);
// gfx->begin();
// ─────────────────────────────────────────────────────────────────

// ─────────────────────────────────────────────────────────────────
// AUDIO INIT NOTES
//
// Wire2 (audio bus) init:
// Wire1.begin(AUDIO_SDA, AUDIO_SCL);
//
// ES8311 init (speaker + codec):
// es8311_handle_t handle = es8311_create(Wire1, ES8311_ADDR);
// es8311_clock_config_t clk = {
//     .mclk_inverted = false,
//     .sclk_inverted = false,
//     .mclk_from_mclk_pin = true,
//     .sample_frequency = I2S_SAMPLE_RATE
// };
// es8311_init(handle, &clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16);
// es8311_microphone_enable(handle, false); // use ES7210 for mic
//
// ES7210 init (mic ADC):
// Controlled via I2C at ES7210_ADDR
// Outputs audio on I2S_ASDOUT
//
// I2S init (Arduino framework):
// i2s.setPins(I2S_SCLK, I2S_LRCK, I2S_DSDIN, I2S_ASDOUT, I2S_MCLK);
// i2s.begin(I2S_MODE_STD, I2S_SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT,
//           I2S_SLOT_MODE_MONO);
// ─────────────────────────────────────────────────────────────────

#endif
