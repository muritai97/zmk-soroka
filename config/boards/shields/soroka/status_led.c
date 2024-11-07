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
static float brightness_coef = 0.01;

static const struct led_rgb OFF = {0, 0, 0};
static const struct led_rgb PINK = {255, 20, 147};
static const struct led_rgb RED = {255, 0, 0};

// Красный крест для USB подключения
static const struct led_rgb usb_frames[][MATRIX_HEIGHT][MATRIX_WIDTH] = {
    {
        {OFF, OFF, RED, OFF, OFF},
        {OFF, RED, RED, RED, OFF},
        {RED, RED, RED, RED, RED},
        {OFF, RED, RED, RED, OFF},
        {OFF, OFF, RED, OFF, OFF}
    },
    {
        {OFF, OFF, OFF, OFF, OFF},
        {PINK, PINK, OFF, OFF, OFF},
        {OFF, PINK, PINK, OFF, OFF},
        {OFF, PINK, PINK, PINK, PINK},
        {OFF, OFF, PINK, OFF, OFF}
    }
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
// Work queue and periodic timer
#define ANIMATION_WORK_Q_STACK_SIZE 1024
#define ANIMATION_WORK_Q_PRIORITY 5
K_THREAD_STACK_DEFINE(animation_work_q_stack, ANIMATION_WORK_Q_STACK_SIZE);
struct k_work_q animation_work_q;
struct k_timer animation_timer;

enum animation_type {
    ANIMATION_NONE,
    ANIMATION_USB,
    ANIMATION_BATTERY,
};
static enum animation_type current_animation = ANIMATION_NONE;
static int current_frame = 0;

void clear_leds() {
    if (!device_is_ready(led_strip)) return;
    for (int i = 0; i < STRIP_NUM_PIXELS; i++) {
        pixels[i] = OFF;
    }
    led_strip_update_rgb(led_strip, pixels, STRIP_NUM_PIXELS);
}

void transition_to_frame(const struct led_rgb target_frame[MATRIX_HEIGHT][MATRIX_WIDTH]) {
    struct led_rgb current_frame[STRIP_NUM_PIXELS];
    for (int i = 0; i < STRIP_NUM_PIXELS; i++) {
        current_frame[i] = pixels[i];
    }

    for (int row = 0; row < MATRIX_HEIGHT; row++) {
        for (int col = 0; col < MATRIX_WIDTH; col++) {
            int index = row * MATRIX_WIDTH + col;
            pixels[index].r = round(
                current_frame[index].r + (target_frame[row][col].r - current_frame[index].r) / TRANSITION_STEPS * brightness_coef);
            pixels[index].g = round(
                current_frame[index].g + (target_frame[row][col].g - current_frame[index].g) / TRANSITION_STEPS * brightness_coef);
            pixels[index].b = round(
                current_frame[index].b + (target_frame[row][col].b - current_frame[index].b) / TRANSITION_STEPS * brightness_coef);
        }
    }
    led_strip_update_rgb(led_strip, pixels, STRIP_NUM_PIXELS);
}

void animate_frame() {
    const struct led_rgb (*frames)[MATRIX_HEIGHT][MATRIX_WIDTH];
    int num_frames;

    if (current_animation == ANIMATION_USB) {
        frames = usb_frames;
        num_frames = sizeof(usb_frames) / sizeof(usb_frames[0]);
    } else if (current_animation == ANIMATION_BATTERY) {
        frames = battery_frames;
        num_frames = sizeof(battery_frames) / sizeof(battery_frames[0]);
    } else {
        k_timer_stop(&animation_timer);
        return;
    }

    transition_to_frame(frames[current_frame]);
    current_frame = (current_frame + 1) % num_frames;
}

void show_usb_animation() {
    current_animation = ANIMATION_USB;
    current_frame = 0;
    k_timer_start(&animation_timer, K_MSEC(FRAME_DELAY_MS), K_MSEC(FRAME_DELAY_MS));
}

void show_battery_animation() {
    current_animation = ANIMATION_BATTERY;
    current_frame = 0;
    k_timer_start(&animation_timer, K_MSEC(FRAME_DELAY_MS), K_MSEC(FRAME_DELAY_MS));
}

void hide_animation() {
    k_timer_stop(&animation_timer);
    clear_leds();
}

int usb_listener(const zmk_event_t *eh) {
    const struct zmk_usb_conn_state_changed *usb_ev = as_zmk_usb_conn_state_changed(eh);
    if (usb_ev == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    usb_conn_state = usb_ev->conn_state;

    if (usb_ev->conn_state == ZMK_USB_CONN_POWERED) {
        show_usb_animation();
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(usb_listener, usb_listener);
ZMK_SUBSCRIPTION(usb_listener, zmk_usb_conn_state_changed);

static int init_led_matrix(const struct device *dev) {
    if (!device_is_ready(led_strip)) {
        return -1;
    }
    k_work_queue_init(&animation_work_q);
    k_work_queue_start(&animation_work_q, animation_work_q_stack,
                       K_THREAD_STACK_SIZEOF(animation_work_q_stack),
                       ANIMATION_WORK_Q_PRIORITY, NULL);
    k_timer_init(&animation_timer, animate_frame, NULL);
    set_brightness(brightness_coef);
    return 0;
}
SYS_INIT(init_led_matrix, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

void show_battery() {
    // k_work_schedule_for_queue(&animation_work_q, &battery_animation_work, K_NO_WAIT);
}

// Функция для отключения анимации батареи
void hide_battery() {
    // clear_leds();
}