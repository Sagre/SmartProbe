#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/net_event.h>
#include <errno.h>
#include <string.h>
#include "ping.h"
#include "wifi.h"
#include "mqtt.h"
#include <zephyr/sys/poweroff.h>
#include <esp_sleep.h>
#include "moisture.h"
#include "room.h"
#include "sensor_module.h"
#include <zephyr/logging/log.h>
#include <zephyr/pm/pm.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/policy.h>
#include <zephyr/drivers/gpio.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define LED_BLINK_DELAY_MS	500
#define MQTT_DISCONNECT_DELAY_MS	1000
#define DEEP_SLEEP_DURATION_SEC	60

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

static const struct sensor_module sensors[] = {
#ifdef CONFIG_MOISTURE_SENSOR
    {
        .key = "soil",
        .label = "Soil",
        .unit = "%",
        .init = moisture_init,
        .read = moisture_read_percent,
        .err_value = MOISTURE_ERROR_VALUE,
    },
#endif
#ifdef CONFIG_ROOM_SENSOR
    {
        .key = "temp",
        .label = "Temperature",
        .unit = "C°",
        .init = room_init,
        .read = room_read_temperature,
        .err_value = ROOM_ERROR_VALUE,
    },
    {
        .key = "press",
        .label = "Pressure",
        .unit = "kPa",
        .init = room_init,
        .read = room_read_pressure,
        .err_value = ROOM_ERROR_VALUE,
    },
    {
        .key = "hum",
        .label = "Humidity",
        .unit = "%",
        .init = room_init,
        .read = room_read_humidity,
        .err_value = ROOM_ERROR_VALUE,
    },
#endif
};

static int initialize_sensors(void)
{
    for (size_t i = 0; i < ARRAY_SIZE(sensors); i++) {
        if (sensors[i].init) {
            int err = sensors[i].init();
            if (err != 0) {
                LOG_ERR("Sensor %s init failed: %d", sensors[i].key, err);
                return err;
            }
        }
    }
    return 0;
}

static enum sensor_e sensor_key_to_enum(const char *key)
{
    if (strcmp(key, "soil") == 0) {
        return SENSOR_SOIL;
    }
    if (strcmp(key, "temp") == 0) {
        return SENSOR_TEMP;
    }
    if (strcmp(key, "press") == 0) {
        return SENSOR_PRESS;
    }
    if (strcmp(key, "hum") == 0) {
        return SENSOR_HUM;
    }

    return 0;
}

static void read_and_publish_sensors(void)
{
    for (size_t i = 0; i < ARRAY_SIZE(sensors); i++) {
        float value;
        int err = sensors[i].read(&value);
        if (err != 0 || value == sensors[i].err_value) {
            LOG_ERR("Failed to read %s sensor: %d", sensors[i].key, err);
            continue;
        }

        enum sensor_e sensor_id = sensor_key_to_enum(sensors[i].key);
        if (sensor_id == 0) {
            LOG_ERR("Unknown sensor key for MQTT publish: %s", sensors[i].key);
            continue;
        }

        if (mqtt_service_publish_sensor(sensor_id, value) != 0) {
            LOG_ERR("MQTT publish failed for %s", sensors[i].key);
            blink_led(4, 2000);
        }
    }
}

int main(void)
{
    pm_policy_state_lock_get(PM_STATE_SUSPEND_TO_IDLE, PM_ALL_SUBSTATES);
    pm_policy_state_lock_get(PM_STATE_STANDBY, PM_ALL_SUBSTATES);
    
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

    if (initialize_sensors() != 0) {
        LOG_ERR("Sensor initialization failed");
        blink_led(5, MQTT_DISCONNECT_DELAY_MS);
        goto cleanup;
    }

    read_and_publish_sensors();

    k_sleep(K_SECONDS(1));
    mqtt_service_disconnect();
    wifi_disconnect();

cleanup:
    pm_policy_state_lock_put(PM_STATE_SUSPEND_TO_IDLE, PM_ALL_SUBSTATES);
    enter_deep_sleep(DEEP_SLEEP_DURATION_SEC);
    return 0;
}

