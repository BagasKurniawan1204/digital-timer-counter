#include "ui.h"

// TFT instance
TFT_eSPI tft = TFT_eSPI();

// Y position of the OUT1/OUT2 lamps. Kept above the HMI button bar, which
// occupies the bottom ~45 px of the home screen.
#define UI_OUT_LAMP_Y   172

// Cleared by ui_draw_home_static() so the CH1 labels are repainted the next
// time ui_update_counter() runs after returning from an HMI screen.
static bool ui_labels_drawn = false;

// Width of a value column. Clearing only the column - rather than everything
// from x to the right edge - keeps the Counter 2 labels at x=180 alive.
#define UI_VAL_W_CH1    78      // x=100..178, stops before the CH2 labels
#define UI_VAL_W_CH2    60      // x=260..320, up to the right edge

static void draw_value_field(int16_t x, int16_t y, int16_t w, int32_t value) {
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%ld", (long)value);
    tft.fillRect(x, y, w, 18, TFT_BLACK);
    tft.drawString(buffer, x, y, 2);
}

static void draw_text_field(int16_t x, int16_t y, int16_t w, const char *text) {
    tft.fillRect(x, y, w, 18, TFT_BLACK);
    tft.drawString(text, x, y, 2);
}

/**
 * @brief Draw the Channel 1 row labels for the active mode
 * Called from ui_init and again whenever the CH1 mode changes.
 */
static void draw_ch1_labels(bool timer_mode) {
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.fillRect(20, 50, 150, 26, TFT_BLACK);
    tft.drawString(timer_mode ? "Timer 1" : "Counter 1", 20, 50, 4);

    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.fillRect(20, 80, 80, 78, TFT_BLACK);
    if (timer_mode) {
        tft.drawString("Remain :", 20, 80, 2);
        tft.drawString("Setpt  :", 20, 110, 2);
        tft.drawString("State  :", 20, 140, 2);
    } else {
        tft.drawString("Current:", 20, 80, 2);
        tft.drawString("Preset :", 20, 110, 2);
        tft.drawString("Freq Hz:", 20, 140, 2);
    }
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

void ui_draw_home_static() {
    tft.fillScreen(TFT_BLACK);

    // Header
    tft.setTextSize(2);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawCentreString("Digital Timer & Counter", 160, 10, 2);

    // Base layout (Labels)
    tft.setTextSize(1);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString("Counter 2", 180, 50, 4);

    // Counter 1 labels depend on the active CH1 mode
    draw_ch1_labels(ch1_get_mode() == CH1_MODE_TIMER);

    tft.setTextColor(TFT_YELLOW, TFT_BLACK);

    // Counter 2 labels
    tft.drawString("Current:", 180, 80, 2);
    tft.drawString("Preset :", 180, 110, 2);
    tft.drawString("Freq Hz:", 180, 140, 2);

    // The value fields and lamps are blank until the next ui_update_counter();
    // force the CH1 labels to be re-evaluated in case the mode changed while
    // another screen was showing.
    ui_labels_drawn = false;
}

void ui_init() {
    pinMode(14, OUTPUT);
    pinMode(13, OUTPUT);
    tft.init();
    tft.setRotation(3); // Set landscape

    ui_draw_home_static();
}

void ui_update_counter(int32_t counter1_current, int32_t counter1_preset, int32_t counter1_frequency,
                       int32_t counter2_current, int32_t counter2_preset, int32_t counter2_frequency,
                       Ch1Mode ch1_mode) {
    // Redraw the CH1 labels only when the mode actually changes, so the
    // periodic refresh stays flicker-free.
    static Ch1Mode last_ch1_mode = CH1_MODE_COUNTER;
    bool timer_mode = (ch1_mode == CH1_MODE_TIMER);

    if (!ui_labels_drawn || ch1_mode != last_ch1_mode) {
        draw_ch1_labels(timer_mode);
        last_ch1_mode = ch1_mode;
        ui_labels_drawn = true;
    }

    tft.setTextColor(TFT_GREEN, TFT_BLACK);

    bool ch1_output;

    if (timer_mode) {
        Ch1TimerStatus_t tmr = ct_timer_get_status();

        // Update Timer 1: remaining / setpoint / state
        draw_value_field(100, 80, UI_VAL_W_CH1, (int32_t)tmr.remaining_s);
        draw_value_field(100, 110, UI_VAL_W_CH1, (int32_t)tmr.setpoint_s);
        draw_text_field(100, 140, UI_VAL_W_CH1, ct_timer_state_to_string(tmr.state));

        ch1_output = tmr.output_state;
    } else {
        // Update Counter 1
        draw_value_field(100, 80, UI_VAL_W_CH1, counter1_current);
        draw_value_field(100, 110, UI_VAL_W_CH1, counter1_preset);
        draw_value_field(100, 140, UI_VAL_W_CH1, counter1_frequency);

        ch1_output = (counter1_current >= counter1_preset);
    }

    // Update Counter 2
    draw_value_field(260, 80, UI_VAL_W_CH2, counter2_current);
    draw_value_field(260, 110, UI_VAL_W_CH2, counter2_preset);
    draw_value_field(260, 140, UI_VAL_W_CH2, counter2_frequency);

    // Show output indicators only while the threshold condition is met.
    draw_output_indicator(20, UI_OUT_LAMP_Y, ch1_output, "OUT1");
    draw_output_indicator(260, UI_OUT_LAMP_Y, counter2_current >= counter2_preset, "OUT2");
}

void ui_calibrate_touch(uint16_t calData[5]) {
    tft.fillScreen(TFT_BLACK);
    tft.setCursor(20, 0);
    tft.setTextFont(2);
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.println("Touch corners as indicated");

    tft.calibrateTouch(calData, TFT_MAGENTA, TFT_BLACK, 15);
    tft.fillScreen(TFT_BLACK);
    // The caller decides what to redraw and persists calData.
}

bool ui_get_touch(uint16_t *x, uint16_t *y) {
    return tft.getTouch(x, y);
}