/* Copyright 2023 Colin Lam (Ploopy Corporation)
 * Copyright 2020 Christopher Courtney, aka Drashna Jael're  (@drashna) <drashna@live.com>
 * Copyright 2019 Sunjun Kim
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include QMK_KEYBOARD_H

#include "rgblight.h"
#include "print.h"

#include "../../../common/tmag5273wheel.h"
#include "pointing_device_gestures.h"

/* ========================================================================= */
/* 定数                                                                      */
/* ========================================================================= */

/* スクロール処理の実行間隔 (ms)。 */
#define WHEEL_READ_INTERVAL_MS 10
/* デッドゾーン判定の余裕。TMAG5273_WHEEL_DEADZONE がこれより大きいこと。 */
#define WHEEL_DEADZONE_MARGIN 10
/* |delta| がこれ以下なら「動いていない」とみなす。 */
#define WHEEL_MOVE_THRESHOLD 2
/* 読み取りに失敗しているセンサーを再試行する間隔 (ms)。
   失敗中のI2C通信でメインループを塞がないよう、毎サイクルは試さない。 */
#define WHEEL_RETRY_INTERVAL_MS 200

/* ノブ押下を検出するマトリクス位置 (keymap の左ノブ/右ノブ)。 */
#define KNOB_ROW 0
#define LEFT_KNOB_COL 6
#define RIGHT_KNOB_COL 7

/* 起動LED: OS検出後の点灯時間と、OS検出が来なかった場合の保険タイムアウト。 */
#define STARTUP_LED_ON_MS 1000
#define STARTUP_LED_FALLBACK_MS 2000

/* mouse_report の v/h は WHEEL_EXTENDED_REPORT 有効時のみ int16_t。
   HIDレポートの論理最小値に合わせ、最小値は -128 / -32768 ではなく -127 / -32767 とする。 */
#ifdef WHEEL_EXTENDED_REPORT
#    define WHEEL_REPORT_MAX ((int32_t)INT16_MAX)
#    define WHEEL_REPORT_MIN ((int32_t)INT16_MIN + 1)
#else
#    define WHEEL_REPORT_MAX ((int32_t)INT8_MAX)
#    define WHEEL_REPORT_MIN ((int32_t)INT8_MIN + 1)
#endif

/* ========================================================================= */
/* ホイール状態                                                              */
/* ========================================================================= */

typedef struct {
    const uint8_t addr;    /* TMAG5273 の I2C アドレス */
    const pin_t   pwr_pin; /* センサーの電源制御ピン */

    bool     configured;      /* 起動時のセンサー初期化に成功したか (失敗なら再起動まで無効) */
    bool     valid;           /* position / deadzone_center が有効か (読み取り成功済みか) */
    uint32_t retry_timer;     /* 直近の失敗時刻 (再試行間隔の計測用) */
    uint16_t position;        /* 直近の角度 */
    uint16_t deadzone_center; /* デッドゾーン中心 */
    uint32_t idle_ms;         /* 無操作の継続時間 */
    bool     idle_handled;    /* 無操作によるデッドゾーン移動を実施済みか */

    int32_t lowres_tick; /* 低解像度スクロールの積算値 */
    int32_t highres_rem; /* 高解像度スクロールの余り */
} wheel_t;

static wheel_t left_wheel  = {.addr = TMAG5273_D0_I2C_ADDRESS, .pwr_pin = TMAG5273_D0_PWR_PIN};
static wheel_t right_wheel = {.addr = TMAG5273_D1_I2C_ADDRESS, .pwr_pin = TMAG5273_D1_PWR_PIN};

static uint32_t last_scroll_time = 0;

/* 起動LED。 */
static uint32_t startup_led_timer   = 0;
static uint32_t startup_led_timeout = STARTUP_LED_FALLBACK_MS;
static bool     startup_led_active  = false;

/* ========================================================================= */
/* LED レイヤー                                                              */
/* ========================================================================= */

const rgblight_segment_t PROGMEM mac_layer_colour[]     = RGBLIGHT_LAYER_SEGMENTS({0, 2, HSV_NAVGREEN});
const rgblight_segment_t PROGMEM windows_layer_colour[] = RGBLIGHT_LAYER_SEGMENTS({0, 2, HSV_NAVBLUE});
const rgblight_segment_t PROGMEM other_layer_colour[]   = RGBLIGHT_LAYER_SEGMENTS({0, 2, HSV_GESTUREYELLOW});

/* 起動時にホストOSを示すための点灯レイヤー。 */
const rgblight_segment_t *const PROGMEM my_rgb_layers[] = RGBLIGHT_LAYERS_LIST(mac_layer_colour, windows_layer_colour, other_layer_colour);

enum { MAC_LAYER_COLOUR = 0, WINDOWS_LAYER_COLOUR, OTHER_LAYER_COLOUR };

enum my_keycodes { PKC_GESTURE = SAFE_RANGE, PKC_DRAG_SCROLL };

enum { LAYER_BASE = 0 };

/* ========================================================================= */
/* OS 判定                                                                   */
/* ========================================================================= */

/* Cmd 系ショートカットを使うOS。 */
static inline bool host_is_apple(os_variant_t os) {
    return os == OS_MACOS || os == OS_IOS;
}

/* 高解像度スクロールを使うOS。macOS などは非対応のため低解像度にする。
   参考: https://github.com/qmk/qmk_firmware/issues/17585#issuecomment-2325248167 */
static inline bool host_uses_hires_scroll(os_variant_t os) {
    return os == OS_WINDOWS || os == OS_LINUX;
}

/* ========================================================================= */
/* ジェスチャー                                                              */
/* ========================================================================= */

/* サブモジュール側の既定関数を上書きし、方向ごとの動作をここで定義する。 */
void pointing_device_gestures_trigger(uint8_t direction) {
    const os_variant_t host_os  = detected_host_os();
    const bool         is_apple = host_is_apple(host_os);

    /* 方向は 0-7、東から時計回り。 */
    switch (direction) {
        case 0: /* East: 次の仮想デスクトップ */
            if (host_os == OS_WINDOWS) {
                tap_code16(LCG(KC_RIGHT)); /* CTRL + GUI + RIGHT */
            } else if (host_os == OS_LINUX) {
                tap_code16(LCA(KC_RIGHT)); /* CTRL + ALT + RIGHT */
            } else {
                tap_code16(LCTL(KC_RIGHT)); /* macOS: CTRL + RIGHT */
            }
            break;

        case 1: /* South-East: ペースト */
            tap_code16(is_apple ? G(KC_V) : C(KC_V));
            break;

        case 2: /* South: コピー */
            tap_code16(is_apple ? G(KC_C) : C(KC_C));
            break;

        case 3: /* South-West: カット */
            tap_code16(is_apple ? G(KC_X) : C(KC_X));
            break;

        case 4: /* West: 前の仮想デスクトップ */
            if (host_os == OS_WINDOWS) {
                tap_code16(LCG(KC_LEFT)); /* CTRL + GUI + LEFT */
            } else if (host_os == OS_LINUX) {
                tap_code16(LCA(KC_LEFT)); /* CTRL + ALT + LEFT */
            } else {
                tap_code16(LCTL(KC_LEFT)); /* macOS: CTRL + LEFT */
            }
            break;

        case 5: /* North-West: Undo */
            tap_code16(is_apple ? G(KC_Z) : C(KC_Z));
            break;

        case 6: /* North: Task View / Mission Control (Linux は未割り当て) */
            if (host_os == OS_WINDOWS) {
                tap_code16(LGUI(KC_TAB));
            } else if (is_apple) {
                tap_code16(LCTL(KC_UP));
            }
            break;

        case 7: /* North-East: Redo */
            tap_code16(is_apple ? LSG(KC_Z) : C(KC_Y));
            break;

        default:
            break;
    }
}

/* 上記関数を上書きしているため、配列自体はコンパイルを通すためのダミー。 */
const uint16_t PROGMEM pointing_device_gestures[NUM_GESTURE_DIRECTIONS] = GESTURES_CARDINAL_AND_ORDINAL_DIRECTIONS(KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO, KC_NO);

/* ========================================================================= */
/* キーマップ                                                                */
/* ========================================================================= */

// clang-format off
#define LAYER_TRANSPARENT LAYOUT( \
    _______, _______, _______, _______, \
    _______, _______, _______, _______)

const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    // キー配置は見たままの並び
    // 上段: 左上 | 左上中央 | 右上中央 | 右上
    // 下段: 右下 | 左下 | 左ノブ | 右ノブ

    [LAYER_BASE] = LAYOUT(
        MS_BTN4,     KC_NO,           MS_BTN3, MS_BTN5,
        PKC_GESTURE, PKC_DRAG_SCROLL, MS_BTN1, MS_BTN2
    ),
    [1] = LAYER_TRANSPARENT,
    [2] = LAYER_TRANSPARENT,
    [3] = LAYER_TRANSPARENT,
    [4] = LAYER_TRANSPARENT,
    [5] = LAYER_TRANSPARENT,
    [6] = LAYER_TRANSPARENT,
    [7] = LAYER_TRANSPARENT,
};
// clang-format on

/* ========================================================================= */
/* 初期化                                                                    */
/* ========================================================================= */

/* 角度の基準位置を現在角度に合わせ直し、状態をリセットする。 */
static void wheel_resync(wheel_t *w, uint16_t angle) {
    w->position        = angle;
    w->deadzone_center = angle;
    w->idle_ms         = 0;
    w->idle_handled    = false;
    w->lowres_tick     = 0;
    w->highres_rem     = 0;
    w->valid           = true;
}

/* センサーに給電し、初期化して現在角度を基準にする。
   センサーの初期化に失敗した場合は configured = false とし、そのセンサーだけ無効にする
   (もう一方は影響を受けない)。
   初期角度の読み取りに失敗した場合は valid = false のままにし、
   wheel_poll() が最初に読み取りに成功した時点で基準を設定する。 */
static void wheel_init(wheel_t *w) {
    w->valid       = false;
    w->retry_timer = timer_read32();

    gpio_write_pin_high(w->pwr_pin);
    wait_us(370); /* TMAG tstart_power_up + 100us の余裕 */

    w->configured = tmag5273_init_device(w->addr);
    if (!w->configured) {
        dprintf("Wheel 0x%02X: sensor init failed, disabled\n", w->addr);
        return;
    }
    wait_ms(20);

    uint16_t angle;
    if (tmag5273_read_angle(w->addr, &angle)) {
        wheel_resync(w, angle);
    } else {
        w->retry_timer = timer_read32();
        dprintf("Wheel 0x%02X: initial angle read failed\n", w->addr);
    }
}

void keyboard_pre_init_user(void) {
    /* 両センサーの電源ピンを出力にして OFF にしておく。 */
    wheel_t *const wheels[] = {&left_wheel, &right_wheel};
    for (uint8_t i = 0; i < ARRAY_SIZE(wheels); i++) {
        gpio_set_pin_output_push_pull(wheels[i]->pwr_pin);
        gpio_write_pin_low(wheels[i]->pwr_pin);
    }
}

void keyboard_post_init_user(void) {
    /* ---- デバッグ ----
       有効にする場合は post_rules.mk の CONSOLE_ENABLE = yes も有効にすること。 */
    // debug_enable   = true;
    // debug_matrix   = true;
    // debug_keyboard = true;
    // debug_mouse    = true;

    /* ---- スクロールホイール ---- */
    tmag5273_init();
    wheel_init(&left_wheel);
    wheel_init(&right_wheel);
    last_scroll_time = timer_read32();

    /* ---- LED ---- */
    rgblight_layers = my_rgb_layers;

    rgblight_enable_noeeprom();
    rgblight_mode_noeeprom(RGBLIGHT_MODE_STATIC_LIGHT);
    rgblight_sethsv_noeeprom(0, 0, RGBLIGHT_VAL_STEP * 4);

    /* OS検出が来なくても消灯できるよう、保険のタイマーを始動する。 */
    startup_led_timer   = timer_read32();
    startup_led_timeout = STARTUP_LED_FALLBACK_MS;
    startup_led_active  = true;
}

/* ========================================================================= */
/* キー処理                                                                  */
/* ========================================================================= */

bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    switch (keycode) {
        case PKC_GESTURE:
            if (record->event.pressed) {
                pointing_device_gestures_start();
            } else {
                pointing_device_gestures_end();
            }
            return false;

        case PKC_DRAG_SCROLL:
            is_drag_scroll = record->event.pressed;
            return false;

        default:
            return true;
    }
}

/* ========================================================================= */
/* スクロール処理                                                            */
/* ========================================================================= */

static inline bool wheel_outside_deadzone(int16_t distance) {
    const int16_t limit = TMAG5273_WHEEL_DEADZONE - WHEEL_DEADZONE_MARGIN;
    return distance > limit || distance < -limit;
}

static inline void wheel_clear_accumulators(wheel_t *w) {
    w->lowres_tick = 0;
    w->highres_rem = 0;
}

/* 角度を読み取り、変化量を返す。デッドゾーン外かどうかは *outside に入る。
   一定時間動きがなければ、デッドゾーン中心を現在位置へ移す (誤スクロール防止)。

   読み取りに失敗したサイクルは、そのセンサーだけスクロールなし (0, outside = false) とする。
   失敗後に最初に成功した読み取りは基準位置の再設定にのみ使い、スクロールは出さない
   (失敗前後の角度差による突然のスクロールを防ぐ)。左右のセンサーは独立して扱う。 */
static int16_t wheel_poll(wheel_t *w, uint32_t elapsed_ms, bool *outside) {
    uint16_t raw;

    /* 初期化に失敗したセンサー、または再試行間隔の待機中は何もしない。 */
    if (!w->configured || (!w->valid && timer_elapsed32(w->retry_timer) < WHEEL_RETRY_INTERVAL_MS)) {
        *outside = false;
        return 0;
    }

    if (!tmag5273_read_angle(w->addr, &raw)) {
        if (w->valid) {
            dprintf("Wheel 0x%02X: angle read failed\n", w->addr);
        }
        w->valid       = false;
        w->retry_timer = timer_read32();
        wheel_clear_accumulators(w);
        *outside = false;
        return 0;
    }

    if (!w->valid) {
        /* 初回、または失敗からの復帰。ここで基準を取り直す。 */
        wheel_resync(w, raw);
        dprintf("Wheel 0x%02X: angle read OK, resynced to %u\n", w->addr, (unsigned)raw);
        *outside = false;
        return 0;
    }

    *outside = wheel_outside_deadzone(calculate_deadzone_distance(raw, &w->deadzone_center));

    const int16_t delta = calculate_wheel_delta(raw, w->position);
    w->position         = raw;

    if (abs(delta) > WHEEL_MOVE_THRESHOLD) {
        w->idle_ms      = 0;
        w->idle_handled = false;
    } else if (!w->idle_handled) {
        w->idle_ms += elapsed_ms;
        if (w->idle_ms >= TMAG5273_DEADZONE_PROTECTOR_TIMEOUT) {
            w->deadzone_center = w->position;
            w->idle_handled    = true; /* 動くまでは1回だけ移動する */
            dprintf("Wheel 0x%02X idle, deadzone moved to: %u\n", w->addr, (unsigned)w->deadzone_center);
        }
    }

    return delta;
}

/* 変化量をスクロール量に変換する。
   sign: 回転方向の向き (+1 / -1)。戻り値の正負は mouse_report の v/h と同じ向き。
   余りは次回に持ち越し、デッドゾーン内では積算をクリアする。 */
static int16_t wheel_scroll(wheel_t *w, int16_t delta, int8_t sign, bool outside, bool hires, int32_t speed_div) {
    if (!outside) {
        wheel_clear_accumulators(w);
        return 0;
    }

    const int32_t signed_delta = (int32_t)sign * delta;

    if (hires) {
        const int32_t total = w->highres_rem + signed_delta;
        int32_t       ticks = total / speed_div;

        if (ticks > WHEEL_REPORT_MAX || ticks < WHEEL_REPORT_MIN) {
            /* センサーのグリッチ等で巨大な値が出た場合は、頭打ちにして余りを捨てる。 */
            ticks          = (ticks > 0) ? WHEEL_REPORT_MAX : WHEEL_REPORT_MIN;
            w->highres_rem = 0;
        } else {
            w->highres_rem = total % speed_div;
        }
        return (int16_t)ticks;
    }

    w->lowres_tick += signed_delta;
    if (w->lowres_tick >= TMAG5273_LOWRES_TICK_SIZE) {
        w->lowres_tick -= TMAG5273_LOWRES_TICK_SIZE;
        return 1;
    }
    if (w->lowres_tick <= -TMAG5273_LOWRES_TICK_SIZE) {
        w->lowres_tick += TMAG5273_LOWRES_TICK_SIZE;
        return -1;
    }
    return 0;
}

report_mouse_t pointing_device_task_user(report_mouse_t mouse_report) {
    /* 読み取りを間引く。 */
    const uint32_t elapsed_ms = timer_elapsed32(last_scroll_time);
    if (elapsed_ms <= WHEEL_READ_INTERVAL_MS) {
        return mouse_report;
    }
    last_scroll_time = timer_read32();

    const bool hires = host_uses_hires_scroll(detected_host_os());

    bool          left_outside, right_outside;
    const int16_t left_delta  = wheel_poll(&left_wheel, elapsed_ms, &left_outside);
    const int16_t right_delta = wheel_poll(&right_wheel, elapsed_ms, &right_outside);

    /* 左ホイール: 縦スクロール (向きは反転)。右ホイール: 横スクロール。 */
    const int16_t v = wheel_scroll(&left_wheel, left_delta, -1, left_outside, hires, TMAG5273_VERTICAL_WHEEL_SPEED_DIV);
    const int16_t h = wheel_scroll(&right_wheel, right_delta, +1, right_outside, hires, TMAG5273_HORIZONAL_WHEEL_SPEED_DIV);

    /* ノブ押下中はスクロールを出さず、積算もクリアする。 */
    if (matrix_is_on(KNOB_ROW, LEFT_KNOB_COL)) {
        mouse_report.v = 0;
        wheel_clear_accumulators(&left_wheel);
    } else if (v != 0) {
        mouse_report.v = v;
    }

    if (matrix_is_on(KNOB_ROW, RIGHT_KNOB_COL)) {
        mouse_report.h = 0;
        wheel_clear_accumulators(&right_wheel);
    } else if (h != 0) {
        mouse_report.h = h;
    }

    return mouse_report;
}

/* ========================================================================= */
/* 起動時のOS表示LED                                                         */
/* ========================================================================= */

static void startup_led_layers_off(void) {
    rgblight_set_layer_state(MAC_LAYER_COLOUR, false);
    rgblight_set_layer_state(WINDOWS_LAYER_COLOUR, false);
    rgblight_set_layer_state(OTHER_LAYER_COLOUR, false);
}

/* OS検出が確定したら、そのOSに応じた色を1秒だけ点灯する。 */
bool process_detected_host_os_user(os_variant_t detected_os) {
    /* 検出が安定するまで待つ (保険タイマーは post_init で動いている)。 */
    if (detected_os == OS_UNSURE) {
        return true;
    }

    /* 消灯済みでも再点灯できるようにする (KVM切替などでの再検出対策)。 */
    rgblight_enable_noeeprom();
    startup_led_layers_off();

    switch (detected_os) {
        case OS_MACOS:
        case OS_IOS:
            rgblight_set_layer_state(MAC_LAYER_COLOUR, true);
            break;

        case OS_WINDOWS:
            rgblight_set_layer_state(WINDOWS_LAYER_COLOUR, true);
            break;

        default:
            rgblight_set_layer_state(OTHER_LAYER_COLOUR, true);
            break;
    }

    startup_led_timer   = timer_read32();
    startup_led_timeout = STARTUP_LED_ON_MS;
    startup_led_active  = true;

    return true;
}

/* 時間が経ったらLEDを消す。 */
void housekeeping_task_user(void) {
    if (startup_led_active && timer_elapsed32(startup_led_timer) >= startup_led_timeout) {
        startup_led_layers_off();
        rgblight_disable_noeeprom();
        startup_led_active = false;
    }
}