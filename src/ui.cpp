#include "ui.h"

// TFT instance
TFT_eSPI tft = TFT_eSPI();

static void draw_value_field(int16_t x, int16_t y, int32_t value) {
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%-7ld", (long)value);
    tft.fillRect(x, y, tft.width() - x, 18, TFT_BLACK);
    tft.drawString(buffer, x, y, 2);
}

void ui_init() {
    pinMode(14, OUTPUT);
    pinMode(13, OUTPUT);
    tft.init();
    tft.setRotation(1); // Set landscape
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
}

void ui_update_counter(int32_t counter1_current, int32_t counter1_preset, int32_t counter1_frequency,
                       int32_t counter2_current, int32_t counter2_preset, int32_t counter2_frequency) {
    tft.setTextColor(TFT_GREEN, TFT_BLACK);

    // Update Counter 1 (pad with spaces to overwrite old text)
    draw_value_field(100, 80, counter1_current);
    draw_value_field(100, 110, counter1_preset);
    draw_value_field(100, 140, counter1_frequency);

    // Update Counter 2
    draw_value_field(260, 80, counter2_current);
    draw_value_field(260, 110, counter2_preset);
    draw_value_field(260, 140, counter2_frequency);
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