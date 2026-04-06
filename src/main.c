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
#include <zephyr/logging/log.h>
#include <zephyr/pm/pm.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/policy.h>
#include <zephyr/drivers/gpio.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

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
    
    if (init_led() != 0) {
        LOG_ERR("Failed to init LED");
        goto cleanup;
    }

    if (wifi_init() != 0) {
        LOG_ERR("WiFi init failed");
        blink_led(10, 500);
        goto cleanup;
    }

    if (wifi_connect() != 0) {
        LOG_ERR("WiFi connect failed");
        blink_led(10, 500);
        goto cleanup;
    }

    wifi_wait_for_connect();

    if (wifi_status() != 0) {
        LOG_ERR("WiFi status failed");
        blink_led(10, 500);
        goto cleanup;
    }

    wifi_wait_for_ipv4();
    LOG_INF("Ready...");

    ping("192.168.178.30", 4);

    if (mqtt_service_init() != 0) {
        LOG_ERR("MQTT init failed");
        blink_led(5, 1000);
        goto cleanup;
    }

    if (mqtt_service_connect() != 0) {
        LOG_ERR("MQTT connect failed");
        blink_led(5, 1000);
        goto cleanup;
    }

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
    k_sleep(K_SECONDS(1));
    mqtt_service_disconnect();
    wifi_disconnect();

cleanup:
    pm_policy_state_lock_put(PM_STATE_SUSPEND_TO_IDLE, PM_ALL_SUBSTATES);
    enter_deep_sleep(60);
    return 0;
}

