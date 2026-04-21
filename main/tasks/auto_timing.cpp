#include "auto_timing.h"

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cluster/cluster_integration.h"

static const char *TAG = "auto_timing";

// Calibration sweep intervals (ms)
static const uint16_t k_cal_intervals[AUTO_TIMING_CAL_STEPS] = {
    500, 550, 600, 650, 700, 750, 800
};

typedef enum {
    AT_STATE_CALIBRATING = 0,
    AT_STATE_MONITORING,
} at_state_t;

static struct {
    bool            active;
    at_state_t      state;
    uint16_t        current_interval_ms;

    // Share counters — written from stratum task, read from auto_timing task.
    // Single 32-bit writes on Xtensa LX7 are atomic enough for a rate tracker.
    volatile uint32_t total_accepted;
    volatile uint32_t total_rejected;

    // Rolling monitoring window
    uint32_t        window_accepted;
    uint32_t        window_rejected;
    int64_t         window_start_ms;
    int64_t         last_adjustment_ms;

    // Calibration
    uint8_t         cal_step;
    float           cal_results[AUTO_TIMING_CAL_STEPS];
    uint32_t        cal_accepted_start;
    uint32_t        cal_rejected_start;
    int64_t         cal_step_start_ms;
} s;

static TaskHandle_t s_task = NULL;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static int64_t ms_now(void)
{
    return esp_timer_get_time() / 1000LL;
}

static float rejection_rate(uint32_t accepted, uint32_t rejected)
{
    uint32_t total = accepted + rejected;
    if (total == 0) return 0.0f;
    return 100.0f * (float)rejected / (float)total;
}

static void apply_interval(uint16_t ms)
{
    if (ms < AUTO_TIMING_MIN_INTERVAL_MS) ms = AUTO_TIMING_MIN_INTERVAL_MS;
    if (ms > AUTO_TIMING_MAX_INTERVAL_MS) ms = AUTO_TIMING_MAX_INTERVAL_MS;
    if (ms == s.current_interval_ms) return;
    ESP_LOGI(TAG, "Job interval %u ms → %u ms", s.current_interval_ms, ms);
    s.current_interval_ms = ms;
    cluster_integration_broadcast_timing(ms);
}

// ---------------------------------------------------------------------------
// Calibration
// ---------------------------------------------------------------------------

static void begin_cal_step(uint8_t step)
{
    apply_interval(k_cal_intervals[step]);
    s.cal_step = step;
    s.cal_accepted_start = s.total_accepted;
    s.cal_rejected_start = s.total_rejected;
    s.cal_step_start_ms  = ms_now();
    ESP_LOGI(TAG, "Calibration %d/%d: testing %u ms interval",
             step + 1, AUTO_TIMING_CAL_STEPS, k_cal_intervals[step]);
}

static void finish_cal_step(void)
{
    uint32_t acc = s.total_accepted - s.cal_accepted_start;
    uint32_t rej = s.total_rejected - s.cal_rejected_start;
    float rate = rejection_rate(acc, rej);
    s.cal_results[s.cal_step] = rate;
    ESP_LOGI(TAG, "Calibration %d/%d: %.2f%% rejection (%lu acc / %lu rej)",
             s.cal_step + 1, AUTO_TIMING_CAL_STEPS, rate,
             (unsigned long)acc, (unsigned long)rej);
}

static void complete_calibration(void)
{
    uint8_t best = 0;
    float   best_rate = s.cal_results[0];
    for (uint8_t i = 1; i < AUTO_TIMING_CAL_STEPS; i++) {
        if (s.cal_results[i] < best_rate) {
            best_rate = s.cal_results[i];
            best      = i;
        }
    }
    ESP_LOGI(TAG, "Calibration done — optimal interval: %u ms (%.2f%% rejection)",
             k_cal_intervals[best], best_rate);
    apply_interval(k_cal_intervals[best]);

    s.state              = AT_STATE_MONITORING;
    s.window_accepted    = 0;
    s.window_rejected    = 0;
    s.window_start_ms    = ms_now();
    s.last_adjustment_ms = ms_now();
}

static void tick_calibration(void)
{
    if ((ms_now() - s.cal_step_start_ms) < AUTO_TIMING_CAL_STEP_MS) return;

    finish_cal_step();

    if (s.cal_step + 1 < AUTO_TIMING_CAL_STEPS) {
        begin_cal_step(s.cal_step + 1);
    } else {
        complete_calibration();
    }
}

// ---------------------------------------------------------------------------
// Monitoring
// ---------------------------------------------------------------------------

static void tick_monitoring(void)
{
    int64_t now = ms_now();

    // Accumulate window counters from the total counters
    // (window_* are reset at window boundary, totals keep growing)
    // We track window shares directly via notify calls, not derived here.

    if ((now - s.window_start_ms) < AUTO_TIMING_WINDOW_MS) return;

    float rate = rejection_rate(s.window_accepted, s.window_rejected);
    ESP_LOGI(TAG, "Window done: %.2f%% rejection (%lu acc / %lu rej) @ %u ms",
             rate,
             (unsigned long)s.window_accepted,
             (unsigned long)s.window_rejected,
             s.current_interval_ms);

    // Reset window
    s.window_accepted = 0;
    s.window_rejected = 0;
    s.window_start_ms = now;

    // Respect stabilisation period
    if ((now - s.last_adjustment_ms) < AUTO_TIMING_STABILIZE_MS) return;

    if (rate > AUTO_TIMING_REJECT_HIGH_PCT) {
        ESP_LOGW(TAG, "Rejection %.2f%% > %.1f%% — increasing interval",
                 rate, AUTO_TIMING_REJECT_HIGH_PCT);
        apply_interval(s.current_interval_ms + AUTO_TIMING_STEP_UP_MS);
        s.last_adjustment_ms = now;
    } else if (rate < AUTO_TIMING_REJECT_LOW_PCT &&
               s.current_interval_ms > AUTO_TIMING_MIN_INTERVAL_MS) {
        ESP_LOGI(TAG, "Rejection %.2f%% < %.1f%% — decreasing interval",
                 rate, AUTO_TIMING_REJECT_LOW_PCT);
        apply_interval(s.current_interval_ms - AUTO_TIMING_STEP_DOWN_MS);
        s.last_adjustment_ms = now;
    }
}

// ---------------------------------------------------------------------------
// Task
// ---------------------------------------------------------------------------

static void auto_timing_task(void *pv)
{
    ESP_LOGI(TAG, "Started — calibrating over %d × %d s steps",
             AUTO_TIMING_CAL_STEPS, AUTO_TIMING_CAL_STEP_MS / 1000);
    begin_cal_step(0);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        if (s.state == AT_STATE_CALIBRATING) {
            tick_calibration();
        } else {
            tick_monitoring();
        }
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void auto_timing_init(void)
{
    memset(&s, 0, sizeof(s));
    s.current_interval_ms = AUTO_TIMING_DEFAULT_INTERVAL_MS;
    s.state  = AT_STATE_CALIBRATING;
    s.active = false;
}

void auto_timing_start(void)
{
    if (s.active) return;
    s.active = true;
    xTaskCreate(auto_timing_task, "auto_timing", 3072, NULL, 5, &s_task);
    ESP_LOGI(TAG, "Auto-timing task started (initial interval %u ms)",
             s.current_interval_ms);
}

void auto_timing_notify_accepted(void)
{
    if (!s.active) return;
    s.total_accepted++;
    if (s.state == AT_STATE_MONITORING) s.window_accepted++;
}

void auto_timing_notify_rejected(void)
{
    if (!s.active) return;
    s.total_rejected++;
    if (s.state == AT_STATE_MONITORING) s.window_rejected++;
}

uint16_t auto_timing_get_interval_ms(void)
{
    return s.current_interval_ms;
}

bool auto_timing_is_active(void)
{
    return s.active;
}

const char *auto_timing_state_str(void)
{
    switch (s.state) {
        case AT_STATE_CALIBRATING: return "calibrating";
        case AT_STATE_MONITORING:  return "monitoring";
        default:                   return "unknown";
    }
}
