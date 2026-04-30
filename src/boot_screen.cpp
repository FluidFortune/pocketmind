// Pisces Moon OS — PocketMind Edition
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// fluidfortune.com

/**
 * POCKETMIND BOOT SCREEN v1.2
 * Hardware: Waveshare ESP32-S3-Touch-LCD-4B
 * Resolution: 480x480 RGB parallel display
 *
 * v1.2 changes:
 *   - Rainbow cycling letters on PISCES MOON OS title
 *   - Faster boot delays
 *   - SD shows N/A in amber
 *   - WiFi connect moved to after boot in main_4b.cpp
 */

#include "boot_screen.h"
#include <Arduino.h>
#include <Arduino_GFX_Library.h>

extern Arduino_GFX* gfx;

#define BOOT_BG         0x0000
#define BOOT_HEADER_BG  0x0121
#define BOOT_DIVIDER    0x0240
#define BOOT_SECTION    0x0180
#define BOOT_LABEL      0x03E0
#define BOOT_ADDR       0x0280
#define BOOT_TS         0x0160
#define BOOT_OK         0x07E0
#define BOOT_OK_BG      0x00C0
#define BOOT_ACTIVE     0x07FF
#define BOOT_ACTIVE_BG  0x0011
#define BOOT_WARN       0xFFE0
#define BOOT_WARN_BG    0x0840
#define BOOT_FAIL       0xF800
#define BOOT_FAIL_BG    0x4000
#define BOOT_PROGRESS   0x03A0
#define BOOT_AMBER      0xFD20

#define SCR_W  480
#define SCR_H  480

static int bootY = 0;

// ─────────────────────────────────────────────────────────────────
// RAINBOW COLOR HELPER
// Returns a cycling RGB565 color based on position and offset
// Same technique as Pisces Moon OS T-Deck title
// ─────────────────────────────────────────────────────────────────
static uint16_t rainbowColor(int index, int offset) {
    int hue = (index * 30 + offset) % 360;
    float h = hue / 60.0f;
    float s = 1.0f;
    float v = 1.0f;
    int i = (int)h;
    float f = h - i;
    float p = v * (1 - s);
    float q = v * (1 - s * f);
    float t = v * (1 - s * (1 - f));
    float r, g, b;
    switch (i % 6) {
        case 0: r = v; g = t; b = p; break;
        case 1: r = q; g = v; b = p; break;
        case 2: r = p; g = v; b = t; break;
        case 3: r = p; g = q; b = v; break;
        case 4: r = t; g = p; b = v; break;
        default: r = v; g = p; b = q; break;
    }
    uint8_t ri = (uint8_t)(r * 31);
    uint8_t gi = (uint8_t)(g * 63);
    uint8_t bi = (uint8_t)(b * 31);
    return (ri << 11) | (gi << 5) | bi;
}

static void drawBootHeader() {
    gfx->fillRect(0, 0, SCR_W, 16, BOOT_HEADER_BG);
    gfx->drawFastHLine(0, 16, SCR_W, BOOT_DIVIDER);
    gfx->setTextSize(1);
    gfx->setTextColor(BOOT_OK);
    gfx->setCursor(6, 5);
    gfx->print("PISCES MOON OS  //  PocketMind Edition");
    gfx->setTextColor(BOOT_SECTION);
    gfx->setCursor(340, 5);
    gfx->print("BIOS v1.2.0 / ESP32-S3");
    bootY = 20;
}

static void drawBootSection(const char* name) {
    gfx->drawFastHLine(0, bootY, SCR_W, BOOT_DIVIDER);
    bootY += 2;
    gfx->setTextColor(BOOT_SECTION);
    gfx->setTextSize(1);
    gfx->setCursor(4, bootY);
    gfx->print("// ");
    gfx->print(name);
    bootY += 9;
    gfx->drawFastHLine(0, bootY, SCR_W, BOOT_DIVIDER);
    bootY += 2;
}

static void drawBootLine(const char* timestamp, const char* label,
                         const char* detail, int tagType, const char* tagText) {
    gfx->setTextSize(1);
    gfx->setTextColor(BOOT_TS);
    gfx->setCursor(4, bootY);
    gfx->print(timestamp);
    gfx->setTextColor(BOOT_LABEL);
    gfx->setCursor(40, bootY);
    gfx->print(label);
    if (detail != nullptr) {
        gfx->setTextColor(BOOT_ADDR);
        gfx->setCursor(260, bootY);
        gfx->print(detail);
    }
    uint16_t tagFg, tagBg;
    switch (tagType) {
        case 1:  tagFg = BOOT_ACTIVE; tagBg = BOOT_ACTIVE_BG; break;
        case 2:  tagFg = BOOT_WARN;   tagBg = BOOT_WARN_BG;   break;
        case 3:  tagFg = BOOT_FAIL;   tagBg = BOOT_FAIL_BG;   break;
        default: tagFg = BOOT_OK;     tagBg = BOOT_OK_BG;     break;
    }
    int tagW = max(28, (int)(strlen(tagText) * 6) + 8);
    int tagX = SCR_W - tagW - 4;
    gfx->fillRect(tagX, bootY - 1, tagW, 9, tagBg);
    gfx->drawRect(tagX, bootY - 1, tagW, 9, tagFg);
    gfx->setTextColor(tagFg);
    gfx->setCursor(tagX + 4, bootY);
    gfx->print(tagText);
    bootY += 11;
}

static void drawBootProgress(int percent) {
    int barX = 4;
    int barY = bootY + 3;
    int barW = SCR_W - 8;
    int barH = 6;
    gfx->drawRect(barX, barY, barW, barH, BOOT_DIVIDER);
    int fillW = (barW - 2) * percent / 100;
    gfx->fillRect(barX + 1, barY + 1, fillW, barH - 2, BOOT_PROGRESS);
    gfx->setTextColor(BOOT_SECTION);
    gfx->setTextSize(1);
    gfx->setCursor(4, barY + 8);
    gfx->print("LOADING PISCES SHELL");
    char pctStr[8];
    snprintf(pctStr, sizeof(pctStr), "%d%%", percent);
    gfx->setCursor(SCR_W - 30, barY + 8);
    gfx->print(pctStr);
    bootY = barY + 20;
}

static void drawBootFooter() {
    gfx->fillRect(0, SCR_H - 14, SCR_W, 14, BOOT_HEADER_BG);
    gfx->drawFastHLine(0, SCR_H - 14, SCR_W, BOOT_DIVIDER);
    gfx->setTextSize(1);
    gfx->setTextColor(BOOT_SECTION);
    gfx->setCursor(4, SCR_H - 10);
    gfx->print("CORE1 READY / CORE0 ACTIVE");
    gfx->setTextColor(BOOT_OK);
    gfx->setCursor(SCR_W - 76, SCR_H - 10);
    gfx->print("[ BOOT OK ]");
}

static void drawCircuitBackground() {
    gfx->fillScreen(0x0000);
    uint16_t gridColor  = 0x0120;
    uint16_t traceColor = 0x0300;
    uint16_t padColor   = 0x0460;
    for (int x = 0; x < SCR_W; x += 24) gfx->drawFastVLine(x, 0, SCR_H, gridColor);
    for (int y = 0; y < SCR_H; y += 24) gfx->drawFastHLine(0, y, SCR_W, gridColor);
    gfx->drawFastHLine(0,   12, 100, traceColor);
    gfx->drawFastVLine(100, 12, 24,  traceColor);
    gfx->drawFastHLine(100, 36, 80,  traceColor);
    gfx->drawFastHLine(260, 12, 140, traceColor);
    gfx->drawFastVLine(260, 12, 18,  traceColor);
    gfx->drawFastHLine(280, 30, 80,  traceColor);
    gfx->drawFastHLine(80,  60, 50,  traceColor);
    gfx->drawFastVLine(80,  48, 12,  traceColor);
    gfx->drawFastHLine(300, 48, 100, traceColor);
    gfx->drawFastVLine(360, 30, 18,  traceColor);
    gfx->drawFastHLine(0,   420, 80,  traceColor);
    gfx->drawFastVLine(80,  420, 24,  traceColor);
    gfx->drawFastHLine(80,  444, 120, traceColor);
    gfx->drawFastHLine(240, 432, 100, traceColor);
    gfx->drawFastVLine(240, 432, 24,  traceColor);
    gfx->drawFastHLine(300, 456, 100, traceColor);
    gfx->drawFastHLine(140, 390, 80,  traceColor);
    gfx->drawFastVLine(340, 390, 30,  traceColor);
    gfx->fillRect(98,  34, 5, 5, padColor);
    gfx->fillRect(178, 34, 5, 5, padColor);
    gfx->fillRect(78,  58, 5, 5, padColor);
    gfx->fillRect(258, 28, 5, 5, padColor);
    gfx->fillRect(358, 28, 5, 5, padColor);
    gfx->fillRect(78,  418, 5, 5, padColor);
    gfx->fillRect(198, 442, 5, 5, padColor);
    gfx->fillRect(338, 388, 5, 5, padColor);
}

// ─────────────────────────────────────────────────────────────────
// RAINBOW TITLE DRAW
// Draws "PISCES MOON OS" letter by letter with cycling hue
// Each call advances the color offset for animation effect
// ─────────────────────────────────────────────────────────────────
static void drawRainbowTitle(int x, int y, int offset) {
    const char* title = "PISCES MOON OS";
    int len = strlen(title);
    gfx->setTextSize(3);
    int charW = 18;  // 3 * 6px per char
    for (int i = 0; i < len; i++) {
        if (title[i] == ' ') continue;
        gfx->setTextColor(rainbowColor(i, offset));
        gfx->setCursor(x + i * charW, y);
        char ch[2] = {title[i], 0};
        gfx->print(ch);
    }
}

static void drawSplash() {
    drawCircuitBackground();

    // Glow border
    for (int i = 6; i >= 0; i--) {
        uint16_t c = (i == 0) ? 0x07E0 : (0x0040 + i * 0x0020);
        gfx->drawRect(80 - i*2, 160 - i*2, 320 + i*4, 160 + i*4, c);
    }

    // Title box
    gfx->fillRect(82, 162, 316, 156, 0x0000);

    // Rainbow title — animate 8 cycles then hold
    int tx = (SCR_W - 14 * 18) / 2;
    int ty = 178;

    for (int frame = 0; frame < 24; frame++) {
        // Clear title area
        gfx->fillRect(82, ty - 2, 316, 24, 0x0000);
        drawRainbowTitle(tx, ty, frame * 15);
        delay(60);
    }

    // Hold final frame
    gfx->fillRect(82, ty - 2, 316, 24, 0x0000);
    drawRainbowTitle(tx, ty, 0);

    // Divider
    gfx->drawFastHLine(100, 204, 280, 0x0240);

    // Edition
    gfx->setTextSize(2);
    gfx->setTextColor(0x07FF);
    int ex = (SCR_W - 17 * 2 * 6) / 2;
    gfx->setCursor(ex, 214);
    gfx->print("PocketMind Edition");

    // Tagline
    gfx->setTextSize(1);
    gfx->setTextColor(0x0480);
    gfx->setCursor(160, 248);
    gfx->print("YOUR SIGNAL. YOUR SHORE.");

    // Version
    gfx->setTextColor(0x0480);
    gfx->setCursor(172, 300);
    gfx->print("v1.0.0  //  First Light");

    delay(1000);
}

static void drawEasterEgg() {
    gfx->fillScreen(0x0000);
    gfx->setTextSize(1);
    int y = SCR_H / 2 - 8;
    int x = 40;
    gfx->setTextColor(0x07E0);
    gfx->setCursor(x, y);
    gfx->print("Elevating L");
    for (int i = 0; i < 5; i++) {
        delay(400);
        gfx->setTextColor(0x07E0);
        gfx->print(" .");
    }
    delay(600);
    gfx->setTextColor(BOOT_AMBER);
    gfx->setCursor(x, y + 20);
    gfx->print("Still Loading, Come Back Later");
    delay(1200);
}

void run_boot_sequence(bool sd_ok, bool touch_ok, bool pmu_ok, bool rtc_ok) {
    gfx->fillScreen(BOOT_BG);

    drawBootHeader();
    drawBootSection("MEMORY MAP");

    drawBootLine("00:00", "HIMEM.SYS",          "0x00008000", 0, "OK");   delay(25);
    drawBootLine("00:01", "Shadow RAM",          "0x00010000", 0, "OK");   delay(25);
    drawBootLine("00:02", "L2 Cache",            "0x00045000", 0, "OK");   delay(25);

    drawBootSection("PERIPHERALS");

    drawBootLine("00:03", "CPU ESP32-S3 240MHz", nullptr,      0, "DONE"); delay(30);
    drawBootLine("00:04", "I2C SDA:8 SCL:9",    nullptr,      0, "OK");   delay(30);
    drawBootLine("00:05", "ST7701 RGB 480x480",  nullptr,      0, "OK");   delay(30);
    drawBootLine("00:06", "GT911 Touch Pulse",   nullptr,
                 touch_ok ? 0 : 3, touch_ok ? "OK" : "FAIL");             delay(30);
    drawBootLine("00:07", "AXP2101 PMU",         nullptr,
                 pmu_ok ? 0 : 2, pmu_ok ? "OK" : "WARN");                 delay(30);
    drawBootLine("00:08", "SD_CARD0",            nullptr,
                 2, "N/A");                                                delay(30);
    drawBootLine("00:09", "PCF85063 RTC",        nullptr,
                 rtc_ok ? 0 : 2, rtc_ok ? "OK" : "WARN");                 delay(30);

    drawBootSection("PROCESS SPAWN");

    drawBootLine("00:10", "RECORDING_ENGINE",    nullptr,      1, "ACTIVE");    delay(30);
    drawBootLine("00:11", "LIGHTHOUSE_CLIENT",   nullptr,      1, "ACTIVE");    delay(30);
    drawBootLine("00:12", "WiFi Auto-Connect",   nullptr,      2, "TRIGGERED"); delay(30);
    drawBootLine("00:13", "CORE_0_AI_ENGINE",    nullptr,      1, "ACTIVE");    delay(30);

    drawBootProgress(100);
    drawBootFooter();

    delay(600);

    // Rainbow screen sweep transition
    for (int y = 0; y < SCR_H; y++) {
        uint8_t  r = (uint8_t)((float)y / SCR_H * 31);
        uint8_t  g = (uint8_t)((float)(SCR_H - y) / SCR_H * 63);
        uint8_t  b = (uint8_t)((float)y / SCR_H * 31);
        uint16_t c = (r << 11) | (g << 5) | b;
        gfx->drawFastHLine(0, y, SCR_W, c);
    }
    delay(200);

    drawSplash();
    drawEasterEgg();
}
