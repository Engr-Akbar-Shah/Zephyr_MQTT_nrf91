/*
Auhtor : Engr Akbar Shah

Date : 21-05-2025

Component : MQTT.h
*/

#ifndef _MQTT_H_
#define _MQTT_H_

#include "stdint.h"
#include <zephyr/net/mqtt.h>

#define DEVICE_ID_SIZE 16

extern char DEVICE_ID[DEVICE_ID_SIZE];

extern bool CONNECT_MQTT;
extern bool RECONNECT_MQTT;
extern bool DISCONNECT_MQTT;

void MQTT_configure(void);

int data_publish(struct mqtt_client *c, enum mqtt_qos qos,
				 uint8_t *data, size_t len);

void mqtt_create_topic_subscribe(char *topic_name, const char *format, ...); // void mqtt_create_topic_subscribe(const char *format, ...);
void mqtt_create_topic_publish(char *topic_name, const char *format, ...);

#endif