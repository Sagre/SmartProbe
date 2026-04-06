enum moisture_cal_type {
    MOISTURE_CAL_DRY,
    MOISTURE_CAL_WET
};

void moisture_calibrate(enum moisture_cal_type type);
int moisture_read_percent(void);
int moisture_init(void);