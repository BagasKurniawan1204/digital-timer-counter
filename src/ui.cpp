#include "ui.h"

// TFT instance
TFT_eSPI tft = TFT_eSPI();

// Y position of the OUT1/OUT2 lamps. Kept above the HMI button bar, which
// occupies the bottom ~45 px of the home screen.
#define UI_OUT_LAMP_Y   172

// Cleared by ui_draw_home_static() so both columns' labels are repainted the
// next time ui_update_counter() runs after returning from an HMI screen.
static bool ui_labels_drawn = false;

// Width of a value column. Clearing only the column - rather than everything
// from x to the right edge - keeps the Counter 2 labels at x=180 alive.
#define UI_VAL_W_CH1    78      // x=100..178, stops before the CH2 labels
#define UI_VAL_W_CH2    60      // x=260..320, up to the right edge

// Per-channel column geometry. Channel index is 0-based (0 = CH1, 1 = CH2).
static const struct {
    int16_t label_x;    // Title and row labels
    int16_t label_w;    // Clear width for the row labels
    int16_t title_w;    // Clear width for the column title
    int16_t value_x;    // Value fields
    int16_t value_w;    // Clear width for the value fields
} UI_COL[2] = {
    {  20, 80, 150, 100, UI_VAL_W_CH1 },
    { 180, 80, 140, 260, UI_VAL_W_CH2 },
};

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
 * @brief Draw one channel's column title and row labels for the active mode
 * @param ch 0-based channel index
 * Called from ui_draw_home_static and again whenever that channel's mode
 * changes.
 */
static void draw_ch_labels(uint8_t ch, bool timer_mode) {
    if (ch > 1) return;

    const int16_t x = UI_COL[ch].label_x;
    char title[12];
    snprintf(title, sizeof(title), "%s %u", timer_mode ? "Timer" : "Counter",
             (unsigned)(ch + 1));

    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.fillRect(x, 50, UI_COL[ch].title_w, 26, TFT_BLACK);
    tft.drawString(title, x, 50, 4);

    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.fillRect(x, 80, UI_COL[ch].label_w, 78, TFT_BLACK);
    if (timer_mode) {
        tft.drawString("Remain :", x, 80, 2);
        tft.drawString("Setpt  :", x, 110, 2);
        tft.drawString("State  :", x, 140, 2);
    } else {
        tft.drawString("Current:", x, 80, 2);
        tft.drawString("Preset :", x, 110, 2);
        tft.drawString("Freq Hz:", x, 140, 2);
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

    // Base layout (Labels). Both columns' labels depend on that channel's mode.
    tft.setTextSize(1);
    for (uint8_t ch = 0; ch < 2; ch++) {
        draw_ch_labels(ch, ch_get_mode(ch) == CH_MODE_TIMER);
    }

    // The value fields and lamps are blank until the next ui_update_counter();
    // force the labels to be re-evaluated in case a mode changed while another
    // screen was showing.
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
                       ChOpMode ch1_mode, ChOpMode ch2_mode) {
    const ChOpMode mode[2]    = { ch1_mode, ch2_mode };
    const int32_t  current[2] = { counter1_current, counter2_current };
    const int32_t  preset[2]  = { counter1_preset, counter2_preset };
    const int32_t  freq[2]    = { counter1_frequency, counter2_frequency };
    const char*    lamp[2]    = { "OUT1", "OUT2" };

    // Redraw a column's labels only when its mode actually changes, so the
    // periodic refresh stays flicker-free.
    static ChOpMode last_mode[2] = { CH_MODE_COUNTER, CH_MODE_COUNTER };
    bool relabel = !ui_labels_drawn;

    for (uint8_t ch = 0; ch < 2; ch++) {
        if (relabel || mode[ch] != last_mode[ch]) {
            draw_ch_labels(ch, mode[ch] == CH_MODE_TIMER);
            last_mode[ch] = mode[ch];
        }
    }
    ui_labels_drawn = true;

    tft.setTextColor(TFT_GREEN, TFT_BLACK);

    for (uint8_t ch = 0; ch < 2; ch++) {
        const int16_t x = UI_COL[ch].value_x;
        const int16_t w = UI_COL[ch].value_w;
        bool output;

        if (mode[ch] == CH_MODE_TIMER) {
            ChTimerStatus_t tmr = ct_timer_get_status(ch);

            // Timer column: remaining / setpoint / state
            draw_value_field(x, 80, w, (int32_t)tmr.remaining_s);
            draw_value_field(x, 110, w, (int32_t)tmr.setpoint_s);
            draw_text_field(x, 140, w, ct_timer_state_to_string(tmr.state));

            output = tmr.output_state;
        } else {
            // Counter column: current / preset / frequency
            draw_value_field(x, 80, w, current[ch]);
            draw_value_field(x, 110, w, preset[ch]);
            draw_value_field(x, 140, w, freq[ch]);

            output = (current[ch] >= preset[ch]);
        }

        // Show output indicators only while the threshold condition is met.
        draw_output_indicator(ch == 0 ? 20 : 260, UI_OUT_LAMP_Y, output, lamp[ch]);
    }
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