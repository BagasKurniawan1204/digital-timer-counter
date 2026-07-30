#include <Arduino.h>
#include <WiFi.h>
#include <math.h>
#include <ESP_Google_Sheet_Client.h>

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

struct LogEntry {
    unsigned long startMs;
    unsigned long endMs;
    unsigned long durationMs;
    double errorPercent;
};

struct ChannelLogger {
    const char *sheetRange;
    int32_t lastLoggedCount;
    unsigned long intervalStartMs;
    unsigned long lastFlushMs;

    LogEntry buffer[kMaxBufferedRows];
    size_t bufferCount;

    explicit ChannelLogger(const char *range)
        : sheetRange(range),
          lastLoggedCount(0),
          intervalStartMs(0),
          lastFlushMs(0),
          bufferCount(0) {}
};

bool loggerEnabled = false;
bool loggerInitialized = false;

ChannelLogger ch1Logger(kSheet1Range);
ChannelLogger ch2Logger(kSheet2Range);

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
bool flushChannelLogger(ChannelLogger &logger, unsigned long nowMs) {
    if (logger.bufferCount == 0) {
        logger.lastFlushMs = nowMs;
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
        logger.lastFlushMs = nowMs;
        return true;
    }

    Serial.printf("[SheetLogger] Flush FAILED for %s (buffer tetap disimpan, akan dicoba lagi): %s\n",
                  logger.sheetRange, GSheet.errorReason().c_str());
    return false;
}

// Menambahkan satu entri baru ke buffer channel. Kalau buffer sudah penuh,
// paksa flush dulu supaya ada tempat -- ini jaga-jaga saja, karena flush
// yang memicu request API.
void bufferChannelEntry(ChannelLogger &logger, const LogEntry &entry, unsigned long nowMs) {
    if (logger.bufferCount >= kMaxBufferedRows) {
        Serial.printf("[SheetLogger] Buffer %s penuh, flush paksa sebelum menambah entri baru\n",
                      logger.sheetRange);
        flushChannelLogger(logger, nowMs);
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

// Mengecek satu channel: perubahan count hanya dicatat jika cooldown 200 ms
// sudah berakhir. Perubahan yang terjadi selama cooldown tetap ditandai telah
// terlihat agar tidak ikut dicatat setelah cooldown berakhir.
// Buffer di-flush hanya saat penuh.
void processChannel(ChannelLogger &logger, int32_t currentCount, unsigned long nowMs) {
    if (currentCount != logger.lastLoggedCount) {
        // Selalu perbarui nilai terakhir yang diamati. Dengan demikian,
        // perubahan di dalam cooldown tidak akan tersimpan belakangan.
        logger.lastLoggedCount = currentCount;

        if (nowMs - logger.intervalStartMs >= kChangeCooldownMs) {
            LogEntry entry;
            entry.startMs = logger.intervalStartMs;
            entry.endMs = nowMs;
            entry.durationMs = nowMs - logger.intervalStartMs;
            entry.errorPercent = calcErrorPercent(entry.durationMs);

            bufferChannelEntry(logger, entry, nowMs);
            logger.intervalStartMs = nowMs;
        }
    }

    bool bufferFull = (logger.bufferCount >= kMaxBufferedRows);
    if (bufferFull) {
        flushChannelLogger(logger, nowMs);
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

    int32_t initialCh1 = 0;
    int32_t initialCh2 = 0;
    readCounts(initialCh1, initialCh2);

    unsigned long nowMs = millis();
    ch1Logger.lastLoggedCount = initialCh1;
    ch1Logger.intervalStartMs = nowMs;
    ch1Logger.lastFlushMs = nowMs;

    ch2Logger.lastLoggedCount = initialCh2;
    ch2Logger.intervalStartMs = nowMs;
    ch2Logger.lastFlushMs = nowMs;
}

void spreadsheet_log_loop() {
    if (!loggerInitialized) {
        return;
    }

    // Debug: cetak status WiFi & GSheet tiap 5 detik, supaya kalau koneksi
    // gagal diam-diam (misal SSID/password salah), kita bisa lihat langsung
    // di Serial Monitor, bukan cuma "tidak ada yang terjadi".
    static unsigned long lastStatusPrintMs = 0;
    unsigned long nowStatusMs = millis();
    if (nowStatusMs - lastStatusPrintMs > 5000) {
        lastStatusPrintMs = nowStatusMs;
        Serial.printf("[SheetLogger] WiFi status=%d (3=connected) IP=%s GSheet.ready=%d ch1Buf=%u ch2Buf=%u\n",
                      (int)WiFi.status(),
                      WiFi.localIP().toString().c_str(),
                      (int)GSheet.ready(),
                      (unsigned)ch1Logger.bufferCount,
                      (unsigned)ch2Logger.bufferCount);
    }

    if (WiFi.status() != WL_CONNECTED) {
        return;
    }

    if (!GSheet.ready()) {
        return;
    }

    int32_t currentCh1 = 0;
    int32_t currentCh2 = 0;
    readCounts(currentCh1, currentCh2);

    unsigned long nowMs = millis();

    // Channel 1 & 2 diproses SENDIRI-SENDIRI, tidak saling tergantung --
    // baik soal deteksi perubahan maupun soal kapan masing-masing di-flush.
    processChannel(ch1Logger, currentCh1, nowMs);
    processChannel(ch2Logger, currentCh2, nowMs);
}
