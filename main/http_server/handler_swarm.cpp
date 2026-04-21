#include "esp_http_server.h"
#include "esp_http_client.h"
#include "esp_log.h"

#include "nvs_config.h"
#include "http_cors.h"
#include "http_utils.h"
#include "global_state.h"
#include "cluster/cluster.h"
#include "cluster/cluster_watchdog.h"

#include "ArduinoJson.h"

static const char* TAG = "http_swarm";

esp_err_t PATCH_update_swarm(httpd_req_t *req)
{
    // close connection when out of scope
    ConGuard g(http_server, req);

    // Set CORS headers
    if (set_cors_headers(req) != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    int total_len = req->content_len;
    int cur_len = 0;
    char *buf = ((rest_server_context_t *) (req->user_ctx))->scratch;
    int received = 0;
    if (total_len >= SCRATCH_BUFSIZE) {
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "content too long");
        return ESP_FAIL;
    }
    while (cur_len < total_len) {
        received = httpd_req_recv(req, buf + cur_len, total_len);
        if (received <= 0) {
            /* Respond with 500 Internal Server Error */
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to post control value");
            return ESP_FAIL;
        }
        cur_len += received;
    }
    buf[total_len] = '\0';

    // Parse JSON to check for cluster settings
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, buf);
    if (error == DeserializationError::Ok) {
        if (doc.containsKey("clusterMode")) {
            Config::setClusterMode((uint16_t)doc["clusterMode"].as<int>());
        }
        if (doc.containsKey("clusterChannel")) {
            Config::setClusterChannel((uint16_t)doc["clusterChannel"].as<int>());
        }
    }

    // Also store raw swarm config
    Config::setSwarmConfig(buf);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":true}");
    return ESP_OK;
}

// Helper: find slave IP by slave_id from the active slave table
// Returns pointer to ip_addr string in the slave_info array (valid until next poll), or NULL.
static const char* find_slave_ip(int slave_id)
{
    const cluster_slave_info_t *slaves = cluster_get_slave_info();
    for (int i = 0; i < CLUSTER_MAX_SLAVES; i++) {
        if (slaves[i].active && slaves[i].slave_id == (uint8_t)slave_id) {
            return slaves[i].ip_addr;
        }
    }
    return NULL;
}

// PATCH /api/swarm/slaves/:id  — forward config to slave via its own /api/system HTTP endpoint
esp_err_t PATCH_swarm_slave(httpd_req_t *req)
{
    ConGuard g(http_server, req);

    if (set_cors_headers(req) != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    if (cluster_get_mode() != CLUSTER_MODE_MASTER) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Not in master mode");
        return ESP_FAIL;
    }

    int slave_id = -1;
    if (sscanf(req->uri, "/api/swarm/slaves/%d", &slave_id) != 1 || slave_id < 1) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid slave id");
        return ESP_FAIL;
    }

    const char *slave_ip = find_slave_ip(slave_id);
    if (!slave_ip || slave_ip[0] == '\0') {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Slave not found or no IP");
        return ESP_FAIL;
    }

    int total_len = req->content_len;
    int cur_len = 0;
    char *buf = ((rest_server_context_t *)(req->user_ctx))->scratch;
    if (total_len >= SCRATCH_BUFSIZE) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "content too long");
        return ESP_FAIL;
    }
    while (cur_len < total_len) {
        int received = httpd_req_recv(req, buf + cur_len, total_len);
        if (received <= 0) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read body");
            return ESP_FAIL;
        }
        cur_len += received;
    }
    buf[total_len] = '\0';

    JsonDocument doc;
    if (deserializeJson(doc, buf) != DeserializationError::Ok) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    // Translate cluster UI field names to the slave's standard /api/system field names
    JsonDocument out;
    if (doc["frequency"].is<uint16_t>() && doc["frequency"].as<uint16_t>() > 0)
        out["frequency"]    = doc["frequency"].as<uint16_t>();
    if (doc["coreVoltage"].is<uint16_t>() && doc["coreVoltage"].as<uint16_t>() > 0)
        out["coreVoltage"]  = doc["coreVoltage"].as<uint16_t>();
    if (doc["fanSpeed"].is<uint16_t>())
        out["manualFanSpeed"] = doc["fanSpeed"].as<uint16_t>();
    if (doc["fanMode"].is<uint16_t>())
        out["autofanspeed"] = doc["fanMode"].as<uint16_t>();
    if (doc["targetTemp"].is<uint16_t>() && doc["targetTemp"].as<uint16_t>() > 0)
        out["pidTargetTemp"] = doc["targetTemp"].as<uint16_t>();

    char body[256];
    size_t body_len = serializeJson(out, body, sizeof(body));

    char url[48];
    snprintf(url, sizeof(url), "http://%s/api/system", slave_ip);

    esp_http_client_config_t http_cfg = {};
    http_cfg.url        = url;
    http_cfg.method     = HTTP_METHOD_PATCH;
    http_cfg.timeout_ms = 5000;

    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body, (int)body_len);
    esp_err_t err = esp_http_client_perform(client);
    int status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    httpd_resp_set_type(req, "application/json");
    if (err == ESP_OK && status_code == 200) {
        httpd_resp_sendstr(req, "{\"success\":true}");
    } else {
        ESP_LOGE(TAG, "Slave %d config failed: err=%d http=%d url=%s", slave_id, err, status_code, url);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to reach slave");
        return ESP_FAIL;
    }
    return ESP_OK;
}

// POST /api/swarm/slaves/:id/restart  — restart slave via its own /api/system/restart endpoint
esp_err_t POST_swarm_slave_restart(httpd_req_t *req)
{
    ConGuard g(http_server, req);

    if (set_cors_headers(req) != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    if (cluster_get_mode() != CLUSTER_MODE_MASTER) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Not in master mode");
        return ESP_FAIL;
    }

    int slave_id = -1;
    if (sscanf(req->uri, "/api/swarm/slaves/%d/restart", &slave_id) != 1 || slave_id < 1) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid slave id");
        return ESP_FAIL;
    }

    const char *slave_ip = find_slave_ip(slave_id);
    if (!slave_ip || slave_ip[0] == '\0') {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Slave not found or no IP");
        return ESP_FAIL;
    }

    char url[56];
    snprintf(url, sizeof(url), "http://%s/api/system/restart", slave_ip);

    esp_http_client_config_t http_cfg = {};
    http_cfg.url        = url;
    http_cfg.method     = HTTP_METHOD_POST;
    http_cfg.timeout_ms = 5000;

    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    esp_err_t err = esp_http_client_perform(client);
    int status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    httpd_resp_set_type(req, "application/json");
    if (err == ESP_OK && (status_code == 200 || status_code == 204)) {
        httpd_resp_sendstr(req, "{\"success\":true}");
    } else {
        ESP_LOGE(TAG, "Slave %d restart failed: err=%d http=%d url=%s", slave_id, err, status_code, url);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to reach slave");
        return ESP_FAIL;
    }
    return ESP_OK;
}


esp_err_t GET_swarm(httpd_req_t *req)
{
    // close connection when out of scope
    ConGuard g(http_server, req);

    if (is_network_allowed(req) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }

    httpd_resp_set_type(req, "application/json");

    // Set CORS headers
    if (set_cors_headers(req) != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // Build cluster-aware response
    JsonDocument doc;

    cluster_mode_t mode = cluster_get_mode();

    switch (mode) {
        case CLUSTER_MODE_DISABLED:
            doc["mode"] = "disabled";
            break;
        case CLUSTER_MODE_MASTER:
            doc["mode"] = "master";
            break;
        case CLUSTER_MODE_SLAVE:
            doc["mode"] = "slave";
            break;
    }

    doc["clusterMode"] = (int)Config::getClusterMode();
    doc["clusterChannel"] = (int)Config::getClusterChannel();

    char *hostname = NULL;

    if (mode == CLUSTER_MODE_MASTER) {
        Board *board = SYSTEM_MODULE.getBoard();
        hostname = Config::getHostname();

        int active_count = cluster_get_active_slave_count();
        doc["active_slaves"] = active_count;
        doc["total_hashrate"] = cluster_get_total_hashrate();
        doc["bestDiff"] = cluster_get_best_diff();
        doc["transport"] = "espnow";

        // Master device info
        JsonObject master = doc["master"].to<JsonObject>();
        master["hostname"] = hostname;
        master["hashrate"] = SYSTEM_MODULE.getCurrentHashrate();
        master["temp"] = POWER_MANAGEMENT_MODULE.getChipTempMax();
        master["fan_rpm"] = POWER_MANAGEMENT_MODULE.getFanRPM(0);
        master["frequency"] = board->getAsicFrequency();
        master["core_voltage"] = board->getAsicVoltageMillis();
        master["power"] = POWER_MANAGEMENT_MODULE.getPower();
        master["voltage_in"] = POWER_MANAGEMENT_MODULE.getVoltage() / 1000.0f;

        // Sum total power across master + slaves
        float total_power = POWER_MANAGEMENT_MODULE.getPower();

        uint64_t now_ms = ::now_ms();

        JsonArray slaves = doc["slaves"].to<JsonArray>();
        const cluster_slave_info_t *slave_info = cluster_get_slave_info();

        for (int i = 0; i < CLUSTER_MAX_SLAVES; i++) {
            if (slave_info[i].active) {
                JsonObject slave = slaves.add<JsonObject>();
                slave["id"] = slave_info[i].slave_id;
                slave["hostname"] = slave_info[i].hostname;
                slave["ip_addr"] = slave_info[i].ip_addr;
                // hashrate stored as GH/s * 100, convert to GH/s for JSON
                slave["hashrate"] = slave_info[i].hashrate / 100.0;
                slave["temp"] = slave_info[i].temp;
                slave["fan_rpm"] = slave_info[i].fan_rpm;
                slave["shares_accepted"] = slave_info[i].shares_accepted;
                slave["shares_rejected"] = slave_info[i].shares_rejected;
                slave["shares_submitted"] = slave_info[i].shares_submitted;
                slave["frequency"] = slave_info[i].frequency;
                slave["core_voltage"] = slave_info[i].core_voltage;
                slave["power"] = slave_info[i].power;
                slave["voltage_in"] = slave_info[i].voltage_in;

                uint64_t age_ms = now_ms - slave_info[i].last_heartbeat_ms;
                slave["last_seen"] = (uint32_t)age_ms;
                slave["state"] = (age_ms < 5000) ? "online" : "warning";

                total_power += slave_info[i].power;
            }
        }

        doc["total_power"] = total_power;
    } else if (mode == CLUSTER_MODE_SLAVE) {
        doc["registered"] = cluster_slave_is_registered();
        doc["slave_id"] = cluster_slave_get_id();
        doc["shares_submitted"] = cluster_slave_get_shares_submitted();
    }

    esp_err_t result = sendJsonResponse(req, doc);
    free(hostname);  // safe to call with NULL
    return result;
}

// GET /api/swarm/watchdog
esp_err_t GET_swarm_watchdog(httpd_req_t *req)
{
    ConGuard g(http_server, req);

    if (set_cors_headers(req) != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");

    watchdog_status_t status;
    cluster_watchdog_get_status(&status);

    JsonDocument doc;
    doc["enabled"]         = status.enabled;
    doc["running"]         = status.running;
    doc["throttled_count"] = status.throttled_count;

    auto encodeDevice = [&](JsonObject obj, const watchdog_device_status_t &d) {
        obj["is_throttled"]       = d.is_throttled;
        obj["is_recovering"]      = d.is_recovering;
        obj["throttle_reason"]    = d.throttle_reason;
        obj["last_temp"]          = d.last_temp;
        obj["last_vin"]           = d.last_vin;
        obj["current_frequency"]  = d.current_frequency;
        obj["current_voltage"]    = d.current_voltage;
        obj["original_frequency"] = d.original_frequency;
        obj["original_voltage"]   = d.original_voltage;
        obj["throttle_count"]     = d.throttle_count;
    };

    encodeDevice(doc["master"].to<JsonObject>(), status.master);

    JsonArray slaves = doc["slaves"].to<JsonArray>();
    const cluster_slave_info_t *slave_info = cluster_get_slave_info();
    for (int i = 0; i < CLUSTER_MAX_SLAVES; i++) {
        if (slave_info[i].active) {
            JsonObject s = slaves.add<JsonObject>();
            s["slot"]     = i;
            s["slave_id"] = slave_info[i].slave_id;
            s["hostname"] = slave_info[i].hostname;
            encodeDevice(s, status.slaves[i]);
        }
    }

    return sendJsonResponse(req, doc);
}

// POST /api/swarm/watchdog  body: {"enabled": true|false}
esp_err_t POST_swarm_watchdog(httpd_req_t *req)
{
    ConGuard g(http_server, req);

    if (set_cors_headers(req) != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    int total_len = req->content_len;
    int cur_len = 0;
    char *buf = ((rest_server_context_t *)(req->user_ctx))->scratch;
    if (total_len >= SCRATCH_BUFSIZE) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "content too long");
        return ESP_FAIL;
    }
    while (cur_len < total_len) {
        int received = httpd_req_recv(req, buf + cur_len, total_len);
        if (received <= 0) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read body");
            return ESP_FAIL;
        }
        cur_len += received;
    }
    buf[total_len] = '\0';

    JsonDocument doc;
    if (deserializeJson(doc, buf) != DeserializationError::Ok) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    if (!doc["enabled"].is<bool>()) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'enabled' field");
        return ESP_FAIL;
    }

    bool enable = doc["enabled"].as<bool>();
    esp_err_t err = cluster_watchdog_enable(enable);

    httpd_resp_set_type(req, "application/json");
    if (err == ESP_OK) {
        httpd_resp_sendstr(req, "{\"success\":true}");
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Watchdog error");
        return ESP_FAIL;
    }
    return ESP_OK;
}
