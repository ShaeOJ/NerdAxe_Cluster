#pragma once

#include "esp_http_server.h"

esp_err_t GET_swarm(httpd_req_t *req);
esp_err_t PATCH_update_swarm(httpd_req_t *req);
esp_err_t PATCH_swarm_slave(httpd_req_t *req);
esp_err_t POST_swarm_slave_restart(httpd_req_t *req);
esp_err_t GET_swarm_watchdog(httpd_req_t *req);
esp_err_t POST_swarm_watchdog(httpd_req_t *req);