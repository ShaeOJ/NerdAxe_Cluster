#include "cluster_integration.h"
#include "cluster.h"
#include "cluster_master.h"
#include "cluster_slave.h"

#include <stdlib.h>
#include "esp_log.h"

static const char *TAG = "cluster_integ";

void cluster_integration_distribute_work(void *notify_ptr, int pool,
                                          const char *extranonce_str,
                                          int extranonce_2_len,
                                          uint32_t version_mask,
                                          uint32_t stratum_difficulty,
                                          uint32_t asic_diff,
                                          uint32_t en2_tick)
{
    if (cluster_get_mode() != CLUSTER_MODE_MASTER) {
        return;
    }

    if (cluster_get_active_slave_count() == 0) {
        return;
    }

    ESP_LOGD(TAG, "Distributing work to %d slaves (pool %d, tick %lu)",
             cluster_get_active_slave_count(), pool, (unsigned long)en2_tick);

    cluster_master_distribute_work(notify_ptr, pool,
                                    extranonce_str, extranonce_2_len,
                                    version_mask, stratum_difficulty, asic_diff,
                                    en2_tick);
}

void cluster_integration_forward_share(const char *job_id_str,
                                        const char *extranonce2,
                                        uint32_t ntime, uint32_t nonce,
                                        uint32_t version, int pool_id,
                                        double difficulty)
{
    if (cluster_get_mode() != CLUSTER_MODE_SLAVE) {
        return;
    }

    // Convert the job_id string back to uint32 (it was stored as decimal string by slave_work_task)
    uint32_t job_id = (uint32_t)strtoul(job_id_str ? job_id_str : "0", NULL, 10);
    cluster_slave_forward_share(job_id, extranonce2,
                                 ntime, nonce, version, pool_id, difficulty);
}

bool cluster_integration_is_master(void)
{
    return cluster_get_mode() == CLUSTER_MODE_MASTER;
}

bool cluster_integration_is_slave(void)
{
    return cluster_get_mode() == CLUSTER_MODE_SLAVE;
}

float cluster_integration_get_total_hashrate(void)
{
    if (cluster_get_mode() != CLUSTER_MODE_MASTER) return 0.0f;
    return cluster_master_get_total_hashrate();
}

void cluster_integration_broadcast_timing(uint16_t interval_ms)
{
    if (cluster_get_mode() != CLUSTER_MODE_MASTER) return;
    cluster_master_broadcast_timing(interval_ms);
}
