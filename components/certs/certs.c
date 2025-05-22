#include "certs.h"
#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <modem/nrf_modem_lib.h>
#include <modem/modem_key_mgmt.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(CERTS);

#define MQTT_SEC_TAG 30

const char *modem_key_mgmt_cred_type_str(enum modem_key_mgmt_cred_type key)
{
    switch (key)
    {
    case MODEM_KEY_MGMT_CRED_TYPE_CA_CHAIN:
        return "MODEM_KEY_MGMT_CRED_TYPE_CA_CHAIN";
    case MODEM_KEY_MGMT_CRED_TYPE_PUBLIC_CERT:
        return "MODEM_KEY_MGMT_CRED_TYPE_PUBLIC_CERT";
    case MODEM_KEY_MGMT_CRED_TYPE_PRIVATE_CERT:
        return "MODEM_KEY_MGMT_CRED_TYPE_PRIVATE_CERT";
    default:
        return "UNKNOWN_CRED_TYPE";
    }
}

static int write_certificates_to_modem(uint32_t TAG, enum modem_key_mgmt_cred_type key, const void *buf, size_t len)
{
    int err;
    bool exists;

    err = modem_key_mgmt_exists(TAG, key, &exists);
    if (err)
    {
        LOG_ERR("Failed to check for certificates [ %s ] err %d\n", modem_key_mgmt_cred_type_str(key), err);
        return err;
    }

    if (exists)
    {
        /* For the sake of simplicity we delete what is provisioned
         * with our security tag and reprovision our certificate.
         */
        err = modem_key_mgmt_delete(TAG, key);
        if (err)
        {
            LOG_ERR("Failed to delete existing certificate [ %s ], err [%d]\n",
                    modem_key_mgmt_cred_type_str(key), err);
        }
    }

    err = modem_key_mgmt_write(TAG, key, buf, len);
    if (err)
    {
        LOG_ERR("Failed to write certificate [ %s ]to modem\n", modem_key_mgmt_cred_type_str(key));
        return err;
    }
    LOG_INF("Updated CERT [%s]\n\r", modem_key_mgmt_cred_type_str(key));

    return err;
}

int write_device_certs_to_modem(void)
{
    int err;
    err = nrf_modem_lib_init();
    if (err)
    {
        // LOG_ERR("Modem info initialization failed, error: %d", err);
        return err;
    }

    write_certificates_to_modem(MQTT_SEC_TAG, MODEM_KEY_MGMT_CRED_TYPE_CA_CHAIN, (const void *)root_ca, sizeof(root_ca));

    write_certificates_to_modem(MQTT_SEC_TAG, MODEM_KEY_MGMT_CRED_TYPE_PUBLIC_CERT, (const void *)device_cert, sizeof(device_cert));

    write_certificates_to_modem(MQTT_SEC_TAG, MODEM_KEY_MGMT_CRED_TYPE_PRIVATE_CERT, (const void *)private_key, sizeof(private_key));

    k_msleep(2000);

    err = nrf_modem_lib_shutdown();
    if (err)
    {
        LOG_ERR("Modem library shutdown failed, error: %d", err);
    }
    else
    {
        LOG_INF("Modem library successfully shut down");
    }
    k_msleep(4000);
    return err;
}
