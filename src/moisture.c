#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(moisture, LOG_LEVEL_DBG);
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>

#include "moisture.h"

/* Get references from the overlay */
static const struct gpio_dt_spec pwr_gpio = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), pwr_gpios);
static const struct gpio_dt_spec gnd_gpio = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), gnd_gpios);
static const struct adc_dt_spec adc_chan = ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 0);

#define RTC_VALID_MAGIC 0x1234ABCD

struct persistent_data {
    uint32_t magic;
    int32_t cal_dry_raw;
    int32_t cal_wet_raw;
};

// Place the struct in the RTC RAM no-init section
static struct persistent_data rtc_data __attribute__((section(".rtc_noinit")));

static int read_moisture(void) {
    int16_t buf;
    struct adc_sequence sequence = {
        .buffer = &buf,
        .buffer_size = sizeof(buf),
    };

    /* 1. Power on the sensor */
    gpio_pin_set_dt(&pwr_gpio, 1);
    k_msleep(500); // Allow sensor to stabilize (consumes 5mA when active) [3]

    /* 2. Read ADC value */
    adc_sequence_init_dt(&adc_chan, &sequence);
    int err = adc_read(adc_chan.dev, &sequence);
    
    /* 3. Power off sensor immediately to save LiPo life */
    gpio_pin_set_dt(&pwr_gpio, 0);

    if (err) { return err; }

    /* 4. Logic calibration based on sensor datasheet [6, 7]
     * Sensor has an inverse ratio: 
     * Air (Dry) ~ 520 (10-bit scale) -> ~2080 (12-bit scale)
     * Water (Wet) ~ 260 (10-bit scale) -> ~1040 (12-bit scale)
     */
    return (int)buf;
}

/**
 * Record a calibration value based on the sensor's current state.
 * Air (Dry) is typically ~520 (10-bit) while Water (Wet) is ~260 [1, 3].
 */
void moisture_calibrate(enum moisture_cal_type type) {
    int raw_val = read_moisture(); // Calls the ADC logic from previous implementation
    
    if (raw_val < 0) {
        LOG_ERR("Failed to read sensor for calibration");
        return;
    }

    if (type == MOISTURE_CAL_DRY) {
        rtc_data.cal_dry_raw = raw_val;
        LOG_INF("Dry calibration point recorded: %d", rtc_data.cal_dry_raw);
    } else {
        rtc_data.cal_wet_raw = raw_val;
        LOG_INF("Wet calibration point recorded: %d", rtc_data.cal_wet_raw);
    }
}

/**
 * Reads moisture and returns percentage (0-100).
 * If not calibrated, returns the raw ADC value.
 */
int moisture_read_percent(void) {
    int raw = read_moisture();
    if (raw < 0) return raw;

    /* Check if both calibration points are present */
    if (rtc_data.cal_dry_raw == -1 || rtc_data.cal_wet_raw == -1 || rtc_data.magic != RTC_VALID_MAGIC) {
        LOG_WRN("Missing calibration. Set both DRY and WET points. Returning raw: %d", raw);
        return raw;
    }

    /* Out of range check & Clipping logic [1, 4] */
    if (raw > rtc_data.cal_dry_raw || raw < rtc_data.cal_wet_raw) {
        LOG_WRN("Measured raw value %d is outside calibration range [%d, %d]", 
                raw, rtc_data.cal_wet_raw, rtc_data.cal_dry_raw);
    }

    /* Calculate Percentage (Inverse Ratio)
     * Humidity 0% = cal_dry_raw (Higher voltage)
     * Humidity 100% = cal_wet_raw (Lower voltage)
     */
    int32_t range = rtc_data.cal_dry_raw - rtc_data.cal_wet_raw;
    if (range <= 0) {
        LOG_ERR("Invalid calibration range: Dry must be > Wet");
        return -1;
    }

    int32_t percentage = ((rtc_data.cal_dry_raw - raw) * 100) / range;

    /* Hard clipping to 0-100% */
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

void moisture_init(void) {
    gpio_pin_configure_dt(&gnd_gpio, GPIO_OUTPUT_ACTIVE);
    /* Set Pin 6 (IO3) as VCC control, start powered off */
    gpio_pin_configure_dt(&pwr_gpio, GPIO_OUTPUT_INACTIVE);

    gpio_pin_set_dt(&pwr_gpio, 1);

    if (!device_is_ready(adc_chan.dev)) {
        LOG_ERR("ADC device not ready");
        return;
    }

    int err = adc_channel_setup_dt(&adc_chan);
    if (err) {
        LOG_ERR("Failed to setup ADC channel (err %d)", err);
        return;
    }

    if (rtc_data.cal_dry_raw == -1 || rtc_data.cal_wet_raw == -1 || rtc_data.magic != RTC_VALID_MAGIC) {
        LOG_INF("Measuring dry in 10 Seconds");

        k_sleep(K_SECONDS(10));
        moisture_calibrate(MOISTURE_CAL_DRY);
        LOG_INF("Measuring wet in 10 seconds");
        k_sleep(K_SECONDS(10));
        moisture_calibrate(MOISTURE_CAL_WET);
        LOG_INF("Dry: %d, Wet: %d", rtc_data.cal_dry_raw, rtc_data.cal_wet_raw);
        rtc_data.magic = RTC_VALID_MAGIC;
    }

}
