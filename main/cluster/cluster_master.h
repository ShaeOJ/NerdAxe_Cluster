#pragma once

#include "cluster.h"
#include "cluster_protocol.h"

// Initialize master-mode state
void cluster_master_init(void);

// Deinitialize master-mode state
void cluster_master_deinit(void);

// Called when a $REGISTER registration message is received from a slave
// src_mac: the ESP-NOW source MAC address of the slave
void cluster_master_on_register(const cluster_register_t *reg, const uint8_t *src_mac);

// Called when a $CLSHR share message is received from a slave
void cluster_master_on_share(const cluster_share_t *share);

// Called when a $CLHBT heartbeat message is received from a slave.
// src_mac: ESP-NOW source MAC — used to send a NACK if the slave is no longer in the registry.
void cluster_master_on_heartbeat(const cluster_heartbeat_data_t *hb, const uint8_t *src_mac);

// Check for stale slaves (no heartbeat within timeout) — called periodically
void cluster_master_timeout_check(void);

// Distribute work to all active slaves when a new mining.notify arrives.
// Called from create_jobs_task after master processes its own work.
// notify: the parsed mining_notify from stratum
// pool: pool index (0 or 1)
// extranonce_str: extranonce1 from pool subscription
// extranonce_2_len: length of extranonce2 field
// version_mask: version mask from pool
// stratum_difficulty: current pool difficulty
// asic_diff: selected ASIC difficulty
void cluster_master_distribute_work(void *notify_ptr, int pool,
                                     const char *extranonce_str,
                                     int extranonce_2_len,
                                     uint32_t version_mask,
                                     uint32_t stratum_difficulty,
                                     uint32_t asic_diff,
                                     uint32_t en2_tick);

// Get number of currently active (non-stale) slaves
int cluster_master_get_active_count(void);

// Get pointer to slave info array
const cluster_slave_info_t* cluster_master_get_slaves(void);

// Get total hashrate of all slaves + master
float cluster_master_get_total_hashrate(void);

// Get best difficulty seen across all slave shares this session
double cluster_master_get_best_diff(void);

// Send a config command to a specific slave by slave_id
// Returns true if the slave was found and the message was sent
bool cluster_master_send_config(uint8_t slave_id, const cluster_config_cmd_t *cfg);

// Send a restart command to a specific slave by slave_id
// Returns true if the slave was found and the message was sent
bool cluster_master_send_restart(uint8_t slave_id);

// Broadcast the current auto-timing interval to all active slaves via $CLTIM
void cluster_master_broadcast_timing(uint16_t interval_ms);
