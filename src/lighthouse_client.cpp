// Pisces Moon OS — PocketMind Branch
// Copyright (C) 2026 Eric Becker / Fluid Fortune
// SPDX-License-Identifier: AGPL-3.0-or-later
// fluidfortune.com

/**
 * LIGHTHOUSE CLIENT v1.0
 *
 * HTTP client that connects the ESP32 to The Lighthouse server.
 * Handles all communication between the device and the AI bridge.
 *
 * SPI Bus Treaty compliance:
 *   lighthouse_transcribe() reads from SD. It acquires spi_mutex
 *   before opening the file and releases immediately after reading.
 *   Never holds the mutex during HTTP transmission.
 */

#include "lighthouse_client.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "SdFat.h"
#include <freertos/semphr.h>

extern volatile bool wifi_in_use;
extern SemaphoreHandle_t spi_mutex;
extern SdFat sd;

static String _base_url = "";

// ─────────────────────────────────────────────────────────────────
// INIT
// ─────────────────────────────────────────────────────────────────
void lighthouse_init() {
    _base_url = String("http://") + LIGHTHOUSE_HOST + ":" + LIGHTHOUSE_PORT;
    Serial.printf("[LIGHTHOUSE] Server: %s\n", _base_url.c_str());
}

// ─────────────────────────────────────────────────────────────────
// HEALTH CHECK
// ─────────────────────────────────────────────────────────────────
bool lighthouse_health_check() {
    if (WiFi.status() != WL_CONNECTED) return false;

    HTTPClient http;
    WiFiClient client;
    http.begin(client, _base_url + "/health");
    http.setTimeout(5000);

    wifi_in_use = true;
    int code = http.GET();
    wifi_in_use = false;
    http.end();

    return (code == 200);
}

// ─────────────────────────────────────────────────────────────────
// CHAT
// ─────────────────────────────────────────────────────────────────
String lighthouse_chat(const String& message, const String& target) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[LIGHTHOUSE] WiFi not connected");
        return "";
    }

    // Build JSON payload
    JsonDocument doc;
    doc["message"]   = message;
    doc["target"]    = target;
    doc["device_id"] = "pocketmind-tdeck";

    String payload;
    serializeJson(doc, payload);

    HTTPClient http;
    WiFiClient client;
    http.begin(client, _base_url + "/chat");
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(LIGHTHOUSE_TIMEOUT);

    Serial.printf("[LIGHTHOUSE] → %s: %s\n", target.c_str(), message.substring(0, 60).c_str());

    wifi_in_use = true;
    int code = http.POST(payload);
    wifi_in_use = false;

    if (code != 200) {
        Serial.printf("[LIGHTHOUSE] HTTP error: %d\n", code);
        http.end();
        return "";
    }

    String response_body = http.getString();
    http.end();

    // Parse response
    JsonDocument resp;
    if (deserializeJson(resp, response_body) != DeserializationError::Ok) {
        Serial.println("[LIGHTHOUSE] Failed to parse response JSON");
        return "";
    }

    String response = resp["response"].as<String>();
    Serial.printf("[LIGHTHOUSE] ← %s\n", response.substring(0, 60).c_str());

    return response;
}

// ─────────────────────────────────────────────────────────────────
// TRANSCRIBE
// Reads WAV from SD card, sends to /transcribe endpoint
// SPI Bus Treaty: acquire mutex to read file, release before HTTP
// ─────────────────────────────────────────────────────────────────
String lighthouse_transcribe(const char* wav_path) {
    if (WiFi.status() != WL_CONNECTED) return "";

    // Read WAV file into PSRAM buffer
    // Treaty: take mutex, read file, release mutex, THEN send HTTP
    uint8_t* wav_data = nullptr;
    size_t   wav_size = 0;

    if (spi_mutex && xSemaphoreTake(spi_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        FsFile file = sd.open(wav_path, O_READ);
        if (file) {
            wav_size = file.size();
            wav_data = (uint8_t*)ps_malloc(wav_size);
            if (wav_data) {
                file.read(wav_data, wav_size);
                Serial.printf("[LIGHTHOUSE] Read %u bytes from %s\n", wav_size, wav_path);
            }
            file.close();
        } else {
            Serial.printf("[LIGHTHOUSE] Cannot open %s\n", wav_path);
        }
        xSemaphoreGive(spi_mutex);
    }

    if (!wav_data || wav_size == 0) {
        if (wav_data) free(wav_data);
        return "";
    }

    // Now send HTTP — mutex is released
    HTTPClient http;
    WiFiClient client;
    http.begin(client, _base_url + "/transcribe");
    http.setTimeout(60000);  // Transcription can take time for long files

    // Multipart form upload
    String boundary = "----PocketMindBoundary";
    String header = "--" + boundary + "\r\n";
    header += "Content-Disposition: form-data; name=\"file\"; filename=\"recording.wav\"\r\n";
    header += "Content-Type: audio/wav\r\n\r\n";
    String footer = "\r\n--" + boundary + "--\r\n";

    size_t total_len = header.length() + wav_size + footer.length();

    http.addHeader("Content-Type", "multipart/form-data; boundary=" + boundary);
    http.addHeader("Content-Length", String(total_len));

    // Stream the multipart body
    wifi_in_use = true;

    // Build complete body in PSRAM
    uint8_t* body = (uint8_t*)ps_malloc(total_len);
    if (body) {
        memcpy(body, header.c_str(), header.length());
        memcpy(body + header.length(), wav_data, wav_size);
        memcpy(body + header.length() + wav_size, footer.c_str(), footer.length());

        free(wav_data);
        wav_data = nullptr;

        int code = http.POST(body, total_len);
        free(body);

        wifi_in_use = false;

        if (code != 200) {
            Serial.printf("[LIGHTHOUSE] Transcribe HTTP error: %d\n", code);
            http.end();
            return "";
        }

        String resp_body = http.getString();
        http.end();

        JsonDocument resp;
        if (deserializeJson(resp, resp_body) == DeserializationError::Ok) {
            String transcript = resp["transcript"].as<String>();
            Serial.printf("[LIGHTHOUSE] Transcript: %s\n", transcript.substring(0, 80).c_str());
            return transcript;
        }
    } else {
        free(wav_data);
        wifi_in_use = false;
        Serial.println("[LIGHTHOUSE] ps_malloc failed for body buffer");
    }

    http.end();
    return "";
}

// ─────────────────────────────────────────────────────────────────
// EVALUATE
// ─────────────────────────────────────────────────────────────────
String lighthouse_evaluate(const String& transcript, const String& target) {
    if (WiFi.status() != WL_CONNECTED) return "";

    JsonDocument doc;
    doc["transcript"] = transcript;
    doc["target"]     = target;

    String payload;
    serializeJson(doc, payload);

    HTTPClient http;
    WiFiClient client;
    http.begin(client, _base_url + "/evaluate");
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(LIGHTHOUSE_TIMEOUT);

    wifi_in_use = true;
    int code = http.POST(payload);
    wifi_in_use = false;

    if (code != 200) {
        Serial.printf("[LIGHTHOUSE] Evaluate HTTP error: %d\n", code);
        http.end();
        return "";
    }

    String result = http.getString();
    http.end();
    return result;
}

// ─────────────────────────────────────────────────────────────────
// SUMMARIZE
// ─────────────────────────────────────────────────────────────────
String lighthouse_summarize(const String& transcripts_json, const String& target) {
    if (WiFi.status() != WL_CONNECTED) return "";

    // Build request body
    String payload = "{\"transcripts\":" + transcripts_json + ",\"target\":\"" + target + "\"}";

    HTTPClient http;
    WiFiClient client;
    http.begin(client, _base_url + "/summarize");
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(60000);  // Summary can take longer

    wifi_in_use = true;
    int code = http.POST(payload);
    wifi_in_use = false;

    if (code != 200) {
        Serial.printf("[LIGHTHOUSE] Summarize HTTP error: %d\n", code);
        http.end();
        return "";
    }

    String resp_body = http.getString();
    http.end();

    JsonDocument resp;
    if (deserializeJson(resp, resp_body) == DeserializationError::Ok) {
        return resp["summary"].as<String>();
    }

    return "";
}

// ─────────────────────────────────────────────────────────────────
// HELPERS
// ─────────────────────────────────────────────────────────────────
const char* lighthouse_target_name(const String& target) {
    if (target == TARGET_CLAUDE)   return "Claude";
    if (target == TARGET_GEMINI)   return "Gemini";
    if (target == TARGET_CHATGPT)  return "ChatGPT";
    if (target == TARGET_DEEPSEEK) return "DeepSeek";
    if (target == TARGET_OLLAMA)   return "Ollama";
    return "AI";
}
