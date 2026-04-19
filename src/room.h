#include <zephyr/drivers/sensor.h>

#define ROOM_ERROR_VALUE -999.0f
int room_init(void);
int room_read_temperature(float *value);
int room_read_pressure(float *value);
int room_read_humidity(float *value);