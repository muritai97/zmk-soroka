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

static enum zmk_usb_conn_state usb_conn_state = ZMK_USB_CONN_NONE;
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

// Переменные для планировщика плавного перехода
static const struct led_rgb (*target_frame)[MATRIX_WIDTH];
static int current_step;

// Функция для обновления состояния перехода
void scheduled_transition_step(struct k_work *work) {
    for (int row = 0; row < MATRIX_HEIGHT; row++) {
        for (int col = 0; col < MATRIX_WIDTH; col++) {
            int index = row * MATRIX_WIDTH + col;

            pixels[index].r = round(
                pixels[index].r + (target_frame[row][col].r - pixels[index].r) * current_step / TRANSITION_STEPS * brightness_coef);
            pixels[index].g = round(
                pixels[index].g + (target_frame[row][col].g - pixels[index].g) * current_step / TRANSITION_STEPS * brightness_coef);
            pixels[index].b = round(
                pixels[index].b + (target_frame[row][col].b - pixels[index].b) * current_step / TRANSITION_STEPS * brightness_coef);
        }
    }
    led_strip_update_rgb(led_strip, pixels, STRIP_NUM_PIXELS);

    if (++current_step <= TRANSITION_STEPS) {
        k_work_schedule(&transition_work, K_MSEC(FRAME_DELAY_MS / TRANSITION_STEPS));
    } else {
        k_work_schedule(&clear_leds_work, K_NO_WAIT);  // Очищаем светодиоды после завершения
    }
}

// Определение планировщика для перехода
K_WORK_DELAYABLE_DEFINE(transition_work, scheduled_transition_step);

// Функция для запуска перехода
void start_transition_to_frame(const struct led_rgb frame[MATRIX_HEIGHT][MATRIX_WIDTH]) {
    target_frame = frame;
    current_step = 1;
    k_work_schedule(&transition_work, K_NO_WAIT);
}

// Функция для очистки светодиодов через планировщик
void scheduled_clear_leds(struct k_work *work) {
    for (int i = 0; i < STRIP_NUM_PIXELS; i++) {
        pixels[i] = OFF;
    }
    led_strip_update_rgb(led_strip, pixels, STRIP_NUM_PIXELS);
}

// Определение планировщика для очистки светодиодов
K_WORK_DELAYABLE_DEFINE(clear_leds_work, scheduled_clear_leds);

// Функция для отображения красного креста при подключении USB
void show_usb_animation(struct k_work *work) {
    start_transition_to_frame(usb_frames);
    k_msleep(FRAME_DELAY_MS);  // Показывать крест в течение одного кадра
    return;
}

// Планировщик для анимации USB
K_WORK_DELAYABLE_DEFINE(usb_animation_work, show_usb_animation);

// Функция для анимации батареи
void show_battery_animation(struct k_work *work) {
    int num_frames = sizeof(battery_frames) / sizeof(battery_frames[0]);
    for (int frame = 0; frame < num_frames; frame++) {
        start_transition_to_frame(battery_frames[frame]);
        k_msleep(FRAME_DELAY_MS);
    }
    return;
}

// Установка яркости
void set_brightness(float coef) {
    brightness_coef = fmax(0.0, fmin(coef, 1.0));  
}

// Планировщик работы анимации
K_WORK_DELAYABLE_DEFINE(battery_animation_work, show_battery_animation);

// Функция для запуска анимации батареи
void show_battery() {
    k_work_schedule(&battery_animation_work, K_NO_WAIT);
    return;
}

// Функция для отключения анимации батареи
void hide_battery() {
    k_work_schedule(&clear_leds_work, K_NO_WAIT);
    return;
}

// Обработчик события подключения USB
int usb_listener(const zmk_event_t *eh) {
    const struct zmk_usb_conn_state_changed *usb_ev = as_zmk_usb_conn_state_changed(eh);
    if (usb_ev == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    usb_conn_state = usb_ev->conn_state;

    if (usb_ev->conn_state != ZMK_USB_CONN_NONE) {
        k_work_schedule(&usb_animation_work, K_NO_WAIT);
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(usb_listener, usb_listener);
ZMK_SUBSCRIPTION(usb_listener, zmk_usb_conn_state_changed);

// Инициализация системы
void init_led_matrix() {
    if (!device_is_ready(led_strip)) {
        return;
    }
    set_brightness(0.5); 
}
SYS_INIT(init_led_matrix, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
