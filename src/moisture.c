#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(moisture, LOG_LEVEL_INF);
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/fs/nvs.h>

#include "moisture.h"

/* Get references from the overlay */
static const struct gpio_dt_spec pwr_gpio = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), pwr_gpios);
static const struct gpio_dt_spec gnd_gpio = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), gnd_gpios);
static const struct adc_dt_spec adc_chan = ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 0);

#define CALIBRATION_ID 1

struct persistent_data {
    int32_t cal_dry_raw;
    int32_t cal_wet_raw;
};

struct persistent_data rtc_data;

static struct nvs_fs fs;

void init_nvs(void) {
    int rc;
    struct flash_pages_info info;

    /* Define the flash partition to use (storage_partition is default on ESP32) */
    fs.flash_device = FIXED_PARTITION_DEVICE(storage_partition);
    if (!device_is_ready(fs.flash_device)) {
        printk("Flash device not ready\n");
        return;
    }

    fs.offset = FIXED_PARTITION_OFFSET(storage_partition);
    rc = flash_get_page_info_by_offs(fs.flash_device, fs.offset, &info);
    if (rc) {
        printk("Unable to get page info\n");
        return;
    }

    fs.sector_size = info.size;
    fs.sector_count = 3; // Use 3 sectors for wear leveling

    rc = nvs_mount(&fs);
    if (rc) {
        printk("Flash Init failed\n");
    }
}

void save_calibration(struct persistent_data *data) {
    nvs_write(&fs, CALIBRATION_ID, data, sizeof(struct persistent_data));
}

void load_calibration(struct persistent_data *data) {
    int rc = nvs_read(&fs, CALIBRATION_ID, data, sizeof(struct persistent_data));
    if (rc <= 0) {
        printk("No calibration found, setting defaults\n");
        data->cal_dry_raw = -1;
        data->cal_wet_raw = -1;
    } else {
        printk("Loaded: Dry=%d, Wet=%d\n", data->cal_dry_raw, data->cal_wet_raw);
    }
}

static int read_moisture(void) {
    int16_t buf;
    struct adc_sequence sequence = {
        .buffer = &buf,
        .buffer_size = sizeof(buf),
    };

    gpio_pin_set_dt(&pwr_gpio, 1);
    k_msleep(500);

    adc_sequence_init_dt(&adc_chan, &sequence);
    int err = adc_read(adc_chan.dev, &sequence);
    
    gpio_pin_set_dt(&pwr_gpio, 0);

    if (err) { return err; }

    return (int)buf;
}

void moisture_calibrate(enum moisture_cal_type type) {
    int raw_val = read_moisture();
    
    if (raw_val < 0) {
        LOG_ERR("Failed to read sensor for calibration");
        return;
    }

    if (type == MOISTURE_CAL_DRY) {
        rtc_data.cal_dry_raw = raw_val;
    } else {
        rtc_data.cal_wet_raw = raw_val;
    }
}

int moisture_read_percent(void) {
    int raw = read_moisture();
    if (raw < 0) return raw;

    if (rtc_data.cal_dry_raw == -1 || rtc_data.cal_wet_raw == -1) {
        LOG_WRN("Missing calibration. Set both DRY and WET points. Returning raw: %d", raw);
        return raw;
    }

    if (raw > rtc_data.cal_dry_raw || raw < rtc_data.cal_wet_raw) {
        LOG_WRN("Measured raw value %d is outside calibration range [%d, %d]", 
                raw, rtc_data.cal_wet_raw, rtc_data.cal_dry_raw);
    }

    int32_t range = rtc_data.cal_dry_raw - rtc_data.cal_wet_raw;
    if (range <= 0) {
        LOG_ERR("Invalid calibration range: Dry must be > Wet");
        return -1;
    }

    int32_t percentage = ((rtc_data.cal_dry_raw - raw) * 100) / range;

    if (percentage < 0) {
        percentage = 0;
        LOG_WRN("Measured value is below min value: %d, Measured: %d, Dry: %d, Wet: %d", percentage, raw, rtc_data.cal_dry_raw, rtc_data.cal_wet_raw);
    }
    if (percentage > 100) {
        percentage = 100;
        LOG_WRN("Measured value is above min value: %d, Measured: %d, Dry: %d, Wet: %d", percentage, raw, rtc_data.cal_dry_raw, rtc_data.cal_wet_raw);
    }

    return (int)percentage;
}

int moisture_init(void) {
    gpio_pin_configure_dt(&gnd_gpio, GPIO_OUTPUT_ACTIVE);
    gpio_pin_configure_dt(&pwr_gpio, GPIO_OUTPUT_INACTIVE);

    gpio_pin_set_dt(&pwr_gpio, 1);

    if (!device_is_ready(adc_chan.dev)) {
        LOG_ERR("ADC device not ready");
        return -1;
    }

    int err = adc_channel_setup_dt(&adc_chan);
    if (err) {
        LOG_ERR("Failed to setup ADC channel (err %d)", err);
        return -1;
    }

    init_nvs();
    load_calibration(&rtc_data);

    if (rtc_data.cal_dry_raw == -1 || rtc_data.cal_wet_raw == -1) {
        LOG_INF("No valid calibration found. Starting calibration. Dry in 10 seconds");
        k_sleep(K_SECONDS(10));
        moisture_calibrate(MOISTURE_CAL_DRY);
        LOG_INF("No valid calibration found. Starting calibration. Wet in 10 seconds");
        k_sleep(K_SECONDS(10));
        moisture_calibrate(MOISTURE_CAL_WET);
        save_calibration(&rtc_data);
    }

    return 0;
}
