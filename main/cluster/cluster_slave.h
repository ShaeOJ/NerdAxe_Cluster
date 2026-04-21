#pragma once

#include "cluster.h"
#include "cluster_protocol.h"

// Initialize slave-mode state
void cluster_slave_init(void);

// Deinitialize slave-mode state
void cluster_slave_deinit(void);

// Called when a discovery beacon is received from a master
void cluster_slave_on_beacon(const uint8_t *master_mac);

// Called when a $CLWRK work message is received
void cluster_slave_on_work(const cluster_work_t *work);

// Called when a $CLACK acknowledgement is received
void cluster_slave_on_ack(const cluster_ack_t *ack);

// Try to register with discovered master — called periodically until registered
void cluster_slave_try_register(void);

// Send heartbeat to master — called periodically
void cluster_slave_send_heartbeat(void);

// Forward a share to the master instead of submitting directly to pool
// Called from asic_result_task when in slave mode
// job_id: the numeric cluster job_id from the work received
void cluster_slave_forward_share(uint32_t job_id, const char *extranonce2,
                                  uint32_t ntime, uint32_t nonce,
                                  uint32_t version, int pool_id,
                                  double difficulty);

// Handle a $CLCFG config command received from master — saves settings to NVS and restarts
void cluster_slave_on_config(const cluster_config_cmd_t *cfg);

// Handle a $CLRST restart command received from master
void cluster_slave_on_restart(void);

// Check if registered with a master
bool cluster_slave_is_reg(void);

// Get assigned slave ID
uint8_t cluster_slave_get_assigned_id(void);

// Get local shares forwarded to master
uint32_t cluster_slave_get_shares(void);

// Reconnect watchdog: if registered but no work received within CLUSTER_WORK_TIMEOUT_MS,
// clear registered state so the main loop re-registers with the master.
// Backup for when the master NACK is lost over ESP-NOW.
void cluster_slave_check_reconnect(void);
