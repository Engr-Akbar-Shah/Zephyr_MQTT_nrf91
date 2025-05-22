/*
Auhtor : Engr Akbar Shah

Date : 21-05-2025

Component : MQTT.c
*/

#include <stdio.h>
#include <string.h>
#include <ncs_version.h>
#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <nrf_modem_at.h>
#include <zephyr/logging/log.h>
#if NCS_VERSION_NUMBER < 0x20600
#include <zephyr/random/rand32.h>
#else
#include <zephyr/random/random.h>
#endif
#include <modem/modem_key_mgmt.h>
#include "mqtt.h"
#include "lte.h"

#define MAX_TOPICS 5		  // Maximum number of topics to store
#define MAX_TOPICS_LENGTH 256 // Maximum length of each topics string

#define MQTT_THREAD_PRIORITY 5
#define MQTT_THREAD_STACKSIZE 4096
K_THREAD_STACK_DEFINE(mqtt_stack, MQTT_THREAD_STACKSIZE);

LOG_MODULE_REGISTER(MQTT);

char DEVICE_ID[DEVICE_ID_SIZE] = {'\0'};

struct k_thread mqtt_thread_data;
static struct sockaddr_storage broker;

static uint8_t rx_buffer[CONFIG_MQTT_MESSAGE_BUFFER_SIZE];
static uint8_t tx_buffer[CONFIG_MQTT_MESSAGE_BUFFER_SIZE];
static uint8_t payload_buf[CONFIG_MQTT_PAYLOAD_BUFFER_SIZE];

char SUBSCRIBE_TOPICS[MAX_TOPICS][MAX_TOPICS_LENGTH];
char PUBLISH_TOPICS[MAX_TOPICS][MAX_TOPICS_LENGTH];

uint8_t NUM_SUBSCRIBE_TOPICS = 0;
uint8_t NUM_PUBLISH_TOPICS = 0;

bool CONNECT_MQTT = true;
bool RECONNECT_MQTT = true;
bool DISCONNECT_MQTT = false;

/*
Function : mqtt_create_topic_subscribe

Description : Creates and stores a new MQTT topic string for subscribing. It formats the
			  string with the given arguments and saves it to the subscribe list.

Parameter :
- topic_name : Optional output buffer to receive the formatted topic.
- format : Format string for the topic.
- ... : Additional arguments to format the string.

Return : void

Example Call :
				mqtt_create_topic_subscribe(NULL, "devices/%s/state", DEVICE_ID);
*/
void mqtt_create_topic_subscribe(char *topic_name,
								 const char *format, ...)
{

	if (NUM_SUBSCRIBE_TOPICS >= MAX_TOPICS)
	{
		LOG_ERR("Maximum number of SUBSCRIBE topics reached\n");
		return;
	}
	va_list args;
	va_start(args, format);

	vsnprintf(SUBSCRIBE_TOPICS[NUM_SUBSCRIBE_TOPICS], MAX_TOPICS_LENGTH, format, args);

	if (topic_name != NULL)
	{
		strncpy(topic_name, SUBSCRIBE_TOPICS[NUM_SUBSCRIBE_TOPICS], MAX_TOPICS_LENGTH);
	}
	va_end(args);

	LOG_DBG("Subscribe topic added: %s", SUBSCRIBE_TOPICS[NUM_SUBSCRIBE_TOPICS]);

	NUM_SUBSCRIBE_TOPICS++;
}

/*
Function : mqtt_create_topic_publish

Description : Creates and stores a new MQTT topic string for publishing. It formats the
			  string with the given arguments and saves it to the publish list.

Parameter :
- topic_name : Optional output buffer to receive the formatted topic.
- format : Format string for the topic.
- ... : Additional arguments to format the string.

Return : void

Example Call :
				mqtt_create_topic_publish(NULL, "devices/%s/data", DEVICE_ID);
*/
void mqtt_create_topic_publish(char *topic_name,
							   const char *format, ...)
{
	if (NUM_PUBLISH_TOPICS >= MAX_TOPICS)
	{
		LOG_ERR("Maximum number of SUBSCRIBE topics reached\n");
		return;
	}
	va_list args;
	va_start(args, format);

	vsnprintf(PUBLISH_TOPICS[NUM_PUBLISH_TOPICS], MAX_TOPICS_LENGTH, format, args);

	if (topic_name != NULL)
	{
		strncpy(topic_name, PUBLISH_TOPICS[NUM_PUBLISH_TOPICS], MAX_TOPICS_LENGTH);
	}

	va_end(args);

	LOG_DBG("Publish topic added: %s", PUBLISH_TOPICS[NUM_PUBLISH_TOPICS]);

	NUM_PUBLISH_TOPICS++;
}

/*
Function : subscribe

Description : Subscribes to all topics in the subscribe list using the MQTT client.

Parameter :
- c : Pointer to the MQTT client structure.

Return :
0 on success, or negative error code on failure.

Example Call :
				subscribe(&client);
*/
static int subscribe(struct mqtt_client *const c)
{
	struct mqtt_topic subscribe_topics[NUM_SUBSCRIBE_TOPICS];

	for (int i = 0; i < NUM_SUBSCRIBE_TOPICS; i++)
	{
		subscribe_topics[i].topic.utf8 = SUBSCRIBE_TOPICS[i];
		subscribe_topics[i].topic.size = strlen(SUBSCRIBE_TOPICS[i]);
		subscribe_topics[i].qos = MQTT_QOS_1_AT_LEAST_ONCE;

		LOG_INF("Subscribing to: %s len %u", SUBSCRIBE_TOPICS[i],
				(unsigned int)strlen(SUBSCRIBE_TOPICS[i]));
	}

	const struct mqtt_subscription_list subscription_list = {
		.list = subscribe_topics,
		.list_count = NUM_SUBSCRIBE_TOPICS,
		.message_id = sys_rand32_get(),
	};

	return mqtt_subscribe(c, &subscription_list);
}

/*
Function : data_print

Description : Prints received or published data to the logs with a prefix.

Parameter :
- prefix : Text to prefix before the data.
- data : The actual data buffer.
- len : Length of the data.

Return : void

Example Call :
				data_print("Received: ", payload_buf, length);
*/
static void data_print(uint8_t *prefix,
					   uint8_t *data,
					   size_t len)
{
	char buf[len + 1];

	memcpy(buf, data, len);
	buf[len] = 0;
	LOG_INF("%s%s", (char *)prefix, (char *)buf);
}

/*
Function : data_publish

Description : Publishes MQTT data to the first publish topic.

Parameter :
- c : Pointer to the MQTT client.
- qos : Quality of Service level.
- data : Data buffer to send.
- len : Length of the data.

Return :
0 on success, or a negative error code on failure.

Example Call :
				data_publish(&client, MQTT_QOS_1_AT_LEAST_ONCE, buf, strlen(buf));
*/
int data_publish(struct mqtt_client *c,
				 enum mqtt_qos qos,
				 uint8_t *data,
				 size_t len)
{
	struct mqtt_publish_param param;

	param.message.topic.qos = qos;
	param.message.topic.topic.utf8 = PUBLISH_TOPICS[0];
	param.message.topic.topic.size = strlen(PUBLISH_TOPICS[0]);
	param.message.payload.data = data;
	param.message.payload.len = len;
	param.message_id = sys_rand32_get();
	param.dup_flag = 0;
	param.retain_flag = 0;

	data_print("Publishing: ", data, len);
	LOG_INF("to topic: %s len: %u",
			PUBLISH_TOPICS[0],
			(unsigned int)strlen(PUBLISH_TOPICS[0]));

	return mqtt_publish(c, &param);
}

/*
Function : get_received_payload

Description : Retrieves the full payload from an incoming MQTT message.

Parameter :
- c : Pointer to the MQTT client.
- length : Expected payload length.

Return :
0 on success, -EMSGSIZE if too large, or another negative error code.

Example Call :
				get_received_payload(&client, evt->param.publish.payload.len);
*/
static int get_received_payload(struct mqtt_client *c,
								size_t length)
{
	int ret;
	int err = 0;

	/* Return an error if the payload is larger than the payload buffer.
	 * Note: To allow new messages, we have to read the payload before returning.
	 */
	if (length > sizeof(payload_buf))
	{
		err = -EMSGSIZE;
	}

	/* Truncate payload until it fits in the payload buffer. */
	while (length > sizeof(payload_buf))
	{
		ret = mqtt_read_publish_payload_blocking(
			c, payload_buf, (length - sizeof(payload_buf)));
		if (ret == 0)
		{
			return -EIO;
		}
		else if (ret < 0)
		{
			return ret;
		}

		length -= ret;
	}

	ret = mqtt_readall_publish_payload(c, payload_buf, length);
	if (ret)
	{
		return ret;
	}

	return err;
}

/*
Function : mqtt_evt_handler

Description : Handles various MQTT client events including connect, disconnect, publish,
			  and subscription acknowledgements.

Parameter :
- c : Pointer to the MQTT client.
- evt : Pointer to the MQTT event.

Return : void

Example Call :
				Handled internally by the MQTT client.
*/
static void mqtt_evt_handler(struct mqtt_client *const c,
							 const struct mqtt_evt *evt)
{
	int err;

	switch (evt->type)
	{
	case MQTT_EVT_CONNACK:
		if (evt->result != 0)
		{
			LOG_ERR("MQTT connect failed: %d", evt->result);
			break;
		}

		LOG_INF("MQTT client connected");
		subscribe(c);
		break;

	case MQTT_EVT_DISCONNECT:
		if (RECONNECT_MQTT)
		{
			LOG_INF("MQTT client disconnected Unexpectedly Reconnecting: %d", evt->result);
			CONNECT_MQTT = true;
		}
		else
		{
			LOG_INF("MQTT client disconnected: %d", evt->result);
		}
		break;

	case MQTT_EVT_PUBLISH:
	{
		const struct mqtt_publish_param *p = &evt->param.publish;

		LOG_INF("MQTT PUBLISH result=%d len=%d  TOPIC: %s",
				evt->result, p->message.payload.len, p->message.topic.topic.utf8);

		err = get_received_payload(c, p->message.payload.len);

		if (p->message.topic.qos == MQTT_QOS_1_AT_LEAST_ONCE)
		{
			const struct mqtt_puback_param ack = {
				.message_id = p->message_id};

			mqtt_publish_qos1_ack(c, &ack); /* Send acknowledgment. */
		}

		if (err >= 0)
		{
			data_print("Received: ", payload_buf, p->message.payload.len);
		}
		else if (err == -EMSGSIZE)
		{
			LOG_ERR("Received payload (%d bytes) is larger than the payload buffer "
					"size (%d bytes).",
					p->message.payload.len, sizeof(payload_buf));
		}
		else
		{
			LOG_ERR("get_received_payload failed: %d", err);
			LOG_INF("Disconnecting MQTT client...");

			err = mqtt_disconnect(c);
			if (err)
			{
				LOG_ERR("Could not disconnect: %d", err);
			}
		}
	}
	break;

	case MQTT_EVT_PUBACK:
		if (evt->result != 0)
		{
			LOG_ERR("MQTT PUBACK error: %d", evt->result);
			break;
		}

		LOG_INF("PUBACK packet id: %u", evt->param.puback.message_id);
		break;

	case MQTT_EVT_SUBACK:
		if (evt->result != 0)
		{
			LOG_ERR("MQTT SUBACK error: %d", evt->result);
			break;
		}

		LOG_INF("SUBACK packet id: %u", evt->param.suback.message_id);
		break;

	case MQTT_EVT_PINGRESP:
		if (evt->result != 0)
		{
			LOG_ERR("MQTT PINGRESP error: %d", evt->result);
		}
		break;

	default:
		LOG_INF("Unhandled MQTT event type: %d", evt->type);
		break;
	}
}

/*
Function : fds_init

Description : Initializes the file descriptor structure for polling MQTT socket.

Parameter :
- c : Pointer to the MQTT client.
- fds : Pointer to the pollfd struct to initialize.

Return :
0 on success.

Example Call :
				fds_init(&client, &fds);
*/
static int fds_init(struct mqtt_client *c,
					struct pollfd *fds)
{
	if (c->transport.type == MQTT_TRANSPORT_NON_SECURE)
	{
		fds->fd = c->transport.tcp.sock;
	}
	else
	{

		fds->fd = c->transport.tls.sock;
	}

	fds->events = POLLIN;

	return 0;
}

/*
Function : broker_init

Description : Resolves the broker IP address and sets up the connection structure.

Parameter : void

Return :
0 on success, or a negative error code on failure.

Example Call :
				broker_init();
*/
static int broker_init(void)
{
	int err;
	struct addrinfo *result;
	struct addrinfo *addr;
	struct addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_STREAM};

	if (CONFIG_MQTT_BROKER_PORT == 0 || CONFIG_MQTT_BROKER_HOSTNAME == NULL)
	{
		LOG_ERR("Invalid broker configuration");
		return -EINVAL;
	}

	err = getaddrinfo(CONFIG_MQTT_BROKER_HOSTNAME, NULL, &hints, &result);
	if (err)
	{
		LOG_ERR("getaddrinfo failed: %d", err);
		return -ECHILD;
	}

	addr = result;

	while (addr != NULL)
	{

		if (addr->ai_addrlen == sizeof(struct sockaddr_in))
		{
			struct sockaddr_in *broker4 =
				((struct sockaddr_in *)&broker);
			char ipv4_addr[NET_IPV4_ADDR_LEN];

			broker4->sin_addr.s_addr =
				((struct sockaddr_in *)addr->ai_addr)
					->sin_addr.s_addr;
			broker4->sin_family = AF_INET;
			broker4->sin_port = htons(CONFIG_MQTT_BROKER_PORT);

			inet_ntop(AF_INET, &broker4->sin_addr.s_addr,
					  ipv4_addr, sizeof(ipv4_addr));
			LOG_INF("IPv4 Address found %s", (char *)(ipv4_addr));

			break;
		}
		else
		{
			LOG_ERR("ai_addrlen = %u should be %u or %u",
					(unsigned int)addr->ai_addrlen,
					(unsigned int)sizeof(struct sockaddr_in),
					(unsigned int)sizeof(struct sockaddr_in6));
		}

		addr = addr->ai_next;
	}

	freeaddrinfo(result);

	return err;
}

/*
Function : client_init

Description : Initializes the MQTT client structure and TLS configuration.

Parameter : 
- client : Pointer to the MQTT client.

Return : 
0 on success, or a negative error code on failure.

Example Call : 
				client_init(&client);
*/
int client_init(struct mqtt_client *client)
{
	int err;

	mqtt_client_init(client);

	err = broker_init();
	if (err)
	{
		LOG_ERR("Failed to initialize broker connection");
		return err;
	}

	client->broker = &broker;
	client->evt_cb = mqtt_evt_handler;
	client->client_id.utf8 = DEVICE_ID;
	client->client_id.size = strlen(client->client_id.utf8);
	client->password = NULL;
	client->user_name = NULL;
	client->protocol_version = MQTT_VERSION_3_1_1;

	client->rx_buf = rx_buffer;
	client->rx_buf_size = sizeof(rx_buffer);
	client->tx_buf = tx_buffer;
	client->tx_buf_size = sizeof(tx_buffer);

	struct mqtt_sec_config *tls_cfg = &(client->transport).tls.config;
	static sec_tag_t sec_tag_list[] = {CONFIG_MQTT_TLS_SEC_TAG};

	LOG_INF("TLS enabled");
	client->transport.type = MQTT_TRANSPORT_SECURE;

	tls_cfg->peer_verify = CONFIG_MQTT_TLS_PEER_VERIFY;
	tls_cfg->cipher_list = NULL;
	tls_cfg->cipher_count = 0;
	tls_cfg->sec_tag_count = ARRAY_SIZE(sec_tag_list);
	tls_cfg->sec_tag_list = sec_tag_list;
	tls_cfg->hostname = CONFIG_MQTT_BROKER_HOSTNAME;

	tls_cfg->session_cache = IS_ENABLED(CONFIG_MQTT_TLS_SESSION_CACHING) ? TLS_SESSION_CACHE_ENABLED : TLS_SESSION_CACHE_DISABLED;

	return err;
}

/*
Function : mqtt_handle_disconnect

Description : Gracefully disconnects the MQTT client.

Parameter : 
- client : Pointer to the MQTT client.

Return : void

Example Call : 
				mqtt_handle_disconnect(&client);
*/
static void mqtt_handle_disconnect(struct mqtt_client *client)
{
	LOG_INF("Disconnecting MQTT client");
	int err = mqtt_disconnect(client);
	if (err)
	{
		LOG_ERR("Could not disconnect MQTT client: %d", err);
	}
	LOG_WRN("MQTT client disconnected\n");
}

/*
Function : mqtt_poll_events

Description : Polls MQTT events, manages keepalive, and handles input.

Parameter : 
- client : Pointer to the MQTT client.
- fds : Pointer to pollfd struct.

Return : void

Example Call : 
				mqtt_poll_events(&client, &fds);
*/
static void mqtt_poll_events(struct mqtt_client *client,
							 struct pollfd *fds)
{
	int err;
	err = poll(fds, 1, mqtt_keepalive_time_left(client));
	if (err < 0)
	{
		LOG_ERR("Error in poll(): %d", errno);
		return;
	}

	err = mqtt_live(client);
	if ((err != 0) && (err != -EAGAIN))
	{
		LOG_ERR("Error in mqtt_live: %d", err);
		return;
	}

	if ((fds->revents & POLLIN) == POLLIN)
	{
		err = mqtt_input(client);
		if (err != 0)
		{
			LOG_ERR("Error in mqtt_input: %d", err);
			return;
		}
	}

	if ((fds->revents & POLLERR) == POLLERR)
	{
		LOG_ERR("POLLERR");
		return;
	}

	if ((fds->revents & POLLNVAL) == POLLNVAL)
	{
		LOG_ERR("POLLNVAL");
		return;
	}
}

/*
Function : mqtt_connect_fds

Description : Connects the MQTT client to the broker and initializes socket polling.

Parameter : 
- client : Pointer to the MQTT client.
- fds : Pointer to pollfd struct.

Return : void

Example Call : 
				mqtt_connect_fds(&client, &fds);
*/
static void mqtt_connect_fds(struct mqtt_client *client,
							 struct pollfd *fds)
{
	int err;
	LOG_INF("Connection to broker using mqtt_connect");
	err = mqtt_connect(client);
	if (err)
	{
		LOG_ERR("Error in mqtt_connect: %d", err);
	}

	err = fds_init(client, fds);
	if (err)
	{
		LOG_ERR("Error in fds_init: %d", err);
		return;
	}
}

/*
Function : mqtt__thread

Description : MQTT thread loop that manages connection, reconnection, and polling.

Parameter : void

Return : void

Example Call : 
				Automatically called by MQTT_configure().
*/
void mqtt__thread()
{
	int err;

	static struct pollfd fds;
	static struct mqtt_client client;

	uint32_t connect_attempt = 0;

	err = client_init(&client);
	if (err)
	{
		LOG_ERR("Failed to initialize MQTT client: %d", err);
		return;
	}

	while (1)
	{
		if (CONNECT_MQTT == true)
		{
			if (connect_attempt++ > 0)
			{
				LOG_INF("Reconnecting in %d seconds...",
						CONFIG_MQTT_RECONNECT_DELAY_S);
				k_sleep(K_SECONDS(CONFIG_MQTT_RECONNECT_DELAY_S));
			}

			mqtt_connect_fds(&client, &fds);

			CONNECT_MQTT = false;
		}

		mqtt_poll_events(&client, &fds);

		if (DISCONNECT_MQTT)
		{
			mqtt_handle_disconnect(&client);
			DISCONNECT_MQTT = false;
		}
		k_msleep(100);
	}
}

/*
Function : MQTT_configure

Description : Starts the MQTT thread responsible for connecting and managing the MQTT client.

Parameter : void

Return : void

Example Call : 
				MQTT_configure();
*/
void MQTT_configure(void)
{

	k_thread_create(&mqtt_thread_data, mqtt_stack, MQTT_THREAD_STACKSIZE,
					(k_thread_entry_t)mqtt__thread, NULL, NULL, NULL,
					MQTT_THREAD_PRIORITY, 0, K_NO_WAIT);
}