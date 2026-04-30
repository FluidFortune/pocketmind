// Pisces Moon OS — PocketMind Branch
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// fluidfortune.com

/**
 * LIGHTHOUSE TERMINAL v1.0
 *
 * PocketMind AI companion app for the LilyGo T-Deck Plus.
 * Runs within the Pisces Moon OS launcher suite.
 *
 * Controls:
 *   SPACE (hold)    = record voice memo
 *   SPACE (release) = stop recording, send to AI pipeline
 *   TAB             = cycle AI target (Claude → Gemini → ChatGPT → DeepSeek → Ollama)
 *   S               = summarize today's recordings
 *   Q / header tap  = exit
 *
 * Based on voice_terminal.cpp from Pisces Moon OS.
 * Replaces ask_gemini() with lighthouse_chat().
 * Adds recording engine integration.
 * All recordings saved permanently to SD /recordings/
 */

#include "lighthouse_terminal.h"
#include "lighthouse_client.h"
#include "recording_engine.h"
#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <Arduino_GFX_Library.h>
#include "touch.h"
#include "trackball.h"
#include "keyboard.h"
#include "SdFat.h"

extern Arduino_GFX*       gfx;
extern SdFat              sd;
extern volatile bool      wifi_in_use;
extern SemaphoreHandle_t  spi_mutex;

// ─────────────────────────────────────────────────────────────────
// COLORS (Pisces Moon palette)
// ─────────────────────────────────────────────────────────────────
#define LT_BG        0x0000
#define LT_HEADER    0x0841
#define LT_GREEN     0x07E0
#define LT_CYAN      0x07FF
#define LT_WHITE     0xFFFF
#define LT_DIM       0x4208
#define LT_RED       0xF800
#define LT_AMBER     0xFD20
#define LT_MAGENTA   0xF81F
#define LT_RECORD    0xF800
#define LT_PURPLE    0x781F

// ─────────────────────────────────────────────────────────────────
// CONVERSATION DISPLAY
// ─────────────────────────────────────────────────────────────────
#define LT_MAX_LINES  7
static String _lines[LT_MAX_LINES];
static int    _line_count = 0;

static void _add_line(const String& who, const String& text) {
    String full = who + ": " + text;
    if (_line_count >= LT_MAX_LINES) {
        for (int i = 0; i < LT_MAX_LINES - 1; i++)
            _lines[i] = _lines[i + 1];
        _line_count = LT_MAX_LINES - 1;
    }
    if (full.length() > 53) full = full.substring(0, 50) + "...";
    _lines[_line_count++] = full;
}

// ─────────────────────────────────────────────────────────────────
// CURRENT TARGET
// ─────────────────────────────────────────────────────────────────
static const char* _targets[] = {
    TARGET_CLAUDE,
    TARGET_GEMINI,
    TARGET_CHATGPT,
    TARGET_DEEPSEEK,
    TARGET_OLLAMA
};
static int _target_idx = 0;  // Default: Claude

static String _current_target() {
    return String(_targets[_target_idx]);
}

static void _next_target() {
    _target_idx = (_target_idx + 1) % (sizeof(_targets) / sizeof(_targets[0]));
}

// ─────────────────────────────────────────────────────────────────
// SCREEN DRAWING
// ─────────────────────────────────────────────────────────────────
static void _draw_screen(const String& status, uint16_t status_color, bool recording) {
    // Header bar
    gfx->fillRect(0, 0, 320, 22, LT_HEADER);
    gfx->drawFastHLine(0, 22, 320, LT_GREEN);
    gfx->setTextSize(1);

    // Title
    gfx->setTextColor(LT_GREEN);
    gfx->setCursor(6, 7);
    gfx->print("LIGHTHOUSE");

    // Current target indicator
    gfx->setTextColor(LT_CYAN);
    gfx->setCursor(90, 7);
    gfx->print(lighthouse_target_name(_current_target()));

    // Recording count
    gfx->setTextColor(LT_DIM);
    gfx->setCursor(180, 7);
    gfx->printf("REC:%d", recording_get_count());

    // Exit hint
    gfx->setTextColor(LT_DIM);
    gfx->setCursor(250, 7);
    gfx->print("[TAP=EXIT]");

    // Conversation area
    gfx->fillRect(0, 24, 320, 185, LT_BG);
    gfx->setTextSize(1);

    for (int i = 0; i < _line_count; i++) {
        uint16_t color = LT_DIM;
        if (_lines[i].startsWith("You:"))      color = LT_CYAN;
        if (_lines[i].startsWith("Claude:"))   color = LT_GREEN;
        if (_lines[i].startsWith("Gemini:"))   color = LT_AMBER;
        if (_lines[i].startsWith("ChatGPT:"))  color = LT_GREEN;
        if (_lines[i].startsWith("DeepSeek:")) color = LT_PURPLE;
        if (_lines[i].startsWith("Ollama:"))   color = LT_MAGENTA;
        if (_lines[i].startsWith("["))         color = LT_AMBER;

        gfx->setTextColor(color);
        gfx->setCursor(4, 26 + i * 24);
        gfx->print(_lines[i]);
    }

    // Status / recording bar
    gfx->fillRect(0, 211, 320, 29, recording ? LT_RECORD : 0x0821);
    gfx->drawFastHLine(0, 211, 320, recording ? LT_RED : LT_DIM);

    if (recording) {
        // Show recording duration
        uint32_t dur_s = recording_get_duration_ms() / 1000;
        gfx->setTextSize(2);
        gfx->setTextColor(LT_WHITE);
        gfx->setCursor(60, 217);
        gfx->printf("REC  %02lu:%02lu", dur_s / 60, dur_s % 60);
    } else {
        gfx->setTextSize(1);
        gfx->setTextColor(status_color);
        int sx = max(4, (int)(160 - status.length() * 3));
        gfx->setCursor(sx, 221);
        gfx->print(status);
    }
}

static void _draw_keyboard_input(const String& current) {
    gfx->fillRect(0, 211, 320, 29, 0x0821);
    gfx->drawFastHLine(0, 211, 320, LT_DIM);
    gfx->setTextSize(1);
    gfx->setTextColor(LT_WHITE);
    gfx->setCursor(4, 221);
    String display = "> " + current;
    if (display.length() > 52) display = "> ..." + current.substring(current.length() - 47);
    gfx->print(display);
}

// ─────────────────────────────────────────────────────────────────
// AI PIPELINE — runs after recording stops
// ─────────────────────────────────────────────────────────────────
static void _process_recording(const RecordingInfo& info) {
    // 1. Transcribe
    _draw_screen("Transcribing...", LT_AMBER, false);
    String transcript = lighthouse_transcribe(info.path);

    if (transcript.length() == 0) {
        _add_line("[ERROR]", "Transcription failed");
        _draw_screen("Transcription failed. Try again.", LT_RED, false);
        return;
    }

    _add_line("You", transcript);
    _draw_screen("Sending to " + String(lighthouse_target_name(_current_target())) + "...", LT_AMBER, false);

    // 2. Send to AI via Lighthouse
    String response = lighthouse_chat(transcript, _current_target());

    if (response.length() == 0) {
        _add_line("[ERROR]", "No response from Lighthouse");
        _draw_screen("Check Lighthouse server.", LT_RED, false);
        return;
    }

    _add_line(String(lighthouse_target_name(_current_target())), response);

    // 3. Background evaluate (non-blocking display update)
    _draw_screen("Done. Hold SPACE for next.", LT_GREEN, false);

    // 4. Queue evaluation — send in background
    // (Evaluation result saved to SD alongside the WAV)
    String eval_result = lighthouse_evaluate(transcript, _current_target());
    if (eval_result.length() > 0) {
        // Save evaluation alongside recording
        String eval_path = String(info.path);
        eval_path.replace(".wav", ".json");

        if (spi_mutex && xSemaphoreTake(spi_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
            FsFile eval_file = sd.open(eval_path.c_str(), O_WRITE | O_CREAT | O_TRUNC);
            if (eval_file) {
                eval_file.print(eval_result);
                eval_file.close();
            }
            xSemaphoreGive(spi_mutex);
        }

        // Save transcript
        String txt_path = String(info.path);
        txt_path.replace(".wav", ".txt");

        if (spi_mutex && xSemaphoreTake(spi_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
            FsFile txt_file = sd.open(txt_path.c_str(), O_WRITE | O_CREAT | O_TRUNC);
            if (txt_file) {
                txt_file.print(transcript);
                txt_file.close();
            }
            xSemaphoreGive(spi_mutex);
        }
    }
}

// ─────────────────────────────────────────────────────────────────
// MAIN ENTRY POINT
// Called from Pisces Moon OS launcher
// ─────────────────────────────────────────────────────────────────
void run_lighthouse_terminal() {
    gfx->fillScreen(LT_BG);
    _line_count = 0;
    _target_idx = 0;  // Start with Claude

    // Check WiFi
    if (WiFi.status() != WL_CONNECTED) {
        gfx->setTextColor(LT_RED);
        gfx->setTextSize(1);
        gfx->setCursor(10, 60); gfx->print("Lighthouse Terminal requires WiFi.");
        gfx->setCursor(10, 80); gfx->print("Connect via WIFI JOIN first.");
        gfx->setTextColor(LT_DIM);
        gfx->setCursor(10, 110); gfx->print("Tap header to exit.");
        while (true) {
            int16_t tx, ty;
            if (get_touch(&tx, &ty) && ty < 30) return;
            if (get_keypress()) return;
            delay(50);
        }
    }

    // Check Lighthouse
    _draw_screen("Checking Lighthouse...", LT_AMBER, false);
    if (!lighthouse_health_check()) {
        _add_line("[WARN]", "Lighthouse unreachable — check server");
        _draw_screen("Server offline. Check OmniBox.", LT_RED, false);
        delay(2000);
        // Don't exit — user might want to type anyway
    } else {
        _add_line("[READY]", "SPACE=record  TAB=switch AI  S=summary  Q=exit");
        _draw_screen("Hold SPACE to record your prompt", LT_DIM, false);
    }

    _draw_screen("Hold SPACE=record  TAB=target  S=summary  Q=exit", LT_DIM, false);

    // ── MAIN LOOP ─────────────────────────────────────────────────
    while (true) {
        // Touch header = exit
        int16_t tx, ty;
        if (get_touch(&tx, &ty) && ty < 24) {
            while (get_touch(&tx, &ty)) delay(10);
            break;
        }

        char k = get_keypress();

        // Q = quit
        if (k == 'q' || k == 'Q') break;

        // TAB = cycle AI target
        if (k == '\t') {
            _next_target();
            _add_line("[TARGET]", String("Switched to ") + lighthouse_target_name(_current_target()));
            _draw_screen("Target: " + String(lighthouse_target_name(_current_target())), LT_CYAN, false);
            continue;
        }

        // S = summarize today
        if (k == 's' || k == 'S') {
            _draw_screen("Building daily summary...", LT_AMBER, false);
            // TODO: collect today's transcripts from SD and send to /summarize
            // For now placeholder
            _add_line("[INFO]", "Summary feature coming in v1.1");
            _draw_screen("Done.", LT_GREEN, false);
            continue;
        }

        // SPACE = push to talk
        if (k == ' ') {
            // Start recording
            if (!recording_start()) {
                _add_line("[ERROR]", "Could not start recording");
                _draw_screen("Recording failed. Check SD card.", LT_RED, false);
                continue;
            }

            // Draw recording state
            _draw_screen("", LT_WHITE, true);

            // Wait for SPACE release or Q
            while (true) {
                char held = get_keypress();
                // Update duration display
                _draw_screen("", LT_WHITE, true);

                if (held == ' ' || held == 'q' || held == 'Q') {
                    // Stop recording
                    RecordingInfo info;
                    recording_stop(&info);

                    if (held == 'q' || held == 'Q') break;

                    if (info.bytes_written < 16000) {
                        // Too short — probably accidental press
                        _add_line("[INFO]", "Too short — try again");
                        _draw_screen("Recording too short. Try again.", LT_AMBER, false);
                        break;
                    }

                    // Process recording through AI pipeline
                    _process_recording(info);
                    break;
                }
                delay(100);
            }
            continue;
        }

        // Any other key = keyboard text input mode
        if (k >= 32 && k <= 126) {
            String prompt = String((char)k);
            _draw_keyboard_input(prompt);

            while (true) {
                char c = get_keypress();
                if (c == 13 || c == 10) break;   // Enter = send
                if (c == 'q' && prompt.length() == 0) goto exit_loop;
                if (c == 8 || c == 127) {          // Backspace
                    if (prompt.length() > 0) prompt.remove(prompt.length() - 1);
                } else if (c >= 32 && c <= 126) {
                    prompt += c;
                }
                _draw_keyboard_input(prompt);
                delay(15);
            }

            if (prompt.length() > 0) {
                _add_line("You", prompt);
                _draw_screen("Asking " + String(lighthouse_target_name(_current_target())) + "...", LT_AMBER, false);

                String response = lighthouse_chat(prompt, _current_target());

                if (response.length() > 0) {
                    _add_line(String(lighthouse_target_name(_current_target())), response);
                    _draw_screen("Done.", LT_GREEN, false);
                } else {
                    _add_line("[ERROR]", "No response — check Lighthouse");
                    _draw_screen("No response. Check server.", LT_RED, false);
                }
            }
        }

        delay(20);
        yield();
    }

    exit_loop:
    gfx->fillScreen(LT_BG);
}
