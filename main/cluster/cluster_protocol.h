#pragma once

#include "cluster.h"
#include <stddef.h>

// NMEA-style message types
#define CLUSTER_MSG_WORK      "$CLWRK"   // Work distribution (master -> slave)
#define CLUSTER_MSG_SHARE     "$CLSHR"   // Share submission (slave -> master)
#define CLUSTER_MSG_HEARTBEAT "$CLHBT"   // Heartbeat (slave -> master)
#define CLUSTER_MSG_REGISTER  "$CLREG"   // Registration request (slave -> master)
#define CLUSTER_MSG_ACK       "$CLACK"   // Registration ack (master -> slave)
#define CLUSTER_MSG_CONFIG    "$CLCFG"   // Config command (master -> slave)
#define CLUSTER_MSG_RESTART   "$CLRST"   // Restart command (master -> slave)
#define CLUSTER_MSG_TIMING    "$CLTIM"   // Auto-timing interval sync (master -> slave)

// Encode functions — return number of bytes written to buf, or -1 on error
int cluster_protocol_encode_work(const cluster_work_t *work, char *buf, size_t buf_len);
int cluster_protocol_encode_share(const cluster_share_t *share, char *buf, size_t buf_len);
int cluster_protocol_encode_heartbeat(const cluster_heartbeat_data_t *hb, char *buf, size_t buf_len);
int cluster_protocol_encode_register(const cluster_register_t *reg, char *buf, size_t buf_len);
int cluster_protocol_encode_ack(const cluster_ack_t *ack, char *buf, size_t buf_len);

// Decode functions — return 0 on success, -1 on error (checksum mismatch or parse failure)
int cluster_protocol_decode_work(const char *buf, size_t len, cluster_work_t *work);
int cluster_protocol_decode_share(const char *buf, size_t len, cluster_share_t *share);
int cluster_protocol_decode_heartbeat(const char *buf, size_t len, cluster_heartbeat_data_t *hb);
int cluster_protocol_decode_register(const char *buf, size_t len, cluster_register_t *reg);
int cluster_protocol_decode_ack(const char *buf, size_t len, cluster_ack_t *ack);

// Config command encode/decode
int cluster_protocol_encode_config(const cluster_config_cmd_t *cfg, char *buf, size_t buf_len);
int cluster_protocol_decode_config(const char *buf, size_t len, cluster_config_cmd_t *cfg);

// Restart command encode/decode
int cluster_protocol_encode_restart(const cluster_restart_cmd_t *cmd, char *buf, size_t buf_len);
int cluster_protocol_decode_restart(const char *buf, size_t len, cluster_restart_cmd_t *cmd);

// Timing sync encode — interval_ms is the ASIC job interval to broadcast to slaves
int cluster_protocol_encode_timing(uint16_t interval_ms, char *buf, size_t buf_len);

// Identify message type from buffer — returns pointer to type string constant, or NULL
const char* cluster_protocol_identify(const char *buf, size_t len);

// Calculate XOR checksum between '$' and '*'
uint8_t cluster_protocol_checksum(const char *buf, size_t len);
