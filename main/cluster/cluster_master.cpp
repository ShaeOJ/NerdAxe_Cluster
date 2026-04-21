#include "cluster_master.h"
#include "cluster_espnow.h"
#include "cluster_protocol.h"
#include "cluster.h"

#include <string.h>
#include <pthread.h>

#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "mining.h"
#include "global_state.h"
#include "tasks/create_jobs_task.h"

static const char *TAG = "cluster_master";

// Slave registry
static cluster_slave_info_t s_slaves[CLUSTER_MAX_SLAVES];
static pthread_mutex_t s_slaves_mutex = PTHREAD_MUTEX_INITIALIZER;

// Best difficulty seen across all slave shares this session
static double s_best_diff = 0.0;

// Share queue — shares from slaves waiting to be submitted to pool
static QueueHandle_t s_share_queue = NULL;
static TaskHandle_t s_share_task_handle = NULL;

// Recent share dedup buffer — slaves send each share 3x for reliability
#define RECENT_SHARES_SIZE 32
struct recent_share_entry_t {
    uint32_t nonce;
    uint32_t job_id;
    uint8_t  slave_id;
    bool     valid;
};
static recent_share_entry_t s_recent_shares[RECENT_SHARES_SIZE];
static int s_recent_shares_idx = 0;

static bool is_duplicate_share(const cluster_share_t *share)
{
    for (int i = 0; i < RECENT_SHARES_SIZE; i++) {
        if (s_recent_shares[i].valid &&
            s_recent_shares[i].nonce == share->nonce &&
            s_recent_shares[i].job_id == share->job_id &&
            s_recent_shares[i].slave_id == share->slave_id) {
            return true;
        }
    }
    return false;
}

static void record_share(const cluster_share_t *share)
{
    s_recent_shares[s_recent_shares_idx].nonce = share->nonce;
    s_recent_shares[s_recent_shares_idx].job_id = share->job_id;
    s_recent_shares[s_recent_shares_idx].slave_id = share->slave_id;
    s_recent_shares[s_recent_shares_idx].valid = true;
    s_recent_shares_idx = (s_recent_shares_idx + 1) % RECENT_SHARES_SIZE;
}

// Job ID derived from stratum job_id hex (ClusterAxe compatible)
// Fallback counter used when strtoul returns 0
static uint32_t s_job_id_fallback = 0x80000000;

// Job mapping: cluster job_id -> stratum job_id + extranonce2 (for share submission)
struct cluster_job_map_entry_t {
    uint32_t cluster_job_id;
    char stratum_job_id[32];
    char extranonce2[17]; // max 8 bytes hex = 16 chars + null
    int pool_id;
    bool valid;
};
static cluster_job_map_entry_t s_job_map[CLUSTER_JOB_MAP_SIZE];
static int s_job_map_index = 0;

static void store_job_mapping(uint32_t cluster_job_id, const char *stratum_job_id,
                               const char *extranonce2, int pool_id)
{
    cluster_job_map_entry_t *entry = &s_job_map[s_job_map_index % CLUSTER_JOB_MAP_SIZE];
    entry->cluster_job_id = cluster_job_id;
    strncpy(entry->stratum_job_id, stratum_job_id, sizeof(entry->stratum_job_id) - 1);
    entry->stratum_job_id[sizeof(entry->stratum_job_id) - 1] = '\0';
    strncpy(entry->extranonce2, extranonce2, sizeof(entry->extranonce2) - 1);
    entry->extranonce2[sizeof(entry->extranonce2) - 1] = '\0';
    entry->pool_id = pool_id;
    entry->valid = true;
    s_job_map_index++;
}

// Look up stratum job_id and extranonce2 from job map by cluster job_id
static bool lookup_job_mapping(uint32_t cluster_job_id, char *stratum_job_id, size_t sjid_len,
                                char *extranonce2, size_t en2_len, int *pool_id)
{
    for (int i = 0; i < CLUSTER_JOB_MAP_SIZE; i++) {
        if (s_job_map[i].valid && s_job_map[i].cluster_job_id == cluster_job_id) {
            strncpy(stratum_job_id, s_job_map[i].stratum_job_id, sjid_len - 1);
            stratum_job_id[sjid_len - 1] = '\0';
            strncpy(extranonce2, s_job_map[i].extranonce2, en2_len - 1);
            extranonce2[en2_len - 1] = '\0';
            *pool_id = s_job_map[i].pool_id;
            return true;
        }
    }
    return false;
}

// Share submitter task — drains share queue and submits to stratum
static void share_submit_task(void *pvParameters)
{
    cluster_share_t share;

    while (1) {
        if (xQueueReceive(s_share_queue, &share, portMAX_DELAY) == pdTRUE) {
            if (!STRATUM_MANAGER) {
                ESP_LOGW(TAG, "No stratum manager, dropping slave share");
                continue;
            }

            // Look up the stratum job_id and extranonce2 from our job map
            char stratum_job_id[32];
            char en2_hex[17];
            int pool_id = 0;

            if (!lookup_job_mapping(share.job_id, stratum_job_id, sizeof(stratum_job_id),
                                     en2_hex, sizeof(en2_hex), &pool_id)) {
                ESP_LOGW(TAG, "JOB MAP MISS: cluster_job_id=%lu slave=%d — share DROPPED (map has %d entries)",
                         (unsigned long)share.job_id, share.slave_id, s_job_map_index);
                continue;
            }

            ESP_LOGI(TAG, "POOL SUBMIT: slave=%d job=%s en2=%s nonce=%08lX ntime=%08lX ver=%08lX diff=%.1f",
                     share.slave_id, stratum_job_id, en2_hex,
                     (unsigned long)share.nonce, (unsigned long)share.ntime,
                     (unsigned long)share.version, share.difficulty);

            // Submit to stratum pool using the looked-up stratum job_id
            STRATUM_MANAGER->submitShare(
                pool_id,
                stratum_job_id,
                en2_hex,
                share.ntime,
                share.nonce,
                share.version);

            // Update slave stats
            pthread_mutex_lock(&s_slaves_mutex);
            for (int i = 0; i < CLUSTER_MAX_SLAVES; i++) {
                if (s_slaves[i].active && s_slaves[i].slave_id == share.slave_id) {
                    s_slaves[i].shares_accepted++;
                    break;
                }
            }
            pthread_mutex_unlock(&s_slaves_mutex);
        }
    }
}

void cluster_master_init(void)
{
    memset(s_slaves, 0, sizeof(s_slaves));
    memset(s_job_map, 0, sizeof(s_job_map));
    memset(s_recent_shares, 0, sizeof(s_recent_shares));
    s_job_id_fallback = 0x80000000;
    s_job_map_index = 0;
    s_recent_shares_idx = 0;

    // Create share queue
    s_share_queue = xQueueCreate(CLUSTER_SHARE_QUEUE_DEPTH, sizeof(cluster_share_t));
    if (!s_share_queue) {
        ESP_LOGE(TAG, "Failed to create share queue");
        return;
    }

    // Create share submitter task
    xTaskCreate(share_submit_task, "cl_share_sub", 4096, NULL, 8, &s_share_task_handle);

    ESP_LOGI(TAG, "Master initialized, waiting for slaves");
}

void cluster_master_deinit(void)
{
    if (s_share_task_handle) {
        vTaskDelete(s_share_task_handle);
        s_share_task_handle = NULL;
    }
    if (s_share_queue) {
        vQueueDelete(s_share_queue);
        s_share_queue = NULL;
    }
    memset(s_slaves, 0, sizeof(s_slaves));
}

void cluster_master_on_register(const cluster_register_t *reg, const uint8_t *src_mac)
{
    pthread_mutex_lock(&s_slaves_mutex);

    // Check if already registered (by MAC from ESP-NOW frame)
    for (int i = 0; i < CLUSTER_MAX_SLAVES; i++) {
        if (s_slaves[i].active && memcmp(s_slaves[i].mac, src_mac, 6) == 0) {
            ESP_LOGI(TAG, "Slave %s re-registering (id=%d)", reg->hostname, s_slaves[i].slave_id);
            // Update hostname and IP, refresh timestamp
            strncpy(s_slaves[i].hostname, reg->hostname, sizeof(s_slaves[i].hostname) - 1);
            strncpy(s_slaves[i].ip_addr, reg->ip_addr, sizeof(s_slaves[i].ip_addr) - 1);
            s_slaves[i].last_heartbeat_ms = now_ms();

            // Send ACK
            cluster_ack_t ack = {};
            ack.slave_id = s_slaves[i].slave_id;
            ack.accepted = true;
            strncpy(ack.hostname, reg->hostname, sizeof(ack.hostname) - 1);

            char msg[CLUSTER_MSG_MAX_LEN];
            int len = cluster_protocol_encode_ack(&ack, msg, sizeof(msg));
            if (len > 0) {
                cluster_espnow_send(src_mac, (const uint8_t *)msg, len);
            }

            pthread_mutex_unlock(&s_slaves_mutex);
            // Slave re-registered — send current work immediately (slave may have rebooted)
            cluster_retrigger_work_distribution();
            return;
        }
    }

    // Find free slot
    int free_slot = -1;
    for (int i = 0; i < CLUSTER_MAX_SLAVES; i++) {
        if (!s_slaves[i].active) {
            free_slot = i;
            break;
        }
    }

    if (free_slot < 0) {
        ESP_LOGW(TAG, "No free slave slots, rejecting %s", reg->hostname);
        cluster_ack_t ack = {};
        ack.slave_id = 0;
        ack.accepted = false;

        char msg[CLUSTER_MSG_MAX_LEN];
        int len = cluster_protocol_encode_ack(&ack, msg, sizeof(msg));
        if (len > 0) {
            cluster_espnow_send(src_mac, (const uint8_t *)msg, len);
        }

        pthread_mutex_unlock(&s_slaves_mutex);
        return;
    }

    // Register new slave
    cluster_slave_info_t *slave = &s_slaves[free_slot];
    slave->active = true;
    slave->slave_id = (uint8_t)(free_slot + 1); // slave_id 1-8, 0 is master
    memcpy(slave->mac, src_mac, 6);
    strncpy(slave->hostname, reg->hostname, sizeof(slave->hostname) - 1);
    slave->hostname[sizeof(slave->hostname) - 1] = '\0';
    strncpy(slave->ip_addr, reg->ip_addr, sizeof(slave->ip_addr) - 1);
    slave->ip_addr[sizeof(slave->ip_addr) - 1] = '\0';
    slave->hashrate = 0;
    slave->temp = 0;
    slave->shares_accepted = 0;
    slave->shares_rejected = 0;
    slave->shares_submitted = 0;
    slave->last_heartbeat_ms = now_ms();
    slave->registered_at_ms = now_ms();

    // Add as ESP-NOW peer
    cluster_espnow_add_peer(src_mac);

    ESP_LOGI(TAG, "Registered slave %d: %s (" MACSTR ") ip=%s",
             slave->slave_id, slave->hostname, MAC2STR(slave->mac), slave->ip_addr);

    // Send ACK
    cluster_ack_t ack = {};
    ack.slave_id = slave->slave_id;
    ack.accepted = true;
    strncpy(ack.hostname, reg->hostname, sizeof(ack.hostname) - 1);

    char msg[CLUSTER_MSG_MAX_LEN];
    int len = cluster_protocol_encode_ack(&ack, msg, sizeof(msg));
    if (len > 0) {
        cluster_espnow_send(src_mac, (const uint8_t *)msg, len);
    }

    pthread_mutex_unlock(&s_slaves_mutex);
    // Send current work immediately so the slave doesn't wait for next mining.notify
    cluster_retrigger_work_distribution();
}

void cluster_master_on_share(const cluster_share_t *share)
{
    if (!s_share_queue) return;

    // Dedup — slaves send each share 3x for reliability, only enqueue once
    if (is_duplicate_share(share)) {
        ESP_LOGI(TAG, "DEDUP: duplicate from slave %d (nonce %08lX) — good, means 3x send working",
                 share->slave_id, (unsigned long)share->nonce);
        return;
    }
    ESP_LOGI(TAG, "SHARE RX: slave=%d job=%lu nonce=%08lX (new, enqueueing)",
             share->slave_id, (unsigned long)share->job_id, (unsigned long)share->nonce);
    record_share(share);

    if (share->difficulty > s_best_diff) {
        s_best_diff = share->difficulty;
    }

    // Enqueue the share for submission (non-blocking)
    if (xQueueSendToBack(s_share_queue, share, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Share queue full, dropping share from slave %d", share->slave_id);
    }
}

void cluster_master_on_heartbeat(const cluster_heartbeat_data_t *hb, const uint8_t *src_mac)
{
    pthread_mutex_lock(&s_slaves_mutex);
    bool found = false;

    for (int i = 0; i < CLUSTER_MAX_SLAVES; i++) {
        if (s_slaves[i].active && s_slaves[i].slave_id == hb->slave_id) {
            s_slaves[i].hashrate = hb->hashrate;
            s_slaves[i].temp = hb->temp;
            s_slaves[i].fan_rpm = hb->fan_rpm;
            s_slaves[i].shares_submitted = hb->shares;
            s_slaves[i].frequency = hb->frequency;
            s_slaves[i].core_voltage = hb->core_voltage;
            s_slaves[i].power = hb->power;
            s_slaves[i].voltage_in = hb->voltage_in;
            s_slaves[i].last_heartbeat_ms = now_ms();
            found = true;

            ESP_LOGD(TAG, "Heartbeat from slave %d (%s): %.2f GH/s, %.1fC",
                     hb->slave_id, s_slaves[i].hostname,
                     hb->hashrate / 100.0, hb->temp);
            break;
        }
    }

    pthread_mutex_unlock(&s_slaves_mutex);

    if (!found) {
        // Slave thinks it's registered but master has no record of it (timed out and evicted,
        // or master rebooted). Send a NACK so the slave immediately clears its registered state
        // and re-registers rather than staying stuck in a zombie state.
        ESP_LOGW(TAG, "Heartbeat from unknown slave_id=%d (" MACSTR "), sending NACK to trigger re-register",
                 hb->slave_id, MAC2STR(src_mac));
        cluster_ack_t nack = {};
        nack.slave_id = 0;
        nack.accepted = false;
        char msg[CLUSTER_MSG_MAX_LEN];
        int len = cluster_protocol_encode_ack(&nack, msg, sizeof(msg));
        if (len > 0) {
            // Peer may have been removed when the slot was evicted — add it back so the send works.
            cluster_espnow_add_peer(src_mac);
            cluster_espnow_send(src_mac, (const uint8_t *)msg, len);
        }
    }
}

void cluster_master_timeout_check(void)
{
    uint64_t now = now_ms();
    pthread_mutex_lock(&s_slaves_mutex);

    for (int i = 0; i < CLUSTER_MAX_SLAVES; i++) {
        if (s_slaves[i].active &&
            (now - s_slaves[i].last_heartbeat_ms) > CLUSTER_SLAVE_TIMEOUT_MS) {
            ESP_LOGW(TAG, "Slave %d (%s) timed out, removing",
                     s_slaves[i].slave_id, s_slaves[i].hostname);
            cluster_espnow_remove_peer(s_slaves[i].mac);
            memset(&s_slaves[i], 0, sizeof(cluster_slave_info_t));
        }
    }

    pthread_mutex_unlock(&s_slaves_mutex);
}

void cluster_master_distribute_work(void *notify_ptr, int pool,
                                     const char *extranonce_str,
                                     int extranonce_2_len,
                                     uint32_t version_mask,
                                     uint32_t stratum_difficulty,
                                     uint32_t asic_diff,
                                     uint32_t en2_tick)
{
    mining_notify *notify = (mining_notify *)notify_ptr;

    pthread_mutex_lock(&s_slaves_mutex);

    // Count active slaves
    int active_count = 0;
    for (int i = 0; i < CLUSTER_MAX_SLAVES; i++) {
        if (s_slaves[i].active) active_count++;
    }

    if (active_count == 0) {
        pthread_mutex_unlock(&s_slaves_mutex);
        return;
    }

    // Partition nonce space: master gets slot 0, slaves get 1..N
    int total_workers = 1 + active_count; // master + slaves
    uint32_t range = 0xFFFFFFFF / total_workers;

    int worker_index = 1; // 0 is master

    for (int i = 0; i < CLUSTER_MAX_SLAVES; i++) {
        if (!s_slaves[i].active) continue;

        // ClusterAxe: base job_id = strtoul(stratum_job_id_hex)
        uint32_t base_job_id = 0;
        if (notify->job_id) {
            base_job_id = (uint32_t)strtoul(notify->job_id, NULL, 16);
        }
        if (base_job_id == 0) {
            base_job_id = s_job_id_fallback++;
        }

        // Each slave gets a unique cluster_job_id to:
        // (a) give it a unique extranonce2 so slaves search different hash spaces,
        // (b) avoid job-map collisions where all slaves for one stratum job share the same key, and
        // (c) change each tick (en2_tick) so the ASIC hashes a fresh block header
        //     rather than cycling through the same nonces repeatedly.
        uint32_t job_id = base_job_id + (uint32_t)worker_index + en2_tick * CLUSTER_MAX_SLAVES;

        // Generate unique extranonce2 for this slave from its unique job_id
        char en2_str[extranonce_2_len * 2 + 1];
        snprintf(en2_str, sizeof(en2_str), "%0*lx",
                 extranonce_2_len * 2, (unsigned long)job_id);

        // Construct coinbase and merkle root for this slave's unique extranonce2
        int coinbase_tx_len = strlen(notify->coinbase_1) + strlen(extranonce_str) +
                              strlen(en2_str) + strlen(notify->coinbase_2);
        char *coinbase_tx = (char *)malloc(coinbase_tx_len + 1);
        if (!coinbase_tx) {
            ESP_LOGE(TAG, "OOM building coinbase for slave %d", s_slaves[i].slave_id);
            worker_index++;
            continue;
        }
        snprintf(coinbase_tx, coinbase_tx_len + 1, "%s%s%s%s",
                 notify->coinbase_1, extranonce_str, en2_str, notify->coinbase_2);

        // Calculate merkle root
        char merkle_root_hex[65];
        calculate_merkle_root_hash(coinbase_tx, notify->_merkle_branches,
                                    notify->n_merkle_branches, merkle_root_hex);
        free(coinbase_tx);

        // Build cluster work message
        cluster_work_t work = {};
        work.target_slave_id = s_slaves[i].slave_id;
        work.job_id = job_id;

        // Convert merkle root hex to bytes
        for (int j = 0; j < 32; j++) {
            unsigned int val;
            sscanf(merkle_root_hex + j * 2, "%2x", &val);
            work.merkle_root[j] = (uint8_t)val;
        }

        memcpy(work.prev_block_hash, notify->_prev_block_hash, 32);
        work.version = notify->version;
        work.version_mask = version_mask;
        work.nbits = notify->target;
        work.ntime = notify->ntime;
        work.nonce_start = range * worker_index;
        work.nonce_end = (worker_index < total_workers - 1) ?
                          (range * (worker_index + 1) - 1) : 0xFFFFFFFF;

        // Store extranonce2 bytes
        work.extranonce2_len = (uint8_t)extranonce_2_len;
        for (int j = 0; j < extranonce_2_len && j < (int)sizeof(work.extranonce2); j++) {
            unsigned int val;
            sscanf(en2_str + j * 2, "%2x", &val);
            work.extranonce2[j] = (uint8_t)val;
        }

        work.clean_jobs = true; // always signal new work to slaves
        work.pool_diff = stratum_difficulty;
        work.pool_id = (uint8_t)pool;

        // Store job mapping for share submission
        store_job_mapping(job_id, notify->job_id ? notify->job_id : "", en2_str, pool);

        // Encode and broadcast work
        char msg[CLUSTER_MSG_MAX_LEN];
        int msg_len = cluster_protocol_encode_work(&work, msg, sizeof(msg));
        if (msg_len > 0) {
            cluster_espnow_broadcast((const uint8_t *)msg, msg_len);
            ESP_LOGD(TAG, "Distributed work to slave %d: job_id=%lu nonce=%08lX-%08lX",
                     s_slaves[i].slave_id, (unsigned long)job_id,
                     (unsigned long)work.nonce_start, (unsigned long)work.nonce_end);
        }

        worker_index++;
    }

    pthread_mutex_unlock(&s_slaves_mutex);
}

int cluster_master_get_active_count(void)
{
    int count = 0;
    pthread_mutex_lock(&s_slaves_mutex);
    for (int i = 0; i < CLUSTER_MAX_SLAVES; i++) {
        if (s_slaves[i].active) count++;
    }
    pthread_mutex_unlock(&s_slaves_mutex);
    return count;
}

const cluster_slave_info_t* cluster_master_get_slaves(void)
{
    return s_slaves;
}

double cluster_master_get_best_diff(void)
{
    return s_best_diff;
}

bool cluster_master_send_config(uint8_t slave_id, const cluster_config_cmd_t *cfg)
{
    uint8_t mac[6];
    bool found = false;

    pthread_mutex_lock(&s_slaves_mutex);
    for (int i = 0; i < CLUSTER_MAX_SLAVES; i++) {
        if (s_slaves[i].active && s_slaves[i].slave_id == slave_id) {
            memcpy(mac, s_slaves[i].mac, 6);
            found = true;
            break;
        }
    }
    pthread_mutex_unlock(&s_slaves_mutex);

    if (!found) {
        ESP_LOGW(TAG, "send_config: slave %d not found", slave_id);
        return false;
    }

    char msg[CLUSTER_MSG_MAX_LEN];
    int len = cluster_protocol_encode_config(cfg, msg, sizeof(msg));
    if (len <= 0) return false;

    cluster_espnow_send(mac, (const uint8_t *)msg, len);
    ESP_LOGI(TAG, "Sent config to slave %d: freq=%u mv=%u fan=%u mode=%u temp=%u",
             slave_id, cfg->frequency, cfg->core_voltage,
             cfg->fan_speed, cfg->fan_mode, cfg->target_temp);
    return true;
}

bool cluster_master_send_restart(uint8_t slave_id)
{
    uint8_t mac[6];
    bool found = false;

    pthread_mutex_lock(&s_slaves_mutex);
    for (int i = 0; i < CLUSTER_MAX_SLAVES; i++) {
        if (s_slaves[i].active && s_slaves[i].slave_id == slave_id) {
            memcpy(mac, s_slaves[i].mac, 6);
            found = true;
            break;
        }
    }
    pthread_mutex_unlock(&s_slaves_mutex);

    if (!found) {
        ESP_LOGW(TAG, "send_restart: slave %d not found", slave_id);
        return false;
    }

    cluster_restart_cmd_t cmd = { .slave_id = slave_id };
    char msg[CLUSTER_MSG_MAX_LEN];
    int len = cluster_protocol_encode_restart(&cmd, msg, sizeof(msg));
    if (len <= 0) return false;

    cluster_espnow_send(mac, (const uint8_t *)msg, len);
    ESP_LOGI(TAG, "Sent restart to slave %d", slave_id);
    return true;
}

void cluster_master_broadcast_timing(uint16_t interval_ms)
{
    char msg[CLUSTER_MSG_MAX_LEN];
    int len = cluster_protocol_encode_timing(interval_ms, msg, sizeof(msg));
    if (len <= 0) {
        ESP_LOGW(TAG, "Failed to encode timing message");
        return;
    }
    cluster_espnow_broadcast((const uint8_t *)msg, len);
    ESP_LOGI(TAG, "Broadcast timing interval %u ms to slaves", interval_ms);
}

float cluster_master_get_total_hashrate(void)
{
    float total = 0;

    // Add master hashrate
    total = SYSTEM_MODULE.getCurrentHashrate();

    // Add slave hashrates (stored as GH/s * 100)
    pthread_mutex_lock(&s_slaves_mutex);
    for (int i = 0; i < CLUSTER_MAX_SLAVES; i++) {
        if (s_slaves[i].active) {
            total += s_slaves[i].hashrate / 100.0f;
        }
    }
    pthread_mutex_unlock(&s_slaves_mutex);

    return total;
}
