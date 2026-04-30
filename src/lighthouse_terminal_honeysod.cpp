// Pisces Moon OS — PocketMind Edition
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// fluidfortune.com

/**
 * LIGHTHOUSE TERMINAL — HoneySod (Waveshare ESP32-S3-Touch-LCD-2.8)
 * v1.0 "First Light"
 *
 * Voice-first UI for 240×320 display.
 * No software keyboard — PTT voice input only.
 * Built-in microphone → Lighthouse transcribe → AI response
 *
 * Layout (portrait 240×320):
 *   Header    — title + target selector     (0-48px)
 *   Chat area — scrollable response display (48-220px)
 *   Status    — connection / processing     (220-250px)
 *   PTT       — large push to talk button   (250-320px)
 */

#include "lighthouse_terminal_honeysod.h"
#include "lighthouse_client.h"
#include "recording_engine.h"
#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <ArduinoJson.h>
#include <Arduino_GFX_Library.h>

// ─────────────────────────────────────────────────────────────────
// EXTERNAL REFERENCES
// ─────────────────────────────────────────────────────────────────
extern Arduino_GFX*       gfx;
extern volatile bool      wifi_in_use;
extern SemaphoreHandle_t  spi_mutex;

// ─────────────────────────────────────────────────────────────────
// DISPLAY DIMENSIONS
// ─────────────────────────────────────────────────────────────────
#define SCR_W   240
#define SCR_H   320

// ─────────────────────────────────────────────────────────────────
// LAYOUT ZONES
// ─────────────────────────────────────────────────────────────────
#define HEADER_H    48
#define CHAT_Y      HEADER_H
#define CHAT_H      172
#define STATUS_Y    (CHAT_Y + CHAT_H)
#define STATUS_H    30
#define PTT_Y       (STATUS_Y + STATUS_H)
#define PTT_H       (SCR_H - PTT_Y)

// ─────────────────────────────────────────────────────────────────
// COLORS
// ─────────────────────────────────────────────────────────────────
#define C_BG            0x0000
#define C_HEADER        0x0841
#define C_GREEN         0x07E0
#define C_CYAN          0x07FF
#define C_WHITE         0xFFFF
#define C_DIM           0x4208
#define C_RED           0xF800
#define C_AMBER         0xFD20
#define C_MAGENTA       0xF81F
#define C_PURPLE        0x781F
#define C_TARGET_BG     0x0421
#define C_PTT_IDLE      0x0841
#define C_PTT_ACTIVE    0xF800

// ─────────────────────────────────────────────────────────────────
// AI TARGETS
// ─────────────────────────────────────────────────────────────────
static const char* TARGETS[]      = {"claude", "gemini", "chatgpt", "deepseek", "ollama"};
static const char* TARGET_NAMES[] = {"Claude", "Gemini", "ChatGPT", "DeepSeek", "Ollama"};
static const uint16_t TARGET_COLORS[] = {C_GREEN, C_AMBER, C_GREEN, C_PURPLE, C_MAGENTA};
static const int TARGET_COUNT = 5;
static int _target_idx = 0;

// ─────────────────────────────────────────────────────────────────
// CONVERSATION STATE
// ─────────────────────────────────────────────────────────────────
#define MAX_LINES    6
#define MAX_LINE_LEN 36

static char     _lines[MAX_LINES][MAX_LINE_LEN + 1];
static uint16_t _line_colors[MAX_LINES];
static int      _line_count = 0;

static void _add_line(const char* prefix, const char* text, uint16_t color) {
    char full[128];
    snprintf(full, sizeof(full), "%s: %s", prefix, text);
    if (_line_count >= MAX_LINES) {
        for (int i = 0; i < MAX_LINES - 1; i++) {
            memcpy(_lines[i], _lines[i+1], MAX_LINE_LEN + 1);
            _line_colors[i] = _line_colors[i+1];
        }
        _line_count = MAX_LINES - 1;
    }
    strncpy(_lines[_line_count], full, MAX_LINE_LEN);
    _lines[_line_count][MAX_LINE_LEN] = 0;
    if (strlen(full) > MAX_LINE_LEN) {
        _lines[_line_count][MAX_LINE_LEN - 3] = '.';
        _lines[_line_count][MAX_LINE_LEN - 2] = '.';
        _lines[_line_count][MAX_LINE_LEN - 1] = '.';
    }
    _line_colors[_line_count] = color;
    _line_count++;
}

// ─────────────────────────────────────────────────────────────────
// TOUCH — CST328
// Simple polling via Wire — no TAMC_GT911 needed
// CST328 uses same I2C register layout as CST816
// ─────────────────────────────────────────────────────────────────
#define CST328_ADDR     0x1A
#define CST328_REG      0x00

static bool _get_touch(int16_t* tx, int16_t* ty) {
    Wire.beginTransmission(CST328_ADDR);
    Wire.write(CST328_REG);
    if (Wire.endTransmission(false) != 0) return false;

    Wire.requestFrom(CST328_ADDR, 7);
    if (Wire.available() < 7) return false;

    uint8_t data[7];
    for (int i = 0; i < 7; i++) data[i] = Wire.read();

    uint8_t fingers = data[2] & 0x0F;
    if (fingers == 0) return false;

    *tx = ((data[3] & 0x0F) << 8) | data[4];
    *ty = ((data[5] & 0x0F) << 8) | data[6];

    Serial.printf("[TOUCH] x=%d y=%d\n", *tx, *ty);
    return true;
}

// ─────────────────────────────────────────────────────────────────
// DRAW FUNCTIONS
// ─────────────────────────────────────────────────────────────────
static void _draw_header() {
    gfx->fillRect(0, 0, SCR_W, HEADER_H, C_HEADER);
    gfx->drawFastHLine(0, HEADER_H, SCR_W, C_GREEN);

    gfx->setTextSize(1);
    gfx->setTextColor(C_GREEN);
    gfx->setCursor(4, 6);
    gfx->print("LIGHTHOUSE");

    // Target button
    int bx = 4;
    int by = 22;
    int bw = SCR_W - 8;
    gfx->fillRect(bx, by, bw, 18, C_TARGET_BG);
    gfx->drawRect(bx, by, bw, 18, TARGET_COLORS[_target_idx]);
    gfx->setTextColor(TARGET_COLORS[_target_idx]);
    gfx->setCursor(bx + 4, by + 5);
    gfx->print(TARGET_NAMES[_target_idx]);
    gfx->setCursor(bx + bw - 14, by + 5);
    gfx->print(">");

    // WiFi status
    gfx->setTextColor(WiFi.status() == WL_CONNECTED ? C_GREEN : C_RED);
    gfx->setCursor(SCR_W - 30, 6);
    gfx->print(WiFi.status() == WL_CONNECTED ? "WiFi" : "DISC");
}

static void _draw_chat() {
    gfx->fillRect(0, CHAT_Y, SCR_W, CHAT_H, C_BG);
    gfx->setTextSize(1);
    int line_h = CHAT_H / MAX_LINES;
    for (int i = 0; i < _line_count; i++) {
        gfx->setTextColor(_line_colors[i]);
        gfx->setCursor(4, CHAT_Y + 4 + i * line_h);
        gfx->print(_lines[i]);
    }
}

static void _draw_status(const char* msg, uint16_t color) {
    gfx->fillRect(0, STATUS_Y, SCR_W, STATUS_H, C_BG);
    gfx->drawFastHLine(0, STATUS_Y, SCR_W, C_DIM);
    gfx->setTextSize(1);
    gfx->setTextColor(color);
    gfx->setCursor(4, STATUS_Y + 10);
    gfx->print(msg);
}

static void _draw_ptt(bool active) {
    uint16_t bg = active ? C_PTT_ACTIVE : C_PTT_IDLE;
    uint16_t fg = active ? C_WHITE : C_GREEN;

    gfx->fillRect(0, PTT_Y, SCR_W, PTT_H, bg);
    gfx->drawFastHLine(0, PTT_Y, SCR_W, active ? C_RED : C_DIM);
    gfx->drawRect(4, PTT_Y + 4, SCR_W - 8, PTT_H - 8,
                  active ? C_RED : C_DIM);

    gfx->setTextSize(2);
    gfx->setTextColor(fg);

    if (active) {
        uint32_t dur_s = recording_get_duration_ms() / 1000;
        char label[24];
        snprintf(label, sizeof(label), "REC %02lu:%02lu", dur_s / 60, dur_s % 60);
        int lx = (SCR_W - strlen(label) * 12) / 2;
        gfx->setCursor(lx, PTT_Y + (PTT_H - 16) / 2);
        gfx->print(label);
    } else {
        const char* l1 = "HOLD TO";
        const char* l2 = "TALK";
        int lx1 = (SCR_W - strlen(l1) * 12) / 2;
        int lx2 = (SCR_W - strlen(l2) * 12) / 2;
        int ly  = PTT_Y + (PTT_H - 36) / 2;
        gfx->setCursor(lx1, ly);
        gfx->print(l1);
        gfx->setCursor(lx2, ly + 18);
        gfx->print(l2);
    }
}

static void _draw_full_ui(bool ptt_active) {
    _draw_header();
    _draw_chat();
    _draw_status("Ready.", C_DIM);
    _draw_ptt(ptt_active);
}

// ─────────────────────────────────────────────────────────────────
// AI PIPELINE
// ─────────────────────────────────────────────────────────────────
static void _send_text(const char* text) {
    _add_line("You", text, C_CYAN);
    _draw_chat();

    char status[48];
    snprintf(status, sizeof(status), "Asking %s...", TARGET_NAMES[_target_idx]);
    _draw_status(status, C_AMBER);

    String response = lighthouse_chat(String(text), String(TARGETS[_target_idx]));

    if (response.length() > 0) {
        _add_line(TARGET_NAMES[_target_idx], response.c_str(),
                  TARGET_COLORS[_target_idx]);
        _draw_chat();
        _draw_status("Done.", C_GREEN);
    } else {
        _add_line("[ERR]", "No response", C_RED);
        _draw_chat();
        _draw_status("Check Lighthouse.", C_RED);
    }
}

static void _process_voice(const RecordingInfo& info) {
    _draw_status("Transcribing...", C_AMBER);
    _draw_ptt(false);

    String transcript = lighthouse_transcribe(info.path);

    if (transcript.length() == 0) {
        _add_line("[ERR]", "Transcription failed", C_RED);
        _draw_chat();
        _draw_status("Transcription failed.", C_RED);
        return;
    }

    _send_text(transcript.c_str());
}

// ─────────────────────────────────────────────────────────────────
// MAIN ENTRY POINT
// ─────────────────────────────────────────────────────────────────
void run_lighthouse_terminal_honeysod() {
    Wire.begin(TOUCH_SDA, TOUCH_SCL);
    lighthouse_init();

    _line_count = 0;
    _target_idx = 0;

    gfx->fillScreen(C_BG);
    _draw_full_ui(false);

    if (WiFi.status() != WL_CONNECTED) {
        _add_line("[WARN]", "WiFi not connected", C_RED);
        _draw_chat();
    } else if (!lighthouse_health_check()) {
        _add_line("[WARN]", "Lighthouse unreachable", C_AMBER);
        _draw_chat();
    } else {
        _add_line("[READY]", "Tap target to switch AI", C_DIM);
        _add_line("[READY]", "Hold button to speak", C_DIM);
        _draw_chat();
    }

    bool ptt_held      = false;
    bool touch_was_on  = false;
    uint32_t ptt_start = 0;

    while (true) {
        int16_t tx = 0, ty = 0;
        bool touched = _get_touch(&tx, &ty);

        if (touched && !touch_was_on) {
            touch_was_on = true;

            // Header — target selector
            if (ty >= 22 && ty < HEADER_H) {
                _target_idx = (_target_idx + 1) % TARGET_COUNT;
                _draw_header();
            }
            // PTT zone
            else if (ty >= PTT_Y) {
                if (!ptt_held) {
                    if (recording_start()) {
                        ptt_held  = true;
                        ptt_start = millis();
                        _draw_ptt(true);
                    }
                }
            }
        }

        if (!touched && touch_was_on) {
            touch_was_on = false;
            if (ptt_held) {
                ptt_held = false;
                RecordingInfo info;
                recording_stop(&info);
                _draw_ptt(false);

                uint32_t dur = millis() - ptt_start;
                if (dur < 500 || info.bytes_written < 4000) {
                    _add_line("[INFO]", "Too short", C_AMBER);
                    _draw_chat();
                } else {
                    _process_voice(info);
                }
            }
        }

        if (ptt_held) {
            static uint32_t last_update = 0;
            if (millis() - last_update > 500) {
                last_update = millis();
                _draw_ptt(true);
            }
        }

        delay(15);
        yield();
    }
}
