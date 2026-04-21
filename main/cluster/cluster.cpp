#include "cluster.h"
#include "cluster_espnow.h"
#include "cluster_master.h"
#include "cluster_slave.h"
#include "cluster_watchdog.h"
#include "tasks/create_jobs_task.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "cluster";

static cluster_mode_t s_cluster_mode = CLUSTER_MODE_DISABLED;
static uint8_t s_wifi_channel = 1;
static TaskHandle_t s_cluster_task_handle = NULL;

// Main cluster task — runs beacon (master) or discovery (slave)
static void cluster_task(void *pvParameters)
{
    if (s_cluster_mode == CLUSTER_MODE_MASTER) {
        ESP_LOGI(TAG, "Starting master cluster task");
        cluster_master_init();

        uint32_t beacon_count = 0;
        while (1) {
            // Broadcast discovery beacon
            cluster_espnow_broadcast_beacon();
            // Check for stale slaves
            cluster_master_timeout_check();

            // Periodically re-broadcast current work so slaves that missed initial
            // delivery (ESP-NOW is fire-and-forget) recover within seconds rather
            // than waiting for the full CLUSTER_WORK_TIMEOUT_MS self-heal cycle.
            beacon_count++;
            if (beacon_count >= CLUSTER_WORK_RESEND_BEACONS) {
                beacon_count = 0;
                if (cluster_master_get_active_count() > 0) {
                    cluster_retrigger_work_distribution();
                }
            }

            vTaskDelay(pdMS_TO_TICKS(CLUSTER_BEACON_INTERVAL_MS));
        }
    } else if (s_cluster_mode == CLUSTER_MODE_SLAVE) {
        ESP_LOGI(TAG, "Starting slave cluster task");
        cluster_slave_init();

        while (1) {
            // Reconnect watchdog: self-deregister if no work received from master within timeout.
            // This fires before the registration attempt so a single loop tick handles the reset.
            cluster_slave_check_reconnect();

            // Attempt registration if not yet registered (or if check_reconnect cleared it)
            if (!cluster_slave_is_registered()) {
                cluster_slave_try_register();
            }
            // Send heartbeat if registered
            if (cluster_slave_is_registered()) {
                cluster_slave_send_heartbeat();
            }
            vTaskDelay(pdMS_TO_TICKS(CLUSTER_HEARTBEAT_INTERVAL_MS));
        }
    }

    // Should never reach here
    vTaskDelete(NULL);
}

void cluster_init(cluster_mode_t mode, uint8_t wifi_channel)
{
    if (mode == CLUSTER_MODE_DISABLED) {
        return;
    }

    s_cluster_mode = mode;
    s_wifi_channel = wifi_channel;

    ESP_LOGI(TAG, "Initializing cluster in %s mode on channel %d",
             mode == CLUSTER_MODE_MASTER ? "MASTER" : "SLAVE", wifi_channel);

    // Initialize ESP-NOW transport
    cluster_espnow_init(wifi_channel);

    // Initialize safety watchdog (loads enabled state from NVS)
    cluster_watchdog_init();

    // Create cluster task
    xTaskCreate(cluster_task, "cluster", 4096, NULL, 5, &s_cluster_task_handle);
}

void cluster_deinit(void)
{
    if (s_cluster_task_handle) {
        vTaskDelete(s_cluster_task_handle);
        s_cluster_task_handle = NULL;
    }

    cluster_espnow_deinit();

    if (s_cluster_mode == CLUSTER_MODE_MASTER) {
        cluster_master_deinit();
    } else if (s_cluster_mode == CLUSTER_MODE_SLAVE) {
        cluster_slave_deinit();
    }

    s_cluster_mode = CLUSTER_MODE_DISABLED;
    ESP_LOGI(TAG, "Cluster deinitialized");
}

cluster_mode_t cluster_get_mode(void)
{
    return s_cluster_mode;
}

int cluster_get_active_slave_count(void)
{
    if (s_cluster_mode != CLUSTER_MODE_MASTER) {
        return 0;
    }
    return cluster_master_get_active_count();
}

const cluster_slave_info_t* cluster_get_slave_info(void)
{
    return cluster_master_get_slaves();
}

float cluster_get_total_hashrate(void)
{
    return cluster_master_get_total_hashrate();
}

bool cluster_slave_is_registered(void)
{
    return cluster_slave_is_reg();
}

uint8_t cluster_slave_get_id(void)
{
    return cluster_slave_get_assigned_id();
}

uint32_t cluster_slave_get_shares_submitted(void)
{
    return cluster_slave_get_shares();
}

double cluster_get_best_diff(void)
{
    if (s_cluster_mode != CLUSTER_MODE_MASTER) return 0.0;
    return cluster_master_get_best_diff();
}

bool cluster_send_slave_config(uint8_t slave_id, const cluster_config_cmd_t *cfg)
{
    if (s_cluster_mode != CLUSTER_MODE_MASTER) return false;
    return cluster_master_send_config(slave_id, cfg);
}

bool cluster_send_slave_restart(uint8_t slave_id)
{
    if (s_cluster_mode != CLUSTER_MODE_MASTER) return false;
    return cluster_master_send_restart(slave_id);
}
