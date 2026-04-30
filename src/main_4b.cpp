// Pisces Moon OS — PocketMind Edition
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// fluidfortune.com

/**
 * POCKETMIND EDITION — main_4b.cpp
 * Arduino entry point for Waveshare ESP32-S3-Touch-LCD-4B
 *
 * v1.3 changes:
 *   - WiFiManager captive portal replaces hardcoded credentials
 *   - Portal SSID: "PocketMind-Setup"
 *   - Connect to that network from phone to configure WiFi
 *   - Credentials saved to flash — survives reboots
 *   - If saved network unavailable, shows portal again
 *   - Boot screen appears before WiFi blocks
 */

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <Arduino_GFX_Library.h>
#include "SdFat.h"
#include <freertos/semphr.h>
#include "hal_4b.h"
#include "boot_screen.h"
#include "lighthouse_terminal_4b.h"
#include "lighthouse_client.h"
#include "recording_engine.h"

// ─────────────────────────────────────────────────────────────────
// WIFI CONFIG
// Portal appears as "PocketMind-Setup" hotspot on first boot
// or when saved network is unavailable
// ─────────────────────────────────────────────────────────────────
#define WIFI_PORTAL_SSID  "PocketMind-Setup"
#define WIFI_PORTAL_PASS  "pocketmind"
#define WIFI_TIMEOUT_MS   30000   // portal timeout before continuing offline

// ─────────────────────────────────────────────────────────────────
// DISPLAY OBJECTS
// ─────────────────────────────────────────────────────────────────
Arduino_XCA9554SWSPI *expander = new Arduino_XCA9554SWSPI(
    7, 0, 2, 1, &Wire, EXPANDER_ADDR);

Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
    LCD_DE,   LCD_VSYNC, LCD_HSYNC, LCD_PCLK,
    LCD_B0,   LCD_B1,   LCD_B2,   LCD_B3,   LCD_B4,
    LCD_G0,   LCD_G1,   LCD_G2,   LCD_G3,   LCD_G4,   LCD_G5,
    LCD_R0,   LCD_R1,   LCD_R2,   LCD_R3,   LCD_R4,
    1, 10, 8, 50,
    1, 10, 8, 20);

Arduino_GFX *gfx = new Arduino_RGB_Display(
    LCD_WIDTH, LCD_HEIGHT, rgbpanel, 0, true,
    expander, GFX_NOT_DEFINED,
    st7701_type1_init_operations, sizeof(st7701_type1_init_operations));

// ─────────────────────────────────────────────────────────────────
// SHARED GLOBALS
// ─────────────────────────────────────────────────────────────────
SdFat              sd;
SemaphoreHandle_t  spi_mutex    = NULL;
volatile bool      sd_in_use    = false;
volatile bool      wifi_in_use  = false;

// ─────────────────────────────────────────────────────────────────
// WIFI PORTAL SCREEN
// Shown on display while captive portal is active
// ─────────────────────────────────────────────────────────────────
static void showPortalScreen() {
    gfx->fillScreen(0x0000);

    gfx->setTextSize(2);
    gfx->setTextColor(0x07E0);
    gfx->setCursor(60, 80);
    gfx->print("WiFi Setup");

    gfx->drawFastHLine(40, 110, 400, 0x0240);

    gfx->setTextSize(1);
    gfx->setTextColor(0x07FF);
    gfx->setCursor(40, 130);
    gfx->print("Connect your phone to:");

    gfx->setTextSize(2);
    gfx->setTextColor(0xFD20);
    gfx->setCursor(60, 155);
    gfx->print("PocketMind-Setup");

    gfx->setTextSize(1);
    gfx->setTextColor(0x07FF);
    gfx->setCursor(40, 195);
    gfx->print("Password: pocketmind");

    gfx->setCursor(40, 220);
    gfx->print("Then open: 192.168.4.1");

    gfx->setCursor(40, 245);
    gfx->print("Select your WiFi network");
    gfx->setCursor(40, 258);
    gfx->print("and enter your password.");

    gfx->drawFastHLine(40, 290, 400, 0x0240);

    gfx->setTextColor(0x4208);
    gfx->setCursor(40, 305);
    gfx->print("Portal closes automatically");
    gfx->setCursor(40, 318);
    gfx->print("after connection or 30 seconds.");
}

static void showConnectingScreen() {
    gfx->fillScreen(0x0000);
    gfx->setTextSize(2);
    gfx->setTextColor(0x07E0);
    gfx->setCursor(80, 200);
    gfx->print("Connecting...");
    gfx->setTextSize(1);
    gfx->setTextColor(0x4208);
    gfx->setCursor(100, 240);
    gfx->print("Please wait");
}

static void showOfflineScreen() {
    gfx->fillScreen(0x0000);
    gfx->setTextSize(2);
    gfx->setTextColor(0xFD20);
    gfx->setCursor(100, 190);
    gfx->print("Offline Mode");
    gfx->setTextSize(1);
    gfx->setTextColor(0x4208);
    gfx->setCursor(60, 230);
    gfx->print("Lighthouse unreachable.");
    gfx->setCursor(60, 245);
    gfx->print("Reboot to retry WiFi setup.");
    delay(2000);
}

// ─────────────────────────────────────────────────────────────────
// SETUP
// ─────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    Serial.println("[POCKETMIND] Booting...");

    // ── I2C for IO expander ───────────────────────────────────────
    Wire.begin(EXPANDER_SDA, EXPANDER_SCL);

    // ── Display init ──────────────────────────────────────────────
    expander->pinMode(EXPANDER_PIN_LCD_BL,  OUTPUT);
    expander->pinMode(EXPANDER_PIN_LCD_RST, OUTPUT);

    expander->digitalWrite(EXPANDER_PIN_LCD_BL,  LOW);
    delay(200);
    expander->digitalWrite(EXPANDER_PIN_LCD_RST, LOW);
    delay(200);
    expander->digitalWrite(EXPANDER_PIN_LCD_RST, HIGH);
    delay(200);

    if (!gfx->begin()) {
        Serial.println("[POCKETMIND] Display init FAILED");
        while (1) { delay(1000); }
    }

    expander->digitalWrite(EXPANDER_PIN_LCD_BL, HIGH);
    gfx->fillScreen(0x0000);
    Serial.println("[POCKETMIND] Display OK");

    // ── SPI mutex ─────────────────────────────────────────────────
    spi_mutex = xSemaphoreCreateMutex();

    // ── Boot screen — appears immediately, no WiFi blocking ───────
    run_boot_sequence(false, true, true, true);

    // ── WiFiManager ───────────────────────────────────────────────
    // Show portal screen on display while user configures
    WiFiManager wm;
    wm.setConfigPortalTimeout(30);  // 30 seconds then continue offline
    wm.setConnectTimeout(15);

    // Callback when portal opens
    wm.setAPCallback([](WiFiManager* wm) {
        Serial.println("[POCKETMIND] WiFi portal active — PocketMind-Setup");
        showPortalScreen();
    });

    // Callback when connecting to saved network
    wm.setSaveConfigCallback([]() {
        Serial.println("[POCKETMIND] WiFi credentials saved");
        showConnectingScreen();
    });

    showConnectingScreen();

    bool connected = wm.autoConnect(WIFI_PORTAL_SSID, WIFI_PORTAL_PASS);

    if (connected) {
        Serial.printf("[POCKETMIND] WiFi OK: %s\n",
                      WiFi.localIP().toString().c_str());
    } else {
        Serial.println("[POCKETMIND] WiFi FAILED — continuing offline");
        showOfflineScreen();
    }

    // ── Recording engine on Core 0 ────────────────────────────────
    recording_engine_init();
    xTaskCreatePinnedToCore(
        recording_task, "RecCore",
        8192, NULL, 1, NULL, 0);

    // ── Lighthouse client ─────────────────────────────────────────
    lighthouse_init();

    Serial.println("[POCKETMIND] Launching terminal...");
}

// ─────────────────────────────────────────────────────────────────
// LOOP
// ─────────────────────────────────────────────────────────────────
void loop() {
    run_lighthouse_terminal_4b();
    delay(500);
}
