// Pisces Moon OS — PocketMind Edition
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// fluidfortune.com

/**
 * POCKETMIND EDITION — main_honeysod.cpp
 * Arduino entry point for Waveshare ESP32-S3-Touch-LCD-2.8
 * (HoneySod — voice-first PocketMind device)
 *
 * Features:
 *   - ST7789 SPI display 240x320
 *   - CST328 capacitive touch
 *   - Built-in microphone
 *   - PCM5101 audio decoder + speaker
 *   - MicroSD storage
 *   - Battery charging
 *   - WiFiManager captive portal
 */

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <SPI.h>
#include <Arduino_GFX_Library.h>
#include "SdFat.h"
#include <freertos/semphr.h>
#include "hal_honeysod.h"
#include "boot_screen.h"
#include "lighthouse_terminal_honeysod.h"
#include "lighthouse_client.h"
#include "recording_engine.h"

// ─────────────────────────────────────────────────────────────────
// WIFI CONFIG
// ─────────────────────────────────────────────────────────────────
#define WIFI_PORTAL_SSID  "PocketMind-Setup"
#define WIFI_PORTAL_PASS  "pocketmind"

// ─────────────────────────────────────────────────────────────────
// DISPLAY — ST7789 via SPI
// ─────────────────────────────────────────────────────────────────
Arduino_DataBus *bus = new Arduino_ESP32SPI(
    LCD_DC, LCD_CS, LCD_SCK, LCD_MOSI, LCD_MISO);

Arduino_GFX *gfx = new Arduino_ST7789(
    bus, LCD_RST,
    0,      // rotation
    true,   // IPS
    LCD_WIDTH, LCD_HEIGHT);

// ─────────────────────────────────────────────────────────────────
// SHARED GLOBALS
// ─────────────────────────────────────────────────────────────────
SdFat             sd;
SemaphoreHandle_t spi_mutex   = NULL;
volatile bool     sd_in_use   = false;
volatile bool     wifi_in_use = false;

// ─────────────────────────────────────────────────────────────────
// WIFI PORTAL SCREENS
// ─────────────────────────────────────────────────────────────────
static void showPortalScreen() {
    gfx->fillScreen(0x0000);
    gfx->setTextSize(2);
    gfx->setTextColor(0x07E0);
    gfx->setCursor(10, 30);
    gfx->print("WiFi Setup");
    gfx->drawFastHLine(0, 55, LCD_WIDTH, 0x0240);
    gfx->setTextSize(1);
    gfx->setTextColor(0x07FF);
    gfx->setCursor(4, 70);
    gfx->print("Connect phone to:");
    gfx->setTextSize(1);
    gfx->setTextColor(0xFD20);
    gfx->setCursor(4, 90);
    gfx->print("PocketMind-Setup");
    gfx->setTextColor(0x07FF);
    gfx->setCursor(4, 115);
    gfx->print("Password: pocketmind");
    gfx->setCursor(4, 135);
    gfx->print("Open: 192.168.4.1");
    gfx->setCursor(4, 155);
    gfx->print("Pick your network");
    gfx->setCursor(4, 168);
    gfx->print("and enter password.");
    gfx->setTextColor(0x4208);
    gfx->setCursor(4, 200);
    gfx->print("Times out in 30s.");
}

static void showConnectingScreen() {
    gfx->fillScreen(0x0000);
    gfx->setTextSize(2);
    gfx->setTextColor(0x07E0);
    gfx->setCursor(20, 140);
    gfx->print("Connecting");
    gfx->setTextSize(1);
    gfx->setTextColor(0x4208);
    gfx->setCursor(50, 170);
    gfx->print("Please wait...");
}

static void showOfflineScreen() {
    gfx->fillScreen(0x0000);
    gfx->setTextSize(2);
    gfx->setTextColor(0xFD20);
    gfx->setCursor(20, 130);
    gfx->print("Offline");
    gfx->setTextSize(1);
    gfx->setTextColor(0x4208);
    gfx->setCursor(10, 165);
    gfx->print("Reboot to retry WiFi.");
    delay(2000);
}

// ─────────────────────────────────────────────────────────────────
// SETUP
// ─────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    Serial.println("[HONEYSOD] Booting...");

    // ── Backlight on ──────────────────────────────────────────────
    pinMode(LCD_BL, OUTPUT);
    analogWrite(LCD_BL, 255);

    // ── Display init ──────────────────────────────────────────────
    if (!gfx->begin()) {
        Serial.println("[HONEYSOD] Display init FAILED");
        while (1) { delay(1000); }
    }
    gfx->fillScreen(0x0000);
    Serial.println("[HONEYSOD] Display OK");

    // ── I2C for touch ─────────────────────────────────────────────
    Wire.begin(TOUCH_SDA, TOUCH_SCL);

    // ── SPI mutex ─────────────────────────────────────────────────
    spi_mutex = xSemaphoreCreateMutex();

    // ── SD card ───────────────────────────────────────────────────
    bool sd_ok = false;
    if (spi_mutex && xSemaphoreTake(spi_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        sd_ok = sd.begin(SD_CS, SD_SCK_MHZ(20));
        xSemaphoreGive(spi_mutex);
    }
    Serial.printf("[HONEYSOD] SD: %s\n", sd_ok ? "OK" : "FAIL");

    // ── Boot screen ───────────────────────────────────────────────
    run_boot_sequence(sd_ok, true, true, true);

    // ── WiFiManager ───────────────────────────────────────────────
    WiFiManager wm;
    wm.setConfigPortalTimeout(30);
    wm.setConnectTimeout(15);

    wm.setAPCallback([](WiFiManager* wm) {
        Serial.println("[HONEYSOD] WiFi portal active");
        showPortalScreen();
    });

    wm.setSaveConfigCallback([]() {
        Serial.println("[HONEYSOD] WiFi credentials saved");
        showConnectingScreen();
    });

    showConnectingScreen();

    bool connected = wm.autoConnect(WIFI_PORTAL_SSID, WIFI_PORTAL_PASS);

    if (connected) {
        Serial.printf("[HONEYSOD] WiFi OK: %s\n",
                      WiFi.localIP().toString().c_str());
    } else {
        Serial.println("[HONEYSOD] WiFi FAILED — offline mode");
        showOfflineScreen();
    }

    // ── Recording engine on Core 0 ────────────────────────────────
    recording_engine_init();
    xTaskCreatePinnedToCore(
        recording_task, "RecCore",
        8192, NULL, 1, NULL, 0);

    // ── Lighthouse client ─────────────────────────────────────────
    lighthouse_init();

    Serial.println("[HONEYSOD] Launching terminal...");
}

// ─────────────────────────────────────────────────────────────────
// LOOP
// ─────────────────────────────────────────────────────────────────
void loop() {
    run_lighthouse_terminal_honeysod();
    delay(500);
}
