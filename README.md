# NRF91_MQTT_CLIENT

## Project Overview

This project implements a secure MQTT client for Nordic Semiconductor's nRF91 series (like nRF9160, nRF9161), built on Zephyr RTOS. It provides a complete solution to securely connect an embedded device to any MQTT broker (AWS IoT, Azure IoT Hub, Mosquitto, etc.), publish and subscribe to topics, and manage TLS certificates programmatically.

> ✅ Built on Nordic Connect SDK (NCS) **v2.5.0**

---

## Project Goals

* Secure MQTT communication via TLS (using modem key management)
* Dynamic publish/subscribe topic creation
* Easy certificate management
* Support for various MQTT brokers (AWS IoT, Azure IoT, etc.)
* Developer-friendly architecture

---

## Project Structure

```
MQTT_ZEPHYR/
├── src/
│   └── main.c                    # Entry point
├── components/
│   ├── mqtt/                    # MQTT logic
│   ├── lte/                     # LTE and modem support
│   └── certs/                   # TLS certificates and generated certs.h
├── boards/                      # Device overlays
├── prj.conf                     # Zephyr project config
├── update_certs.py             # Script to process certificates
├── sample.yaml                 # Build config
├── Kconfig, CMakeLists.txt     # Build system
```

---

## Certificate Management

### Supported File Types

* PEM files (.pem, .crt, .key)
* Naming must contain one of the following:

  * `device`, `cert`, `certificate` → for `device_cert`
  * `private`, `key` → for `private_key`
  * `root` → for `root_ca`

### Auto-Generation with `update_certs.py`

```bash
python3 update_certs.py
```

This script:

* Scans `components/certs/` for matching cert files
* Rejects if multiple files match a category
* Replaces `certs.h` completely

### Example Files:

* `certificate.pem.crt`
* `private.pem.key`
* `AmazonRootCA1.pem`

### Output File: `components/certs/certs.h`

```c
#ifndef _CERTIFICATES_H
#define _CERTIFICATES_H

int write_device_certs_to_modem(void);

static const char device_cert[] = "...";
static const char private_key[] = "...";
static const char root_ca[] = "...";

#endif
```

---

## Configuration (`prj.conf`)

```ini
# Enable MQTT support
CONFIG_MQTT_LIB=y
CONFIG_MQTT_TLS=y

# Buffers
CONFIG_MQTT_MESSAGE_BUFFER_SIZE=512
CONFIG_MQTT_PAYLOAD_BUFFER_SIZE=1024

# TLS Config
CONFIG_MQTT_TLS_SEC_TAG=30
CONFIG_MQTT_TLS_PEER_VERIFY=2
CONFIG_MQTT_TLS_SESSION_CACHING=y

# Enable logging
CONFIG_LOG=y
```

---

## Required Changes for Custom MQTT Brokers

### TLS Sec Tag (important!)

```c
#define MQTT_SEC_TAG 30
```

This sec tag (30) is used to store certs via `modem_key_mgmt_write()`

### Replace in `prj.conf`:

* `CONFIG_MQTT_BROKER_HOSTNAME="<your-broker-url>"`
* `CONFIG_MQTT_BROKER_PORT=8883` (for TLS)

---

## Connecting to MQTT Brokers

### AWS IoT

* Upload the `device certificate`, `private key`, and `AmazonRootCA1.pem`
* Use the script to generate `certs.h`
* Set `CONFIG_MQTT_BROKER_HOSTNAME="<your-aws-endpoint>"`

### Azure IoT / Custom Broker

* Convert your certs to PEM if needed
* Match filenames for detection
* Run `update_certs.py`

---

## Topic Creation

### Subscribe Topics:

```c
mqtt_create_topic_subscribe(NULL, "devices/%s/commands", DEVICE_ID);
```

### Publish Topics:

```c
mqtt_create_topic_publish(NULL, "devices/%s/data", DEVICE_ID);
```

---

## Publishing Data

```c
data_publish(&client, MQTT_QOS_1_AT_LEAST_ONCE, data, strlen(data));
```

## Receiving Data

* Subscribed topics are handled in `mqtt_evt_handler()`
* Payloads are printed using `data_print()`

---

## LTE Connectivity

Handled in `lte` component. Make sure:

```c
#include "lte.h"
lte_connect();
```

---

## Building the Project

```bash
west build -b nrf9160dk_nrf9160ns .
west flash
```

---

## Troubleshooting

* Make sure only one cert file matches each type (device, private, root)
* Check logs using RTT or UART
* Verify broker credentials and endpoint

---

## Author

**Engr Akbar Shah**

---

## Date

**21-05-2025**

---
