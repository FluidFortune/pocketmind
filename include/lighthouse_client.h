// Pisces Moon OS — PocketMind Branch
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// fluidfortune.com

#ifndef LIGHTHOUSE_CLIENT_H
#define LIGHTHOUSE_CLIENT_H

#include <Arduino.h>

// ─────────────────────────────────────────────────────────────────
// LIGHTHOUSE SERVER CONFIG
// Set LIGHTHOUSE_HOST to your OmniBox VM's local IP
// or Tailscale IP for remote access.
// ─────────────────────────────────────────────────────────────────
#define LIGHTHOUSE_HOST     "192.168.12.177"
#define LIGHTHOUSE_PORT     8000
#define LIGHTHOUSE_TIMEOUT  30000  // 30s — AI responses can take time

// Available targets — must match bridge registrations on server
#define TARGET_CLAUDE    "claude"
#define TARGET_GEMINI    "gemini"
#define TARGET_CHATGPT   "chatgpt"
#define TARGET_DEEPSEEK  "deepseek"
#define TARGET_OLLAMA    "ollama"

// ─────────────────────────────────────────────────────────────────
// LIGHTHOUSE CLIENT API
// ─────────────────────────────────────────────────────────────────

// Initialize the client (sets wifi_in_use flag handling)
void lighthouse_init();

// Check if The Lighthouse server is reachable
bool lighthouse_health_check();

// Send a text message to an AI target
// Returns the AI response as a String
// Returns "" on error
String lighthouse_chat(const String& message, const String& target = TARGET_CLAUDE);

// Transcribe an audio file on the SD card
// Sends the WAV file to /transcribe endpoint
// Returns transcript as String
String lighthouse_transcribe(const char* wav_path);

// Evaluate a transcript — returns JSON string
// {"keep": true, "tag": "idea", "summary": "...", "action": "..."}
String lighthouse_evaluate(const String& transcript, const String& target = TARGET_CLAUDE);

// Request daily summary of transcripts
// transcripts_json: JSON array of transcript objects
String lighthouse_summarize(const String& transcripts_json, const String& target = TARGET_CLAUDE);

// Get current target name (for display)
const char* lighthouse_target_name(const String& target);

#endif
