#ifndef SENSOR_MODULE_H
#define SENSOR_MODULE_H

#ifdef __cplusplus
extern "C" {
#endif

struct sensor_module {
    const char *key;
    const char *label;
    const char *unit;
    int (*init)(void);
    int (*read)(float *value);
    float err_value;
};

#ifdef __cplusplus
}
#endif

#endif /* SENSOR_MODULE_H */
