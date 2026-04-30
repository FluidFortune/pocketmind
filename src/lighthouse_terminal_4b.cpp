// Pisces Moon OS — PocketMind Edition
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// fluidfortune.com

/**
 * LIGHTHOUSE TERMINAL — Waveshare ESP32-S3-Touch-LCD-4B
 * v1.2 "Floating Point Touch"
 *
 * GT911 touch with normalized 0.0-1.0 coordinate transform
 * (same approach as T-Deck Plus — no setRotation needed)
 */

#include "lighthouse_terminal_4b.h"
#include "lighthouse_client.h"
#include "recording_engine.h"
#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <TAMC_GT911.h>
#include <ArduinoJson.h>
#include <Arduino_GFX_Library.h>

// ─────────────────────────────────────────────────────────────────
// EXTERNAL REFERENCES
// ─────────────────────────────────────────────────────────────────
extern Arduino_GFX*       gfx;
extern volatile bool      wifi_in_use;
extern SemaphoreHandle_t  spi_mutex;

// ─────────────────────────────────────────────────────────────────
// TOUCH
// ─────────────────────────────────────────────────────────────────
#define TOUCH_SDA       8
#define TOUCH_SCL       9
#define TOUCH_INT       1
#define TOUCH_RST       2
#define TOUCH_WIDTH     480
#define TOUCH_HEIGHT    480

static TAMC_GT911 touch(TOUCH_SDA, TOUCH_SCL, TOUCH_INT, TOUCH_RST,
                         TOUCH_WIDTH, TOUCH_HEIGHT);

// ─────────────────────────────────────────────────────────────────
// DISPLAY DIMENSIONS
// ─────────────────────────────────────────────────────────────────
#define SCR_W   480
#define SCR_H   480

// ─────────────────────────────────────────────────────────────────
// LAYOUT ZONES (pixels)
// ─────────────────────────────────────────────────────────────────
#define HEADER_H        48
#define CHAT_Y          HEADER_H
#define CHAT_H          200
#define PTT_Y           (CHAT_Y + CHAT_H + 4)
#define PTT_H           52
#define KB_Y            (PTT_Y + PTT_H + 4)
#define KB_H            (SCR_H - KB_Y)
#define KEY_ROWS        4
#define KEY_H           (KB_H / KEY_ROWS)

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
#define C_KEY_BG        0x1082
#define C_KEY_ACTIVE    0x2945
#define C_KEY_BORDER    0x2104
#define C_PTT_IDLE      0x0841
#define C_PTT_ACTIVE    0xF800
#define C_TARGET_BG     0x0421

// ─────────────────────────────────────────────────────────────────
// KEYBOARD LAYOUT
// ─────────────────────────────────────────────────────────────────
static const char* KB_ROWS[KEY_ROWS] = {
    "QWERTYUIOP",
    "ASDFGHJKL",
    "ZXCVBNM<",
    " >",
};

#define KEY_BACKSPACE   '<'
#define KEY_ENTER       '>'
#define KEY_SPACE       ' '

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
#define MAX_LINES    8
#define MAX_LINE_LEN 58

static char     _lines[MAX_LINES][MAX_LINE_LEN + 1];
static uint16_t _line_colors[MAX_LINES];
static int      _line_count = 0;

static void _add_line(const char* prefix, const char* text, uint16_t color) {
    char full[256];
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
// KEYBOARD INPUT STATE
// ─────────────────────────────────────────────────────────────────
static char _input[256];
static int  _input_len = 0;

// ─────────────────────────────────────────────────────────────────
// DRAW FUNCTIONS
// ─────────────────────────────────────────────────────────────────
static void _draw_header() {
    gfx->fillRect(0, 0, SCR_W, HEADER_H, C_HEADER);
    gfx->drawFastHLine(0, HEADER_H, SCR_W, C_GREEN);
    gfx->setTextSize(2);
    gfx->setTextColor(C_GREEN);
    gfx->setCursor(8, 12);
    gfx->print("LIGHTHOUSE");
    int tx = 220;
    int tw = 180;
    gfx->fillRect(tx, 8, tw, 32, C_TARGET_BG);
    gfx->drawRect(tx, 8, tw, 32, TARGET_COLORS[_target_idx]);
    gfx->setTextSize(1);
    gfx->setTextColor(TARGET_COLORS[_target_idx]);
    gfx->setCursor(tx + 6, 18);
    gfx->print(TARGET_NAMES[_target_idx]);
    gfx->setCursor(tx + tw - 20, 18);
    gfx->print(" >");
    gfx->setTextColor(WiFi.status() == WL_CONNECTED ? C_GREEN : C_RED);
    gfx->setTextSize(1);
    gfx->setCursor(SCR_W - 40, 18);
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
    gfx->drawFastHLine(0, CHAT_Y + CHAT_H - 18, SCR_W, C_DIM);
    gfx->setTextColor(C_CYAN);
    gfx->setCursor(4, CHAT_Y + CHAT_H - 14);
    char preview[54];
    if (_input_len > 0) {
        snprintf(preview, sizeof(preview), "> %s_", _input);
    } else {
        snprintf(preview, sizeof(preview), "> _");
    }
    gfx->print(preview);
}

static void _draw_ptt(bool active) {
    uint16_t bg = active ? C_PTT_ACTIVE : C_PTT_IDLE;
    uint16_t fg = active ? C_WHITE : C_DIM;
    gfx->fillRect(4, PTT_Y, SCR_W - 8, PTT_H, bg);
    gfx->drawRect(4, PTT_Y, SCR_W - 8, PTT_H, active ? C_RED : C_DIM);
    gfx->setTextSize(2);
    gfx->setTextColor(fg);
    if (active) {
        uint32_t dur_s = recording_get_duration_ms() / 1000;
        char label[32];
        snprintf(label, sizeof(label), "REC %02lu:%02lu", dur_s / 60, dur_s % 60);
        int lx = (SCR_W - strlen(label) * 12) / 2;
        gfx->setCursor(lx, PTT_Y + 14);
        gfx->print(label);
    } else {
        const char* label = "HOLD TO TALK";
        int lx = (SCR_W - strlen(label) * 12) / 2;
        gfx->setCursor(lx, PTT_Y + 14);
        gfx->print(label);
    }
}

static void _draw_keyboard() {
    gfx->fillRect(0, KB_Y, SCR_W, KB_H, C_BG);
    for (int row = 0; row < KEY_ROWS; row++) {
        const char* keys = KB_ROWS[row];
        int nkeys = strlen(keys);
        int key_w = SCR_W / nkeys;
        int y = KB_Y + row * KEY_H;
        for (int col = 0; col < nkeys; col++) {
            char k = keys[col];
            int x = col * key_w;
            gfx->fillRect(x + 1, y + 1, key_w - 2, KEY_H - 2, C_KEY_BG);
            gfx->drawRect(x + 1, y + 1, key_w - 2, KEY_H - 2, C_KEY_BORDER);
            gfx->setTextSize(2);
            gfx->setTextColor(C_WHITE);
            char label[4] = {k, 0};
            if (k == KEY_BACKSPACE) { label[0] = '<'; label[1] = 0; }
            if (k == KEY_ENTER)     { strcpy(label, "OK"); }
            if (k == KEY_SPACE)     { strcpy(label, "___"); }
            int lx = x + (key_w - strlen(label) * 12) / 2;
            int ly = y + (KEY_H - 16) / 2;
            gfx->setCursor(lx, ly);
            gfx->print(label);
        }
        gfx->drawFastHLine(0, y, SCR_W, C_KEY_BORDER);
    }
}

static void _draw_status(const char* msg, uint16_t color) {
    gfx->fillRect(0, CHAT_Y + CHAT_H - 18, SCR_W, 18, C_BG);
    gfx->setTextSize(1);
    gfx->setTextColor(color);
    gfx->setCursor(4, CHAT_Y + CHAT_H - 14);
    gfx->print(msg);
}

static void _draw_full_ui(bool ptt_active) {
    _draw_header();
    _draw_chat();
    _draw_ptt(ptt_active);
    _draw_keyboard();
}

// ─────────────────────────────────────────────────────────────────
// TOUCH DETECTION — FLOATING POINT TRANSFORM
// Same approach as T-Deck Plus — no setRotation needed
// Normalize raw coordinates to 0.0-1.0, then scale to display.
// If axes are wrong, edit the four mapping lines below.
// ─────────────────────────────────────────────────────────────────
static bool _get_touch(int16_t* tx, int16_t* ty) {
    touch.read();
    if (touch.isTouched && touch.touches > 0) {
        float raw_x = (float)touch.points[0].x;
        float raw_y = (float)touch.points[0].y;

        // Normalize to 0.0 - 1.0
        float nx = raw_x / (float)TOUCH_WIDTH;
        float ny = raw_y / (float)TOUCH_HEIGHT;

        // Map to display coordinates
        // ─── If touch is wrong, comment out current pair, uncomment another ───
        *tx = (int16_t)(nx * (float)SCR_W);            // direct
        *ty = (int16_t)(ny * (float)SCR_H);

        // *tx = (int16_t)(ny * (float)SCR_W);          // swap X/Y
        // *ty = (int16_t)(nx * (float)SCR_H);

        // *tx = (int16_t)((1.0f - nx) * (float)SCR_W); // flip X
        // *ty = (int16_t)(ny * (float)SCR_H);

        // *tx = (int16_t)(nx * (float)SCR_W);          // flip Y
        // *ty = (int16_t)((1.0f - ny) * (float)SCR_H);

        // *tx = (int16_t)((1.0f - nx) * (float)SCR_W); // flip both
        // *ty = (int16_t)((1.0f - ny) * (float)SCR_H);

        // *tx = (int16_t)((1.0f - ny) * (float)SCR_W); // swap + flip both
        // *ty = (int16_t)((1.0f - nx) * (float)SCR_H);

        // Clamp to display bounds
        if (*tx < 0) *tx = 0;
        if (*tx >= SCR_W) *tx = SCR_W - 1;
        if (*ty < 0) *ty = 0;
        if (*ty >= SCR_H) *ty = SCR_H - 1;

        Serial.printf("[TOUCH] raw=(%.0f,%.0f) → mapped=(%d,%d)\n",
                      raw_x, raw_y, *tx, *ty);
        return true;
    }
    return false;
}

static char _key_at(int16_t tx, int16_t ty) {
    if (ty < KB_Y || ty >= SCR_H) return 0;
    int row = (ty - KB_Y) / KEY_H;
    if (row < 0 || row >= KEY_ROWS) return 0;
    const char* keys = KB_ROWS[row];
    int nkeys = strlen(keys);
    int key_w = SCR_W / nkeys;
    int col = tx / key_w;
    if (col < 0 || col >= nkeys) return 0;
    return keys[col];
}

static void _flash_key(int16_t tx, int16_t ty) {
    int row = (ty - KB_Y) / KEY_H;
    if (row < 0 || row >= KEY_ROWS) return;
    const char* keys = KB_ROWS[row];
    int nkeys = strlen(keys);
    int key_w = SCR_W / nkeys;
    int col = tx / key_w;
    if (col < 0 || col >= nkeys) return;
    int x = col * key_w;
    int y = KB_Y + row * KEY_H;
    gfx->fillRect(x + 1, y + 1, key_w - 2, KEY_H - 2, C_KEY_ACTIVE);
    delay(60);
    gfx->fillRect(x + 1, y + 1, key_w - 2, KEY_H - 2, C_KEY_BG);
    gfx->drawRect(x + 1, y + 1, key_w - 2, KEY_H - 2, C_KEY_BORDER);
    char k = keys[col];
    char label[4] = {k, 0};
    if (k == KEY_BACKSPACE) { label[0] = '<'; label[1] = 0; }
    if (k == KEY_ENTER)     { strcpy(label, "OK"); }
    if (k == KEY_SPACE)     { strcpy(label, "___"); }
    gfx->setTextSize(2);
    gfx->setTextColor(C_WHITE);
    int lx = x + (key_w - strlen(label) * 12) / 2;
    int ly = y + (KEY_H - 16) / 2;
    gfx->setCursor(lx, ly);
    gfx->print(label);
}

// ─────────────────────────────────────────────────────────────────
// AI PIPELINE
// ─────────────────────────────────────────────────────────────────
static void _send_text(const char* text) {
    _add_line("You", text, C_CYAN);
    _draw_chat();
    char status[64];
    snprintf(status, sizeof(status), "Asking %s...", TARGET_NAMES[_target_idx]);
    _draw_status(status, C_AMBER);
    String response = lighthouse_chat(String(text), String(TARGETS[_target_idx]));
    if (response.length() > 0) {
        _add_line(TARGET_NAMES[_target_idx], response.c_str(),
                  TARGET_COLORS[_target_idx]);
        _draw_chat();
        _draw_status("Done.", C_GREEN);
    } else {
        _add_line("[ERROR]", "No response — check Lighthouse", C_RED);
        _draw_chat();
        _draw_status("Check Lighthouse server.", C_RED);
    }
}

static void _process_voice_recording(const RecordingInfo& info) {
    _draw_status("Transcribing...", C_AMBER);
    _draw_ptt(false);
    String transcript = lighthouse_transcribe(info.path);
    if (transcript.length() == 0) {
        _add_line("[ERROR]", "Transcription failed", C_RED);
        _draw_chat();
        _draw_status("Transcription failed.", C_RED);
        return;
    }
    _send_text(transcript.c_str());
}

// ─────────────────────────────────────────────────────────────────
// MAIN ENTRY POINT
// ─────────────────────────────────────────────────────────────────
void run_lighthouse_terminal_4b() {
    Wire.begin(TOUCH_SDA, TOUCH_SCL);
    touch.begin();
    // No setRotation — using floating point transform in _get_touch instead

    lighthouse_init();

    _line_count = 0;
    _input_len  = 0;
    _input[0]   = 0;
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
        _add_line("[READY]", "Touch target to switch AI", C_DIM);
        _add_line("[READY]", "Hold PTT to speak, type to chat", C_DIM);
        _draw_chat();
    }

    bool ptt_held     = false;
    bool touch_was_on = false;
    uint32_t ptt_start = 0;

    while (true) {
        int16_t tx = 0, ty = 0;
        bool touched = _get_touch(&tx, &ty);

        if (touched && !touch_was_on) {
            touch_was_on = true;

            if (ty < HEADER_H) {
                if (tx >= 220 && tx < 400) {
                    _target_idx = (_target_idx + 1) % TARGET_COUNT;
                    _draw_header();
                }
            } else if (ty >= PTT_Y && ty < PTT_Y + PTT_H) {
                if (!ptt_held) {
                    if (recording_start()) {
                        ptt_held  = true;
                        ptt_start = millis();
                        _draw_ptt(true);
                    }
                }
            } else if (ty >= KB_Y) {
                char k = _key_at(tx, ty);
                if (k) {
                    _flash_key(tx, ty);
                    if (k == KEY_BACKSPACE) {
                        if (_input_len > 0) {
                            _input_len--;
                            _input[_input_len] = 0;
                        }
                    } else if (k == KEY_ENTER) {
                        if (_input_len > 0) {
                            char msg[256];
                            strncpy(msg, _input, sizeof(msg) - 1);
                            _input_len = 0;
                            _input[0]  = 0;
                            _send_text(msg);
                        }
                    } else if (k == KEY_SPACE) {
                        if (_input_len < 250) {
                            _input[_input_len++] = ' ';
                            _input[_input_len]   = 0;
                        }
                    } else {
                        if (_input_len < 250) {
                            char c = k + 32;
                            _input[_input_len++] = c;
                            _input[_input_len]   = 0;
                        }
                    }
                    _draw_chat();
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
                if (dur < 500 || info.bytes_written < 8000) {
                    _add_line("[INFO]", "Too short — hold longer", C_AMBER);
                    _draw_chat();
                } else {
                    _process_voice_recording(info);
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
