#include "cluster_slave.h"
#include "cluster_espnow.h"
#include "cluster_protocol.h"
#include "cluster.h"

#include <string.h>
#include <pthread.h>

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "mining.h"
#include "mining_utils.h"
#include "global_state.h"
#include "nvs_config.h"
#include "boards/board.h"
#include "esp_netif.h"

static const char *TAG = "cluster_slave";

// Slave state
static bool s_registered = false;
static uint8_t s_slave_id = 0;
static uint8_t s_master_mac[6] = {0};
static bool s_master_discovered = false;
static pthread_mutex_t s_state_mutex = PTHREAD_MUTEX_INITIALIZER;

// Work queue — cluster work items waiting to be processed
static QueueHandle_t s_work_queue = NULL;
#define WORK_QUEUE_SIZE 4

// Slave work processing task
static TaskHandle_t s_work_task_handle = NULL;

// Local share tracking for heartbeat
static uint32_t s_local_shares_accepted = 0;
static uint32_t s_local_shares_rejected = 0;

// Timestamp of last work received from master (ms). Seeded on ACK so the
// CLUSTER_WORK_TIMEOUT_MS countdown starts from registration, not from boot.
static uint64_t s_last_work_ms = 0;

// Process received work: convert cluster_work_t to bm_job and send to ASIC
static void slave_work_task(void *pvParameters)
{
    cluster_work_t work;
    Board *board = SYSTEM_MODULE.getBoard();
    uint32_t last_job_id = 0;
    bool has_last_job = false;

    while (1) {
        if (xQueueReceive(s_work_queue, &work, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        // Skip duplicate work — master broadcasts CLWRK repeatedly
        if (has_last_job && work.job_id == last_job_id && !work.clean_jobs) {
            continue;
        }
        last_job_id = work.job_id;
        has_last_job = true;

        if (!board || !board->getAsics()) {
            ESP_LOGW(TAG, "Board/ASICs not ready, dropping work");
            continue;
        }

        Asic *asics = board->getAsics();

        // Construct a bm_job from cluster_work_t
        bm_job *job = (bm_job *)malloc(sizeof(bm_job));
        if (!job) {
            ESP_LOGE(TAG, "OOM allocating bm_job");
            continue;
        }
        memset(job, 0, sizeof(bm_job));

        job->version = work.version;
        job->version_mask = work.version_mask;

        // prev_block_hash arrives in stratum byte order (4-byte words with swapped endianness).
        // swap_endian_words_bin converts to block-header order (what test_nonce_value expects).
        swap_endian_words_bin(work.prev_block_hash, job->prev_block_hash, 32);

        // merkle_root bytes are already in block-header order (direct hex2bin on master side)
        memcpy(job->merkle_root, work.merkle_root, 32);

        // Big-endian versions for ASIC sendWork():
        // prev_block_hash_be = raw stratum bytes, fully reversed
        memcpy(job->prev_block_hash_be, work.prev_block_hash, 32);
        reverse_bytes(job->prev_block_hash_be, 32);

        // merkle_root_be = word-swapped then fully reversed (matches construct_bm_job logic)
        swap_endian_words_bin(work.merkle_root, job->merkle_root_be, 32);
        reverse_bytes(job->merkle_root_be, 32);

        job->ntime = work.ntime;
        job->target = work.nbits;
        job->starting_nonce = work.nonce_start;
        job->pool_diff = work.pool_diff;
        // Clamp ASIC hardware difficulty to board's max (e.g. 256 for BM1366).
        // pool_diff (e.g. 13248) far exceeds ASIC hardware capability.
        // Software in asic_result_task checks nonce_diff >= pool_diff for share forwarding.
        uint32_t asic_max = board->getAsicMaxDifficulty();
        job->asic_diff = (work.pool_diff < asic_max) ? work.pool_diff : asic_max;
        job->pool_id = work.pool_id;

        // Store cluster job_id as the jobid (numeric, converted to string)
        char job_id_str[16];
        snprintf(job_id_str, sizeof(job_id_str), "%lu", (unsigned long)work.job_id);
        job->jobid = strdup(job_id_str);

        // Convert extranonce2 bytes to hex string
        char en2_hex[17];
        for (int i = 0; i < work.extranonce2_len; i++) {
            sprintf(en2_hex + i * 2, "%02x", work.extranonce2[i]);
        }
        en2_hex[work.extranonce2_len * 2] = '\0';
        job->extranonce2 = strdup(en2_hex);

        // Set ASIC difficulty and send work
        asics->setJobDifficultyMask(job->asic_diff);

        // Use the nonce_start as an incrementing extranonce for sendWork
        uint32_t extranonce_2 = work.nonce_start;
        int asic_job_id = asics->sendWork(extranonce_2, job);

        ESP_LOGI(TAG, "Sent cluster work to ASIC: job_id=%lu asic_id=%d nonce=%08lX-%08lX pool_diff=%lu asic_diff=%lu",
                 (unsigned long)work.job_id, asic_job_id,
                 (unsigned long)work.nonce_start, (unsigned long)work.nonce_end,
                 (unsigned long)work.pool_diff, (unsigned long)job->asic_diff);

        // Store in asicJobs so asic_result_task can look it up
        asicJobs.storeJob(job, asic_job_id);
    }
}

void cluster_slave_init(void)
{
    s_registered = false;
    s_slave_id = 0;
    s_master_discovered = false;
    s_local_shares_accepted = 0;
    s_local_shares_rejected = 0;
    s_last_work_ms = 0;
    memset(s_master_mac, 0, sizeof(s_master_mac));

    // Create work queue
    s_work_queue = xQueueCreate(WORK_QUEUE_SIZE, sizeof(cluster_work_t));
    if (!s_work_queue) {
        ESP_LOGE(TAG, "Failed to create work queue");
        return;
    }

    // Create work processing task
    xTaskCreate(slave_work_task, "cl_slave_work", 8192, NULL, 10, &s_work_task_handle);

    ESP_LOGI(TAG, "Slave initialized, scanning for master beacon");
}

void cluster_slave_deinit(void)
{
    if (s_work_task_handle) {
        vTaskDelete(s_work_task_handle);
        s_work_task_handle = NULL;
    }
    if (s_work_queue) {
        vQueueDelete(s_work_queue);
        s_work_queue = NULL;
    }
    s_registered = false;
    s_master_discovered = false;
}

void cluster_slave_on_beacon(const uint8_t *master_mac)
{
    pthread_mutex_lock(&s_state_mutex);

    if (!s_master_discovered) {
        memcpy(s_master_mac, master_mac, 6);
        s_master_discovered = true;
        ESP_LOGI(TAG, "Discovered master: " MACSTR, MAC2STR(master_mac));

        // Add master as ESP-NOW peer
        cluster_espnow_add_peer(master_mac);
    }

    pthread_mutex_unlock(&s_state_mutex);
}

void cluster_slave_on_work(const cluster_work_t *work)
{
    pthread_mutex_lock(&s_state_mutex);
    uint8_t my_id = s_slave_id;
    bool registered = s_registered;
    pthread_mutex_unlock(&s_state_mutex);

    if (!registered) {
        ESP_LOGD(TAG, "Received work but not registered, ignoring");
        return;
    }

    // Filter: only accept work targeted at us (or broadcast id 0xFF)
    // Check target_slave_id before updating the timestamp so we don't reset the timer
    // on work meant for other slaves (master broadcasts per-slave CLWRK).
    if (work->target_slave_id != my_id && work->target_slave_id != 0xFF) {
        return;
    }

    // Reset the work-timeout watchdog on any valid incoming work.
    pthread_mutex_lock(&s_state_mutex);
    s_last_work_ms = now_ms();
    pthread_mutex_unlock(&s_state_mutex);

    // Enqueue work for processing
    if (s_work_queue) {
        if (xQueueSendToBack(s_work_queue, work, 0) != pdTRUE) {
            ESP_LOGW(TAG, "Work queue full, dropping cluster work");
        }
    }
}

void cluster_slave_on_ack(const cluster_ack_t *ack)
{
    pthread_mutex_lock(&s_state_mutex);

    if (ack->accepted) {
        s_slave_id = ack->slave_id;
        s_registered = true;
        // Seed the work-timeout timer from registration so the slave gets
        // CLUSTER_WORK_TIMEOUT_MS to receive first work before self-deregistering.
        s_last_work_ms = now_ms();
        ESP_LOGI(TAG, "Registered with master, assigned slave_id=%d", s_slave_id);
    } else {
        ESP_LOGW(TAG, "Registration rejected by master (or master sent NACK — will re-register)");
        s_registered = false;
    }

    pthread_mutex_unlock(&s_state_mutex);
}

void cluster_slave_try_register(void)
{
    pthread_mutex_lock(&s_state_mutex);
    bool discovered = s_master_discovered;
    uint8_t master_mac[6];
    memcpy(master_mac, s_master_mac, 6);
    pthread_mutex_unlock(&s_state_mutex);

    if (!discovered) {
        return;
    }

    // Build registration message in ClusterAxe format: $REGISTER,hostname,ip_addr*XX
    cluster_register_t reg = {};

    // Get hostname from NVS
    char *hostname = Config::getHostname();
    if (hostname) {
        strncpy(reg.hostname, hostname, sizeof(reg.hostname) - 1);
        free(hostname);
    }

    // Get our IP address
    esp_netif_ip_info_t ip_info;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
        snprintf(reg.ip_addr, sizeof(reg.ip_addr), IPSTR, IP2STR(&ip_info.ip));
    } else {
        strncpy(reg.ip_addr, "0.0.0.0", sizeof(reg.ip_addr));
    }

    char msg[CLUSTER_MSG_MAX_LEN];
    int len = cluster_protocol_encode_register(&reg, msg, sizeof(msg));
    if (len > 0) {
        cluster_espnow_send(master_mac, (const uint8_t *)msg, len);
        ESP_LOGI(TAG, "Sent registration to master: %s ip=%s", reg.hostname, reg.ip_addr);
    }
}

void cluster_slave_send_heartbeat(void)
{
    pthread_mutex_lock(&s_state_mutex);
    bool registered = s_registered;
    uint8_t slave_id = s_slave_id;
    uint8_t master_mac[6];
    memcpy(master_mac, s_master_mac, 6);
    pthread_mutex_unlock(&s_state_mutex);

    if (!registered) return;

    Board *board = SYSTEM_MODULE.getBoard();
    if (!board) return;

    cluster_heartbeat_data_t hb = {};
    hb.slave_id = slave_id;
    // ClusterAxe: hashrate as GH/s * 100 (uint32)
    hb.hashrate = (uint32_t)(SYSTEM_MODULE.getCurrentHashrate() * 100.0f);
    hb.temp = POWER_MANAGEMENT_MODULE.getChipTempMax();
    hb.fan_rpm = POWER_MANAGEMENT_MODULE.getFanRPM(0);
    hb.shares = s_local_shares_accepted;
    hb.frequency = (uint16_t)board->getAsicFrequency();
    hb.core_voltage = (uint16_t)board->getAsicVoltageMillis();
    hb.power = POWER_MANAGEMENT_MODULE.getPower();
    hb.voltage_in = POWER_MANAGEMENT_MODULE.getVoltage() / 1000.0f;

    char msg[CLUSTER_MSG_MAX_LEN];
    int len = cluster_protocol_encode_heartbeat(&hb, msg, sizeof(msg));
    if (len > 0) {
        cluster_espnow_send(master_mac, (const uint8_t *)msg, len);
    }
}

void cluster_slave_forward_share(uint32_t job_id, const char *extranonce2,
                                  uint32_t ntime, uint32_t nonce,
                                  uint32_t version, int pool_id,
                                  double difficulty)
{
    pthread_mutex_lock(&s_state_mutex);
    bool registered = s_registered;
    uint8_t slave_id = s_slave_id;
    uint8_t master_mac[6];
    memcpy(master_mac, s_master_mac, 6);
    pthread_mutex_unlock(&s_state_mutex);

    if (!registered) {
        ESP_LOGW(TAG, "Not registered, cannot forward share");
        return;
    }

    cluster_share_t share = {};
    share.slave_id = slave_id;
    share.job_id = job_id;
    share.nonce = nonce;
    share.ntime = ntime;
    share.version = version;
    share.pool_id = (uint8_t)pool_id;
    share.difficulty = difficulty;

    // Parse extranonce2 hex to bytes
    if (extranonce2) {
        size_t en2_hex_len = strlen(extranonce2);
        share.extranonce2_len = (uint8_t)(en2_hex_len / 2);
        for (int i = 0; i < share.extranonce2_len && i < (int)sizeof(share.extranonce2); i++) {
            unsigned int val;
            sscanf(extranonce2 + i * 2, "%2x", &val);
            share.extranonce2[i] = (uint8_t)val;
        }
    }

    char msg[CLUSTER_MSG_MAX_LEN];
    int len = cluster_protocol_encode_share(&share, msg, sizeof(msg));
    if (len > 0) {
        // Send share 3 times unconditionally with delays between each send.
        // ESP-NOW fire-and-forget has no delivery confirmation, so redundant sends
        // compensate for over-the-air packet loss. Master deduplicates.
        for (int i = 0; i < 3; i++) {
            cluster_espnow_send(master_mac, (const uint8_t *)msg, len);
            if (i < 2) {
                vTaskDelay(pdMS_TO_TICKS(15));
            }
        }
        s_local_shares_accepted++;
        ESP_LOGI(TAG, "Forwarded share to master (3x): job=%lu nonce=%08lX ntime=%lu ver=%08lX en2_len=%u pool=%u diff=%.1f",
                 (unsigned long)job_id, (unsigned long)nonce, (unsigned long)ntime,
                 (unsigned long)version, share.extranonce2_len, share.pool_id, difficulty);
        ESP_LOGD(TAG, "Raw CLSHR: %.*s", len, msg);
    }
}

void cluster_slave_on_config(const cluster_config_cmd_t *cfg)
{
    pthread_mutex_lock(&s_state_mutex);
    uint8_t my_id = s_slave_id;
    pthread_mutex_unlock(&s_state_mutex);

    if (cfg->slave_id != my_id) {
        return; // not for us
    }

    ESP_LOGI(TAG, "Received config from master: freq=%u mv=%u fan=%u mode=%u temp=%u",
             cfg->frequency, cfg->core_voltage, cfg->fan_speed, cfg->fan_mode, cfg->target_temp);

    if (cfg->frequency > 0) {
        Config::setAsicFrequency(cfg->frequency);
    }
    if (cfg->core_voltage > 0) {
        Config::setAsicVoltage(cfg->core_voltage);
    }
    if (cfg->fan_speed != 0xFF) {
        Config::setFanSpeed(cfg->fan_speed);
    }
    if (cfg->fan_mode != 0xFF) {
        Config::setTempControlMode(cfg->fan_mode);
    }
    if (cfg->target_temp > 0) {
        Config::setPidTargetTemp(cfg->target_temp);
    }

    ESP_LOGI(TAG, "Config applied — changes will be picked up by PowerManagementTask within 2s");
}

void cluster_slave_on_restart(void)
{
    ESP_LOGI(TAG, "Restart command received from master");
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
}

bool cluster_slave_is_reg(void)
{
    pthread_mutex_lock(&s_state_mutex);
    bool reg = s_registered;
    pthread_mutex_unlock(&s_state_mutex);
    return reg;
}

uint8_t cluster_slave_get_assigned_id(void)
{
    pthread_mutex_lock(&s_state_mutex);
    uint8_t id = s_slave_id;
    pthread_mutex_unlock(&s_state_mutex);
    return id;
}

uint32_t cluster_slave_get_shares(void)
{
    return s_local_shares_accepted;
}

void cluster_slave_check_reconnect(void)
{
    pthread_mutex_lock(&s_state_mutex);
    bool registered = s_registered;
    uint64_t last_work = s_last_work_ms;
    pthread_mutex_unlock(&s_state_mutex);

    // Only applies when we believe we are registered.
    if (!registered) return;
    // last_work == 0 means ACK hasn't been processed yet; nothing to check.
    if (last_work == 0) return;

    uint64_t now = now_ms();
    if ((now - last_work) > CLUSTER_WORK_TIMEOUT_MS) {
        // We're registered but haven't received work in CLUSTER_WORK_TIMEOUT_MS ms.
        // The master probably timed us out and evicted our slot. The NACK from the
        // master (sent when it sees our heartbeat) is the fast path; this is the
        // fallback for when the NACK itself is lost over ESP-NOW.
        ESP_LOGW(TAG, "No cluster work received for %llu ms — assuming master dropped us, re-registering",
                 (unsigned long long)(now - last_work));
        pthread_mutex_lock(&s_state_mutex);
        s_registered = false;
        s_last_work_ms = 0;
        pthread_mutex_unlock(&s_state_mutex);
    }
}
