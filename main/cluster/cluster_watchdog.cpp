#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "nvs.h"

#include "cluster_watchdog.h"
#include "cluster.h"
#include "global_state.h"
#include "nvs_config.h"

static const char *TAG = "cluster_wdog";

#define NVS_WD_NAMESPACE  "clu_watchdog"
#define NVS_WD_KEY        "enabled"

// ─────────────────────────────────────────────────────────────────────────────
// Internal state
// ─────────────────────────────────────────────────────────────────────────────

static struct {
    bool                     initialized;
    bool                     enabled;
    bool                     running;
    SemaphoreHandle_t        mutex;
    TaskHandle_t             task_handle;
    watchdog_device_status_t master;
    watchdog_device_status_t slaves[CLUSTER_MAX_SLAVES];
    uint8_t                  throttled_count;
} s_wd = {};

// ─────────────────────────────────────────────────────────────────────────────
// Forward declarations
// ─────────────────────────────────────────────────────────────────────────────

static void watchdog_task(void *arg);
static void check_master(void);
static void check_slaves(void);
static void throttle_master(uint8_t reason);
static void recover_master(void);
static void throttle_slave(int slot, uint8_t reason);
static void recover_slave(int slot);

// ─────────────────────────────────────────────────────────────────────────────
// NVS helpers
// ─────────────────────────────────────────────────────────────────────────────

static void wd_nvs_save(bool enabled)
{
    nvs_handle_t h;
    if (nvs_open(NVS_WD_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, NVS_WD_KEY, enabled ? 1 : 0);
        nvs_commit(h);
        nvs_close(h);
    }
}

static bool wd_nvs_load(void)
{
    nvs_handle_t h;
    uint8_t val = 0;
    if (nvs_open(NVS_WD_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, NVS_WD_KEY, &val);
        nvs_close(h);
    }
    return val != 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

esp_err_t cluster_watchdog_init(void)
{
    if (s_wd.initialized) return ESP_OK;

    s_wd.mutex = xSemaphoreCreateMutex();
    if (!s_wd.mutex) return ESP_ERR_NO_MEM;

    memset(&s_wd.master, 0, sizeof(s_wd.master));
    memset(s_wd.slaves, 0, sizeof(s_wd.slaves));

    s_wd.enabled = wd_nvs_load();
    s_wd.initialized = true;

    ESP_LOGI(TAG, "Watchdog initialised (enabled=%d)", s_wd.enabled);

    if (s_wd.enabled) {
        cluster_watchdog_enable(true);
    }
    return ESP_OK;
}

esp_err_t cluster_watchdog_enable(bool enable)
{
    if (!s_wd.initialized) return ESP_ERR_INVALID_STATE;

    xSemaphoreTake(s_wd.mutex, portMAX_DELAY);

    if (enable && !s_wd.running) {
        BaseType_t r = xTaskCreate(watchdog_task, "cluster_wdog", 4096, NULL, 5, &s_wd.task_handle);
        if (r != pdPASS) {
            xSemaphoreGive(s_wd.mutex);
            return ESP_ERR_NO_MEM;
        }
        s_wd.running = true;
        ESP_LOGI(TAG, "Watchdog task started");
    } else if (!enable && s_wd.running) {
        if (s_wd.task_handle) {
            vTaskDelete(s_wd.task_handle);
            s_wd.task_handle = NULL;
        }
        s_wd.running = false;
        ESP_LOGI(TAG, "Watchdog task stopped");
    }

    s_wd.enabled = enable;
    xSemaphoreGive(s_wd.mutex);

    wd_nvs_save(enable);
    return ESP_OK;
}

bool cluster_watchdog_is_enabled(void)  { return s_wd.enabled; }
bool cluster_watchdog_is_running(void)  { return s_wd.running; }

void cluster_watchdog_get_status(watchdog_status_t *out)
{
    if (!out) return;
    if (!s_wd.initialized || !s_wd.mutex) {
        memset(out, 0, sizeof(*out));
        return;
    }
    xSemaphoreTake(s_wd.mutex, portMAX_DELAY);
    out->enabled         = s_wd.enabled;
    out->running         = s_wd.running;
    out->master          = s_wd.master;
    out->throttled_count = s_wd.throttled_count;
    memcpy(out->slaves, s_wd.slaves, sizeof(out->slaves));
    xSemaphoreGive(s_wd.mutex);
}

uint8_t cluster_watchdog_get_throttled_count(void) { return s_wd.throttled_count; }

// ─────────────────────────────────────────────────────────────────────────────
// Watchdog task
// ─────────────────────────────────────────────────────────────────────────────

static void watchdog_task(void *arg)
{
    ESP_LOGI(TAG, "Running — temp>=%.0f°C or Vin<=%.1fV triggers throttle",
             WATCHDOG_TEMP_THRESHOLD, WATCHDOG_VIN_THRESHOLD);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(WATCHDOG_CHECK_INTERVAL_MS));
        if (!s_wd.enabled) continue;

        xSemaphoreTake(s_wd.mutex, portMAX_DELAY);

        check_master();

        if (cluster_get_mode() == CLUSTER_MODE_MASTER) {
            check_slaves();
        }

        // recount throttled devices
        s_wd.throttled_count = 0;
        if (s_wd.master.is_throttled) s_wd.throttled_count++;
        for (int i = 0; i < CLUSTER_MAX_SLAVES; i++) {
            if (s_wd.slaves[i].is_throttled) s_wd.throttled_count++;
        }

        xSemaphoreGive(s_wd.mutex);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Master checks
// ─────────────────────────────────────────────────────────────────────────────

static void check_master(void)
{
    Board  *board = SYSTEM_MODULE.getBoard();
    float   temp  = POWER_MANAGEMENT_MODULE.getChipTempMax();
    float   vin   = POWER_MANAGEMENT_MODULE.getVoltage() / 1000.0f;
    uint16_t freq = (uint16_t)board->getAsicFrequency();
    uint16_t volt = (uint16_t)board->getAsicVoltageMillis();

    s_wd.master.last_temp = temp;
    s_wd.master.last_vin  = vin;
    // Bug fix: only update tracked freq/voltage from live readings when not throttled.
    // While throttled the watchdog owns these values via its step operations.
    if (!s_wd.master.is_throttled) {
        s_wd.master.current_frequency = freq;
        s_wd.master.current_voltage   = volt;
    }

    uint8_t reason = WD_THROTTLE_NONE;
    if (temp >= WATCHDOG_TEMP_THRESHOLD)             reason |= WD_THROTTLE_TEMP;
    if (vin > 0.0f && vin <= WATCHDOG_VIN_THRESHOLD) reason |= WD_THROTTLE_VIN;

    if (reason != WD_THROTTLE_NONE) {
        if (!s_wd.master.is_throttled) {
            s_wd.master.original_frequency = freq;
            s_wd.master.original_voltage   = volt;
            if (s_wd.master.was_recovered) {
                // Re-throttled after a prior recovery — step the recovery target down
                // so we don't keep restoring to an unstable operating point.
                s_wd.master.recovery_frequency = (s_wd.master.recovery_frequency > WATCHDOG_MIN_FREQUENCY + WATCHDOG_FREQ_STEP)
                    ? s_wd.master.recovery_frequency - WATCHDOG_FREQ_STEP : WATCHDOG_MIN_FREQUENCY;
                s_wd.master.recovery_voltage = (s_wd.master.recovery_voltage > WATCHDOG_MIN_VOLTAGE + WATCHDOG_VOLTAGE_STEP)
                    ? s_wd.master.recovery_voltage - WATCHDOG_VOLTAGE_STEP : WATCHDOG_MIN_VOLTAGE;
                ESP_LOGW(TAG, "MASTER: re-throttle after recovery — lowering recovery target to %u MHz / %u mV",
                         s_wd.master.recovery_frequency, s_wd.master.recovery_voltage);
                s_wd.master.was_recovered = false;
            } else {
                s_wd.master.recovery_frequency = freq;
                s_wd.master.recovery_voltage   = volt;
            }
        }
        s_wd.master.safe_since    = 0;
        s_wd.master.is_recovering = false;
        throttle_master(reason);
    } else if (s_wd.master.is_throttled) {
        s_wd.master.throttle_reason = WD_THROTTLE_NONE;
        bool temp_ok = (temp <= WATCHDOG_TEMP_RECOVERY_THRESHOLD);
        bool vin_ok  = (vin <= 0.0f || vin >= WATCHDOG_VIN_RECOVERY_THRESHOLD);

        if (temp_ok && vin_ok) {
            int64_t now = esp_timer_get_time() / 1000;
            if (s_wd.master.safe_since == 0) {
                s_wd.master.safe_since = now;
                ESP_LOGI(TAG, "MASTER: conditions safe, starting stability timer");
            }
            if ((now - s_wd.master.safe_since) >= WATCHDOG_RECOVERY_STABILITY_MS) {
                recover_master();
            }
        } else {
            s_wd.master.safe_since    = 0;
            s_wd.master.is_recovering = false;
        }
    }
}

static void throttle_master(uint8_t reason)
{
    uint16_t f = s_wd.master.current_frequency;
    uint16_t v = s_wd.master.current_voltage;

    uint16_t nf = (f > WATCHDOG_MIN_FREQUENCY) ? (uint16_t)std::max((int)WATCHDOG_MIN_FREQUENCY, (int)f - WATCHDOG_FREQ_STEP) : f;
    uint16_t nv = (v > WATCHDOG_MIN_VOLTAGE)   ? (uint16_t)std::max((int)WATCHDOG_MIN_VOLTAGE,   (int)v - WATCHDOG_VOLTAGE_STEP) : v;

    bool changed = false;
    if (nf != f) {
        ESP_LOGW(TAG, "MASTER throttle: freq %u→%u MHz (0x%02x)", f, nf, reason);
        Config::setAsicFrequency(nf);
        SYSTEM_MODULE.getBoard()->loadSettings(); // update in-memory value so PM task picks up
        s_wd.master.current_frequency = nf;
        changed = true;
    }
    if (nv != v) {
        ESP_LOGW(TAG, "MASTER throttle: volt %u→%u mV (0x%02x)", v, nv, reason);
        Config::setAsicVoltage(nv);
        SYSTEM_MODULE.getBoard()->loadSettings();
        s_wd.master.current_voltage = nv;
        changed = true;
    }
    if (changed || !s_wd.master.is_throttled) {
        s_wd.master.is_throttled       = true;
        s_wd.master.throttle_reason    = reason;
        s_wd.master.throttle_count++;
        s_wd.master.last_throttle_time = esp_timer_get_time() / 1000;
    }
}

static void recover_master(void)
{
    int64_t now = esp_timer_get_time() / 1000;
    if (s_wd.master.last_recovery_time != 0 &&
        (now - s_wd.master.last_recovery_time) < WATCHDOG_RECOVERY_INTERVAL_MS) {
        return;
    }

    uint16_t cf = s_wd.master.current_frequency;
    uint16_t cv = s_wd.master.current_voltage;
    uint16_t tf = s_wd.master.recovery_frequency ? s_wd.master.recovery_frequency : s_wd.master.original_frequency;
    uint16_t tv = s_wd.master.recovery_voltage   ? s_wd.master.recovery_voltage   : s_wd.master.original_voltage;

    if (cf >= tf && cv >= tv) {
        ESP_LOGI(TAG, "MASTER: recovery complete (freq=%u, volt=%u)", tf, tv);
        s_wd.master.is_throttled       = false;
        s_wd.master.is_recovering      = false;
        s_wd.master.was_recovered      = true;
        s_wd.master.safe_since         = 0;
        s_wd.master.last_recovery_time = 0;
        return;
    }

    if (!s_wd.master.is_recovering) {
        ESP_LOGI(TAG, "MASTER: starting recovery → freq=%u MHz, volt=%u mV", tf, tv);
        s_wd.master.is_recovering = true;
    }

    uint16_t nf = (cf < tf) ? (uint16_t)std::min((int)tf, (int)cf + WATCHDOG_FREQ_STEP) : cf;
    uint16_t nv = (cv < tv) ? (uint16_t)std::min((int)tv, (int)cv + WATCHDOG_VOLTAGE_STEP) : cv;
    bool changed = false;

    if (nf != cf) {
        ESP_LOGI(TAG, "MASTER recovery: freq %u→%u MHz", cf, nf);
        Config::setAsicFrequency(nf);
        SYSTEM_MODULE.getBoard()->loadSettings();
        s_wd.master.current_frequency = nf;
        changed = true;
    }
    if (nv != cv) {
        ESP_LOGI(TAG, "MASTER recovery: volt %u→%u mV", cv, nv);
        Config::setAsicVoltage(nv);
        SYSTEM_MODULE.getBoard()->loadSettings();
        s_wd.master.current_voltage = nv;
        changed = true;
    }
    if (changed) s_wd.master.last_recovery_time = now;
}

// ─────────────────────────────────────────────────────────────────────────────
// Slave checks (master mode only)
// ─────────────────────────────────────────────────────────────────────────────

static void check_slaves(void)
{
    const cluster_slave_info_t *info = cluster_get_slave_info();

    for (int i = 0; i < CLUSTER_MAX_SLAVES; i++) {
        if (!info[i].active) {
            s_wd.slaves[i].is_throttled  = false;
            s_wd.slaves[i].is_recovering = false;
            s_wd.slaves[i].safe_since    = 0;
            continue;
        }

        float    temp  = info[i].temp;
        float    vin   = info[i].voltage_in;
        uint16_t freq  = info[i].frequency;
        uint16_t volt  = info[i].core_voltage;

        s_wd.slaves[i].last_temp = temp;
        s_wd.slaves[i].last_vin  = vin;
        // Bug fix: only update tracked freq/voltage from live readings when not throttled.
        // While throttled the watchdog owns these values via its step operations.
        if (!s_wd.slaves[i].is_throttled) {
            s_wd.slaves[i].current_frequency = freq;
            s_wd.slaves[i].current_voltage   = volt;
        }

        uint8_t reason = WD_THROTTLE_NONE;
        if (temp >= WATCHDOG_TEMP_THRESHOLD)             reason |= WD_THROTTLE_TEMP;
        if (vin > 0.0f && vin <= WATCHDOG_VIN_THRESHOLD) reason |= WD_THROTTLE_VIN;

        if (reason != WD_THROTTLE_NONE) {
            if (!s_wd.slaves[i].is_throttled) {
                s_wd.slaves[i].original_frequency = freq;
                s_wd.slaves[i].original_voltage   = volt;
                if (s_wd.slaves[i].was_recovered) {
                    // Re-throttled after a prior recovery — step recovery target down
                    // so we don't keep restoring to an unstable operating point.
                    s_wd.slaves[i].recovery_frequency = (s_wd.slaves[i].recovery_frequency > WATCHDOG_MIN_FREQUENCY + WATCHDOG_FREQ_STEP)
                        ? s_wd.slaves[i].recovery_frequency - WATCHDOG_FREQ_STEP : WATCHDOG_MIN_FREQUENCY;
                    s_wd.slaves[i].recovery_voltage = (s_wd.slaves[i].recovery_voltage > WATCHDOG_MIN_VOLTAGE + WATCHDOG_VOLTAGE_STEP)
                        ? s_wd.slaves[i].recovery_voltage - WATCHDOG_VOLTAGE_STEP : WATCHDOG_MIN_VOLTAGE;
                    ESP_LOGW(TAG, "SLAVE[%d]: re-throttle after recovery — lowering recovery target to %u MHz / %u mV",
                             i, s_wd.slaves[i].recovery_frequency, s_wd.slaves[i].recovery_voltage);
                    s_wd.slaves[i].was_recovered = false;
                } else {
                    s_wd.slaves[i].recovery_frequency = freq;
                    s_wd.slaves[i].recovery_voltage   = volt;
                }
            }
            s_wd.slaves[i].safe_since    = 0;
            s_wd.slaves[i].is_recovering = false;
            throttle_slave(i, reason);
        } else if (s_wd.slaves[i].is_throttled) {
            s_wd.slaves[i].throttle_reason = WD_THROTTLE_NONE;
            bool temp_ok = (temp <= WATCHDOG_TEMP_RECOVERY_THRESHOLD);
            bool vin_ok  = (vin <= 0.0f || vin >= WATCHDOG_VIN_RECOVERY_THRESHOLD);

            if (temp_ok && vin_ok) {
                int64_t now = esp_timer_get_time() / 1000;
                if (s_wd.slaves[i].safe_since == 0) {
                    s_wd.slaves[i].safe_since = now;
                    ESP_LOGI(TAG, "SLAVE[%d]: conditions safe, starting stability timer", i);
                }
                if ((now - s_wd.slaves[i].safe_since) >= WATCHDOG_RECOVERY_STABILITY_MS) {
                    recover_slave(i);
                }
            } else {
                s_wd.slaves[i].safe_since    = 0;
                s_wd.slaves[i].is_recovering = false;
            }
        }
    }
}

static void throttle_slave(int slot, uint8_t reason)
{
    const cluster_slave_info_t *info = cluster_get_slave_info();
    if (!info[slot].active) return;

    uint16_t f = s_wd.slaves[slot].current_frequency;
    uint16_t v = s_wd.slaves[slot].current_voltage;

    uint16_t nf = (f > WATCHDOG_MIN_FREQUENCY) ? (uint16_t)std::max((int)WATCHDOG_MIN_FREQUENCY, (int)f - WATCHDOG_FREQ_STEP) : f;
    uint16_t nv = (v > WATCHDOG_MIN_VOLTAGE)   ? (uint16_t)std::max((int)WATCHDOG_MIN_VOLTAGE,   (int)v - WATCHDOG_VOLTAGE_STEP) : v;

    bool changed = (nf != f || nv != v);

    if (changed) {
        ESP_LOGW(TAG, "SLAVE[%d] (%s) throttle: freq %u→%u MHz, volt %u→%u mV (0x%02x)",
                 slot, info[slot].hostname, f, nf, v, nv, reason);

        cluster_config_cmd_t cfg = {};
        cfg.slave_id     = info[slot].slave_id;
        cfg.frequency    = nf;
        cfg.core_voltage = nv;
        cfg.fan_speed    = 0xFF;  // no change
        cfg.fan_mode     = 0xFF;  // no change

        if (cluster_send_slave_config(info[slot].slave_id, &cfg)) {
            s_wd.slaves[slot].current_frequency = nf;
            s_wd.slaves[slot].current_voltage   = nv;
        } else {
            ESP_LOGE(TAG, "SLAVE[%d]: failed to send throttle config", slot);
        }
    }

    if (changed || !s_wd.slaves[slot].is_throttled) {
        s_wd.slaves[slot].is_throttled       = true;
        s_wd.slaves[slot].throttle_reason    = reason;
        s_wd.slaves[slot].throttle_count++;
        s_wd.slaves[slot].last_throttle_time = esp_timer_get_time() / 1000;
    }
}

static void recover_slave(int slot)
{
    int64_t now = esp_timer_get_time() / 1000;
    if (s_wd.slaves[slot].last_recovery_time != 0 &&
        (now - s_wd.slaves[slot].last_recovery_time) < WATCHDOG_RECOVERY_INTERVAL_MS) {
        return;
    }

    const cluster_slave_info_t *info = cluster_get_slave_info();
    if (!info[slot].active) return;

    uint16_t cf = s_wd.slaves[slot].current_frequency;
    uint16_t cv = s_wd.slaves[slot].current_voltage;
    uint16_t tf = s_wd.slaves[slot].recovery_frequency ? s_wd.slaves[slot].recovery_frequency : s_wd.slaves[slot].original_frequency;
    uint16_t tv = s_wd.slaves[slot].recovery_voltage   ? s_wd.slaves[slot].recovery_voltage   : s_wd.slaves[slot].original_voltage;

    if (cf >= tf && cv >= tv) {
        ESP_LOGI(TAG, "SLAVE[%d]: recovery complete (freq=%u, volt=%u)", slot, tf, tv);
        s_wd.slaves[slot].is_throttled       = false;
        s_wd.slaves[slot].is_recovering      = false;
        s_wd.slaves[slot].was_recovered      = true;
        s_wd.slaves[slot].safe_since         = 0;
        s_wd.slaves[slot].last_recovery_time = 0;
        return;
    }

    if (!s_wd.slaves[slot].is_recovering) {
        ESP_LOGI(TAG, "SLAVE[%d]: starting recovery → freq=%u MHz, volt=%u mV", slot, tf, tv);
        s_wd.slaves[slot].is_recovering = true;
    }

    uint16_t nf = (cf < tf) ? (uint16_t)std::min((int)tf, (int)cf + WATCHDOG_FREQ_STEP) : cf;
    uint16_t nv = (cv < tv) ? (uint16_t)std::min((int)tv, (int)cv + WATCHDOG_VOLTAGE_STEP) : cv;

    if (nf != cf || nv != cv) {
        cluster_config_cmd_t cfg = {};
        cfg.slave_id     = info[slot].slave_id;
        cfg.frequency    = nf;
        cfg.core_voltage = nv;
        cfg.fan_speed    = 0xFF;
        cfg.fan_mode     = 0xFF;

        if (cluster_send_slave_config(info[slot].slave_id, &cfg)) {
            if (nf != cf) s_wd.slaves[slot].current_frequency = nf;
            if (nv != cv) s_wd.slaves[slot].current_voltage   = nv;
            s_wd.slaves[slot].last_recovery_time = now;
            ESP_LOGI(TAG, "SLAVE[%d] recovery: freq %u→%u, volt %u→%u", slot, cf, nf, cv, nv);
        }
    }
}
