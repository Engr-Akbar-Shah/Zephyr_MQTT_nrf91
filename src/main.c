
#include <stdio.h>
#include <ncs_version.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "mqtt.h"
#include "lte.h"
#include "certs.h"

LOG_MODULE_REGISTER(MQTT_MAIN);

int main(void)
{
	int err;

	char *MQTT_TEST_SUB_TOPIC = NULL;
	char *MQTT_TEST_PUB_TOPIC = NULL;

	err = write_device_certs_to_modem();
	if (err != 0)
	{
		LOG_ERR("Failed to write certs to modem err [%d]", err);
	}

	err = modem_init();
	if (err != 0)
	{
		LOG_ERR("Failed to init modem err [%d]", err);
	}

	err = lte_init();
	if (err != 0)
	{
		LOG_ERR("Failed to init LTE err [%d]", err);
	}

	err = get_modem_info_imei(DEVICE_ID, DEVICE_ID_SIZE);
	if (err != 0)
	{
		LOG_ERR("Failed to get device id IMEI\n\r");
	}
	LOG_INF("MQTT Device ID IMEI [ %s ]", DEVICE_ID);

	mqtt_create_topic_subscribe(MQTT_TEST_SUB_TOPIC, "mqtt/subscribe/telemetry/%s", DEVICE_ID);
	mqtt_create_topic_subscribe(MQTT_TEST_SUB_TOPIC, "mqtt/subscribe/ota/%s", DEVICE_ID);
	mqtt_create_topic_subscribe(MQTT_TEST_SUB_TOPIC, "mqtt/subscribe/command/%s", DEVICE_ID);

	mqtt_create_topic_publish(MQTT_TEST_PUB_TOPIC, "mqtt/%s/publish/test_topic", DEVICE_ID);

	MQTT_configure();
}