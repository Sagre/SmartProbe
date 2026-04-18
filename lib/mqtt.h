#ifndef MQTT_H
#define MQTT_H

#include <zephyr/net/mqtt.h>

enum sensor_e {
    SENSOR_SOIL = 1,
    SENSOR_TEMP = 2,
    SENSOR_PRESS = 3,
    SENSOR_HUM = 4,
};

int mqtt_service_init(void);
int mqtt_service_connect(void);
int mqtt_service_publish(const char *topic, const char *json_payload, enum mqtt_qos qos);
int mqtt_service_disconnect(void);
int mqtt_service_publish_sensor(enum sensor_e sensor, float value);

#endif /* MQTT_H */