#include "cluster_espnow.h"
#include "cluster.h"
#include "cluster_protocol.h"
#include "cluster_master.h"
#include "cluster_slave.h"

#include <string.h>

#include "esp_log.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

static const char *TAG = "cluster_espnow";

// Broadcast MAC (all 0xFF)
static const uint8_t s_broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static uint8_t s_wifi_channel = 1;
static bool s_initialized = false;

// Receive buffer item for the queue
struct espnow_rx_item_t {
    uint8_t src_mac[6];
    uint8_t data[CLUSTER_ESPNOW_MAX_LEN];
    int data_len;
};

// Queue for received messages (processed in cluster task context instead of ISR)
static QueueHandle_t s_rx_queue = NULL;
#define RX_QUEUE_SIZE 16

// ESP-NOW receive callback (runs in WiFi task context — keep short)
static void espnow_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int data_len)
{
    if (!recv_info || !data || data_len <= 0 || data_len > CLUSTER_ESPNOW_MAX_LEN) {
        return;
    }

    espnow_rx_item_t item;
    memcpy(item.src_mac, recv_info->src_addr, 6);
    memcpy(item.data, data, data_len);
    item.data_len = data_len;

    // Non-blocking enqueue — drop if queue is full
    if (xQueueSendToBack(s_rx_queue, &item, 0) != pdTRUE) {
        ESP_LOGW(TAG, "RX queue full, dropping message");
    }
}

// ESP-NOW send callback
static void espnow_send_cb(const esp_now_send_info_t *tx_info, esp_now_send_status_t status)
{
    if (status != ESP_NOW_SEND_SUCCESS) {
        ESP_LOGD(TAG, "Send to " MACSTR " failed", MAC2STR(tx_info->des_addr));
    }
}

// RX processing task — drains queue and dispatches to master/slave handlers
static void espnow_rx_task(void *pvParameters)
{
    espnow_rx_item_t item;

    while (1) {
        if (xQueueReceive(s_rx_queue, &item, portMAX_DELAY) == pdTRUE) {
            cluster_mode_t mode = cluster_get_mode();

            // Check for beacon — accept both "CLAXE" (NerdAxe) and "CLAXE,MASTER" (ClusterAxe)
            if (item.data_len >= CLUSTER_BEACON_MAGIC_LEN &&
                memcmp(item.data, CLUSTER_BEACON_MAGIC, CLUSTER_BEACON_MAGIC_LEN) == 0) {
                // Only slaves care about beacons
                if (mode == CLUSTER_MODE_SLAVE) {
                    cluster_slave_on_beacon(item.src_mac);
                }
                continue;
            }

            // Must be an NMEA message
            const char *msg = (const char *)item.data;
            const char *msg_type = cluster_protocol_identify(msg, item.data_len);

            if (!msg_type) {
                ESP_LOGD(TAG, "Unknown message from " MACSTR, MAC2STR(item.src_mac));
                continue;
            }

            if (mode == CLUSTER_MODE_MASTER) {
                if (strcmp(msg_type, CLUSTER_MSG_REGISTER) == 0) {
                    cluster_register_t reg;
                    if (cluster_protocol_decode_register(msg, item.data_len, &reg) == 0) {
                        // Source MAC comes from ESP-NOW frame
                        cluster_master_on_register(&reg, item.src_mac);
                    }
                } else if (strcmp(msg_type, CLUSTER_MSG_SHARE) == 0) {
                    cluster_share_t share;
                    if (cluster_protocol_decode_share(msg, item.data_len, &share) == 0) {
                        cluster_master_on_share(&share);
                    }
                } else if (strcmp(msg_type, CLUSTER_MSG_HEARTBEAT) == 0) {
                    cluster_heartbeat_data_t hb;
                    if (cluster_protocol_decode_heartbeat(msg, item.data_len, &hb) == 0) {
                        cluster_master_on_heartbeat(&hb, item.src_mac);
                    }
                }
            } else if (mode == CLUSTER_MODE_SLAVE) {
                if (strcmp(msg_type, CLUSTER_MSG_WORK) == 0) {
                    cluster_work_t work;
                    if (cluster_protocol_decode_work(msg, item.data_len, &work) == 0) {
                        cluster_slave_on_work(&work);
                    }
                } else if (strcmp(msg_type, CLUSTER_MSG_CONFIG) == 0) {
                    cluster_config_cmd_t cfg;
                    if (cluster_protocol_decode_config(msg, item.data_len, &cfg) == 0) {
                        cluster_slave_on_config(&cfg);
                    }
                } else if (strcmp(msg_type, CLUSTER_MSG_RESTART) == 0) {
                    cluster_restart_cmd_t rst;
                    if (cluster_protocol_decode_restart(msg, item.data_len, &rst) == 0) {
                        cluster_slave_on_restart();
                    }
                } else if (strcmp(msg_type, CLUSTER_MSG_ACK) == 0) {
                    // Debug: log raw ACK message
                    char ack_dbg[CLUSTER_MSG_MAX_LEN + 1];
                    int dbg_len = item.data_len < (int)sizeof(ack_dbg) - 1 ? item.data_len : (int)sizeof(ack_dbg) - 1;
                    memcpy(ack_dbg, msg, dbg_len);
                    ack_dbg[dbg_len] = '\0';
                    ESP_LOGW(TAG, "Raw ACK (%d bytes): [%s]", item.data_len, ack_dbg);

                    cluster_ack_t ack;
                    if (cluster_protocol_decode_ack(msg, item.data_len, &ack) == 0) {
                        ESP_LOGW(TAG, "Parsed ACK: slave_id=%d accepted=%d", ack.slave_id, ack.accepted);
                        cluster_slave_on_ack(&ack);
                    } else {
                        ESP_LOGW(TAG, "ACK decode failed");
                    }
                }
            }
        }
    }
}

void cluster_espnow_init(uint8_t wifi_channel)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return;
    }

    s_wifi_channel = wifi_channel;

    // Create RX queue
    s_rx_queue = xQueueCreate(RX_QUEUE_SIZE, sizeof(espnow_rx_item_t));
    if (!s_rx_queue) {
        ESP_LOGE(TAG, "Failed to create RX queue");
        return;
    }

    // Initialize ESP-NOW
    esp_err_t ret = esp_now_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_now_init failed: %s", esp_err_to_name(ret));
        return;
    }

    // Register callbacks
    esp_now_register_recv_cb(espnow_recv_cb);
    esp_now_register_send_cb(espnow_send_cb);

    // Add broadcast peer
    esp_now_peer_info_t broadcast_peer = {};
    memcpy(broadcast_peer.peer_addr, s_broadcast_mac, 6);
    broadcast_peer.channel = 0; // Use current channel
    broadcast_peer.encrypt = false;
    broadcast_peer.ifidx = WIFI_IF_STA;

    ret = esp_now_add_peer(&broadcast_peer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add broadcast peer: %s", esp_err_to_name(ret));
    }

    // Create RX processing task
    xTaskCreate(espnow_rx_task, "espnow_rx", 4096, NULL, 6, NULL);

    s_initialized = true;
    ESP_LOGI(TAG, "ESP-NOW initialized on channel %d", wifi_channel);
}

void cluster_espnow_deinit(void)
{
    if (!s_initialized) return;

    esp_now_unregister_recv_cb();
    esp_now_unregister_send_cb();
    esp_now_deinit();

    if (s_rx_queue) {
        vQueueDelete(s_rx_queue);
        s_rx_queue = NULL;
    }

    s_initialized = false;
    ESP_LOGI(TAG, "ESP-NOW deinitialized");
}

void cluster_espnow_broadcast_beacon(void)
{
    // Send ClusterAxe-compatible beacon: "CLAXE,MASTER"
    static const char beacon[] = "CLAXE,MASTER";
    esp_err_t ret = esp_now_send(s_broadcast_mac,
                                  (const uint8_t *)beacon,
                                  strlen(beacon));
    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "Beacon broadcast failed: %s", esp_err_to_name(ret));
    }
}

int cluster_espnow_send(const uint8_t *peer_mac, const uint8_t *data, size_t len)
{
    if (len > CLUSTER_ESPNOW_MAX_LEN) {
        ESP_LOGW(TAG, "Message too large: %d > %d", (int)len, CLUSTER_ESPNOW_MAX_LEN);
        return -1;
    }

    esp_err_t ret = esp_now_send(peer_mac, data, len);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Send failed: %s", esp_err_to_name(ret));
        return -1;
    }
    return 0;
}

int cluster_espnow_broadcast(const uint8_t *data, size_t len)
{
    return cluster_espnow_send(s_broadcast_mac, data, len);
}

void cluster_espnow_add_peer(const uint8_t *mac)
{
    if (esp_now_is_peer_exist(mac)) {
        return;
    }

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, mac, 6);
    peer.channel = 0; // Use current channel
    peer.encrypt = false;
    peer.ifidx = WIFI_IF_STA;

    esp_err_t ret = esp_now_add_peer(&peer);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to add peer " MACSTR ": %s", MAC2STR(mac), esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Added peer " MACSTR, MAC2STR(mac));
    }
}

void cluster_espnow_remove_peer(const uint8_t *mac)
{
    if (!esp_now_is_peer_exist(mac)) {
        return;
    }

    esp_err_t ret = esp_now_del_peer(mac);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to remove peer " MACSTR ": %s", MAC2STR(mac), esp_err_to_name(ret));
    }
}

void cluster_espnow_get_mac(uint8_t *mac_out)
{
    esp_read_mac(mac_out, ESP_MAC_WIFI_STA);
}
