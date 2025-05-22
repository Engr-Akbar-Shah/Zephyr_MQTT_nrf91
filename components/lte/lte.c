/*
Name        : lte.c

Description : Implementation of LTE modem handling for nRF91 using nRF Connect SDK.
              Contains logic for LTE event handling, modem and LTE initialization,
              deinitialization, and information extraction via AT commands.

Developer   : Engr. Akbar Shah

Date        : May 13, 2025
*/

#include <stdio.h>
#include <ncs_version.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <modem/nrf_modem_lib.h>
#include <modem/modem_info.h>
#include <nrf_modem_at.h>
#include <modem/lte_lc.h>
#include "lte.h"

#define LTE_POWER_OFF_RETRIES 10

#define MAX_MODEM_INFO_LEN 30

char MODEM_FW_VERSION[MAX_MODEM_INFO_LEN];
char MODEM_IMEI[MAX_MODEM_INFO_LEN];
char MODEM_ICCID[MAX_MODEM_INFO_LEN];

static K_SEM_DEFINE(lte_connected, 0, 1);

struct modem_param_info mdm_param;

LOG_MODULE_REGISTER(LTE_Nrf91);

/*
Function    : lte_handler

Description : LTE event callback handler that reacts to various modem events such as
              network registration, RRC updates, power saving updates, etc.

Parameter   : const struct lte_lc_evt *const evt - Pointer to the event data from LTE modem.

Return      : void

Example Call: lte_lc_connect_async(lte_handler);
*/
static void lte_handler(const struct lte_lc_evt *const evt)
{
    switch (evt->type)
    {
    case LTE_LC_EVT_NW_REG_STATUS:
        LOG_INF("Network registration status: %d", evt->nw_reg_status);
        switch (evt->nw_reg_status)
        {
        case LTE_LC_NW_REG_NOT_REGISTERED:
            LOG_INF("Network status: Not registered");
            break;
        case LTE_LC_NW_REG_REGISTERED_HOME:
            LOG_INF("Network status: Registered (home)");
            k_sem_give(&lte_connected);
            break;
        case LTE_LC_NW_REG_REGISTERED_ROAMING:
            LOG_INF("Network status: Registered (roaming)");
            k_sem_give(&lte_connected);
            break;
        case LTE_LC_NW_REG_SEARCHING:
            LOG_INF("Network status: Searching");
            break;
        case LTE_LC_NW_REG_REGISTRATION_DENIED:
            LOG_INF("Network status: Registration denied");
            break;
        case LTE_LC_NW_REG_UNKNOWN:
            LOG_INF("Network status: Unknown");
            break;
        case LTE_LC_NW_REG_UICC_FAIL:
            LOG_INF("Network status: UICC failure");
            break;
        }
        break;

    case LTE_LC_EVT_RRC_UPDATE:
        LOG_INF("RRC mode: %s",
                evt->rrc_mode == LTE_LC_RRC_MODE_CONNECTED ? "Connected" : "Idle");
        break;

    case LTE_LC_EVT_CELL_UPDATE:
        LOG_INF("Cell update: cell ID %d, TAC %d",
                evt->cell.id, evt->cell.tac);
        break;
#if CONFIG_LTE_LC_PSM_MODULE
    case LTE_LC_EVT_PSM_UPDATE:
        LOG_INF("PSM params: TAU: %d, Active time: %d",
                evt->psm_cfg.tau, evt->psm_cfg.active_time);
        break;
#endif
#if CONFIG_LTE_LC_EDRX_MODULE
    case LTE_LC_EVT_EDRX_UPDATE:
        LOG_INF("eDRX params: eDRX: %.2f, PTW: %.2f",
                (double)evt->edrx_cfg.edrx, (double)evt->edrx_cfg.ptw);
        break;
#endif
#if CONFIG_LTE_LC_MODEM_SLEEP_MODULE
    case LTE_LC_EVT_MODEM_SLEEP_ENTER:
        LOG_INF("Modem sleep entered, type: %d, time: %lld ms",
                evt->modem_sleep.type, evt->modem_sleep.time);
        break;

    case LTE_LC_EVT_MODEM_SLEEP_EXIT:
        LOG_INF("Modem sleep exited");
        break;
#endif
#if CONFIG_LTE_LC_TAU_PRE_WARNING_MODULE
    case LTE_LC_EVT_TAU_PRE_WARNING:
        LOG_INF("TAU pre-warning received");
        break;
#endif
    default:
        LOG_WRN("Unhandled LTE event: %d", evt->type);
        break;
    }
}

int get_modem_info_fw_version(char *fw_version, size_t len)
{
    if (!fw_version || len == 0)
    {
        return -EINVAL;
    }

    if (modem_info_string_get(MODEM_INFO_FW_VERSION, fw_version, len) <= 0)
    {
        LOG_WRN("Failed to get modem FW version");
        return -EIO;
    }

    LOG_INF("Modem FW version: %s", fw_version);
    return 0;
}

int get_modem_info_imei(char *imei, size_t len)
{
    if (!imei || len == 0)
    {
        return -EINVAL;
    }

    char response[128];
    int err = nrf_modem_at_cmd(response, sizeof(response), "AT+CGSN=1");
    if (err)
    {
        LOG_ERR("Couldn't get IMEI, error: %d", err);
        return err;
    }

    char *start = strchr(response, '"');
    char *end = start ? strchr(start + 1, '"') : NULL;
    if (!start || !end)
    {
        LOG_ERR("Failed to parse IMEI.");
        return -EBADMSG;
    }

    size_t imei_len = end - (start + 1);
    if (imei_len >= len)
        imei_len = len - 1;

    strncpy(imei, start + 1, imei_len);
    imei[imei_len] = '\0';

    return 0;
}

int get_modem_info_iccid(char *iccid, size_t len)
{
    if (!iccid || len == 0)
    {
        return -EINVAL;
    }

    char response[128];
    int err;

    // Set modem to full functionality
    err = nrf_modem_at_cmd(response, sizeof(response), "AT+CFUN=1");
    if (err)
    {
        LOG_ERR("Couldn't set modem to full functionality, error: %d", err);
        return err;
    }

    // Read ICCID
    err = nrf_modem_at_cmd(response, sizeof(response), "AT%%XICCID");
    if (err)
    {
        LOG_ERR("Couldn't get ICCID, error: %d", err);
        return err;
    }

    char *ccid = strchr(response, ':');
    if (!ccid)
    {
        LOG_ERR("Failed to parse ICCID.");
        return -EBADMSG;
    }

    ccid += 1;
    while (*ccid == ' ')
        ccid++; // skip whitespace

    size_t iccid_len = strcspn(ccid, "\r\n");
    if (iccid_len >= len)
        iccid_len = len - 1;

    strncpy(iccid, ccid, iccid_len);
    iccid[iccid_len] = '\0';
    return 0;
}

int modem_deinit(void)
{
    int err;

    // Give the modem time to settle
    k_msleep(8000);

    err = nrf_modem_lib_shutdown();
    if (err)
    {
        LOG_ERR("Modem library shutdown failed, error: %d", err);
    }
    else
    {
        LOG_INF("Modem library successfully shut down");
    }
    return err;
}

int modem_init(void)
{
    int err;

    LOG_INF("Initializing modem library");
    err = nrf_modem_lib_init();
    if (err)
    {
        LOG_ERR("Failed to initialize the modem library, error: %d", err);
        return err;
    }
    if ((err = modem_info_init()))
    {
        LOG_ERR("Modem info init failed, error: %d", err);
        return err;
    }

    if ((err = modem_info_params_init(&mdm_param)))
    {
        LOG_ERR("Modem info param init failed, error: %d", err);
        return err;
    }

    err = get_modem_info_imei(MODEM_IMEI, sizeof(MODEM_IMEI));
    if (!err)
    {
        LOG_INF("IMEI: [ %s ]", MODEM_IMEI);
    }

    err = get_modem_info_iccid(MODEM_ICCID, sizeof(MODEM_ICCID));
    if (!err)
    {
        LOG_INF("ICCID: [ %s ]", MODEM_ICCID);
    }

    err = get_modem_info_fw_version(MODEM_FW_VERSION, sizeof(MODEM_FW_VERSION));
    if (!err)
    {
        LOG_INF("Modem FW version: %s", MODEM_FW_VERSION);
    }
    return err;
}

int lte_deinit(void)
{
    int err;
    int retry_count = 0;
    enum lte_lc_func_mode mode;

    LOG_INF("Powering off LTE modem...");
    err = lte_lc_power_off();
    if (err)
    {
        LOG_ERR("Failed to power off LTE modem, error: %d", err);
        return err;
    }

    // Wait for LTE modem to reach power-off state
    while (retry_count++ < LTE_POWER_OFF_RETRIES)
    {
        err = lte_lc_func_mode_get(&mode);
        if (!err && mode == LTE_LC_FUNC_MODE_POWER_OFF)
        {
            LOG_INF("LTE modem is powered off.");
            break;
        }
        LOG_DBG("Waiting for modem to power off...");
        k_msleep(500);
    }

    if (retry_count >= LTE_POWER_OFF_RETRIES)
    {
        LOG_ERR("Timeout while waiting for LTE modem to power off.");
        return -1;
    }
#if NCS_VERSION_NUMBER < 0x20600
    LOG_INF("Deinitializing LTE link controller...");
    err = lte_lc_deinit();
    if (err)
    {
        LOG_ERR("Failed to deinitialize LTE link controller, error: %d", err);
        return err;
    }
#endif
    return err;
}

int lte_init(void)
{
    int err;
/* lte_lc_init deprecated in >= v2.6.0 */
#if NCS_VERSION_NUMBER < 0x20600
    err = lte_lc_init();
    if (err)
    {
        LOG_ERR("Failed to initialize LTE link control library, error: %d", err);
        return err;
    }
#endif

    LOG_INF("Connecting to LTE network");
    err = lte_lc_connect_async(lte_handler);
    if (err)
    {
        LOG_ERR("Error in lte_lc_connect_async, error: %d", err);
        return err;
    }
    k_sem_take(&lte_connected, K_FOREVER);
    return 0;
}