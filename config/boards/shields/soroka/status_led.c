#include <zmk/events/activity_state_changed.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zephyr/drivers/led.h>
#include <math.h>
#include <stdlib.h>
#include <zmk/activity.h>
#include <zmk/battery.h>
#include <zmk/ble.h>
#include <zmk/endpoints.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/hid_indicators.h>
#include <zmk/keymap.h>
#include <zmk/usb.h>
#include <zmk/workqueue.h>
#include <zephyr/bluetooth/services/bas.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/drivers/led_strip.h>
#include <zmk/rgb_underglow.h>
#include <zmk/events/layer_state_changed.h>

#include <zephyr/drivers/led_strip.h>
#include <zmk/rgb_underglow.h>
#include <zephyr/kernel.h>
#include <math.h>

#include <zephyr/drivers/led_strip.h>
#include <zmk/rgb_underglow.h>
#include <zephyr/kernel.h>
#include <math.h>

#define STRIP_LABEL DT_LABEL(DT_CHOSEN(zmk_underglow))
#define STRIP_NUM_PIXELS DT_PROP(DT_CHOSEN(zmk_underglow), chain_length)

// #define STRIP_NUM_PIXELS 25
#define MATRIX_WIDTH 5
#define MATRIX_HEIGHT 5
#define FRAME_DELAY_MS 600  // Задержка между кадрами
#define TRANSITION_STEPS 20 // Шагов на переход для плавности

static const struct device *led_strip = DEVICE_DT_GET(DT_CHOSEN(zmk_underglow));
static struct led_rgb pixels[STRIP_NUM_PIXELS];
static float brightness_coef = 0.1;  // Значение по умолчанию (0.0 - 1.0)

// Цвета: выключенный и розовый для активного пикселя
static const struct led_rgb OFF_COLOR = {0, 0, 0};
static const struct led_rgb PINK_COLOR = {255, 20, 147};

// Определение кадров анимации (5x5)
static const struct led_rgb animation_frames[][MATRIX_HEIGHT][MATRIX_WIDTH] = {
    {
        {OFF_COLOR, PINK_COLOR, OFF_COLOR, PINK_COLOR, PINK_COLOR},
        {PINK_COLOR, OFF_COLOR, PINK_COLOR, OFF_COLOR, OFF_COLOR},
        {PINK_COLOR, OFF_COLOR, PINK_COLOR, PINK_COLOR, PINK_COLOR},
        {PINK_COLOR, PINK_COLOR, PINK_COLOR, OFF_COLOR, OFF_COLOR},
        {PINK_COLOR, OFF_COLOR, PINK_COLOR, PINK_COLOR, PINK_COLOR}
    },
    {
        {OFF_COLOR, OFF_COLOR, OFF_COLOR, OFF_COLOR, OFF_COLOR},
        {PINK_COLOR, PINK_COLOR, OFF_COLOR, OFF_COLOR, OFF_COLOR},
        {OFF_COLOR, PINK_COLOR, PINK_COLOR, OFF_COLOR, OFF_COLOR},
        {OFF_COLOR, PINK_COLOR, PINK_COLOR, PINK_COLOR, PINK_COLOR},
        {OFF_COLOR, OFF_COLOR, PINK_COLOR, OFF_COLOR, OFF_COLOR}
    },
    {
        {OFF_COLOR, PINK_COLOR, OFF_COLOR, PINK_COLOR, OFF_COLOR},
        {PINK_COLOR, OFF_COLOR, OFF_COLOR, OFF_COLOR, PINK_COLOR},
        {PINK_COLOR, OFF_COLOR, OFF_COLOR, OFF_COLOR, PINK_COLOR},
        {OFF_COLOR, PINK_COLOR, OFF_COLOR, PINK_COLOR, OFF_COLOR},
        {OFF_COLOR, OFF_COLOR, PINK_COLOR, OFF_COLOR, OFF_COLOR}
    },
    {
        {OFF_COLOR, PINK_COLOR, OFF_COLOR, PINK_COLOR, OFF_COLOR},
        {PINK_COLOR, PINK_COLOR, PINK_COLOR, PINK_COLOR, PINK_COLOR},
        {PINK_COLOR, PINK_COLOR, PINK_COLOR, PINK_COLOR, PINK_COLOR},
        {OFF_COLOR, PINK_COLOR, PINK_COLOR, PINK_COLOR, OFF_COLOR},
        {OFF_COLOR, OFF_COLOR, PINK_COLOR, OFF_COLOR, OFF_COLOR}
    }
};
// Функция для очистки светодиодов (выключение всех)
void clear_leds() {
    for (int i = 0; i < STRIP_NUM_PIXELS; i++) {
        pixels[i] = OFF_COLOR;
    }
    led_strip_update_rgb(led_strip, pixels, STRIP_NUM_PIXELS);
}

// Функция для плавного перехода между кадрами
void transition_to_frame(const struct led_rgb target_frame[MATRIX_HEIGHT][MATRIX_WIDTH]) {
    struct led_rgb current_frame[STRIP_NUM_PIXELS];
    for (int i = 0; i < STRIP_NUM_PIXELS; i++) {
        current_frame[i] = pixels[i];
    }

    for (int step = 1; step <= TRANSITION_STEPS; step++) {
        for (int row = 0; row < MATRIX_HEIGHT; row++) {
            for (int col = 0; col < MATRIX_WIDTH; col++) {
                int index = row * MATRIX_WIDTH + col;

                pixels[index].r = round(
                    current_frame[index].r + (target_frame[row][col].r - current_frame[index].r) * step / TRANSITION_STEPS * brightness_coef);
                pixels[index].g = round(
                    current_frame[index].g + (target_frame[row][col].g - current_frame[index].g) * step / TRANSITION_STEPS * brightness_coef);
                pixels[index].b = round(
                    current_frame[index].b + (target_frame[row][col].b - current_frame[index].b) * step / TRANSITION_STEPS * brightness_coef);
            }
        }
        led_strip_update_rgb(led_strip, pixels, STRIP_NUM_PIXELS);
        k_msleep(FRAME_DELAY_MS / TRANSITION_STEPS);
    }
    clear_leds();
}

// Функция анимации батареи
void show_battery_animation(struct k_work *work) {
    int num_frames = sizeof(animation_frames) / sizeof(animation_frames[0]);
    for (int frame = 0; frame < num_frames; frame++) {
        transition_to_frame(animation_frames[frame]);
        k_msleep(FRAME_DELAY_MS);
    }
}

// Установка яркости
void set_brightness(float coef) {
    brightness_coef = fmax(0.0, fmin(coef, 1.0));  // Ограничение в пределах 0-1
}

// Планировщик работы анимации
K_WORK_DELAYABLE_DEFINE(battery_animation_work, show_battery_animation);


// Функция для запуска анимации
void show_battery() {
    k_work_schedule(&battery_animation_work, K_NO_WAIT);
}

void hide_battery() {

}

// Инициализация анимации
void init_led_matrix() {
    set_brightness(0.5);  // Установить начальную яркость
}
// Инициализация системы
SYS_INIT(init_led_matrix, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);


