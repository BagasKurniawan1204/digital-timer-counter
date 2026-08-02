#include <Arduino.h>
#include <WiFi.h>
#include <math.h>
#include <ESP_Google_Sheet_Client.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <esp_task_wdt.h>
#include <Preferences.h>

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

// Watchdog: SheetLogger task didaftarkan ke Task Watchdog Timer sendiri
// (terpisah dari IDLE task) supaya proses flush yang lama tetap "sah" bagi
// watchdog selama kita eksplisit mereset di titik-titik aman, TANPA
// menahan CPU sampai membuat IDLE0 starve dan memicu abort() paksa.
constexpr uint32_t kLoggerWdtTimeoutSec = 15; // lebih longgar dari default 5s

// Kalau WiFi/GSheet belum siap, jangan spam percobaan flush setiap loop --
// beri jeda supaya task ini tetap sering yield ke scheduler/idle task.
constexpr TickType_t kNotReadyBackoffTicks = pdMS_TO_TICKS(200);

// Kalau GSheet terus-menerus TIDAK ready selama ini, coba soft-recovery
// (WiFi + token GSheet direset ulang) TANPA esp_restart(). Ini sengaja
// tidak me-reboot chip supaya millis()/uptime tetap jalan terus -- durasi
// yang dicatat di log (formatElapsed relatif terhadap lastSavedMs) tidak
// terganggu sama sekali oleh soft-recovery ini.
constexpr unsigned long kSoftRecoveryAfterMs = 3UL * 60UL * 1000UL;   // 3 menit stuck
constexpr unsigned long kSoftRecoveryCooldownMs = 3UL * 60UL * 1000UL; // jangan spam recovery

// ============================================================================
// VIRTUAL CLOCK OFFSET (supaya elapsed time TIDAK reset ke 0 tiap reboot)
// ============================================================================
// Nilai yang tercatat di kolom sheet (mis. "60787s:233ms") dihitung dari
// millis() sejak boot. Supaya reflash/restart tidak membuat waktu ini
// "loncat balik ke 0" dan merusak kontinuitas pengujian yang sudah berjalan
// lama, kita simpan sebuah OFFSET (dalam ms) di NVS (flash, persisten lintas
// reboot & reflash -- selama tidak full-chip-erase). Setiap kali flush,
// nilai yang ditulis ke sheet = offset (dimuat sekali saat boot ini) +
// millis() saat ini. Offset itu sendiri di-update & disimpan ulang secara
// berkala supaya boot BERIKUTNYA lagi juga melanjutkan dari titik yang benar.
//
// CARA PAKAI SEKALI SAJA (saat pertama kali flash firmware dengan fitur ini):
//   1. Lihat nilai elapsed TERAKHIR yang sempat tercatat di sheet sebelum
//      reflash (mis. "60787s:233ms" -> 60787*1000 + 233 = 60787233 ms).
//   2. Set kManualSeedMs di bawah ini ke nilai tsb.
//   3. Flash sekali. Setelah itu offset otomatis tersimpan di NVS dan akan
//      terus dilanjutkan secara otomatis pada semua reboot/reflash
//      berikutnya -- kManualSeedMs TIDAK dipakai lagi setelah pertama kali
//      (hanya dipakai kalau NVS kosong/belum pernah diisi).
constexpr unsigned long kManualSeedMs = 60787233UL;  // <-- GANTI nilai ini sebelum flash pertama!
constexpr const char *kClockNvsNamespace = "sheetclock";
constexpr const char *kClockNvsKey = "vOffsetMs";
constexpr unsigned long kClockSaveIntervalMs = 30UL * 1000UL; // simpan tiap 30 detik

Preferences clockPrefs;
unsigned long virtualClockOffsetMs = 0;  // diisi sekali di init(), tetap sepanjang boot ini

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
bool wdtSubscribed = false;

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

// Apakah aman untuk melakukan network call ke Google Sheets sekarang?
// Semua jalur yang bisa memicu flushChannelLogger() HARUS lewat cek ini
// terlebih dahulu -- ini yang sebelumnya hilang di jalur "forced flush"
// dan menyebabkan task memblokir CPU (task_wdt abort / reboot).
bool sheetsReadyForNetworkCall() {
    return (WiFi.status() == WL_CONNECTED) && GSheet.ready();
}

// Mengirim SEMUA baris yang ada di buffer channel ini dalam 1 API call saja.
// Ini yang membuat batching hemat kuota: berapa pun bufferCount-nya (1 s/d
// kMaxBufferedRows), tetap cuma 1 write request ke Google Sheets.
//
// PENTING: fungsi ini melakukan network call yang BLOCKING (TLS handshake +
// HTTP request). Selama panggilan ini task tidak yield, jadi kita daftarkan
// task ke Task Watchdog Timer dengan timeout yang lebih longgar dan reset
// segera setelah panggilan selesai -- supaya panggilan lambat/hang tidak
// membuat IDLE0 starve dan memicu abort() paksa oleh watchdog default.
bool flushChannelLogger(ChannelLogger &logger) {
    if (logger.bufferCount == 0) {
        return true;  // tidak ada apa-apa untuk dikirim, anggap sukses
    }

    if (!sheetsReadyForNetworkCall()) {
        // Jangan pernah memanggil GSheet.values.append() kalau WiFi/token
        // belum siap -- inilah akar penyebab reboot sebelumnya.
        Serial.printf("[SheetLogger] Skip flush %s: WiFi/GSheet belum siap (buffer tetap disimpan)\n",
                      logger.sheetRange);
        return false;
    }

    FirebaseJson response;
    FirebaseJson valueRange;
    valueRange.add("majorDimension", "ROWS");

    for (size_t i = 0; i < logger.bufferCount; ++i) {
        const LogEntry &entry = logger.buffer[i];
        String rowPrefix = "values/[" + String(i) + "]/";
        valueRange.set((rowPrefix + "[0]").c_str(), formatElapsed(virtualClockOffsetMs + entry.startMs));
        valueRange.set((rowPrefix + "[1]").c_str(), formatElapsed(virtualClockOffsetMs + entry.endMs));
        valueRange.set((rowPrefix + "[2]").c_str(), formatElapsed(entry.durationMs));
        valueRange.set((rowPrefix + "[3]").c_str(), entry.errorPercent);
    }

    if (wdtSubscribed) {
        esp_task_wdt_reset();  // feed tepat sebelum masuk ke blocking call
    }

    bool success = GSheet.values.append(&response, spreadsheetId, logger.sheetRange, &valueRange);

    if (wdtSubscribed) {
        esp_task_wdt_reset();  // feed lagi segera setelah blocking call selesai
    }

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
// coba flush dulu supaya ada tempat -- TAPI hanya jika WiFi/GSheet memang
// siap. Sebelumnya fungsi ini memanggil flushChannelLogger() tanpa syarat,
// yang berarti network call blocking bisa terjadi kapan saja termasuk saat
// koneksi sedang tidak stabil, menyebabkan task ini menahan CPU cukup lama
// untuk memicu Task Watchdog Timer abort.
void bufferChannelEntry(ChannelLogger &logger, const LogEntry &entry) {
    if (logger.bufferCount >= kMaxBufferedRows) {
        if (sheetsReadyForNetworkCall()) {
            Serial.printf("[SheetLogger] Buffer %s penuh, flush paksa sebelum menambah entri baru\n",
                          logger.sheetRange);
            flushChannelLogger(logger);
        } else {
            Serial.printf("[SheetLogger] Buffer %s penuh tapi WiFi/GSheet belum siap, tunda flush paksa\n",
                          logger.sheetRange);
        }
    }

    if (logger.bufferCount < kMaxBufferedRows) {
        logger.buffer[logger.bufferCount++] = entry;
    } else {
        // Masih penuh (flush di atas tidak dilakukan atau gagal, misal WiFi
        // lagi putus). Daripada data baru hilang total tanpa jejak, buang
        // entri PALING LAMA dan simpan yang baru -- prioritaskan data
        // terbaru untuk pengujian akurasi.
        Serial.printf("[SheetLogger] Buffer %s masih penuh, entri terlama dibuang\n",
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

// Soft-recovery: paksa WiFi & token GSheet reconnect dari awal TANPA
// esp_restart(). Chip tetap menyala terus, millis()/uptime tidak terganggu,
// jadi durasi yang tercatat di log tetap konsisten -- hanya lapisan
// jaringan & auth yang "dipaksa segar" lagi.
//
// Dipanggil hanya kalau GSheet sudah stuck belum ready cukup lama
// (kSoftRecoveryAfterMs) DAN sudah lewat cooldown sejak percobaan
// recovery terakhir (supaya tidak spam reconnect kalau memang sedang
// ada gangguan jaringan yang lebih lama).
void attemptSoftRecovery() {
    Serial.println("[SheetLogger] GSheet stuck terlalu lama, mencoba SOFT RECOVERY "
                    "(WiFi + token direset ulang, TIDAK reboot chip)...");
    Serial.printf("[SheetLogger] Free heap sebelum recovery: %u bytes\n", (unsigned)ESP.getFreeHeap());

    // 1) Paksa WiFi putus & connect ulang dari nol.
    WiFi.disconnect(true /* wifioff */, false /* eraseap */);
    vTaskDelay(pdMS_TO_TICKS(500));
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    unsigned long wifiWaitStart = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - wifiWaitStart) < 15000UL) {
        vTaskDelay(pdMS_TO_TICKS(250));
    }

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[SheetLogger] Soft recovery: WiFi gagal reconnect, coba lagi nanti");
        return;
    }

    // 2) Pastikan waktu sistem masih valid (kalau NTP sempat "lupa" karena
    // WiFi lama terputus, sinkronkan ulang sebelum minta token baru).
    time_t nowCheck = time(nullptr);
    if (nowCheck < 1577836800) {
        configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov");
        unsigned long ntpWaitStart = millis();
        while (time(nullptr) < 1577836800 && (millis() - ntpWaitStart) < 10000UL) {
            vTaskDelay(pdMS_TO_TICKS(250));
        }
    }

    // 3) Minta GSheet membangun ulang token dari awal (fresh JWT/OAuth2
    // exchange) tanpa membuat ulang objek GSheet itu sendiri.
    GSheet.begin(CLIENT_EMAIL, PROJECT_ID, PRIVATE_KEY);

    Serial.printf("[SheetLogger] Soft recovery selesai. Free heap sesudah: %u bytes\n",
                  (unsigned)ESP.getFreeHeap());
}

// Nilai elapsed "virtual" saat ini = offset yang dimuat saat boot ini +
// millis() saat ini. Offset TIDAK berubah selama boot berjalan (supaya
// durationMs/cooldown logic yang pakai selisih raw millis() tetap valid),
// hanya ditambahkan saat FORMAT ke string untuk sheet.
unsigned long currentVirtualElapsedMs() {
    return virtualClockOffsetMs + millis();
}

// Simpan checkpoint offset ke NVS supaya boot BERIKUTNYA bisa melanjutkan
// dari sini. Dipanggil berkala (bukan tiap loop) supaya tidak membebani
// flash dengan write terlalu sering (flash punya batas siklus tulis).
void saveVirtualClockCheckpoint() {
    unsigned long nowVirtual = currentVirtualElapsedMs();
    clockPrefs.putULong(kClockNvsKey, nowVirtual);
}


void spreadsheetLoggerTask(void * /*pvParameters*/) {
    QueuedLogEntry queued;
    Serial.println("[SheetLogger] Background upload task started on Core 0");

    // Daftarkan task ini ke Task Watchdog Timer secara eksplisit dengan
    // timeout yang lebih longgar daripada default (biasanya 5s untuk
    // IDLE task). Ini memberi jalur network call yang lambat sedikit
    // keleluasaan tanpa membuat watchdog default trigger abort() di IDLE0,
    // sekaligus tetap punya jaring pengaman kalau task ini benar-benar hang.
    // NOTE: signature ini untuk ESP-IDF 4.x (arduino-esp32 2.x), sesuai SDK
    // yang terlihat di boot log ("SDK Version: v4.4.7-dirty"). Kalau project
    // ini pindah ke arduino-esp32 3.x (ESP-IDF 5.x) di masa depan, API-nya
    // berubah jadi esp_task_wdt_init(&esp_task_wdt_config_t{...}).
    //
    // Kalau TWDT sudah diinisialisasi di tempat lain (mis. oleh Arduino core
    // itu sendiri) dengan timeout default, panggilan ini akan mengembalikan
    // ESP_ERR_INVALID_STATE -- aman diabaikan, kita tetap lanjut subscribe
    // task ini ke watchdog yang sudah aktif.
    esp_task_wdt_init(kLoggerWdtTimeoutSec, true /* panic on timeout */);
    if (esp_task_wdt_add(NULL) == ESP_OK) {
        wdtSubscribed = true;
    } else {
        Serial.println("[SheetLogger] WARNING: gagal subscribe ke task watchdog");
    }

    unsigned long notReadySinceMs = 0;       // 0 berarti "sedang ready / belum mulai menghitung"
    unsigned long lastRecoveryAttemptMs = 0;  // 0 berarti "belum pernah coba recovery"

    for (;;) {
        if (xQueueReceive(logEntryQueue, &queued, pdMS_TO_TICKS(1000)) == pdTRUE) {
            ChannelLogger &logger = (queued.channel == 0) ? ch1Logger : ch2Logger;
            bufferChannelEntry(logger, queued.entry);
        }

        if (wdtSubscribed) {
            esp_task_wdt_reset();
        }

        bool ready = sheetsReadyForNetworkCall();
        unsigned long nowMs = millis();

        // WiFi/API hanya dipanggil setelah buffer penuh, dan hanya kalau
        // memang siap -- flushChannelLogger() sendiri sudah menjaga ini,
        // tapi dicek juga di sini supaya kita tidak masuk fungsi tsb sama
        // sekali kalau jelas belum siap (hemat sedikit overhead & log noise).
        if (ready) {
            notReadySinceMs = 0;  // reset penghitung "stuck" begitu ready lagi

            if (ch1Logger.bufferCount >= kMaxBufferedRows) {
                flushChannelLogger(ch1Logger);
            }
            if (ch2Logger.bufferCount >= kMaxBufferedRows) {
                flushChannelLogger(ch2Logger);
            }
        } else {
            if (notReadySinceMs == 0) {
                notReadySinceMs = nowMs;  // mulai hitung sejak pertama kali terdeteksi not-ready
            }

            unsigned long stuckDurationMs = nowMs - notReadySinceMs;
            unsigned long sinceLastRecoveryMs = (lastRecoveryAttemptMs == 0)
                                                     ? kSoftRecoveryCooldownMs  // izinkan percobaan pertama
                                                     : (nowMs - lastRecoveryAttemptMs);

            if (stuckDurationMs >= kSoftRecoveryAfterMs &&
                sinceLastRecoveryMs >= kSoftRecoveryCooldownMs) {
                lastRecoveryAttemptMs = nowMs;
                attemptSoftRecovery();
                // Tidak reset notReadySinceMs di sini -- kalau recovery
                // berhasil, giliran berikutnya "ready" akan jadi true dan
                // otomatis mereset penghitung lewat cabang di atas.
            } else {
                // Beri jeda kecil supaya task ini tetap sering yield ke
                // scheduler selama menunggu WiFi/token siap, alih-alih
                // langsung looping lagi ke xQueueReceive dengan timeout panjang.
                vTaskDelay(kNotReadyBackoffTicks);
            }
        }

        if (wdtSubscribed) {
            esp_task_wdt_reset();
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

    // Muat offset virtual clock dari NVS. Kalau ini pertama kali (belum
    // pernah disimpan sebelumnya), pakai kManualSeedMs sebagai titik awal
    // (sesuai nilai elapsed terakhir sebelum reflash), lalu langsung simpan
    // supaya boot-boot berikutnya otomatis melanjutkan tanpa perlu diedit lagi.
    clockPrefs.begin(kClockNvsNamespace, false /* read-write */);
    bool hadStoredOffset = clockPrefs.isKey(kClockNvsKey);
    virtualClockOffsetMs = clockPrefs.getULong(kClockNvsKey, kManualSeedMs);
    if (!hadStoredOffset) {
        clockPrefs.putULong(kClockNvsKey, virtualClockOffsetMs);
        Serial.printf("[SheetLogger] Virtual clock: belum ada offset tersimpan, "
                      "seed awal dipakai = %lu ms (%s)\n",
                      virtualClockOffsetMs, formatElapsed(virtualClockOffsetMs).c_str());
    } else {
        Serial.printf("[SheetLogger] Virtual clock: melanjutkan dari offset tersimpan "
                      "= %lu ms (%s)\n",
                      virtualClockOffsetMs, formatElapsed(virtualClockOffsetMs).c_str());
    }

    WiFi.setAutoReconnect(true);
    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    Serial.println("Spreadsheet logger Wi-Fi connection started");

    // PENTING: OAuth2/JWT token signing GSheet butuh jam sistem yang valid
    // (dipakai untuk klaim "issued at" / "expiry" token, dan validasi TLS
    // cert Google). ESP32 tanpa RTC battery-backed selalu boot dengan jam
    // di epoch 0 (1970), jadi TANPA sinkronisasi waktu di sini, token akan
    // GAGAL TERUS dengan error -111 ("System time or library reference time
    // was not set") dan GSheet.ready() tidak akan pernah true -- buffer
    // akan penuh selamanya dan terus buang entri terlama tanpa pernah
    // benar-benar terupload.
    //
    // Blocking singkat di sini (tunggu WiFi connect + NTP sync) itu wajar
    // karena hanya terjadi sekali saat boot, sebelum SheetLogger task/RTOS
    // task lain jalan -- bukan di dalam loop yang bisa memicu watchdog.
    Serial.println("[SheetLogger] Menunggu WiFi untuk sinkronisasi waktu NTP...");
    unsigned long wifiWaitStart = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - wifiWaitStart) < 15000UL) {
        delay(250);
    }

    if (WiFi.status() == WL_CONNECTED) {
        // GMT+7 (WIB) = 7*3600 detik offset, tanpa daylight saving.
        // Sesuaikan gmtOffsetSec kalau device dipakai di zona waktu lain --
        // offset ini sebenarnya tidak krusial untuk validitas token (yang
        // penting epoch time benar), tapi berguna untuk timestamp yang masuk akal.
        configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov");

        Serial.print("[SheetLogger] Menunggu waktu NTP tersinkron");
        time_t now = time(nullptr);
        unsigned long ntpWaitStart = millis();
        // Tahun 2020 dipakai sebagai sanity check sederhana: kalau time()
        // masih di bawah itu, berarti belum benar-benar tersinkron NTP.
        while (now < 1577836800 && (millis() - ntpWaitStart) < 10000UL) {
            delay(250);
            Serial.print(".");
            now = time(nullptr);
        }
        Serial.println();

        if (now >= 1577836800) {
            Serial.printf("[SheetLogger] Waktu tersinkron: %s", ctime(&now));
        } else {
            Serial.println("[SheetLogger] WARNING: NTP sync timeout, token GSheet mungkin masih gagal");
        }
    } else {
        Serial.println("[SheetLogger] WARNING: WiFi belum connect, skip NTP sync untuk saat ini "
                        "(token GSheet kemungkinan akan gagal sampai waktu tersinkron manual)");
    }

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
    static unsigned long lastClockSaveMs = 0;
    unsigned long nowStatusMs = millis();

    if (nowStatusMs - lastClockSaveMs > kClockSaveIntervalMs) {
        lastClockSaveMs = nowStatusMs;
        saveVirtualClockCheckpoint();
    }

    if (nowStatusMs - lastStatusPrintMs > 5000) {
        lastStatusPrintMs = nowStatusMs;
        Serial.printf("[SheetLogger] WiFi=%d GSheet.ready=%d ch1Buf=%u ch2Buf=%u queue=%u dropped=%lu freeHeap=%u minFreeHeap=%u\n",
                      (int)WiFi.status(),
                      (int)GSheet.ready(),
                      (unsigned)ch1Logger.bufferCount,
                      (unsigned)ch2Logger.bufferCount,
                      logEntryQueue ? (unsigned)uxQueueMessagesWaiting(logEntryQueue) : 0U,
                      (unsigned long)droppedLogEntries,
                      (unsigned)ESP.getFreeHeap(),
                      (unsigned)ESP.getMinFreeHeap());
    }
}

void spreadsheet_log_capture_counts(int32_t ch1, int32_t ch2, unsigned long nowMs) {
    if (!loggerInitialized || logEntryQueue == nullptr) {
        return;
    }

    captureChannel(0, ch1Capture, ch1, nowMs);
    captureChannel(1, ch2Capture, ch2, nowMs);
}