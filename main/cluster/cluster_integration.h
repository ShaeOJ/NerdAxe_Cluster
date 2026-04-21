#pragma once

#include "cluster.h"

// Called from create_jobs_task after processing mining.notify in master mode
// to distribute work to all registered slaves.
// en2_tick: rolling counter incremented each call so every slave gets a unique
// extranonce2 and therefore hashes a different block header.
// Pass 0 on the initial mining.notify distribution; pass the master's
// extranonce_2 counter on periodic refreshes.
void cluster_integration_distribute_work(void *notify_ptr, int pool,
                                          const char *extranonce_str,
                                          int extranonce_2_len,
                                          uint32_t version_mask,
                                          uint32_t stratum_difficulty,
                                          uint32_t asic_diff,
                                          uint32_t en2_tick = 0);

// Called from asic_result_task in slave mode to forward a share to master
// instead of submitting directly to the pool.
// job_id_str: the numeric cluster job_id stored as the bm_job's jobid string
void cluster_integration_forward_share(const char *job_id_str,
                                        const char *extranonce2,
                                        uint32_t ntime, uint32_t nonce,
                                        uint32_t version, int pool_id,
                                        double difficulty);

// Check if cluster is in master mode (convenience)
bool cluster_integration_is_master(void);

// Check if cluster is in slave mode (convenience)
bool cluster_integration_is_slave(void);

// Get total cluster hashrate (master + all active slaves) in GH/s. Returns 0 when not master.
float cluster_integration_get_total_hashrate(void);

// Broadcast auto-timing interval to all slaves. No-op when not master.
void cluster_integration_broadcast_timing(uint16_t interval_ms);
