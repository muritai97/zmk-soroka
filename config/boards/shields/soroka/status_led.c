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

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);
#define LED_BATTERY_SHOW_DELAY 700  // Значение в миллисекундах

#define STRIP_LABEL DT_LABEL(DT_CHOSEN(zmk_underglow))
#define STRIP_NUM_PIXELS DT_PROP(DT_CHOSEN(zmk_underglow), chain_length)

static const struct device *led_strip = DEVICE_DT_GET(DT_CHOSEN(zmk_underglow));
static struct led_rgb pixels[STRIP_NUM_PIXELS];

static struct k_work_delayable show_battery_work;
static struct k_work_delayable hide_battery_work;

static const struct led_rgb colors[] = {
    { .r = 0,   .g = 128, .b = 0 },    // Зеленый (50% яркости)
    { .r = 128, .g = 0,   .b = 0 },    // Красный (50% яркости)
    { .r = 64,  .g = 0,   .b = 64 },   // Фиолетовый (50% яркости)
    { .r = 0,   .g = 0,   .b = 128 },  // Синий (50% яркости)
    { .r = 128, .g = 10,  .b = 74 }    // Розовый (50% яркости)
};

void show_battery_work_handler(struct k_work *work) {
    if (!device_is_ready(led_strip)) {
        LOG_ERR("LED strip device is not ready");
        return;
    }
    
    for (int i = 0; i < STRIP_NUM_PIXELS; i++) {
        pixels[i] = colors[i % ARRAY_SIZE(colors)];
    }
    led_strip_update_rgb(led_strip, pixels, STRIP_NUM_PIXELS);
    k_work_schedule(&hide_battery_work, K_MSEC(LED_BATTERY_SHOW_DELAY));
}

void hide_battery_work_handler(struct k_work *work) {
    if (!device_is_ready(led_strip)) {
        LOG_ERR("LED strip device is not ready");
        return;
    }

    for (int i = 0; i < STRIP_NUM_PIXELS; i++) {
        pixels[i] = (struct led_rgb){ .r = 0, .g = 0, .b = 0 };
    }
    led_strip_update_rgb(led_strip, pixels, STRIP_NUM_PIXELS);
}

void init_battery_leds() {
    k_work_init_delayable(&show_battery_work, show_battery_work_handler);
    k_work_init_delayable(&hide_battery_work, hide_battery_work_handler);
}

SYS_INIT(init_battery_leds, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

void show_battery() {
    k_work_schedule(&show_battery_work, K_NO_WAIT);
}

void hide_battery() {
    k_work_schedule(&hide_battery_work, K_NO_WAIT);
}
