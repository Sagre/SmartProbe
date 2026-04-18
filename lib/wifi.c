/*
 * Copyright (c) 2023 Craig Peacock.
 * Copyright (c) 2017 ARM Ltd.
 * Copyright (c) 2016 Intel Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/net_mgmt.h>
#include <errno.h>
#include <zephyr/logging/log.h>

#include <zephyr/device.h>
#include "net_sample_common.h"
LOG_MODULE_REGISTER(wifi, LOG_LEVEL_INF);

const struct device *const wifi_dev = DEVICE_DT_GET(DT_NODELABEL(wifi));

#define WIFI_CONNECT_TIMEOUT_SEC	10
#define WIFI_IPV4_TIMEOUT_SEC		15

#define WIFI_SSID	CONFIG_WIFI_SSID
#define WIFI_PSK	CONFIG_WIFI_PSK

static K_SEM_DEFINE(wifi_connected, 0, 1);
static K_SEM_DEFINE(ipv4_address_obtained, 0, 1);


static struct net_mgmt_event_callback wifi_cb;
static struct net_mgmt_event_callback ipv4_cb;

static int check_wifi_status(void) {
    if (!device_is_ready(wifi_dev)) {
        LOG_ERR("WiFi device hardware not ready!");
        return -1;
    }
    return 0;
}

static void handle_wifi_connect_result(struct net_mgmt_event_callback *cb)
{
    const struct wifi_status *status = (const struct wifi_status *)cb->info;

    if (status->status)
    {
        LOG_ERR("Connection request failed (%d)", status->status);
    }
    else
    {
        LOG_INF("Connected");
        k_sem_give(&wifi_connected);
    }
}

static void handle_wifi_disconnect_result(struct net_mgmt_event_callback *cb)
{
    const struct wifi_status *status = (const struct wifi_status *)cb->info;

    if (status->status)
    {
        LOG_INF("Disconnection request (%d)", status->status);
    }
    else
    {
        LOG_INF("Disconnected");
        k_sem_take(&wifi_connected, K_NO_WAIT);
    }
}

static void handle_ipv4_result(struct net_if *iface)
{
    int i = 0;

    for (i = 0; i < NET_IF_MAX_IPV4_ADDR; i++) {

        if (iface->config.ip.ipv4->unicast[i].ipv4.addr_type != NET_ADDR_DHCP) {
            continue;
        }

        k_sem_give(&ipv4_address_obtained);
    }
}

static void wifi_mgmt_event_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event, struct net_if *iface)
{
    switch (mgmt_event)
    {

        case NET_EVENT_WIFI_CONNECT_RESULT:
            handle_wifi_connect_result(cb);
            const struct wifi_status *status = (const struct wifi_status *)cb->info;
            if (status->status == 0) {
                LOG_INF("Connected! Starting DHCPv4...");
                net_dhcpv4_start(iface); // Trigger the IP request
            }
            break;

        case NET_EVENT_WIFI_DISCONNECT_RESULT:
            handle_wifi_disconnect_result(cb);
            break;

        case NET_EVENT_IPV4_ADDR_ADD:
            LOG_INF("IPV4 connected");
            handle_ipv4_result(iface);
            break;

        default:
            break;
    }
}

int wifi_wait_for_connect(void)
{
    return k_sem_take(&wifi_connected, K_SECONDS(WIFI_CONNECT_TIMEOUT_SEC));
}

int wifi_wait_for_ipv4(void)
{
    return k_sem_take(&ipv4_address_obtained, K_SECONDS(WIFI_IPV4_TIMEOUT_SEC));
}

int wifi_init(void)
{
    int ret = check_wifi_status();
    if (ret) return ret;

    net_mgmt_init_event_callback(&wifi_cb, wifi_mgmt_event_handler,
                                 NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT);

    net_mgmt_init_event_callback(&ipv4_cb, wifi_mgmt_event_handler, NET_EVENT_IPV4_ADDR_ADD);

    net_mgmt_add_event_callback(&wifi_cb);
    net_mgmt_add_event_callback(&ipv4_cb);

    return 0;
}

int wifi_connect(void)
{
    struct net_if *iface = net_if_get_default();

    struct wifi_connect_req_params wifi_params = {0};

    wifi_params.ssid = WIFI_SSID;
    wifi_params.psk = WIFI_PSK;
    wifi_params.ssid_length = strlen(WIFI_SSID);
    wifi_params.psk_length = strlen(WIFI_PSK);
    wifi_params.channel = WIFI_CHANNEL_ANY;
    wifi_params.security = WIFI_SECURITY_TYPE_PSK;
    wifi_params.band = WIFI_FREQ_BAND_2_4_GHZ; 
    wifi_params.mfp = WIFI_MFP_OPTIONAL;

    LOG_INF("Connecting to SSID: %s", wifi_params.ssid);

    if (net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &wifi_params, sizeof(struct wifi_connect_req_params)))
    {
        LOG_ERR("WiFi Connection Request Failed");
        return -1;
    }
    return 0;
}

int wifi_status(void)
{
    struct net_if *iface = net_if_get_default();
    
    struct wifi_iface_status status = {0};

    if (net_mgmt(NET_REQUEST_WIFI_IFACE_STATUS, iface, &status,	sizeof(struct wifi_iface_status)))
    {
        LOG_ERR("WiFi Status Request Failed");
        return -1;
    }
    return 0;
}

int wifi_disconnect(void)
{
    struct net_if *iface = net_if_get_default();

    if (net_mgmt(NET_REQUEST_WIFI_DISCONNECT, iface, NULL, 0))
    {
        LOG_ERR("WiFi Disconnection Request Failed");
        return -1;
    }
    return 0;
}