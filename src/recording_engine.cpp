// Pisces Moon OS — PocketMind Branch
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// fluidfortune.com

/**
 * RECORDING ENGINE v1.0
 *
 * The Audio Ghost Engine — runs on Core 0.
 * Mirrors the wardrive_task pattern from Pisces Moon OS.
 *
 * SPI Bus Treaty compliance:
 *   All SD writes acquire spi_mutex with a 100ms timeout.
 *   Hit-and-run: open file, write chunk, close, release.
 *   Never holds mutex during I2S reads.
 *   Respects sd_in_use flag from file manager.
 *
 * WAV format:
 *   16kHz, 16-bit, mono PCM
 *   Whisper-optimal — no resampling needed on server
 */

#include "recording_engine.h"
#include <Arduino.h>
#include <driver/i2s.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "SdFat.h"

extern SdFat sd;
extern SemaphoreHandle_t spi_mutex;
extern volatile bool sd_in_use;

// ─────────────────────────────────────────────────────────────────
// STATE
// ─────────────────────────────────────────────────────────────────
static volatile RecordingState _state         = REC_IDLE;
static volatile uint32_t       _bytes_written = 0;
static volatile uint32_t       _start_ms      = 0;
static char                    _current_path[REC_MAX_PATH] = "";
static char                    _last_path[REC_MAX_PATH]    = "";
static portMUX_TYPE            _rec_mux = portMUX_INITIALIZER_UNLOCKED;

// ─────────────────────────────────────────────────────────────────
// WAV HEADER
// ─────────────────────────────────────────────────────────────────
struct WAVHeader {
    char     riff[4]       = {'R','I','F','F'};
    uint32_t file_size     = 0;
    char     wave[4]       = {'W','A','V','E'};
    char     fmt[4]        = {'f','m','t',' '};
    uint32_t fmt_size      = 16;
    uint16_t audio_format  = 1;        // PCM
    uint16_t num_channels  = REC_CHANNELS;
    uint32_t sample_rate   = REC_SAMPLE_RATE;
    uint32_t byte_rate     = REC_SAMPLE_RATE * REC_CHANNELS * (REC_BITS / 8);
    uint16_t block_align   = REC_CHANNELS * (REC_BITS / 8);
    uint16_t bits_per_sample = REC_BITS;
    char     data[4]       = {'d','a','t','a'};
    uint32_t data_size     = 0;
};

// ─────────────────────────────────────────────────────────────────
// HELPERS
// ─────────────────────────────────────────────────────────────────
static String _make_filename() {
    // Generate timestamp-based filename
    // Format: /recordings/rec_YYYYMMDD_HHMMSS.wav
    // Uses millis() as proxy for time if RTC not available
    uint32_t t = millis() / 1000;
    char buf[REC_MAX_PATH];
    snprintf(buf, sizeof(buf), "%s/rec_%010lu.wav", REC_DIR, (unsigned long)t);
    return String(buf);
}

static int _find_next_number() {
    // Count existing recordings to find next number
    int count = 0;
    FsFile dir = sd.open(REC_DIR);
    if (dir) {
        FsFile entry;
        while (entry.openNext(&dir, O_READ)) {
            if (!entry.isDir()) count++;
            entry.close();
        }
        dir.close();
    }
    return count + 1;
}

static bool _write_wav_header(FsFile& file, uint32_t data_bytes) {
    WAVHeader hdr;
    hdr.data_size = data_bytes;
    hdr.file_size = data_bytes + sizeof(WAVHeader) - 8;
    file.seek(0);
    return file.write((uint8_t*)&hdr, sizeof(hdr)) == sizeof(hdr);
}

// ─────────────────────────────────────────────────────────────────
// I2S INIT / DEINIT
// ─────────────────────────────────────────────────────────────────
static bool _i2s_init() {
    i2s_config_t cfg = {
        .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate          = REC_SAMPLE_RATE,
        .bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count        = REC_DMA_COUNT,
        .dma_buf_len          = REC_DMA_LEN,
        .use_apll             = false,
        .tx_desc_auto_clear   = false,
        .fixed_mclk           = 0
    };
    i2s_pin_config_t pins = {
        .mck_io_num   = REC_MIC_MCLK,
        .bck_io_num   = REC_MIC_SCK,
        .ws_io_num    = REC_MIC_LRCK,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num  = REC_MIC_DIN
    };

    if (i2s_driver_install(REC_I2S_PORT, &cfg, 0, NULL) != ESP_OK) return false;
    if (i2s_set_pin(REC_I2S_PORT, &pins) != ESP_OK) {
        i2s_driver_uninstall(REC_I2S_PORT);
        return false;
    }

    // Warmup — discard first few buffers (mic settling time)
    uint8_t warmup[REC_CHUNK_BYTES];
    size_t read = 0;
    for (int i = 0; i < 4; i++)
        i2s_read(REC_I2S_PORT, warmup, REC_CHUNK_BYTES, &read, pdMS_TO_TICKS(100));

    return true;
}

static void _i2s_deinit() {
    i2s_stop(REC_I2S_PORT);
    i2s_driver_uninstall(REC_I2S_PORT);
}

// ─────────────────────────────────────────────────────────────────
// PUBLIC API
// ─────────────────────────────────────────────────────────────────
void recording_engine_init() {
    // Create recordings directory if it doesn't exist
    if (spi_mutex && xSemaphoreTake(spi_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        if (!sd.exists(REC_DIR)) {
            sd.mkdir(REC_DIR);
            Serial.println("[RECORDING] Created /recordings directory");
        }
        xSemaphoreGive(spi_mutex);
    }
    Serial.println("[RECORDING] Engine initialized");
}

bool recording_start() {
    if (_state != REC_IDLE) {
        Serial.println("[RECORDING] Already recording");
        return false;
    }

    // Generate filename
    String path = _make_filename();
    strncpy(_current_path, path.c_str(), REC_MAX_PATH - 1);

    // Create file with empty WAV header
    bool file_ok = false;
    if (spi_mutex && xSemaphoreTake(spi_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        FsFile file = sd.open(_current_path, O_WRITE | O_CREAT | O_TRUNC);
        if (file) {
            WAVHeader hdr;
            file.write((uint8_t*)&hdr, sizeof(hdr));
            file.close();
            file_ok = true;
        }
        xSemaphoreGive(spi_mutex);
    }

    if (!file_ok) {
        Serial.println("[RECORDING] Failed to create recording file");
        return false;
    }

    portENTER_CRITICAL(&_rec_mux);
    _bytes_written = 0;
    _start_ms      = millis();
    _state         = REC_ACTIVE;
    portEXIT_CRITICAL(&_rec_mux);

    Serial.printf("[RECORDING] Started: %s\n", _current_path);
    return true;
}

bool recording_stop(RecordingInfo* out_info) {
    if (_state != REC_ACTIVE) return false;

    portENTER_CRITICAL(&_rec_mux);
    _state = REC_STOPPING;
    portEXIT_CRITICAL(&_rec_mux);

    // Wait for task to finish current chunk
    uint32_t wait_start = millis();
    while (_state == REC_STOPPING && millis() - wait_start < 2000) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    // Fill out info
    if (out_info) {
        strncpy(out_info->path, _current_path, REC_MAX_PATH - 1);
        out_info->bytes_written  = _bytes_written;
        out_info->duration_ms    = millis() - _start_ms;
        out_info->queued_for_ai  = false;
    }

    strncpy(_last_path, _current_path, REC_MAX_PATH - 1);

    Serial.printf("[RECORDING] Stopped: %s (%u bytes, %ums)\n",
                  _current_path, _bytes_written, millis() - _start_ms);
    return true;
}

RecordingState recording_get_state() { return _state; }

uint32_t recording_get_duration_ms() {
    if (_state == REC_IDLE) return 0;
    return millis() - _start_ms;
}

const char* recording_get_last_path() { return _last_path; }

// ─────────────────────────────────────────────────────────────────
// CORE 0 TASK — The Audio Ghost Engine
// Mirrors wardrive_task() pattern from Pisces Moon OS
// ─────────────────────────────────────────────────────────────────
void recording_task(void* pvParameters) {
    Serial.println("[RECORDING] Audio Ghost Engine on Core 0");

    uint8_t* i2s_buf = (uint8_t*)ps_malloc(REC_CHUNK_BYTES);
    if (!i2s_buf) {
        Serial.println("[RECORDING] FATAL: ps_malloc failed for I2S buffer");
        vTaskDelete(NULL);
        return;
    }

    bool mic_active = false;

    for (;;) {
        RecordingState state = _state;

        if (state == REC_ACTIVE) {
            // Init I2S if not already running
            if (!mic_active) {
                if (_i2s_init()) {
                    mic_active = true;
                    Serial.println("[RECORDING] Mic started");
                } else {
                    Serial.println("[RECORDING] Mic init failed");
                    portENTER_CRITICAL(&_rec_mux);
                    _state = REC_IDLE;
                    portEXIT_CRITICAL(&_rec_mux);
                    vTaskDelay(pdMS_TO_TICKS(100));
                    continue;
                }
            }

            // Read I2S chunk
            size_t bytes_read = 0;
            i2s_read(REC_I2S_PORT, i2s_buf, REC_CHUNK_BYTES, &bytes_read, pdMS_TO_TICKS(50));

            if (bytes_read > 0 && !sd_in_use) {
                // SPI Bus Treaty — acquire mutex, write chunk, release
                if (spi_mutex && xSemaphoreTake(spi_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    FsFile file = sd.open(_current_path, O_WRITE | O_APPEND);
                    if (file) {
                        file.write(i2s_buf, bytes_read);
                        file.close();
                        portENTER_CRITICAL(&_rec_mux);
                        _bytes_written += bytes_read;
                        portEXIT_CRITICAL(&_rec_mux);
                    }
                    xSemaphoreGive(spi_mutex);
                }
            }

        } else if (state == REC_STOPPING) {
            // Flush I2S and finalize WAV header
            if (mic_active) {
                _i2s_deinit();
                mic_active = false;
            }

            // Update WAV header with final size
            if (spi_mutex && xSemaphoreTake(spi_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
                FsFile file = sd.open(_current_path, O_WRITE);
                if (file) {
                    _write_wav_header(file, _bytes_written);
                    file.close();
                    Serial.printf("[RECORDING] WAV header finalized: %u bytes\n", _bytes_written);
                }
                xSemaphoreGive(spi_mutex);
            }

            portENTER_CRITICAL(&_rec_mux);
            _state = REC_DONE;
            portEXIT_CRITICAL(&_rec_mux);

        } else {
            // IDLE or DONE — stop mic if running
            if (mic_active) {
                _i2s_deinit();
                mic_active = false;
            }
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}
