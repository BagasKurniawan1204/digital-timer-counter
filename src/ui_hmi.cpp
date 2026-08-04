/**
 * @file ui_hmi.cpp
 * @brief Touchscreen HMI implementation (see ui_hmi.h for the screen map)
 */

#include "ui_hmi.h"
#include "ui.h"
#include "config.h"
#include "globals.h"
#include "counter_operation.h"
#include "CT_counter.h"
#include "ct_timer.h"
#include "input_config.h"
#include "nvs_config.h"

// =============================================================================
// LAYOUT CONSTANTS
// =============================================================================

#define SCR_W               320
#define SCR_H               240

// A resistive panel driven by a finger needs generous targets.
#define ROW_H               37      // Full-width list row height
#define ROW_PITCH           40      // Row height + gap
#define LIST_TOP            34      // First list row, below the title bar

#define TITLE_Y             6

// Home screen button bar (below the OUT lamps at y=172)
#define HOME_BTN_Y          196
#define HOME_BTN_H          40
#define HOME_BTN_W          100

// One press must produce exactly one action on a noisy resistive panel.
#define TOUCH_LOCKOUT_MS    250

// Colours
#define C_BTN_FACE          TFT_NAVY
#define C_BTN_TEXT          TFT_WHITE
#define C_BTN_SEL           TFT_DARKGREEN   // Currently active option
#define C_BTN_WARN          TFT_MAROON      // Destructive action
#define C_TITLE             TFT_CYAN
#define C_VALUE             TFT_YELLOW

// =============================================================================
// SCREEN / EDITOR IDENTIFIERS
// =============================================================================

typedef enum {
    SCREEN_HOME,
    SCREEN_MENU,
    SCREEN_CH1_CFG,
    SCREEN_CH2_CFG,
    SCREEN_ACTIONS,
    SCREEN_SYSTEM,
    SCREEN_KEYPAD,
    SCREEN_OPTIONS,
    SCREEN_CONFIRM_FACTORY
} Screen_t;

// Numeric fields reachable from the keypad
typedef enum {
    KP_CH1_PRESET,
    KP_CH1_TSET,
    KP_CH1_TDELAY,
    KP_CH2_PRESET,
    KP_CH2_TSET,
    KP_CH2_TDELAY
} KeypadField_t;

// Enumerated fields reachable from the option list
typedef enum {
    OPT_CH1_TYPE,
    OPT_CH1_MODE,
    OPT_CH1_EDGE,
    OPT_CH1_TOUTMODE,
    OPT_CH2_TYPE,
    OPT_CH2_MODE,
    OPT_CH2_EDGE,
    OPT_CH2_TOUTMODE
} OptionField_t;

// =============================================================================
// MODULE STATE
// =============================================================================

static Screen_t s_screen = SCREEN_HOME;
static Screen_t s_return_screen = SCREEN_MENU;   // Where an editor returns to
static bool s_needs_redraw = true;

// Keypad editing state
static KeypadField_t s_kp_field;
static char s_kp_buf[12];
static uint8_t s_kp_len = 0;
static uint32_t s_kp_max = 0;
static const char *s_kp_title = "";
// The keypad opens showing the field's current value. The first digit typed
// replaces it instead of appending, which is what an operator expects.
static bool s_kp_fresh = true;

static OptionField_t s_opt_field;

// Transient confirmation message. It borrows the title strip for 1.5 s, which
// is the only always-free area: the row lists run all the way to the bottom.
static char s_title[36] = "";
static char s_toast[36] = "";
static uint32_t s_toast_until = 0;
static bool s_toast_dirty = false;

// =============================================================================
// SMALL HELPERS
// =============================================================================

typedef struct {
    int16_t x, y, w, h;
} Rect_t;

static bool rect_hit(const Rect_t &r, uint16_t tx, uint16_t ty) {
    return (int16_t)tx >= r.x && (int16_t)tx < r.x + r.w &&
           (int16_t)ty >= r.y && (int16_t)ty < r.y + r.h;
}

/**
 * @brief Draw a filled, outlined button with a centred label
 */
static void draw_button(const Rect_t &r, const char *label, uint16_t face = C_BTN_FACE,
                        uint16_t text = C_BTN_TEXT) {
    tft.fillRect(r.x, r.y, r.w, r.h, face);
    tft.drawRect(r.x, r.y, r.w, r.h, TFT_WHITE);
    tft.setTextColor(text, face);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(label, r.x + r.w / 2, r.y + r.h / 2, 2);
    tft.setTextDatum(TL_DATUM);
}

/**
 * @brief Draw a full-width settings row: label on the left, value on the right
 */
static void draw_row(int index, const char *label, const char *value,
                     uint16_t face = C_BTN_FACE) {
    Rect_t r = {10, (int16_t)(LIST_TOP + index * ROW_PITCH), SCR_W - 20, ROW_H};

    tft.fillRect(r.x, r.y, r.w, r.h, face);
    tft.drawRect(r.x, r.y, r.w, r.h, TFT_WHITE);

    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(C_BTN_TEXT, face);
    tft.drawString(label, r.x + 8, r.y + r.h / 2, 2);

    if (value && value[0]) {
        tft.setTextDatum(MR_DATUM);
        tft.setTextColor(C_VALUE, face);
        tft.drawString(value, r.x + r.w - 8, r.y + r.h / 2, 2);
    }
    tft.setTextDatum(TL_DATUM);
}

static Rect_t row_rect(int index) {
    return Rect_t{10, (int16_t)(LIST_TOP + index * ROW_PITCH), SCR_W - 20, ROW_H};
}

/**
 * @brief Paint the title strip, showing the active toast when there is one
 */
static void paint_title_strip() {
    bool has_toast = (s_toast[0] != '\0');

    tft.fillRect(0, 0, SCR_W, 26, TFT_BLACK);
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(has_toast ? TFT_GREEN : C_TITLE, TFT_BLACK);
    tft.drawString(has_toast ? s_toast : s_title, SCR_W / 2, TITLE_Y, 2);
    tft.setTextDatum(TL_DATUM);

    s_toast_dirty = false;
}

static void draw_title(const char *text) {
    snprintf(s_title, sizeof(s_title), "%s", text);
    paint_title_strip();
}

/**
 * @brief Show a short status message in the title strip for 1.5 s
 *
 * Safe to call before a screen switch: the pending redraw repaints the title
 * strip through paint_title_strip(), which keeps showing the toast until it
 * expires.
 */
static void toast(const char *msg) {
    snprintf(s_toast, sizeof(s_toast), "%s", msg);
    s_toast_until = millis() + 1500;
    s_toast_dirty = true;
}

static void clear_toast_if_expired() {
    if (s_toast[0] && (int32_t)(millis() - s_toast_until) >= 0) {
        s_toast[0] = '\0';
        paint_title_strip();    // Restore the real title
    } else if (s_toast_dirty) {
        paint_title_strip();
    }
}

static void goto_screen(Screen_t s) {
    s_screen = s;
    s_needs_redraw = true;
}

// =============================================================================
// VALUE ACCESS (mirrors what the web handlers read/write)
// =============================================================================

static const char *edge_to_string(EdgeMode e) {
    switch (e) {
        case EDGE_FALLING: return "FALLING";
        case EDGE_BOTH:    return "BOTH";
        default:           return "RISING";
    }
}

static int32_t get_preset(uint8_t channel) {
    CT_counter *c = getCounterInstance(channel);
    return c ? c->getPresetValue() : 0;
}

static void set_preset(uint8_t channel, int32_t value) {
    CT_counter *c = getCounterInstance(channel);
    if (c) c->setPresetValue(value);   // setPresetValue() already writes NVS
}

static void set_input_mode(uint8_t channel, InputMode m) {
    ChannelInputConfig_t cfg = input_config_get(channel);
    cfg.input_mode = m;
    input_config_set_mode(channel, &cfg);
    nvs_save_input_mode(channel, m);
}

static void set_edge_mode(uint8_t channel, EdgeMode e) {
    ChannelInputConfig_t cfg = input_config_get(channel);
    cfg.edge_mode = e;
    input_config_set_mode(channel, &cfg);
    nvs_save_edge_mode(channel, e);
}

static void save_timer_settings(uint8_t channel) {
    ChTimerStatus_t t = ct_timer_get_status(channel);
    nvs_save_timer(channel, t.setpoint_s, t.delay_off_s, t.out_mode);
}

// =============================================================================
// TOUCH INPUT
// =============================================================================

/**
 * @brief Report a single press per finger-down event
 *
 * Rising-edge detection plus a short lockout, so holding a button does not
 * repeat and panel noise cannot double-fire an action.
 */
static bool touch_pressed(uint16_t *x, uint16_t *y) {
    static bool was_down = false;
    static uint32_t last_fire = 0;

    if (tft.getTouch(x, y)) {
        if (!was_down) {
            was_down = true;
            if (millis() - last_fire >= TOUCH_LOCKOUT_MS) {
                last_fire = millis();
                return true;
            }
        }
        return false;   // Still held down
    }

    was_down = false;
    return false;
}

// =============================================================================
// HOME SCREEN
// =============================================================================

static const Rect_t HOME_MENU  = {5,   HOME_BTN_Y, HOME_BTN_W, HOME_BTN_H};
static const Rect_t HOME_RESET = {110, HOME_BTN_Y, HOME_BTN_W, HOME_BTN_H};
static const Rect_t HOME_TRST  = {215, HOME_BTN_Y, HOME_BTN_W, HOME_BTN_H};

static void draw_home_buttons() {
    draw_button(HOME_MENU,  "MENU");
    draw_button(HOME_RESET, "RESET");
    draw_button(HOME_TRST,  "TMR RST");
}

static void draw_home() {
    // The home screen owns the whole display, including the title strip, so a
    // leftover status message must not survive the switch back to it.
    s_toast[0] = '\0';
    s_toast_dirty = false;

    ui_draw_home_static();    // Header + row labels, clears the screen
    draw_home_buttons();
    // Value fields are filled by the next ui_update_counter() from loop().
}

static void handle_home(uint16_t x, uint16_t y) {
    if (rect_hit(HOME_MENU, x, y)) {
        goto_screen(SCREEN_MENU);
        return;
    }

    if (rect_hit(HOME_RESET, x, y)) {
        // Same effect as the web "Reset All": clear the PCNT hardware, the
        // per-channel software totals and the frequency estimates.
        pcnt_reset();
        s_ch1_last_count = 0;
        s_ch1_frequency_hz = 0;
        s_ch2_last_count = 0;
        s_ch2_frequency_hz = 0;
        CT_counter *c1 = getCounterInstance(1);
        CT_counter *c2 = getCounterInstance(2);
        if (c1) c1->reset();
        if (c2) c2->reset();
        return;
    }

    if (rect_hit(HOME_TRST, x, y)) {
        // Rearm whichever channels are running as countdown timers. A channel in
        // counter mode has nothing to reset here - the RESET button covers it.
        for (uint8_t ch = 0; ch < 2; ch++) {
            if (ch_get_mode(ch) == CH_MODE_TIMER) ct_timer_reset(ch);
        }
        return;
    }
}

// =============================================================================
// MAIN MENU
// =============================================================================

static void draw_menu() {
    tft.fillScreen(TFT_BLACK);
    draw_title("MENU");
    draw_row(0, "CH1 Config", "");
    draw_row(1, "CH2 Config", "");
    draw_row(2, "Actions", "");
    draw_row(3, "System", "");
    draw_row(4, "Back", "");
}

static void handle_menu(uint16_t x, uint16_t y) {
    if (rect_hit(row_rect(0), x, y)) goto_screen(SCREEN_CH1_CFG);
    else if (rect_hit(row_rect(1), x, y)) goto_screen(SCREEN_CH2_CFG);
    else if (rect_hit(row_rect(2), x, y)) goto_screen(SCREEN_ACTIONS);
    else if (rect_hit(row_rect(3), x, y)) goto_screen(SCREEN_SYSTEM);
    else if (rect_hit(row_rect(4), x, y)) goto_screen(SCREEN_HOME);
}

// =============================================================================
// NUMERIC KEYPAD
// =============================================================================

// 5 columns x 3 rows. Row 2 holds ESC/OK so every key is a 58x38 target.
static const char *KP_KEYS[3][5] = {
    {"1", "2", "3", "<-",  "CLR"},
    {"4", "5", "6", "00",  "0"  },
    {"7", "8", "9", "ESC", "OK" }
};

#define KP_COL_W    58
#define KP_COL_GAP  4
#define KP_X0       11
#define KP_ROW_H    38
#define KP_ROW_GAP  4
#define KP_Y0       78

static Rect_t kp_rect(int row, int col) {
    return Rect_t{(int16_t)(KP_X0 + col * (KP_COL_W + KP_COL_GAP)),
                  (int16_t)(KP_Y0 + row * (KP_ROW_H + KP_ROW_GAP)),
                  KP_COL_W, KP_ROW_H};
}

/**
 * @brief Repaint just the value line (called on every digit press)
 */
static void draw_kp_value() {
    tft.fillRect(0, 34, SCR_W, 34, TFT_BLACK);
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(s_kp_len ? s_kp_buf : "0", SCR_W / 2, 36, 4);
    tft.setTextDatum(TL_DATUM);
}

static void draw_keypad() {
    tft.fillScreen(TFT_BLACK);

    char hdr[40];
    snprintf(hdr, sizeof(hdr), "%s  (max %lu)", s_kp_title, (unsigned long)s_kp_max);
    draw_title(hdr);

    draw_kp_value();

    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 5; c++) {
            const char *k = KP_KEYS[r][c];
            uint16_t face = C_BTN_FACE;
            if (strcmp(k, "OK") == 0)  face = TFT_DARKGREEN;
            if (strcmp(k, "ESC") == 0) face = C_BTN_WARN;
            draw_button(kp_rect(r, c), k, face);
        }
    }
}

/**
 * @brief Open the keypad for a field, seeded with its current value
 */
static void open_keypad(KeypadField_t field, const char *title, uint32_t max_value,
                        uint32_t current, Screen_t return_to) {
    s_kp_field = field;
    s_kp_title = title;
    s_kp_max = max_value;
    s_return_screen = return_to;

    snprintf(s_kp_buf, sizeof(s_kp_buf), "%lu", (unsigned long)current);
    s_kp_len = strlen(s_kp_buf);
    s_kp_fresh = true;

    goto_screen(SCREEN_KEYPAD);
}

static void keypad_append(const char *digits) {
    // The seeded value is discarded as soon as the operator types a digit.
    if (s_kp_fresh) {
        s_kp_fresh = false;
        s_kp_len = 0;
        s_kp_buf[0] = '\0';
    }

    // The entry may not grow past the digit count of the field maximum, and the
    // buffer must always keep room for its terminator.
    char max_str[12];
    snprintf(max_str, sizeof(max_str), "%lu", (unsigned long)s_kp_max);
    uint8_t max_digits = strlen(max_str);
    uint8_t add = strlen(digits);

    // A lone leading zero is replaced rather than appended to.
    if (s_kp_len == 1 && s_kp_buf[0] == '0') s_kp_len = 0;

    if (s_kp_len + add > max_digits) return;
    if (s_kp_len + add >= sizeof(s_kp_buf)) return;

    memcpy(&s_kp_buf[s_kp_len], digits, add);
    s_kp_len += add;
    s_kp_buf[s_kp_len] = '\0';
    draw_kp_value();
}

static void keypad_commit() {
    uint32_t value = s_kp_len ? (uint32_t)strtoul(s_kp_buf, nullptr, 10) : 0;
    if (value > s_kp_max) value = s_kp_max;

    switch (s_kp_field) {
        case KP_CH1_PRESET:
            set_preset(1, (int32_t)value);
            break;
        case KP_CH2_PRESET:
            set_preset(2, (int32_t)value);
            break;
        case KP_CH1_TSET:
            ct_timer_set_setpoint(0, value);
            save_timer_settings(0);
            break;
        case KP_CH1_TDELAY:
            ct_timer_set_delay_off(0, value);
            save_timer_settings(0);
            break;
        case KP_CH2_TSET:
            ct_timer_set_setpoint(1, value);
            save_timer_settings(1);
            break;
        case KP_CH2_TDELAY:
            ct_timer_set_delay_off(1, value);
            save_timer_settings(1);
            break;
    }

    goto_screen(s_return_screen);
    toast("Saved");
}

static void handle_keypad(uint16_t x, uint16_t y) {
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 5; c++) {
            if (!rect_hit(kp_rect(r, c), x, y)) continue;

            const char *k = KP_KEYS[r][c];

            if (strcmp(k, "OK") == 0) {
                keypad_commit();
            } else if (strcmp(k, "ESC") == 0) {
                goto_screen(s_return_screen);   // Discard the entry
            } else if (strcmp(k, "CLR") == 0) {
                s_kp_len = 0;
                s_kp_buf[0] = '\0';
                s_kp_fresh = false;
                draw_kp_value();
            } else if (strcmp(k, "<-") == 0) {
                if (s_kp_len > 0) s_kp_buf[--s_kp_len] = '\0';
                s_kp_fresh = false;
                draw_kp_value();
            } else {
                keypad_append(k);
            }
            return;
        }
    }
}

// =============================================================================
// OPTION LIST
// =============================================================================

typedef struct {
    const char *title;
    const char *labels[4];
    uint8_t count;
} OptionSpec_t;

static OptionSpec_t option_spec(OptionField_t f) {
    switch (f) {
        case OPT_CH1_TYPE:
        case OPT_CH2_TYPE:
            return OptionSpec_t{f == OPT_CH1_TYPE ? "CH1 Type" : "CH2 Type", {"COUNTER", "TIMER"}, 2};
        case OPT_CH1_TOUTMODE:
        case OPT_CH2_TOUTMODE:
            return OptionSpec_t{"Timer Out Mode", {"LATCH", "DELAY OFF"}, 2};
        case OPT_CH1_MODE:
            return OptionSpec_t{"CH1 Count Mode", {"UP", "DN", "UDA", "UDC"}, 4};
        case OPT_CH2_MODE:
            return OptionSpec_t{"CH2 Count Mode", {"UP", "DN", "UDA", "UDC"}, 4};
        case OPT_CH1_EDGE:
            return OptionSpec_t{"CH1 Edge", {"RISING", "FALLING", "BOTH"}, 3};
        default:
            return OptionSpec_t{"CH2 Edge", {"RISING", "FALLING", "BOTH"}, 3};
    }
}

/**
 * @brief Index of the option currently in effect, so it can be highlighted
 */
static uint8_t option_current(OptionField_t f) {
    switch (f) {
        case OPT_CH1_TYPE:
            return ch_get_mode(0) == CH_MODE_TIMER ? 1 : 0;
        case OPT_CH2_TYPE:
            return ch_get_mode(1) == CH_MODE_TIMER ? 1 : 0;
        case OPT_CH1_TOUTMODE:
            return ct_timer_get_status(0).out_mode == TIMER_OUT_DELAY_OFF ? 1 : 0;
        case OPT_CH2_TOUTMODE:
            return ct_timer_get_status(1).out_mode == TIMER_OUT_DELAY_OFF ? 1 : 0;
        case OPT_CH1_MODE:
            return (uint8_t)input_config_get(0).input_mode;
        case OPT_CH2_MODE:
            return (uint8_t)input_config_get(1).input_mode;
        case OPT_CH1_EDGE:
            return (uint8_t)input_config_get(0).edge_mode;
        default:
            return (uint8_t)input_config_get(1).edge_mode;
    }
}

static void option_apply(OptionField_t f, uint8_t index) {
    switch (f) {
        case OPT_CH1_TYPE:
        case OPT_CH2_TYPE: {
            uint8_t ch = (f == OPT_CH1_TYPE) ? 0 : 1;
            ChOpMode m = (index == 1) ? CH_MODE_TIMER : CH_MODE_COUNTER;
            ch_set_mode(ch, m);
            nvs_save_op_mode(ch, m);
            break;
        }
        case OPT_CH1_TOUTMODE:
        case OPT_CH2_TOUTMODE: {
            uint8_t ch = (f == OPT_CH1_TOUTMODE) ? 0 : 1;
            ct_timer_set_out_mode(ch, index == 1 ? TIMER_OUT_DELAY_OFF : TIMER_OUT_LATCH);
            save_timer_settings(ch);
            break;
        }
        case OPT_CH1_MODE:
            set_input_mode(0, (InputMode)index);
            break;
        case OPT_CH2_MODE:
            set_input_mode(1, (InputMode)index);
            break;
        case OPT_CH1_EDGE:
            set_edge_mode(0, (EdgeMode)index);
            break;
        case OPT_CH2_EDGE:
            set_edge_mode(1, (EdgeMode)index);
            break;
    }
}

static void open_options(OptionField_t f, Screen_t return_to) {
    s_opt_field = f;
    s_return_screen = return_to;
    goto_screen(SCREEN_OPTIONS);
}

static void draw_options() {
    OptionSpec_t spec = option_spec(s_opt_field);
    uint8_t current = option_current(s_opt_field);

    tft.fillScreen(TFT_BLACK);
    draw_title(spec.title);

    for (uint8_t i = 0; i < spec.count; i++) {
        bool active = (i == current);
        draw_row(i, spec.labels[i], active ? "* ACTIVE" : "",
                 active ? C_BTN_SEL : C_BTN_FACE);
    }
    draw_row(spec.count, "Back", "");
}

static void handle_options(uint16_t x, uint16_t y) {
    OptionSpec_t spec = option_spec(s_opt_field);

    for (uint8_t i = 0; i < spec.count; i++) {
        if (rect_hit(row_rect(i), x, y)) {
            option_apply(s_opt_field, i);
            goto_screen(s_return_screen);
            toast("Saved");
            return;
        }
    }

    if (rect_hit(row_rect(spec.count), x, y)) {
        goto_screen(s_return_screen);
    }
}

// =============================================================================
// CHANNEL CONFIG (CH1 / CH2)
// =============================================================================
// Both channels have the same configuration screen; only the channel index
// differs. The rows shown depend on the operating mode, so both variants fit on
// one screen without scrolling: counter mode shows mode/edge/preset, timer mode
// shows setpoint/delay-off/output mode.

// Channel index -> the screen and editor fields belonging to that channel.
static Screen_t ch_cfg_screen(uint8_t ch) {
    return ch == 0 ? SCREEN_CH1_CFG : SCREEN_CH2_CFG;
}

static void draw_ch_cfg(uint8_t ch) {
    bool timer_mode = (ch_get_mode(ch) == CH_MODE_TIMER);
    char buf[16];

    tft.fillScreen(TFT_BLACK);
    snprintf(buf, sizeof(buf), "CH%u CONFIG", (unsigned)(ch + 1));
    draw_title(buf);

    draw_row(0, "Type", timer_mode ? "TIMER" : "COUNTER");

    if (timer_mode) {
        ChTimerStatus_t t = ct_timer_get_status(ch);

        snprintf(buf, sizeof(buf), "%lu s", (unsigned long)t.setpoint_s);
        draw_row(1, "Setpoint", buf);

        snprintf(buf, sizeof(buf), "%lu s", (unsigned long)t.delay_off_s);
        draw_row(2, "Delay Off", buf);

        draw_row(3, "Out Mode",
                 t.out_mode == TIMER_OUT_DELAY_OFF ? "DELAY OFF" : "LATCH");
    } else {
        ChannelInputConfig_t cfg = input_config_get(ch);

        draw_row(1, "Count Mode", input_mode_to_string(cfg.input_mode));
        draw_row(2, "Edge", edge_to_string(cfg.edge_mode));

        snprintf(buf, sizeof(buf), "%ld", (long)get_preset(ch + 1));
        draw_row(3, "Preset", buf);
    }

    draw_row(4, "Back", "");
}

static void handle_ch_cfg(uint8_t ch, uint16_t x, uint16_t y) {
    const bool timer_mode = (ch_get_mode(ch) == CH_MODE_TIMER);
    const Screen_t back_to = ch_cfg_screen(ch);

    if (rect_hit(row_rect(0), x, y)) {
        open_options(ch == 0 ? OPT_CH1_TYPE : OPT_CH2_TYPE, back_to);
        return;
    }

    if (timer_mode) {
        ChTimerStatus_t t = ct_timer_get_status(ch);
        if (rect_hit(row_rect(1), x, y)) {
            open_keypad(ch == 0 ? KP_CH1_TSET : KP_CH2_TSET, "Setpoint (s)",
                        TIMER_SETPOINT_MAX_S, t.setpoint_s, back_to);
        } else if (rect_hit(row_rect(2), x, y)) {
            open_keypad(ch == 0 ? KP_CH1_TDELAY : KP_CH2_TDELAY, "Delay Off (s)",
                        TIMER_SETPOINT_MAX_S, t.delay_off_s, back_to);
        } else if (rect_hit(row_rect(3), x, y)) {
            open_options(ch == 0 ? OPT_CH1_TOUTMODE : OPT_CH2_TOUTMODE, back_to);
        }
    } else {
        if (rect_hit(row_rect(1), x, y)) {
            open_options(ch == 0 ? OPT_CH1_MODE : OPT_CH2_MODE, back_to);
        } else if (rect_hit(row_rect(2), x, y)) {
            open_options(ch == 0 ? OPT_CH1_EDGE : OPT_CH2_EDGE, back_to);
        } else if (rect_hit(row_rect(3), x, y)) {
            int32_t p = get_preset(ch + 1);
            // open_keypad() stores the title pointer, so it must outlive this
            // call - s_kp_title is only read while the keypad is on screen.
            static char preset_title[16];
            snprintf(preset_title, sizeof(preset_title), "CH%u Preset",
                     (unsigned)(ch + 1));
            open_keypad(ch == 0 ? KP_CH1_PRESET : KP_CH2_PRESET, preset_title,
                        999999999, p > 0 ? (uint32_t)p : 0, back_to);
        }
    }

    if (rect_hit(row_rect(4), x, y)) {
        goto_screen(SCREEN_MENU);
    }
}

// =============================================================================
// ACTIONS
// =============================================================================
// 3 columns x 4 rows of action buttons, mirroring the web control buttons.

#define ACT_COL_W   98
#define ACT_COL_GAP 5
#define ACT_X0      6
#define ACT_ROW_H   42
#define ACT_ROW_GAP 5
#define ACT_Y0      30

static Rect_t act_rect(int row, int col) {
    return Rect_t{(int16_t)(ACT_X0 + col * (ACT_COL_W + ACT_COL_GAP)),
                  (int16_t)(ACT_Y0 + row * (ACT_ROW_H + ACT_ROW_GAP)),
                  ACT_COL_W, ACT_ROW_H};
}

static void draw_actions() {
    tft.fillScreen(TFT_BLACK);
    draw_title("ACTIONS");

    draw_button(act_rect(0, 0), "CNT EN");
    draw_button(act_rect(0, 1), "CNT DIS");
    draw_button(act_rect(0, 2), "CNT RST");

    draw_button(act_rect(1, 0), "OUT1 ON");
    draw_button(act_rect(1, 1), "OUT1 OFF");
    draw_button(act_rect(1, 2), "OUT1 AUTO");

    draw_button(act_rect(2, 0), "OUT2 ON");
    draw_button(act_rect(2, 1), "OUT2 OFF");
    draw_button(act_rect(2, 2), "OUT2 AUTO");

    draw_button(act_rect(3, 0), "TMR1 RST");
    draw_button(act_rect(3, 1), "TMR2 RST");
    draw_button(act_rect(3, 2), "BACK");
}

static void set_relay(uint8_t channel, int mode) {
    CT_counter *c = getCounterInstance(channel);
    if (!c) return;
    if (mode < 0) c->clearManualOutputOverride();   // AUTO
    else c->setManualOutputState(mode > 0);
}

static void handle_actions(uint16_t x, uint16_t y) {
    // Row 0: master counter enable / disable / reset (same as the web buttons)
    if (rect_hit(act_rect(0, 0), x, y)) {
        pcnt_resume();
        CT_counter *c1 = getCounterInstance(1);
        CT_counter *c2 = getCounterInstance(2);
        if (c1) c1->enable();
        if (c2) c2->enable();
        continuous_mode = true;
        counter_enabled = true;
        toast("Counter enabled");
    } else if (rect_hit(act_rect(0, 1), x, y)) {
        pcnt_pause();
        CT_counter *c1 = getCounterInstance(1);
        CT_counter *c2 = getCounterInstance(2);
        if (c1) c1->disable();
        if (c2) c2->disable();
        continuous_mode = false;
        counter_enabled = false;
        toast("Counter disabled");
    } else if (rect_hit(act_rect(0, 2), x, y)) {
        pcnt_reset();
        s_ch1_last_count = 0;
        s_ch1_frequency_hz = 0;
        s_ch2_last_count = 0;
        s_ch2_frequency_hz = 0;
        CT_counter *c1 = getCounterInstance(1);
        CT_counter *c2 = getCounterInstance(2);
        if (c1) c1->reset();
        if (c2) c2->reset();
        toast("Counters reset");
    }

    // Rows 1-2: relay override
    else if (rect_hit(act_rect(1, 0), x, y)) { set_relay(1,  1); toast("OUT1 forced ON"); }
    else if (rect_hit(act_rect(1, 1), x, y)) { set_relay(1,  0); toast("OUT1 forced OFF"); }
    else if (rect_hit(act_rect(1, 2), x, y)) { set_relay(1, -1); toast("OUT1 automatic"); }
    else if (rect_hit(act_rect(2, 0), x, y)) { set_relay(2,  1); toast("OUT2 forced ON"); }
    else if (rect_hit(act_rect(2, 1), x, y)) { set_relay(2,  0); toast("OUT2 forced OFF"); }
    else if (rect_hit(act_rect(2, 2), x, y)) { set_relay(2, -1); toast("OUT2 automatic"); }

    // Row 3: per-channel countdown reset, back
    else if (rect_hit(act_rect(3, 0), x, y)) { ct_timer_reset(0); toast("Timer 1 reset"); }
    else if (rect_hit(act_rect(3, 1), x, y)) { ct_timer_reset(1); toast("Timer 2 reset"); }
    else if (rect_hit(act_rect(3, 2), x, y)) { goto_screen(SCREEN_MENU); }
}

// =============================================================================
// SYSTEM
// =============================================================================

static void draw_system() {
    tft.fillScreen(TFT_BLACK);
    draw_title("SYSTEM");
    draw_row(0, "Save to Flash", "");
    draw_row(1, "Factory Reset", "", C_BTN_WARN);
    draw_row(2, "Calibrate Touch", "");
    draw_row(3, "Back", "");
}

/**
 * @brief Collect the live state into a StoredConfig_t and persist it
 * Mirrors the /config/save web handler.
 */
static void save_all_to_flash() {
    StoredConfig_t cfg;
    ChannelInputConfig_t ch1 = input_config_get(0);
    ChannelInputConfig_t ch2 = input_config_get(1);
    ChTimerStatus_t tmr1 = ct_timer_get_status(0);
    ChTimerStatus_t tmr2 = ct_timer_get_status(1);

    cfg.ch1_input_mode = ch1.input_mode;
    cfg.ch1_edge_mode = ch1.edge_mode;
    cfg.ch1_filter = ch1.filter_value;
    cfg.ch1_preset_value = get_preset(1);

    cfg.ch1_op_mode = ch_get_mode(0);
    cfg.ch1_timer_setpoint_s = tmr1.setpoint_s;
    cfg.ch1_timer_delay_off_s = tmr1.delay_off_s;
    cfg.ch1_timer_out_mode = tmr1.out_mode;

    cfg.ch2_input_mode = ch2.input_mode;
    cfg.ch2_edge_mode = ch2.edge_mode;
    cfg.ch2_filter = ch2.filter_value;
    cfg.ch2_preset_value = get_preset(2);

    cfg.ch2_op_mode = ch_get_mode(1);
    cfg.ch2_timer_setpoint_s = tmr2.setpoint_s;
    cfg.ch2_timer_delay_off_s = tmr2.delay_off_s;
    cfg.ch2_timer_out_mode = tmr2.out_mode;

    cfg.modbus_address = MODBUS_SLAVE_ID;
    cfg.modbus_baud = MODBUS_BAUD_RATE;
    cfg.config_version = CONFIG_VERSION;

    toast(nvs_config_save(&cfg) ? "Saved to flash" : "Save failed");
}

/**
 * @brief Run the calibration wizard and persist the result
 */
static void run_calibration() {
    uint16_t cal[TOUCH_CAL_WORDS];
    ui_calibrate_touch(cal);
    tft.setTouch(cal);
    nvs_save_touch_cal(cal);
    goto_screen(SCREEN_SYSTEM);
}

static void handle_system(uint16_t x, uint16_t y) {
    if (rect_hit(row_rect(0), x, y)) {
        save_all_to_flash();
    } else if (rect_hit(row_rect(1), x, y)) {
        goto_screen(SCREEN_CONFIRM_FACTORY);
    } else if (rect_hit(row_rect(2), x, y)) {
        run_calibration();
    } else if (rect_hit(row_rect(3), x, y)) {
        goto_screen(SCREEN_MENU);
    }
}

// =============================================================================
// FACTORY RESET CONFIRMATION
// =============================================================================

static const Rect_t CONFIRM_NO  = {20,  150, 130, 50};
static const Rect_t CONFIRM_YES = {170, 150, 130, 50};

static void draw_confirm_factory() {
    tft.fillScreen(TFT_BLACK);
    draw_title("FACTORY RESET");

    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("Erase all settings and", SCR_W / 2, 60, 2);
    tft.drawString("restore the defaults?", SCR_W / 2, 82, 2);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString("Touch calibration is kept.", SCR_W / 2, 112, 2);
    tft.setTextDatum(TL_DATUM);

    draw_button(CONFIRM_NO, "NO");
    draw_button(CONFIRM_YES, "YES", C_BTN_WARN);
}

/**
 * @brief Clear NVS and re-apply defaults, mirroring the /config/factory handler
 */
static void do_factory_reset() {
    if (nvs_config_reset()) {
        StoredConfig_t cfg;
        nvs_config_get_defaults(&cfg);
        nvs_config_apply(&cfg);
        ct_timer_reset(0);
        ct_timer_reset(1);

        CT_counter *c1 = getCounterInstance(1);
        CT_counter *c2 = getCounterInstance(2);
        if (c1) { c1->setPresetValue(cfg.ch1_preset_value); c1->reset(); }
        if (c2) { c2->setPresetValue(cfg.ch2_preset_value); c2->reset(); }

        goto_screen(SCREEN_SYSTEM);
        toast("Factory reset done");
    } else {
        goto_screen(SCREEN_SYSTEM);
        toast("Reset failed");
    }
}

static void handle_confirm_factory(uint16_t x, uint16_t y) {
    if (rect_hit(CONFIRM_YES, x, y)) {
        do_factory_reset();
    } else if (rect_hit(CONFIRM_NO, x, y)) {
        goto_screen(SCREEN_SYSTEM);
    }
}

// =============================================================================
// PUBLIC INTERFACE
// =============================================================================

void ui_hmi_init() {
    uint16_t cal[TOUCH_CAL_WORDS];

    if (nvs_load_touch_cal(cal)) {
        tft.setTouch(cal);
        Serial.println("[HMI] Touch calibration loaded from NVS");
    } else {
        // First boot: nothing stored yet, so guide the user through the wizard
        // and keep the result for every later boot.
        Serial.println("[HMI] No touch calibration stored, running wizard");
        ui_calibrate_touch(cal);
        tft.setTouch(cal);
        nvs_save_touch_cal(cal);
    }

    s_screen = SCREEN_HOME;
    s_needs_redraw = true;
}

void ui_hmi_process() {
    // Repaint the active screen once after every navigation change.
    if (s_needs_redraw) {
        s_needs_redraw = false;
        switch (s_screen) {
            case SCREEN_HOME:             draw_home();             break;
            case SCREEN_MENU:             draw_menu();             break;
            case SCREEN_CH1_CFG:          draw_ch_cfg(0);          break;
            case SCREEN_CH2_CFG:          draw_ch_cfg(1);          break;
            case SCREEN_ACTIONS:          draw_actions();          break;
            case SCREEN_SYSTEM:           draw_system();           break;
            case SCREEN_KEYPAD:           draw_keypad();           break;
            case SCREEN_OPTIONS:          draw_options();          break;
            case SCREEN_CONFIRM_FACTORY:  draw_confirm_factory();  break;
        }
    }

    if (s_screen != SCREEN_HOME) {
        clear_toast_if_expired();
    }

    uint16_t x, y;
    if (!touch_pressed(&x, &y)) return;

    switch (s_screen) {
        case SCREEN_HOME:             handle_home(x, y);             break;
        case SCREEN_MENU:             handle_menu(x, y);             break;
        case SCREEN_CH1_CFG:          handle_ch_cfg(0, x, y);        break;
        case SCREEN_CH2_CFG:          handle_ch_cfg(1, x, y);        break;
        case SCREEN_ACTIONS:          handle_actions(x, y);          break;
        case SCREEN_SYSTEM:           handle_system(x, y);           break;
        case SCREEN_KEYPAD:           handle_keypad(x, y);           break;
        case SCREEN_OPTIONS:          handle_options(x, y);          break;
        case SCREEN_CONFIRM_FACTORY:  handle_confirm_factory(x, y);  break;
    }
}

bool ui_hmi_is_home() {
    return s_screen == SCREEN_HOME;
}
