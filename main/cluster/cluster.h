#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// Maximum number of slave devices in a cluster
#define CLUSTER_MAX_SLAVES          8
// Discovery beacon broadcast interval (ms)
#define CLUSTER_BEACON_INTERVAL_MS  1000
// Heartbeat send interval from slave (ms)
#define CLUSTER_HEARTBEAT_INTERVAL_MS 2000
// Slave timeout threshold (ms) — mark slave stale after no heartbeat
// With 2 s heartbeat interval, requires ~7 consecutive misses to drop.
#define CLUSTER_SLAVE_TIMEOUT_MS    15000
// Slave self-heal: re-register if registered but no work received within this window (ms).
// Slightly longer than CLUSTER_SLAVE_TIMEOUT_MS so the master NACK fires first.
#define CLUSTER_WORK_TIMEOUT_MS     20000
// How often the master re-broadcasts the current work to recover slaves that missed
// initial delivery (ESP-NOW is fire-and-forget). Unit: beacon cycles (each = CLUSTER_BEACON_INTERVAL_MS).
// 5 cycles × 1000 ms = every 5 seconds.
#define CLUSTER_WORK_RESEND_BEACONS 5
// Maximum NMEA message length
#define CLUSTER_MSG_MAX_LEN         250
// ESP-NOW max payload
#define CLUSTER_ESPNOW_MAX_LEN     250
// Discovery beacon magic
#define CLUSTER_BEACON_MAGIC        "CLAXE"
#define CLUSTER_BEACON_MAGIC_LEN    5
// Share queue depth
#define CLUSTER_SHARE_QUEUE_DEPTH   16
// Job mapping buffer size (only used when NerdAxe runs as master)
#define CLUSTER_JOB_MAP_SIZE        256

// Cluster operating mode
enum cluster_mode_t {
    CLUSTER_MODE_DISABLED = 0,
    CLUSTER_MODE_MASTER   = 1,
    CLUSTER_MODE_SLAVE    = 2,
};

// Work packet distributed from master to slave (maps to $CLWRK)
struct cluster_work_t {
    uint8_t  target_slave_id;
    uint32_t job_id;
    uint8_t  prev_block_hash[32];
    uint8_t  merkle_root[32];
    uint32_t version;
    uint32_t version_mask;
    uint32_t nbits;
    uint32_t ntime;
    uint32_t nonce_start;
    uint32_t nonce_end;
    uint8_t  extranonce2[8];
    uint8_t  extranonce2_len;
    bool     clean_jobs;
    uint32_t pool_diff;
    uint8_t  pool_id;
};

// Share result from slave to master (maps to $CLSHR)
struct cluster_share_t {
    uint8_t  slave_id;
    uint32_t job_id;
    uint32_t nonce;
    uint32_t ntime;
    uint32_t version;
    uint8_t  extranonce2[8];
    uint8_t  extranonce2_len;
    uint8_t  pool_id;
    double   difficulty;
};

// Heartbeat data from slave to master (maps to $CLHBT)
struct cluster_heartbeat_data_t {
    uint8_t  slave_id;
    uint32_t hashrate;       // GH/s * 100
    float    temp;
    uint16_t fan_rpm;
    uint32_t shares;         // total shares submitted
    uint16_t frequency;
    uint16_t core_voltage;   // mV
    float    power;
    float    voltage_in;     // input voltage in V
};

// Registration request from slave to master (maps to $REGISTER)
struct cluster_register_t {
    char     hostname[32];
    char     ip_addr[16];
};

// Acknowledgement from master to slave (maps to $CLACK)
struct cluster_ack_t {
    uint8_t  slave_id;
    bool     accepted;
    char     hostname[32];  // ClusterAxe: echoed hostname (accepted) or "FULL" (rejected)
};

// Config command (master -> slave) — sets ASIC/fan parameters on the slave ($CLCFG)
// Fields set to 0 (or 0xFF for uint8 fields) are treated as "no change"
struct cluster_config_cmd_t {
    uint8_t  slave_id;
    uint16_t frequency;      // MHz; 0 = no change
    uint16_t core_voltage;   // mV; 0 = no change
    uint8_t  fan_speed;      // 0-100; 0xFF = no change
    uint8_t  fan_mode;       // 0=auto, 1=manual; 0xFF = no change
    uint8_t  target_temp;    // °C; 0 = no change
};

// Restart command (master -> slave) ($CLRST)
struct cluster_restart_cmd_t {
    uint8_t slave_id;
};

// Per-slave state tracked by master
struct cluster_slave_info_t {
    bool     active;
    uint8_t  slave_id;
    uint8_t  mac[6];
    char     hostname[32];
    char     ip_addr[16];
    uint32_t hashrate;       // GH/s * 100
    float    temp;
    uint16_t fan_rpm;
    uint32_t shares_accepted;
    uint32_t shares_rejected;
    uint32_t shares_submitted; // from heartbeat
    uint16_t frequency;
    uint16_t core_voltage;   // mV
    float    power;
    float    voltage_in;     // input voltage in V
    uint64_t last_heartbeat_ms;
    uint64_t registered_at_ms;
};

// Initialize cluster module in the given mode on the given WiFi channel
void cluster_init(cluster_mode_t mode, uint8_t wifi_channel);

// Deinitialize cluster module
void cluster_deinit(void);

// Get current cluster mode
cluster_mode_t cluster_get_mode(void);

// Get number of active slaves (master mode only)
int cluster_get_active_slave_count(void);

// Get slave info array (master mode only), returns pointer to internal array
const cluster_slave_info_t* cluster_get_slave_info(void);

// Get total cluster hashrate including master (master mode only)
float cluster_get_total_hashrate(void);

// Check if this slave is registered with a master (slave mode only)
bool cluster_slave_is_registered(void);

// Get assigned slave ID (slave mode only)
uint8_t cluster_slave_get_id(void);

// Get shares forwarded to master (slave mode only)
uint32_t cluster_slave_get_shares_submitted(void);

// Get best difficulty seen across all slave shares (master mode only)
double cluster_get_best_diff(void);

// Send a config command to a slave by slave_id (master mode only)
// Returns true if the slave was found and the message was sent
bool cluster_send_slave_config(uint8_t slave_id, const cluster_config_cmd_t *cfg);

// Send a restart command to a slave by slave_id (master mode only)
// Returns true if the slave was found and the message was sent
bool cluster_send_slave_restart(uint8_t slave_id);
