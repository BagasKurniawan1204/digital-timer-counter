#include "ui.h"
#include "timer_operation.h"
#include "nvs_config.h"

// TFT instance
TFT_eSPI tft = TFT_eSPI();

namespace {
enum class UiPage : uint8_t { Counter, Timer };
UiPage currentPage = UiPage::Counter;
unsigned long lastTouchMs = 0;

void drawButton(int16_t x, int16_t y, int16_t w, int16_t h, const char *label, uint16_t color) {
    tft.fillRoundRect(x, y, w, h, 4, color);
    tft.setTextColor(TFT_BLACK, color);
    tft.drawCentreString(label, x + w / 2, y + 7, 2);
}

void drawTimerPage() {
    const TriggerTimerConfig config = trigger_timer_get_config();
    const TriggerTimerStatus status = trigger_timer_get_status();
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawCentreString("Timer Settings", 160, 8, 4);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString("Timer duration", 18, 55, 2);
    tft.drawString(String(config.durationMs / 1000.0f, 1) + " s", 128, 55, 2);
    tft.drawString("Delay off", 18, 105, 2);
    tft.drawString(String(config.delayOffMs / 1000.0f, 1) + " s", 128, 105, 2);
    tft.drawString("Off method", 18, 155, 2);
    tft.drawString(config.offMode == TIMER_OFF_BY_CTRL ? "CTRL rising" : "Delay off", 128, 155, 2);
    drawButton(210, 45, 45, 32, "+", TFT_GREEN);
    drawButton(260, 45, 45, 32, "-", TFT_RED);
    drawButton(210, 95, 45, 32, "+", TFT_GREEN);
    drawButton(260, 95, 45, 32, "-", TFT_RED);
    drawButton(210, 145, 95, 32, "TOGGLE", TFT_YELLOW);
    drawButton(15, 195, 95, 32, "BACK", TFT_DARKGREY);
    tft.setTextColor(status.relayOn ? TFT_GREEN : TFT_LIGHTGREY, TFT_BLACK);
    tft.drawString(status.relayOn ? "RELAY: ON" : (status.running ? "TIMER: RUN" : "IDLE"), 145, 205, 2);
}

void saveTimerConfig(TriggerTimerConfig config) {
    trigger_timer_set_config(config);
    nvs_save_trigger_timer_config(trigger_timer_get_config());
    drawTimerPage();
}
}  // namespace

static void draw_value_field(int16_t x, int16_t y, int32_t value) {
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%-7ld", (long)value);
    tft.fillRect(x, y, tft.width() - x, 18, TFT_BLACK);
    tft.drawString(buffer, x, y, 2);
}

static void draw_output_indicator(int16_t x, int16_t y, bool active, const char *label) {
    const uint16_t width = 60;
    const uint16_t height = 18;

    tft.fillRect(x, y, width, height, TFT_BLACK);
    if (active) {
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        tft.drawString(label, x, y, 2);
    }
}

void ui_init() {
    pinMode(14, OUTPUT);
    pinMode(13, OUTPUT);
    tft.init();
    tft.setRotation(3); // Set landscape
    tft.fillScreen(TFT_BLACK);
    
    // Header
    tft.setTextSize(2);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawCentreString("Digital Timer & Counter", 160, 10, 2);
    
    // Base layout (Labels)
    tft.setTextSize(1);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString("Counter 1", 20, 50, 4);
    tft.drawString("Counter 2", 180, 50, 4);

    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    
    // Counter 1 labels
    tft.drawString("Current:", 20, 80, 2);
    tft.drawString("Preset :", 20, 110, 2);
    tft.drawString("Freq (Hz) :", 20, 140, 2);
    
    // Counter 2 labels
    tft.drawString("Current:", 180, 80, 2);
    tft.drawString("Preset :", 180, 110, 2);
    tft.drawString("Freq (Hz) :", 180, 140, 2);
    drawButton(215, 195, 95, 32, "TIMER", TFT_ORANGE);
}

void ui_update_counter(int32_t counter1_current, int32_t counter1_preset, int32_t counter1_frequency,
                       int32_t counter2_current, int32_t counter2_preset, int32_t counter2_frequency) {
    if (currentPage != UiPage::Counter) return;
    tft.setTextColor(TFT_GREEN, TFT_BLACK);

    // Update Counter 1 (pad with spaces to overwrite old text)
    draw_value_field(100, 80, counter1_current);
    draw_value_field(100, 110, counter1_preset);
    draw_value_field(100, 140, counter1_frequency);

    // Update Counter 2
    draw_value_field(260, 80, counter2_current);
    draw_value_field(260, 110, counter2_preset);
    draw_value_field(260, 140, counter2_frequency);

    // Show output indicators only while the threshold condition is met.
    draw_output_indicator(20, 210, counter1_current >= counter1_preset, "OUT1");
    draw_output_indicator(260, 210, counter2_current >= counter2_preset, "OUT2");
}

void ui_calibrate_touch() {
    uint16_t calData[5];
    tft.fillScreen(TFT_BLACK);
    tft.setCursor(20, 0);
    tft.setTextFont(2);
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.println("Touch corners as indicated");
    
    tft.calibrateTouch(calData, TFT_MAGENTA, TFT_BLACK, 15);
    tft.fillScreen(TFT_BLACK);
    ui_init(); // Redraw UI after calibration
}

bool ui_get_touch(uint16_t *x, uint16_t *y) {
    return tft.getTouch(x, y);
}

void ui_process_touch() {
    uint16_t x = 0;
    uint16_t y = 0;
    if (!ui_get_touch(&x, &y) || millis() - lastTouchMs < 180UL) return;
    lastTouchMs = millis();

    if (currentPage == UiPage::Counter) {
        if (x >= 200 && y >= 180) {
            currentPage = UiPage::Timer;
            drawTimerPage();
        }
        return;
    }

    if (x < 120 && y >= 185) {
        currentPage = UiPage::Counter;
        ui_init();
        return;
    }

    TriggerTimerConfig config = trigger_timer_get_config();
    constexpr uint32_t kStepMs = 1000UL;
    if (y >= 40 && y <= 85) {
        if (x >= 205 && x < 258) config.durationMs += kStepMs;
        if (x >= 258 && config.durationMs > 1000UL) config.durationMs -= kStepMs;
        saveTimerConfig(config);
    } else if (y >= 90 && y <= 135) {
        if (x >= 205 && x < 258) config.delayOffMs += kStepMs;
        if (x >= 258 && config.delayOffMs > 1000UL) config.delayOffMs -= kStepMs;
        saveTimerConfig(config);
    } else if (y >= 140 && y <= 180 && x >= 200) {
        config.offMode = config.offMode == TIMER_OFF_BY_CTRL ? TIMER_OFF_BY_DELAY : TIMER_OFF_BY_CTRL;
        saveTimerConfig(config);
    }
}
