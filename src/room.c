#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor_data_types.h>
#include <zephyr/logging/log.h>

#include "room.h"

LOG_MODULE_REGISTER(room, LOG_LEVEL_INF);

static const struct gpio_dt_spec bmepwr = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), bme_pwr_gpios);
static const struct device *const dev = DEVICE_DT_GET(DT_ALIAS(bme280));
static bool initialized;

static int fetch_sensor_channel(enum sensor_channel chan, float *value)
{
    if (dev == NULL) {
        LOG_ERR("No BME280 device found in devicetree");
        return -ENODEV;
    }

    if (!device_is_ready(dev)) {
        LOG_ERR("BME280 device %s is not ready", dev->name);
        return -ENODEV;
    }

    struct sensor_value sensor_val;

    int ret = sensor_sample_fetch(dev);
    if (ret < 0) {
        LOG_ERR("sensor_sample_fetch failed: %d", ret);
        return ret;
    }

    ret = sensor_channel_get(dev, chan, &sensor_val);
    if (ret < 0) {
        LOG_ERR("sensor_channel_get for channel %d failed: %d", chan, ret);
        return ret;
    }

    *value = (float)sensor_val.val1 + (float)sensor_val.val2 / 1000000.0f;
    return 0;
}

int room_init(void)
{
    if (initialized) {
        return 0;
    }

    if (!device_is_ready(bmepwr.port)) {
        LOG_ERR("BME280 power GPIO not ready");
        return -ENODEV;
    }

    if (!device_is_ready(dev)) {
        LOG_ERR("BME280 device not ready");
        return -ENODEV;
    }

    int err = gpio_pin_configure_dt(&bmepwr, GPIO_OUTPUT_INACTIVE);
    if (err) {
        LOG_ERR("Failed to configure BME280 power GPIO: %d", err);
        return err;
    }

    gpio_pin_set_dt(&bmepwr, 1);
    initialized = true;
    return 0;
}

int room_read_temperature(float *value)
{
    return fetch_sensor_channel(SENSOR_CHAN_AMBIENT_TEMP, value);
}

int room_read_pressure(float *value)
{
    return fetch_sensor_channel(SENSOR_CHAN_PRESS, value);
}

int room_read_humidity(float *value)
{
    return fetch_sensor_channel(SENSOR_CHAN_HUMIDITY, value);
}
