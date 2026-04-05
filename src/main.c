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

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

const struct pm_state_info *pm_policy_next_state(uint8_t cpu, int32_t ticks)
{
    /* Returning NULL tells the kernel: "Do not enter any sleep state" */
    /* This keeps the CPU and WiFi radio fully powered. */
    return NULL;
}

void enter_deep_sleep(uint32_t seconds) {
    LOG_INF("Entering deep sleep for %u seconds", seconds);
    
    /* Set the wakeup timer (ESP32-C3 RTC timer) */
    esp_sleep_enable_timer_wakeup(seconds * 1000000ULL);
    
    sys_poweroff();
    
    /* The call below will never be reached as the chip resets on wakeup */
    k_sleep(K_FOREVER);
}

int main(void)
{
    pm_policy_state_lock_get(PM_STATE_SUSPEND_TO_IDLE, PM_ALL_SUBSTATES);
    pm_policy_state_lock_get(PM_STATE_STANDBY, PM_ALL_SUBSTATES);
    k_sleep(K_SECONDS(5));
    wifi_init();
    wifi_connect();
    wifi_wait_for_connect();
    wifi_status();
    wifi_wait_for_ipv4();
    LOG_INF("Ready...");

    // Ping Google DNS 4 times
    ping("192.168.178.30", 4);
    mqtt_service_init();
    mqtt_service_connect();

    moisture_init();

    int moisture = moisture_read_percent();
    if (moisture >= 0 && moisture <= 100) mqtt_service_publish_sensor(SENSOR_SOIL, (float)moisture);
    k_sleep(K_SECONDS(1));
    mqtt_service_disconnect();
    wifi_disconnect();

    pm_policy_state_lock_put(PM_STATE_SUSPEND_TO_IDLE, PM_ALL_SUBSTATES);
    enter_deep_sleep(60);
    return(0);
}

