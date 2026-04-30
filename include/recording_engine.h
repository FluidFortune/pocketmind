// Pisces Moon OS — PocketMind Branch
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// fluidfortune.com

#ifndef RECORDING_ENGINE_H
#define RECORDING_ENGINE_H

#include <Arduino.h>

// ─────────────────────────────────────────────────────────────────
// RECORDING ENGINE — The Audio Ghost Engine
//
// Runs on Core 0 alongside the wardrive task (if active).
// Captures I2S audio and writes to SD card.
// SPI Bus Treaty compliant — acquires spi_mutex for all SD writes.
//
// Two modes:
//   MEMO    — records when button held, stops on release
//   SESSION — records continuously until stopped
//
// All recordings saved permanently to /recordings/
// Nothing is auto-deleted. User manages their own storage.
// ─────────────────────────────────────────────────────────────────

// ── I2S MIC PINS (T-Deck Plus) ───────────────────────────────────
// SPM1423 microphone
#define REC_MIC_MCLK   48
#define REC_MIC_LRCK   21
#define REC_MIC_SCK    47
#define REC_MIC_DIN    14
#define REC_I2S_PORT   I2S_NUM_1

// ── AUDIO FORMAT ─────────────────────────────────────────────────
#define REC_SAMPLE_RATE    16000   // 16kHz — optimal for speech/Whisper
#define REC_BITS           16      // 16-bit samples
#define REC_CHANNELS       1       // Mono
#define REC_DMA_COUNT      8
#define REC_DMA_LEN        1024
#define REC_CHUNK_BYTES    8192    // Bytes read per I2S chunk

// ── STORAGE ──────────────────────────────────────────────────────
#define REC_DIR            "/recordings"
#define REC_MAX_PATH       64

// ── RECORDING STATE ──────────────────────────────────────────────
typedef enum {
    REC_IDLE     = 0,   // Not recording
    REC_ACTIVE   = 1,   // Recording in progress
    REC_STOPPING = 2,   // Stop requested, finishing write
    REC_DONE     = 3    // Recording complete, ready for pipeline
} RecordingState;

// ── RECORDING METADATA ───────────────────────────────────────────
struct RecordingInfo {
    char     path[REC_MAX_PATH];    // Full path on SD card
    uint32_t bytes_written;          // Raw audio bytes
    uint32_t duration_ms;            // Duration in milliseconds
    uint32_t start_epoch;            // Unix timestamp of start
    bool     queued_for_ai;          // Has been sent to AI pipeline
};

// ── PUBLIC API ───────────────────────────────────────────────────

// Call once from setup() before init_wardrive_core()
// Creates the /recordings directory on SD if needed
void recording_engine_init();

// Start a new recording
// Returns false if already recording or SD not available
bool recording_start();

// Stop the current recording
// Returns the RecordingInfo for the completed file
// Returns false if not currently recording
bool recording_stop(RecordingInfo* out_info);

// Get current state
RecordingState recording_get_state();

// Get current recording duration in milliseconds
uint32_t recording_get_duration_ms();

// Get the path of the most recently completed recording
const char* recording_get_last_path();

// Get total number of recordings on SD
int recording_get_count();

// Get total storage used by recordings in bytes
uint64_t recording_get_storage_used();

// Internal — spawned as FreeRTOS task on Core 0
// Do not call directly
void recording_task(void* pvParameters);

#endif
