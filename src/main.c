#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/net_event.h>
#include <errno.h>
#include "ping.h"
#include "wifi.h"
#include "mqtt.h"
#include <zephyr/sys/poweroff.h>
#include <esp_sleep.h>
#include "moisture.h"
#include "room.h"
#include <zephyr/logging/log.h>
#include <zephyr/pm/pm.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/policy.h>
#include <zephyr/drivers/gpio.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define LED_BLINK_DELAY_MS	500
#define MQTT_DISCONNECT_DELAY_MS	1000
#define DEEP_SLEEP_DURATION_SEC	60

static const struct gpio_dt_spec bme_pwr = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), bme_pwr_gpios);

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

const struct pm_state_info *pm_policy_next_state(uint8_t cpu, int32_t ticks)
{
    /* Returning NULL tells the kernel: "Do not enter any sleep state" */
    /* This keeps the CPU and WiFi radio fully powered. */
    return NULL;
}

void enter_deep_sleep(uint32_t seconds) {
    esp_sleep_enable_timer_wakeup(seconds * 1000000ULL);
    
    sys_poweroff();
    
    k_sleep(K_FOREVER);
}

int init_led(void) {
    if (!gpio_is_ready_dt(&led)) {
        return -ENODEV;
    }

    /* Configure as output, start in the logically INACTIVE state (LED OFF) */
    return gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
}

void blink_led(int times, int delay_ms) {
    for (int i = 0; i < times; i++) {
        gpio_pin_toggle_dt(&led);
        k_msleep(delay_ms);
    }
}

int main(void)
{
    pm_policy_state_lock_get(PM_STATE_SUSPEND_TO_IDLE, PM_ALL_SUBSTATES);
    pm_policy_state_lock_get(PM_STATE_STANDBY, PM_ALL_SUBSTATES);
    
    if (!gpio_is_ready_dt(&bme_pwr)) {
        return -ENODEV;
    }

    int err = gpio_pin_configure_dt(&bme_pwr, GPIO_OUTPUT_INACTIVE);
    if (err < 0) return err;

    /* 2. Power on the sensor */
    LOG_INF("Powering on BME280 via GPIO5...");
    gpio_pin_set_dt(&bme_pwr, 1);

    if (init_led() != 0) {
        LOG_ERR("Failed to init LED");
        goto cleanup;
    }

    if (wifi_init() != 0) {
        LOG_ERR("WiFi init failed");
        blink_led(10, LED_BLINK_DELAY_MS);
        goto cleanup;
    }

    if (wifi_connect() != 0) {
        LOG_ERR("WiFi connect failed");
        blink_led(10, LED_BLINK_DELAY_MS);
        goto cleanup;
    }

    if(wifi_wait_for_connect()) {
        LOG_ERR("Connection failed");
        blink_led(10, LED_BLINK_DELAY_MS);
        goto cleanup;
    }

    if (wifi_status() != 0) {
        LOG_ERR("WiFi status failed");
        blink_led(10, LED_BLINK_DELAY_MS);
        goto cleanup;
    }

    if (wifi_wait_for_ipv4()) {
        LOG_ERR("IPV4 failed");
        blink_led(10, LED_BLINK_DELAY_MS);
        goto cleanup;
    }
    LOG_INF("Ready...");

    ping("192.168.178.30", 4);

    if (mqtt_service_init() != 0) {
        LOG_ERR("MQTT init failed");
        blink_led(5, MQTT_DISCONNECT_DELAY_MS);
        goto cleanup;
    }

    if (mqtt_service_connect() != 0) {
        LOG_ERR("MQTT connect failed");
        blink_led(5, MQTT_DISCONNECT_DELAY_MS);
        goto cleanup;
    }

#ifdef CONFIG_MOISTURE_SENSOR
    if (moisture_init() != 0) {
        LOG_ERR("Moisture init failed");
        blink_led(4, 2000);
        goto cleanup;
    }

    int moisture = moisture_read_percent();
    if (moisture >= 0 && moisture <= 100) {
        if (mqtt_service_publish_sensor(SENSOR_SOIL, (float)moisture) != 0) {
            blink_led(4, 2000);
            LOG_ERR("MQTT publish failed");
        }
    } else {
        blink_led(4, 2000);
        LOG_ERR("Invalid moisture reading: %d", moisture);
    }
#endif /* CONFIG_MOISTURE_SENSOR */

#ifdef CONFIG_ROOM_SENSOR

    float t, p, h;

    if (read_bme280(&t, &p, &h) == 0) {
        LOG_INF("Temperature: %f C", (double)t);
        LOG_INF("Pressure:    %f kPa", (double)p);
        LOG_INF("Humidity:    %f %%", (double)h);
        if (mqtt_service_publish_sensor(SENSOR_TEMP, t) != 0) {
            LOG_ERR("MQTT publish failed for temperature");
            blink_led(4, 2000);
            k_sleep(K_MSEC(500));
        }
        if (mqtt_service_publish_sensor(SENSOR_PRESS, p) != 0) {
            LOG_ERR("MQTT publish failed for pressure");
            blink_led(4, 2000);
            k_sleep(K_MSEC(500));
        }
        if (mqtt_service_publish_sensor(SENSOR_HUM, h)  != 0) {
            LOG_ERR("MQTT publish failed for humidity");
            blink_led(4, 2000);
            k_sleep(K_MSEC(500));
        }
    } else {
        LOG_ERR("Could not read BME280 sensor");
        blink_led(4, 2000);
    }
#endif /* CONFIG_ROOM_SENSOR */

    k_sleep(K_SECONDS(1));
    mqtt_service_disconnect();
    wifi_disconnect();

cleanup:
    pm_policy_state_lock_put(PM_STATE_SUSPEND_TO_IDLE, PM_ALL_SUBSTATES);
    enter_deep_sleep(DEEP_SLEEP_DURATION_SEC);
    return 0;
}

