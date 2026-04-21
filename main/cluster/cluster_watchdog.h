#pragma once

/**
 * @file cluster_watchdog.h
 * @brief Cluster Safety Watchdog — Temperature and Voltage Protection
 *
 * Monitors temp and input voltage on master and all slaves.
 * Throttles frequency/voltage when thresholds are exceeded,
 * then gradually recovers when conditions normalise.
 *
 * Thresholds:
 *   Temp >= 68°C → throttle
 *   Vin <= 4.9V  → throttle
 *
 * Recovery (hysteresis):
 *   Temp <= 62°C AND Vin >= 5.1V for 30 s → step back up
 */

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// ─── Configuration ─────────────────────────────────────────────────────────
#define WATCHDOG_CHECK_INTERVAL_MS          5000
#define WATCHDOG_TEMP_THRESHOLD             68.0f
#define WATCHDOG_VIN_THRESHOLD              4.75f   // Loosened from 4.9V — tolerates ADC variance on NerdAxe/similar hardware
#define WATCHDOG_FREQ_STEP                  50      // MHz per throttle step
#define WATCHDOG_VOLTAGE_STEP               50      // mV per throttle step
#define WATCHDOG_MIN_FREQUENCY              400     // MHz
#define WATCHDOG_MIN_VOLTAGE                1100    // mV
#define WATCHDOG_TEMP_RECOVERY_THRESHOLD    62.0f
#define WATCHDOG_VIN_RECOVERY_THRESHOLD     4.9f    // Lowered from 5.1V to match new throttle threshold
#define WATCHDOG_RECOVERY_STABILITY_MS      30000   // ms safe before recovering
#define WATCHDOG_RECOVERY_INTERVAL_MS       10000   // ms between recovery steps

// ─── Types ─────────────────────────────────────────────────────────────────
typedef enum {
    WD_THROTTLE_NONE    = 0,
    WD_THROTTLE_TEMP    = (1 << 0),
    WD_THROTTLE_VIN     = (1 << 1),
} watchdog_throttle_reason_t;

typedef struct {
    bool     is_throttled;
    bool     is_recovering;
    bool     was_recovered;        // completed at least one recovery — used to detect re-throttle cycling
    uint8_t  throttle_reason;      // bitmask of watchdog_throttle_reason_t
    float    last_temp;
    float    last_vin;
    uint16_t original_frequency;   // MHz at time of first throttle
    uint16_t original_voltage;     // mV  at time of first throttle
    uint16_t recovery_frequency;   // recovery target — adapts down on re-throttle to prevent cycling
    uint16_t recovery_voltage;     // recovery target — adapts down on re-throttle to prevent cycling
    uint16_t current_frequency;    // watchdog-owned; NOT overwritten from telemetry while throttled
    uint16_t current_voltage;      // watchdog-owned; NOT overwritten from telemetry while throttled
    uint32_t throttle_count;
    int64_t  last_throttle_time;   // ms since boot
    int64_t  safe_since;           // ms since boot (0 = not yet safe)
    int64_t  last_recovery_time;
} watchdog_device_status_t;

typedef struct {
    bool                     enabled;
    bool                     running;
    watchdog_device_status_t master;
    watchdog_device_status_t slaves[8]; // CLUSTER_MAX_SLAVES
    uint8_t                  throttled_count;
} watchdog_status_t;

// ─── Public API ────────────────────────────────────────────────────────────

/**
 * @brief Initialise the watchdog module (call once at startup).
 *        Loads enabled state from NVS and starts the task if enabled.
 */
esp_err_t cluster_watchdog_init(void);

/**
 * @brief Enable or disable the watchdog at runtime.
 *        Persists the setting to NVS.
 */
esp_err_t cluster_watchdog_enable(bool enable);

bool cluster_watchdog_is_enabled(void);
bool cluster_watchdog_is_running(void);

/** @brief Copy current status snapshot (thread-safe). */
void cluster_watchdog_get_status(watchdog_status_t *out);

uint8_t cluster_watchdog_get_throttled_count(void);

#ifdef __cplusplus
}
#endif
