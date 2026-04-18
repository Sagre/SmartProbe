#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/sensor_data_types.h>

LOG_MODULE_REGISTER(room, LOG_LEVEL_INF);

const struct device *const dev = DEVICE_DT_GET_ANY(bosch_bme280);

int read_bme280(float *t, float *p, float *h) {
    if (dev == NULL) {
        LOG_ERR("No BME280 device found in devicetree");
        return -ENODEV;
    }

    if (!device_is_ready(dev)) {
        LOG_ERR("BME280 device %s is not ready", dev->name);
        return -ENODEV;
    }

    LOG_INF("BME280 device %s is ready", dev->name);

    struct sensor_value temp, press, hum;

    /* Fetch fresh samples from all channels */
    int ret = sensor_sample_fetch(dev);
    if (ret < 0) {
        LOG_ERR("sensor_sample_fetch failed: %d", ret);
        return ret;
    }

    /* Read individual channels */
    sensor_channel_get(dev, SENSOR_CHAN_AMBIENT_TEMP, &temp);
    sensor_channel_get(dev, SENSOR_CHAN_PRESS, &press);
    sensor_channel_get(dev, SENSOR_CHAN_HUMIDITY, &hum);

    LOG_INF("Temp: %d.%06d °C | Press: %d.%06d kPa | Hum: %d.%06d %%RH",
            temp.val1, temp.val2,
            press.val1, press.val2,
            hum.val1, hum.val2);
    
    *t = (float)temp.val1 + (float)temp.val2 / 1000000.0f;
    *p = (float)press.val1 + (float)press.val2 / 1000000.0f;
    *h = (float)hum.val1 + (float)hum.val2 / 1000000.0f;

    return 0;
}