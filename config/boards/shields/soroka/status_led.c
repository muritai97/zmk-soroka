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

#define STRIP_LABEL DT_LABEL(DT_CHOSEN(zmk_underglow))
#define STRIP_NUM_PIXELS DT_PROP(DT_CHOSEN(zmk_underglow), chain_length)
#define MATRIX_WIDTH 5
#define MATRIX_HEIGHT 5
#define FRAME_DELAY_MS 600  
#define TRANSITION_STEPS 20 

static const struct device *led_strip = DEVICE_DT_GET(DT_CHOSEN(zmk_underglow));
static struct led_rgb pixels[STRIP_NUM_PIXELS];
static float brightness_coef = 0.1;

static const struct led_rgb OFF = {0, 0, 0};
static const struct led_rgb PINK = {255, 20, 147};
static const struct led_rgb RED = {255, 0, 0};

// Красный крест для USB подключения
static const struct led_rgb usb_frames[MATRIX_HEIGHT][MATRIX_WIDTH] = {
    {OFF, OFF, RED, OFF, OFF},
    {OFF, RED, RED, RED, OFF},
    {RED, RED, RED, RED, RED},
    {OFF, RED, RED, RED, OFF},
    {OFF, OFF, RED, OFF, OFF}
};

// Обычные кадры анимации
static const struct led_rgb battery_frames[][MATRIX_HEIGHT][MATRIX_WIDTH] = {
    {
        {OFF, PINK, OFF, PINK, PINK},
        {PINK, OFF, PINK, OFF, OFF},
        {PINK, OFF, PINK, PINK, PINK},
        {PINK, PINK, PINK, OFF, OFF},
        {PINK, OFF, PINK, PINK, PINK}
    },
    {
        {OFF, OFF, OFF, OFF, OFF},
        {PINK, PINK, OFF, OFF, OFF},
        {OFF, PINK, PINK, OFF, OFF},
        {OFF, PINK, PINK, PINK, PINK},
        {OFF, OFF, PINK, OFF, OFF}
    },
    {
        {OFF, PINK, OFF, PINK, OFF},
        {PINK, OFF, PINK, OFF, PINK},
        {PINK, OFF, OFF, OFF, PINK},
        {OFF, PINK, OFF, PINK, OFF},
        {OFF, OFF, PINK, OFF, OFF}
    },
    {
        {OFF, PINK, OFF, PINK, OFF},
        {PINK, PINK, PINK, PINK, PINK},
        {PINK, PINK, PINK, PINK, PINK},
        {OFF, PINK, PINK, PINK, OFF},
        {OFF, OFF, PINK, OFF, OFF}
    }
};
};

// Функция для очистки светодиодов
void clear_leds() {
    for (int i = 0; i < STRIP_NUM_PIXELS; i++) {
        pixels[i] = OFF;
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

// Функция для отображения красного креста при подключении USB
void show_usb_cross_animation(struct k_work *work) {
    transition_to_frame(usb_frames);
    k_msleep(FRAME_DELAY_MS);  // Показывать крест в течение одного кадра
}

// Планировщик для анимации USB
K_WORK_DELAYABLE_DEFINE(usb_animation_work, show_usb_cross_animation);

// Функция для анимации батареи
void show_battery_animation(struct k_work *work) {
    int num_frames = sizeof(battery_frames) / sizeof(battery_frames[0]);
    for (int frame = 0; frame < num_frames; frame++) {
        transition_to_frame(battery_frames[frame]);
        k_msleep(FRAME_DELAY_MS);
    }
}

// Установка яркости
void set_brightness(float coef) {
    brightness_coef = fmax(0.0, fmin(coef, 1.0));  
}

// Функция для запуска анимации батареи
void show_battery() {
    k_work_schedule(&battery_animation_work, K_NO_WAIT);
}

// Функция для запуска анимации USB
void start_usb_animation() {
    k_work_schedule(&usb_animation_work, K_NO_WAIT);
}

// Обработчик события подключения USB
int usb_listener(const zmk_event_t *eh) {
    const struct zmk_usb_conn_state_changed *usb_ev = as_zmk_usb_conn_state_changed(eh);
    if (usb_ev && usb_ev->conn_state != ZMK_USB_CONN_NONE) {
        start_usb_animation();
    }
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(usb_listener, usb_listener);
ZMK_SUBSCRIPTION(usb_listener, zmk_usb_conn_state_changed);

// Инициализация системы
void init_led_matrix() {
    set_brightness(0.5); 
}
SYS_INIT(init_led_matrix, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
