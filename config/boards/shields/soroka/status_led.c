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
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

// timeout
#define LED_BLINK_PROFILE_DELAY 200
#define LED_BLINK_CONN_DELAY 140
#define LED_FADE_DELAY 2
#define LED_BATTERY_SHOW_DELAY 700
#define LED_BATTERY_SLEEP_SHOW 1000
#define LED_BATTERY_BLINK_DELAY 200
#define LED_USB_BLINK_DELAY 200

#define LED_STATUS_ON 100
#define LED_STATUS_OFF 0
#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>

#include <math.h>
#include <stdlib.h>

#include <zephyr/drivers/led_strip.h>
#include <zmk/rgb_underglow.h>

#include <zmk/activity.h>
#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/keymap.h>

#define STRIP_LABEL DT_LABEL(DT_CHOSEN(zmk_underglow))
#define STRIP_NUM_PIXELS 25

#define STRIP_CHOSEN DT_CHOSEN(zmk_underglow)
#define STRIP_NUM_PIXELS DT_PROP(STRIP_CHOSEN, chain_length)

static const struct device *led_strip = DEVICE_DT_GET(STRIP_CHOSEN);
static struct led_rgb pixels[STRIP_NUM_PIXELS];

// Определение работ для включения и выключения
static struct k_work_delayable show_battery_work;
static struct k_work_delayable hide_battery_work;

// Определение цветов: зеленый, красный, фиолетовый, синий, розовый
static const struct led_rgb colors[] = {
    { .r = 0,   .g = 255, .b = 0 },    // Зеленый
    { .r = 255, .g = 0,   .b = 0 },    // Красный
    { .r = 128, .g = 0,   .b = 128 },  // Фиолетовый
    { .r = 0,   .g = 0,   .b = 255 },  // Синий
    { .r = 255, .g = 20,  .b = 147 }   // Розовый
};

void show_battery_work_handler(struct k_work *work) {
    // Установка цветов для каждого из 25 светодиодов
    for (int i = 0; i < STRIP_NUM_PIXELS; i++) {
        pixels[i] = colors[i % 5];
    }
    led_strip_update_rgb(led_strip, pixels, STRIP_NUM_PIXELS);
}

void hide_battery_work_handler(struct k_work *work) {
    for (int i = 0; i < STRIP_NUM_PIXELS; i++) {
        pixels[i] = (struct led_rgb){ .r = 0, .g = 0, .b = 0 };
    }
    led_strip_update_rgb(led_strip, pixels, STRIP_NUM_PIXELS);
}
// Инициализация работ
void init_battery_leds() {
    k_work_init_delayable(&show_battery_work, show_battery_work_handler);
}

// Вызывается при инициализации системы
SYS_INIT(init_battery_leds, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
void show_battery() {
    // Планирование работы show_battery через 0 мс
    k_work_schedule(&show_battery_work, K_NO_WAIT);
}

void hide_battery() {
    // Планирование работы hide_battery через 0 мс
    k_work_schedule(&hide_battery_work, K_NO_WAIT);
}

