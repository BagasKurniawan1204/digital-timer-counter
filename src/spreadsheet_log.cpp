#include <Arduino.h>
#include <WiFi.h>
#include <math.h>
#include <ESP_Google_Sheet_Client.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "globals.h"
#include "spreadsheet_log.h"
#include "counter_operation.h"

#define WIFI_SSID "bagas"
#define WIFI_PASSWORD "inderhunt"

#define PROJECT_ID "datalogtimerandesp32"
#define CLIENT_EMAIL "datalogging@datalogtimerandesp32.iam.gserviceaccount.com"

const char PRIVATE_KEY[] PROGMEM = "-----BEGIN PRIVATE KEY-----\nMIIEvgIBADANBgkqhkiG9w0BAQEFAASCBKgwggSkAgEAAoIBAQDJbI8hDSW6qmh/\nLe5Gp1+Hqw616d8L/fmikOI1FrnMfQrEJRov271JlLsJUN0GewLrGsqNfpmtMa2S\nhvjTdG8Vx8y8WI3U598nfSEWHrDfYaF0rGjzdN0+qnYydbhJGw517FPEkxHbwUsN\naPKpvX8GZfx/3uHYyUxMBDeZWJLiqzdWSLhHzJwdpASuTp3+/7Zzu9V4c9TjKmdE\nx4ZopIsDM7x3LDjs7sEe3Oz9Nl8xmU/s8nafCo5qoT0YRhClbfHzIsy0ywGmI2d4\n2GSvR0W3qhxhgGTlR0vCVMZaH5K5qq78Y9PdQmo1POkTUUx35qy6kA2rYt7cmyIy\nOfDoSdbNAgMBAAECggEAGrPbF65xWYPnfYYTyBG+mYg6V4XNItRac0w8yNKd8reC\n+rTRJMC/S9Fn/ZisQdnYmF1sC9eu53sl/75dSf8YuOz3aI1ezsz8fvTY3YtXtw1v\nhOhUXi4YeeB0R0U8QejQgR6W46um8ENO6qoGktfDJBCCKbDdBPXOuRHNYyAPEksU\neFx9N+cPPabsxT9Sps8Yi6ZSKJWzWKlukz4pbnNPJRb6w/cCE5ZUTdALPsqzg2K0\nFrJN7nKDaauUXqQ8aqQWC1JVWozBuzhIHL2ZibruYS7otska0wfzgTioLalfZox9\nZwmTsdT4Cyxbp4ZK4sVvZCPqhuD89RfOTut48iqdywKBgQDx/fXgCbHX/72Y1Og/\n2AESbrGVT4k0uYCekZx8hdQW86n3vjqiToRoDNhhBuvYVrE1x3LtHBKGHyZ7vNV7\nbi2ydX5nalDiaw61H/SnOcJGar6VqVy2YQyUNCnuR98CoFljxPwURRfasoUWe9K7\noENA9mS+5vgxLDygYkP8/hsjiwKBgQDVFW25fmeO9H3Py6qQPVO56kAJFP9F55Gx\nqGB2yWNtv45AXt1sSfw1qJ1kRawhsqzVVAHsK0CKfK/YhnI+9unLzDmZI4A80xPa\nHrG1oZX8WHyLBnu508SVmy0svEzboXmutJbn4tTiRk6gZCNI5BU7Jq/4dt5LqkRZ\ngwX4nsdaBwKBgBv7O6UOOew5/Bhh9gD73xPcjNgw/DKGiKLNP4T+jImi3zJwYqNF\n5PWLFrIRdM/tJkyLpXRZXKL5kx+XtC+zi8Eo6NbYakXkDy1OZqG3gglWancvwDKu\ngh/Y8EhHMzhAhlWM/4DwhFObdNwmsTVU0LIAS6HvXx+Ad/oroqTsVQMFAoGBAK3Z\ng+JZrWhHNfa5tnlkb9E5u/Es/nEsVARc2gdQnBzIsuj1/TYCzxGAdpl+9sevna+X\nkNH9H/VdcaL5XnDcxzeNcljtTA1UMdg+PsNwCI0QLNzI911P2A4vwxXCs/plrn/J\nu22J9iJ/NltKGDe6T1Apal0PrqKRUp6tdcu4z/E1AoGBAIFgSS7TYx03cOAjakVF\nOlCjUNJRCS80VCZhPvcSBOhTXhPJ4yUjThJGf9rdsKrKL1pTnMwTZpE9QAEQzTSl\n//t7wYT4OA5YOUtddOTaiMx/pPDFuuAIgzVuPS+PPJbWQr8j0fyAGiz7h1j5nACP\nrMTGPm2F8OCkcgmXn/Nartew\n-----END PRIVATE KEY-----\n";

const char spreadsheetId[] = "1Zw8hMmAMq55zJyS9zg3prypfiQbBBF8JioIg2Wkw63Q";

namespace {

// Dua sheet (tab) terpisah di dalam 1 file spreadsheet yang sama.
// PENTING: nama tab sengaja dibuat TANPA spasi, tanda kurung, atau karakter khusus lain.
constexpr const char *kSheet1Range = "Channel1_ESP32!A1";
constexpr const char *kSheet2Range = "Channel2_Autonics!A1";

// Target interval pulse generator yang diuji: tepat 1 detik (1000 ms).
constexpr unsigned long kExpectedIntervalMs = 1000UL;

// ============================================================================
// KONFIGURASI BATCHING
// ============================================================================
// Data tidak langsung dikirim ke Google Sheets tiap ada perubahan. Sebaliknya,
// disimpan dulu di buffer memori, lalu dikirim sekaligus (1 API call berisi
// banyak baris) dan dikirim hanya ketika buffer penuh (kMaxBufferedRows).
//
// Kenapa ini penting untuk kuota Google Sheets API:
//   - Tanpa batching: 1 perubahan count = 1 write request. Kalau ch1 & ch2
//     berubah cepat, jumlah request/menit gampang lewat limit gratis
//     (default 60 write request/menit/user).
//   - Dengan batching: 40 perubahan count digabung menjadi 1 write request.
//
// Trade-off yang perlu disadari:
//   - Data baru muncul setelah buffer berisi kMaxBufferedRows entri, jadi ini
//     bukan pencatatan real-time. Timestamp tetap dicatat saat perubahan terjadi.
//   - Kalau ESP32 mati/reset sebelum sempat flush, baris yang masih ada di
//     buffer (belum terkirim) akan hilang.
constexpr size_t kMaxBufferedRows = 20;              // batas aman ukuran buffer per channel
// Setelah sebuah perubahan disimpan, abaikan perubahan berikutnya selama 200 ms.
constexpr unsigned long kChangeCooldownMs = 200UL;
constexpr size_t kLogQueueLength = 128;
constexpr uint32_t kLoggerTaskStackWords = 8192;
constexpr UBaseType_t kLoggerTaskPriority = 1;

struct LogEntry {
    unsigned long startMs;
    unsigned long endMs;
    unsigned long durationMs;
    double errorPercent;
};

struct ChannelLogger {
    const char *sheetRange;
    LogEntry buffer[kMaxBufferedRows];
    size_t bufferCount;

    explicit ChannelLogger(const char *range)
        : sheetRange(range),
          bufferCount(0) {}
};

struct CaptureState {
    int32_t lastObservedCount;
    unsigned long lastSavedMs;
};

struct QueuedLogEntry {
    uint8_t channel;
    LogEntry entry;
};

bool loggerEnabled = false;
bool loggerInitialized = false;

ChannelLogger ch1Logger(kSheet1Range);
ChannelLogger ch2Logger(kSheet2Range);
CaptureState ch1Capture = {0, 0};
CaptureState ch2Capture = {0, 0};
QueueHandle_t logEntryQueue = nullptr;
TaskHandle_t loggerTaskHandle = nullptr;
volatile uint32_t droppedLogEntries = 0;

void tokenStatusCallback(TokenInfo info) {
    if (info.status == token_status_error) {
        GSheet.printf("Token info: type = %s, status = %s\n", GSheet.getTokenType(info).c_str(), GSheet.getTokenStatus(info).c_str());
        GSheet.printf("Token error: %s\n", GSheet.getTokenError(info).c_str());
        return;
    }

    GSheet.printf("Token info: type = %s, status = %s\n", GSheet.getTokenType(info).c_str(), GSheet.getTokenStatus(info).c_str());
}

String formatElapsed(unsigned long elapsedMs) {
    char buffer[24];
    snprintf(buffer, sizeof(buffer), "%lus:%03lums", elapsedMs / 1000UL, elapsedMs % 1000UL);
    return String(buffer);
}

void readCounts(int32_t &ch1, int32_t &ch2) {
    ch1= pcnt_ch1_get_count();
    ch2= pcnt_ch2_get_count();
}

// Menghitung selisih durasi aktual terhadap target 1 detik, dalam persen.
double calcErrorPercent(double durationMs) {
    double errorPercent = abs((static_cast<double>(durationMs) - static_cast<double>(kExpectedIntervalMs)) 
                          / static_cast<double>(kExpectedIntervalMs));
    return errorPercent;
}

// Mengirim SEMUA baris yang ada di buffer channel ini dalam 1 API call saja.
// Ini yang membuat batching hemat kuota: berapa pun bufferCount-nya (1 s/d
// kMaxBufferedRows), tetap cuma 1 write request ke Google Sheets.
bool flushChannelLogger(ChannelLogger &logger) {
    if (logger.bufferCount == 0) {
        return true;  // tidak ada apa-apa untuk dikirim, anggap sukses
    }

    FirebaseJson response;
    FirebaseJson valueRange;
    valueRange.add("majorDimension", "ROWS");

    for (size_t i = 0; i < logger.bufferCount; ++i) {
        const LogEntry &entry = logger.buffer[i];
        String rowPrefix = "values/[" + String(i) + "]/";
        valueRange.set((rowPrefix + "[0]").c_str(), formatElapsed(entry.startMs));
        valueRange.set((rowPrefix + "[1]").c_str(), formatElapsed(entry.endMs));
        valueRange.set((rowPrefix + "[2]").c_str(), formatElapsed(entry.durationMs));
        valueRange.set((rowPrefix + "[3]").c_str(), entry.errorPercent);
    }

    bool success = GSheet.values.append(&response, spreadsheetId, logger.sheetRange, &valueRange);
    if (success) {
        Serial.printf("[SheetLogger] Flushed %u row(s) to %s\n",
                      (unsigned)logger.bufferCount, logger.sheetRange);
        response.toString(Serial, true);
        valueRange.clear();
        logger.bufferCount = 0;
        return true;
    }

    Serial.printf("[SheetLogger] Flush FAILED for %s (buffer tetap disimpan, akan dicoba lagi): %s\n",
                  logger.sheetRange, GSheet.errorReason().c_str());
    return false;
}

// Menambahkan satu entri baru ke buffer channel. Kalau buffer sudah penuh,
// paksa flush dulu supaya ada tempat -- ini jaga-jaga saja, karena flush
// yang memicu request API.
void bufferChannelEntry(ChannelLogger &logger, const LogEntry &entry) {
    if (logger.bufferCount >= kMaxBufferedRows) {
        Serial.printf("[SheetLogger] Buffer %s penuh, flush paksa sebelum menambah entri baru\n",
                      logger.sheetRange);
        flushChannelLogger(logger);
    }

    if (logger.bufferCount < kMaxBufferedRows) {
        logger.buffer[logger.bufferCount++] = entry;
    } else {
        // Masih penuh (flush paksa di atas gagal, misal WiFi lagi putus).
        // Daripada data baru hilang total tanpa jejak, buang entri PALING LAMA
        // dan simpan yang baru -- prioritaskan data terbaru untuk pengujian akurasi.
        Serial.printf("[SheetLogger] Buffer %s masih penuh setelah flush paksa, entri terlama dibuang\n",
                      logger.sheetRange);
        for (size_t i = 1; i < kMaxBufferedRows; ++i) {
            logger.buffer[i - 1] = logger.buffer[i];
        }
        logger.buffer[kMaxBufferedRows - 1] = entry;
    }
} 

void captureChannel(uint8_t channel, CaptureState &state,
                    int32_t currentCount, unsigned long nowMs) {
    if (currentCount == state.lastObservedCount) {
        return;
    }

    state.lastObservedCount = currentCount;
    if (nowMs - state.lastSavedMs < kChangeCooldownMs || logEntryQueue == nullptr) {
        return;
    }

    QueuedLogEntry queued = {
        .channel = channel,
        .entry = {
            .startMs = state.lastSavedMs,
            .endMs = nowMs,
            .durationMs = nowMs - state.lastSavedMs,
            .errorPercent = calcErrorPercent(nowMs - state.lastSavedMs),
        },
    };

    if (xQueueSend(logEntryQueue, &queued, 0) == pdTRUE) {
        state.lastSavedMs = nowMs;
    } else {
        ++droppedLogEntries;
    }
}

void spreadsheetLoggerTask(void * /*pvParameters*/) {
    QueuedLogEntry queued;
    Serial.println("[SheetLogger] Background upload task started on Core 0");

    for (;;) {
        if (xQueueReceive(logEntryQueue, &queued, pdMS_TO_TICKS(1000)) == pdTRUE) {
            ChannelLogger &logger = (queued.channel == 0) ? ch1Logger : ch2Logger;
            bufferChannelEntry(logger, queued.entry);
        }

        // WiFi/API hanya dipanggil setelah buffer penuh. Counter task di Core 1
        // tetap merekam timestamp ke queue saat request ini berjalan.
        if (WiFi.status() == WL_CONNECTED && GSheet.ready()) {
            if (ch1Logger.bufferCount >= kMaxBufferedRows) {
                flushChannelLogger(ch1Logger);
            }
            if (ch2Logger.bufferCount >= kMaxBufferedRows) {
                flushChannelLogger(ch2Logger);
            }
        }
    }
}

}  // namespace

void spreadsheet_log_init() {
    if (loggerInitialized) {
        return;
    }

    loggerInitialized = true;
    loggerEnabled = true;

    WiFi.setAutoReconnect(true);
    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    Serial.println("Spreadsheet logger Wi-Fi connection started");

    GSheet.setTokenCallback(tokenStatusCallback);
    GSheet.setPrerefreshSeconds(10 * 60);
    GSheet.begin(CLIENT_EMAIL, PROJECT_ID, PRIVATE_KEY);

    logEntryQueue = xQueueCreate(kLogQueueLength, sizeof(QueuedLogEntry));
    if (logEntryQueue == nullptr) {
        Serial.println("[SheetLogger] ERROR: failed to create log queue");
        return;
    }

    int32_t initialCh1 = 0;
    int32_t initialCh2 = 0;
    readCounts(initialCh1, initialCh2);

    unsigned long nowMs = millis();
    ch1Capture = {initialCh1, nowMs};
    ch2Capture = {initialCh2, nowMs};

    if (xTaskCreatePinnedToCore(
            spreadsheetLoggerTask,
            "SheetLogger",
            kLoggerTaskStackWords,
            nullptr,
            kLoggerTaskPriority,
            &loggerTaskHandle,
            0) != pdPASS) {
        Serial.println("[SheetLogger] ERROR: failed to create upload task");
    }
}

void spreadsheet_log_loop() {
    if (!loggerInitialized) {
        return;
    }

    // Status saja; request API dijalankan oleh spreadsheetLoggerTask.
    static unsigned long lastStatusPrintMs = 0;
    unsigned long nowStatusMs = millis();
    if (nowStatusMs - lastStatusPrintMs > 5000) {
        lastStatusPrintMs = nowStatusMs;
        Serial.printf("[SheetLogger] WiFi=%d GSheet.ready=%d ch1Buf=%u ch2Buf=%u queue=%u dropped=%lu\n",
                      (int)WiFi.status(),
                      (int)GSheet.ready(),
                      (unsigned)ch1Logger.bufferCount,
                      (unsigned)ch2Logger.bufferCount,
                      logEntryQueue ? (unsigned)uxQueueMessagesWaiting(logEntryQueue) : 0U,
                      (unsigned long)droppedLogEntries);
    }
}

void spreadsheet_log_capture_counts(int32_t ch1, int32_t ch2, unsigned long nowMs) {
    if (!loggerInitialized || logEntryQueue == nullptr) {
        return;
    }

    captureChannel(0, ch1Capture, ch1, nowMs);
    captureChannel(1, ch2Capture, ch2, nowMs);
}
