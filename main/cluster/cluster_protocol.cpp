#include "cluster_protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "esp_log.h"

static const char *TAG = "cluster_proto";

// Helper: convert byte array to hex string
static void bytes_to_hex(const uint8_t *bytes, size_t len, char *hex_out)
{
    for (size_t i = 0; i < len; i++) {
        sprintf(hex_out + i * 2, "%02x", bytes[i]);
    }
    hex_out[len * 2] = '\0';
}

// Helper: convert hex string to byte array, returns number of bytes written
static int hex_to_bytes(const char *hex, uint8_t *bytes, size_t max_bytes)
{
    size_t hex_len = strlen(hex);
    if (hex_len % 2 != 0) return -1;
    size_t n = hex_len / 2;
    if (n > max_bytes) return -1;
    for (size_t i = 0; i < n; i++) {
        unsigned int val;
        if (sscanf(hex + i * 2, "%2x", &val) != 1) return -1;
        bytes[i] = (uint8_t)val;
    }
    return (int)n;
}

// Calculate XOR checksum of characters between '$' and '*' (exclusive)
uint8_t cluster_protocol_checksum(const char *buf, size_t len)
{
    uint8_t cksum = 0;
    bool started = false;
    for (size_t i = 0; i < len; i++) {
        if (buf[i] == '$') {
            started = true;
            continue;
        }
        if (buf[i] == '*') {
            break;
        }
        if (started) {
            cksum ^= (uint8_t)buf[i];
        }
    }
    return cksum;
}

// Verify checksum at end of message: *XX
static bool verify_checksum(const char *buf, size_t len)
{
    // Find the '*' character
    const char *star = NULL;
    for (size_t i = 0; i < len; i++) {
        if (buf[i] == '*') {
            star = buf + i;
            break;
        }
    }
    if (!star || (size_t)(star - buf + 3) > len) {
        return false;
    }

    uint8_t expected = cluster_protocol_checksum(buf, len);
    unsigned int provided;
    if (sscanf(star + 1, "%2X", &provided) != 1) {
        return false;
    }
    return expected == (uint8_t)provided;
}

// Append checksum and newline to message
static int append_checksum(char *buf, int pos, size_t buf_len)
{
    uint8_t cksum = cluster_protocol_checksum(buf, pos);
    int written = snprintf(buf + pos, buf_len - pos, "*%02X\r\n", cksum);
    if (written < 0 || (size_t)(pos + written) >= buf_len) return -1;
    return pos + written;
}

// Helper to find next comma-separated token
// Updates *start to point past the comma. Returns pointer to start of token, null-terminates it.
static char* next_token(char *str, char **saveptr)
{
    if (!str && !*saveptr) return NULL;
    char *token_start = str ? str : *saveptr;
    if (!*token_start) return NULL;

    char *comma = strchr(token_start, ',');
    if (comma) {
        *comma = '\0';
        *saveptr = comma + 1;
    } else {
        // Check for * (end of NMEA data before checksum)
        char *star = strchr(token_start, '*');
        if (star) {
            *star = '\0';
            *saveptr = star + 1;
        } else {
            *saveptr = token_start + strlen(token_start);
        }
    }
    return token_start;
}

//
// ENCODE FUNCTIONS
//

int cluster_protocol_encode_work(const cluster_work_t *work, char *buf, size_t buf_len)
{
    char prev_hash_hex[65];
    char merkle_hex[65];
    char en2_hex[17];

    bytes_to_hex(work->prev_block_hash, 32, prev_hash_hex);
    bytes_to_hex(work->merkle_root, 32, merkle_hex);
    bytes_to_hex(work->extranonce2, work->extranonce2_len, en2_hex);

    // ClusterAxe 15-field format (no asic_diff, no stratum_job_id)
    int pos = snprintf(buf, buf_len,
        "$CLWRK,%u,%lu,%s,%s,%lu,%lu,%lu,%lu,%lu,%lu,%s,%u,%u,%lu,%u",
        work->target_slave_id,
        (unsigned long)work->job_id,
        prev_hash_hex,
        merkle_hex,
        (unsigned long)work->version,
        (unsigned long)work->version_mask,
        (unsigned long)work->nbits,
        (unsigned long)work->ntime,
        (unsigned long)work->nonce_start,
        (unsigned long)work->nonce_end,
        en2_hex,
        work->extranonce2_len,
        work->clean_jobs ? 1 : 0,
        (unsigned long)work->pool_diff,
        work->pool_id);

    if (pos < 0 || (size_t)pos >= buf_len) return -1;
    return append_checksum(buf, pos, buf_len);
}

int cluster_protocol_encode_share(const cluster_share_t *share, char *buf, size_t buf_len)
{
    char en2_hex[17];
    bytes_to_hex(share->extranonce2, share->extranonce2_len, en2_hex);

    // ClusterAxe field order: slave_id,job_id,nonce,ntime,version,en2,en2_len,pool_id,difficulty
    int pos = snprintf(buf, buf_len,
        "$CLSHR,%u,%lu,%lu,%lu,%lu,%s,%u,%u,%.1f",
        share->slave_id,
        (unsigned long)share->job_id,
        (unsigned long)share->nonce,
        (unsigned long)share->ntime,
        (unsigned long)share->version,
        en2_hex,
        share->extranonce2_len,
        share->pool_id,
        share->difficulty);

    if (pos < 0 || (size_t)pos >= buf_len) return -1;
    return append_checksum(buf, pos, buf_len);
}

int cluster_protocol_encode_heartbeat(const cluster_heartbeat_data_t *hb, char *buf, size_t buf_len)
{
    // ClusterAxe format: slave_id,hashrate_x100,temp,fan_rpm,shares,freq,core_voltage,power,voltage_in
    int pos = snprintf(buf, buf_len,
        "$CLHBT,%u,%lu,%.1f,%u,%lu,%u,%u,%.2f,%.2f",
        hb->slave_id,
        (unsigned long)hb->hashrate,
        hb->temp,
        hb->fan_rpm,
        (unsigned long)hb->shares,
        hb->frequency,
        hb->core_voltage,
        hb->power,
        hb->voltage_in);

    if (pos < 0 || (size_t)pos >= buf_len) return -1;
    return append_checksum(buf, pos, buf_len);
}

int cluster_protocol_encode_register(const cluster_register_t *reg, char *buf, size_t buf_len)
{
    // ClusterAxe ESP-NOW format: $REGISTER,hostname,ip_addr*XX
    int pos = snprintf(buf, buf_len,
        "$REGISTER,%s,%s",
        reg->hostname,
        reg->ip_addr);

    if (pos < 0 || (size_t)pos >= buf_len) return -1;
    return append_checksum(buf, pos, buf_len);
}

int cluster_protocol_encode_ack(const cluster_ack_t *ack, char *buf, size_t buf_len)
{
    // ClusterAxe format: $CLACK,slave_id,hostname (accepted) or $CLACK,0,FULL (rejected)
    int pos = snprintf(buf, buf_len,
        "$CLACK,%u,%s",
        ack->slave_id,
        ack->accepted ? ack->hostname : "FULL");

    if (pos < 0 || (size_t)pos >= buf_len) return -1;
    return append_checksum(buf, pos, buf_len);
}

//
// DECODE FUNCTIONS
//

int cluster_protocol_decode_work(const char *buf, size_t len, cluster_work_t *work)
{
    if (!verify_checksum(buf, len)) {
        ESP_LOGW(TAG, "CLWRK checksum mismatch");
        return -1;
    }

    // Make mutable copy for tokenizing
    char tmp[CLUSTER_MSG_MAX_LEN];
    size_t copy_len = len < sizeof(tmp) - 1 ? len : sizeof(tmp) - 1;
    memcpy(tmp, buf, copy_len);
    tmp[copy_len] = '\0';

    char *saveptr = NULL;
    char *token;

    // Skip message type "$CLWRK"
    token = next_token(tmp, &saveptr);
    if (!token) return -1;

    // target_slave_id
    token = next_token(NULL, &saveptr);
    if (!token) return -1;
    work->target_slave_id = (uint8_t)atoi(token);

    // job_id
    token = next_token(NULL, &saveptr);
    if (!token) return -1;
    work->job_id = (uint32_t)strtoul(token, NULL, 10);

    // prev_block_hash (hex)
    token = next_token(NULL, &saveptr);
    if (!token) return -1;
    hex_to_bytes(token, work->prev_block_hash, 32);

    // merkle_root (hex)
    token = next_token(NULL, &saveptr);
    if (!token) return -1;
    hex_to_bytes(token, work->merkle_root, 32);

    // version
    token = next_token(NULL, &saveptr);
    if (!token) return -1;
    work->version = (uint32_t)strtoul(token, NULL, 10);

    // version_mask
    token = next_token(NULL, &saveptr);
    if (!token) return -1;
    work->version_mask = (uint32_t)strtoul(token, NULL, 10);

    // nbits
    token = next_token(NULL, &saveptr);
    if (!token) return -1;
    work->nbits = (uint32_t)strtoul(token, NULL, 10);

    // ntime
    token = next_token(NULL, &saveptr);
    if (!token) return -1;
    work->ntime = (uint32_t)strtoul(token, NULL, 10);

    // nonce_start
    token = next_token(NULL, &saveptr);
    if (!token) return -1;
    work->nonce_start = (uint32_t)strtoul(token, NULL, 10);

    // nonce_end
    token = next_token(NULL, &saveptr);
    if (!token) return -1;
    work->nonce_end = (uint32_t)strtoul(token, NULL, 10);

    // extranonce2 (hex)
    token = next_token(NULL, &saveptr);
    if (!token) return -1;
    int en2_bytes = hex_to_bytes(token, work->extranonce2, sizeof(work->extranonce2));

    // extranonce2_len
    token = next_token(NULL, &saveptr);
    if (!token) return -1;
    work->extranonce2_len = (uint8_t)atoi(token);
    if (en2_bytes >= 0 && (uint8_t)en2_bytes != work->extranonce2_len) {
        ESP_LOGW(TAG, "CLWRK en2 length mismatch: %d vs %u", en2_bytes, work->extranonce2_len);
    }

    // clean_jobs
    token = next_token(NULL, &saveptr);
    if (!token) return -1;
    work->clean_jobs = atoi(token) != 0;

    // pool_diff (optional, default 0)
    token = next_token(NULL, &saveptr);
    if (token && *token) {
        work->pool_diff = (uint32_t)strtoul(token, NULL, 10);
    } else {
        work->pool_diff = 0;
    }

    // pool_id (optional, default 0)
    token = next_token(NULL, &saveptr);
    if (token && *token) {
        work->pool_id = (uint8_t)atoi(token);
    } else {
        work->pool_id = 0;
    }

    return 0;
}

int cluster_protocol_decode_share(const char *buf, size_t len, cluster_share_t *share)
{
    if (!verify_checksum(buf, len)) {
        ESP_LOGW(TAG, "CLSHR checksum mismatch");
        return -1;
    }

    char tmp[CLUSTER_MSG_MAX_LEN];
    size_t copy_len = len < sizeof(tmp) - 1 ? len : sizeof(tmp) - 1;
    memcpy(tmp, buf, copy_len);
    tmp[copy_len] = '\0';

    char *saveptr = NULL;
    char *token;

    // Skip "$CLSHR"
    token = next_token(tmp, &saveptr);
    if (!token) return -1;

    // ClusterAxe field order: slave_id,job_id,nonce,ntime,version,en2,en2_len,pool_id,difficulty

    // slave_id
    token = next_token(NULL, &saveptr);
    if (!token) return -1;
    share->slave_id = (uint8_t)atoi(token);

    // job_id
    token = next_token(NULL, &saveptr);
    if (!token) return -1;
    share->job_id = (uint32_t)strtoul(token, NULL, 10);

    // nonce
    token = next_token(NULL, &saveptr);
    if (!token) return -1;
    share->nonce = (uint32_t)strtoul(token, NULL, 10);

    // ntime
    token = next_token(NULL, &saveptr);
    if (!token) return -1;
    share->ntime = (uint32_t)strtoul(token, NULL, 10);

    // version
    token = next_token(NULL, &saveptr);
    if (!token) return -1;
    share->version = (uint32_t)strtoul(token, NULL, 10);

    // extranonce2 (hex)
    token = next_token(NULL, &saveptr);
    if (!token) return -1;
    hex_to_bytes(token, share->extranonce2, sizeof(share->extranonce2));

    // extranonce2_len
    token = next_token(NULL, &saveptr);
    if (!token) return -1;
    share->extranonce2_len = (uint8_t)atoi(token);

    // pool_id (optional, default 0)
    token = next_token(NULL, &saveptr);
    if (token && *token) {
        share->pool_id = (uint8_t)atoi(token);
    } else {
        share->pool_id = 0;
    }

    // difficulty (optional, default 0)
    token = next_token(NULL, &saveptr);
    if (token && *token) {
        share->difficulty = strtod(token, NULL);
    } else {
        share->difficulty = 0;
    }

    return 0;
}

int cluster_protocol_decode_heartbeat(const char *buf, size_t len, cluster_heartbeat_data_t *hb)
{
    if (!verify_checksum(buf, len)) {
        ESP_LOGW(TAG, "CLHBT checksum mismatch");
        return -1;
    }

    char tmp[CLUSTER_MSG_MAX_LEN];
    size_t copy_len = len < sizeof(tmp) - 1 ? len : sizeof(tmp) - 1;
    memcpy(tmp, buf, copy_len);
    tmp[copy_len] = '\0';

    char *saveptr = NULL;
    char *token;

    // Skip "$CLHBT"
    token = next_token(tmp, &saveptr);
    if (!token) return -1;

    // slave_id
    token = next_token(NULL, &saveptr);
    if (!token) return -1;
    hb->slave_id = (uint8_t)atoi(token);

    // hashrate (GH/s * 100 as uint32)
    token = next_token(NULL, &saveptr);
    if (!token) return -1;
    hb->hashrate = (uint32_t)strtoul(token, NULL, 10);

    // temp
    token = next_token(NULL, &saveptr);
    if (!token) return -1;
    hb->temp = strtof(token, NULL);

    // fan_rpm
    token = next_token(NULL, &saveptr);
    if (!token) return -1;
    hb->fan_rpm = (uint16_t)atoi(token);

    // shares (single count)
    token = next_token(NULL, &saveptr);
    if (!token) return -1;
    hb->shares = (uint32_t)strtoul(token, NULL, 10);

    // Extended fields are optional for backwards compat with legacy 5-field heartbeats

    // frequency
    token = next_token(NULL, &saveptr);
    if (token && *token) {
        hb->frequency = (uint16_t)atoi(token);
    } else {
        hb->frequency = 0;
        return 0;
    }

    // core_voltage (mV)
    token = next_token(NULL, &saveptr);
    if (token && *token) {
        hb->core_voltage = (uint16_t)atoi(token);
    } else {
        hb->core_voltage = 0;
        return 0;
    }

    // power
    token = next_token(NULL, &saveptr);
    if (token && *token) {
        hb->power = strtof(token, NULL);
    } else {
        hb->power = 0;
        return 0;
    }

    // voltage_in
    token = next_token(NULL, &saveptr);
    if (token && *token) {
        hb->voltage_in = strtof(token, NULL);
    } else {
        hb->voltage_in = 0;
    }

    return 0;
}

int cluster_protocol_decode_register(const char *buf, size_t len, cluster_register_t *reg)
{
    if (!verify_checksum(buf, len)) {
        ESP_LOGW(TAG, "REGISTER checksum mismatch");
        return -1;
    }

    char tmp[CLUSTER_MSG_MAX_LEN];
    size_t copy_len = len < sizeof(tmp) - 1 ? len : sizeof(tmp) - 1;
    memcpy(tmp, buf, copy_len);
    tmp[copy_len] = '\0';

    char *saveptr = NULL;
    char *token;

    // Skip "$REGISTER" or "$CLREG"
    token = next_token(tmp, &saveptr);
    if (!token) return -1;

    // hostname
    token = next_token(NULL, &saveptr);
    if (!token) return -1;
    strncpy(reg->hostname, token, sizeof(reg->hostname) - 1);
    reg->hostname[sizeof(reg->hostname) - 1] = '\0';

    // ip_addr (optional)
    token = next_token(NULL, &saveptr);
    if (token && *token) {
        strncpy(reg->ip_addr, token, sizeof(reg->ip_addr) - 1);
        reg->ip_addr[sizeof(reg->ip_addr) - 1] = '\0';
    } else {
        strncpy(reg->ip_addr, "0.0.0.0", sizeof(reg->ip_addr));
    }

    return 0;
}

int cluster_protocol_decode_ack(const char *buf, size_t len, cluster_ack_t *ack)
{
    if (!verify_checksum(buf, len)) {
        ESP_LOGW(TAG, "CLACK checksum mismatch");
        return -1;
    }

    char tmp[CLUSTER_MSG_MAX_LEN];
    size_t copy_len = len < sizeof(tmp) - 1 ? len : sizeof(tmp) - 1;
    memcpy(tmp, buf, copy_len);
    tmp[copy_len] = '\0';

    char *saveptr = NULL;
    char *token;

    // Skip "$CLACK"
    token = next_token(tmp, &saveptr);
    if (!token) return -1;

    // slave_id
    token = next_token(NULL, &saveptr);
    if (!token) return -1;
    ack->slave_id = (uint8_t)atoi(token);

    // accepted — ClusterAxe sends hostname (accepted) or "FULL" (rejected)
    // Also handle legacy "OK"/1 and "FULL"/0 formats
    token = next_token(NULL, &saveptr);
    if (!token) return -1;
    ack->accepted = (strcmp(token, "FULL") != 0 && strcmp(token, "0") != 0);

    return 0;
}

int cluster_protocol_encode_config(const cluster_config_cmd_t *cfg, char *buf, size_t buf_len)
{
    int pos = snprintf(buf, buf_len,
        "$CLCFG,%u,%u,%u,%u,%u,%u",
        cfg->slave_id,
        cfg->frequency,
        cfg->core_voltage,
        cfg->fan_speed,
        cfg->fan_mode,
        cfg->target_temp);
    if (pos < 0 || (size_t)pos >= buf_len) return -1;
    return append_checksum(buf, pos, buf_len);
}

int cluster_protocol_decode_config(const char *buf, size_t len, cluster_config_cmd_t *cfg)
{
    if (!verify_checksum(buf, len)) {
        ESP_LOGW(TAG, "CLCFG checksum mismatch");
        return -1;
    }

    char tmp[CLUSTER_MSG_MAX_LEN];
    size_t copy_len = len < sizeof(tmp) - 1 ? len : sizeof(tmp) - 1;
    memcpy(tmp, buf, copy_len);
    tmp[copy_len] = '\0';

    char *saveptr = NULL;
    char *token;

    // Skip "$CLCFG"
    token = next_token(tmp, &saveptr);
    if (!token) return -1;

    token = next_token(NULL, &saveptr);
    if (!token) return -1;
    cfg->slave_id = (uint8_t)atoi(token);

    token = next_token(NULL, &saveptr);
    if (!token) return -1;
    cfg->frequency = (uint16_t)atoi(token);

    token = next_token(NULL, &saveptr);
    if (!token) return -1;
    cfg->core_voltage = (uint16_t)atoi(token);

    token = next_token(NULL, &saveptr);
    if (!token) return -1;
    cfg->fan_speed = (uint8_t)atoi(token);

    token = next_token(NULL, &saveptr);
    if (!token) return -1;
    cfg->fan_mode = (uint8_t)atoi(token);

    token = next_token(NULL, &saveptr);
    if (!token) return -1;
    cfg->target_temp = (uint8_t)atoi(token);

    return 0;
}

int cluster_protocol_encode_restart(const cluster_restart_cmd_t *cmd, char *buf, size_t buf_len)
{
    int pos = snprintf(buf, buf_len, "$CLRST,%u", cmd->slave_id);
    if (pos < 0 || (size_t)pos >= buf_len) return -1;
    return append_checksum(buf, pos, buf_len);
}

int cluster_protocol_decode_restart(const char *buf, size_t len, cluster_restart_cmd_t *cmd)
{
    if (!verify_checksum(buf, len)) {
        ESP_LOGW(TAG, "CLRST checksum mismatch");
        return -1;
    }

    char tmp[CLUSTER_MSG_MAX_LEN];
    size_t copy_len = len < sizeof(tmp) - 1 ? len : sizeof(tmp) - 1;
    memcpy(tmp, buf, copy_len);
    tmp[copy_len] = '\0';

    char *saveptr = NULL;
    char *token;

    // Skip "$CLRST"
    token = next_token(tmp, &saveptr);
    if (!token) return -1;

    token = next_token(NULL, &saveptr);
    if (!token) return -1;
    cmd->slave_id = (uint8_t)atoi(token);

    return 0;
}

int cluster_protocol_encode_timing(uint16_t interval_ms, char *buf, size_t buf_len)
{
    int pos = snprintf(buf, buf_len, "$CLTIM,%u", (unsigned)interval_ms);
    if (pos < 0 || (size_t)pos >= buf_len) return -1;
    return append_checksum(buf, pos, buf_len);
}

const char* cluster_protocol_identify(const char *buf, size_t len)
{
    if (len < 6) return NULL;

    if (strncmp(buf, "$CLWRK", 6) == 0) return CLUSTER_MSG_WORK;
    if (strncmp(buf, "$CLSHR", 6) == 0) return CLUSTER_MSG_SHARE;
    if (strncmp(buf, "$CLHBT", 6) == 0) return CLUSTER_MSG_HEARTBEAT;
    if (strncmp(buf, "$CLACK", 6) == 0) return CLUSTER_MSG_ACK;
    if (strncmp(buf, "$CLCFG", 6) == 0) return CLUSTER_MSG_CONFIG;
    if (strncmp(buf, "$CLRST", 6) == 0) return CLUSTER_MSG_RESTART;
    if (strncmp(buf, "$CLTIM", 6) == 0) return CLUSTER_MSG_TIMING;
    // Match both $CLREG (legacy) and $REGISTER (ClusterAxe)
    if (strncmp(buf, "$CLREG", 6) == 0) return CLUSTER_MSG_REGISTER;
    if (len >= 10 && strncmp(buf, "$REGISTER,", 10) == 0) return CLUSTER_MSG_REGISTER;

    return NULL;
}
