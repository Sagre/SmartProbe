enum moisture_cal_type {
    MOISTURE_CAL_DRY,
    MOISTURE_CAL_WET
};

#define MOISTURE_ERROR_VALUE -1

void moisture_calibrate(enum moisture_cal_type type);
int moisture_read_percent(float *value);
int moisture_init(void);