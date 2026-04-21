#pragma once

#include <stdint.h>
#include <stdbool.h>

// Auto-adjust ASIC job interval based on share rejection rate.
//
// Two phases:
//   1. CALIBRATION — sweeps 7 intervals (500–800 ms, 50 ms steps), 90 s each,
//      records rejection rate at each, then locks onto the best.
//   2. MONITORING  — 5-minute rolling windows; nudges interval ±step if
//      rejection drifts outside the target band.
//
// Only runs in master / standalone mode (not on cluster slaves, which have no
// direct pool connection and therefore no meaningful rejection signal).

// --- Tunables (override via -D in build if needed) ---
#ifndef AUTO_TIMING_MIN_INTERVAL_MS
#define AUTO_TIMING_MIN_INTERVAL_MS     500
#endif
#ifndef AUTO_TIMING_MAX_INTERVAL_MS
#define AUTO_TIMING_MAX_INTERVAL_MS     800
#endif
#ifndef AUTO_TIMING_DEFAULT_INTERVAL_MS
#define AUTO_TIMING_DEFAULT_INTERVAL_MS 500   // calibration starts here
#endif
#ifndef AUTO_TIMING_CAL_STEP_MS
#define AUTO_TIMING_CAL_STEP_MS         90000 // 90 s per calibration interval
#endif
#ifndef AUTO_TIMING_WINDOW_MS
#define AUTO_TIMING_WINDOW_MS           300000 // 5-minute monitoring window
#endif
#ifndef AUTO_TIMING_STABILIZE_MS
#define AUTO_TIMING_STABILIZE_MS        120000 // min 2 min between adjustments
#endif
#ifndef AUTO_TIMING_REJECT_HIGH_PCT
#define AUTO_TIMING_REJECT_HIGH_PCT     5.0f  // increase interval above this
#endif
#ifndef AUTO_TIMING_REJECT_LOW_PCT
#define AUTO_TIMING_REJECT_LOW_PCT      1.0f  // decrease interval below this
#endif
#ifndef AUTO_TIMING_STEP_UP_MS
#define AUTO_TIMING_STEP_UP_MS          50
#endif
#ifndef AUTO_TIMING_STEP_DOWN_MS
#define AUTO_TIMING_STEP_DOWN_MS        25
#endif

// Number of calibration intervals tested (500, 550, 600, 650, 700, 750, 800)
#define AUTO_TIMING_CAL_STEPS           7

// --- Public API ---

// Initialise state — call once before auto_timing_start().
void auto_timing_init(void);

// Start the background task.  Safe to call only once.
void auto_timing_start(void);

// Called from the stratum result handler whenever the pool accepts/rejects a share.
void auto_timing_notify_accepted(void);
void auto_timing_notify_rejected(void);

// Returns the currently selected job interval in ms.
// create_jobs_task uses this to drive the ASIC job timer.
uint16_t auto_timing_get_interval_ms(void);

// True once auto_timing_start() has been called.
bool auto_timing_is_active(void);

// Human-readable state string for logging / UI ("calibrating" / "monitoring").
const char *auto_timing_state_str(void);
