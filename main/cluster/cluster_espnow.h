#pragma once

#include <stdint.h>
#include <stddef.h>

// Initialize ESP-NOW transport on the given WiFi channel
void cluster_espnow_init(uint8_t wifi_channel);

// Deinitialize ESP-NOW transport
void cluster_espnow_deinit(void);

// Broadcast discovery beacon ("CLAXE") — called by master periodically
void cluster_espnow_broadcast_beacon(void);

// Send data to a specific peer MAC address
int cluster_espnow_send(const uint8_t *peer_mac, const uint8_t *data, size_t len);

// Broadcast data to all peers (NULL MAC = broadcast)
int cluster_espnow_broadcast(const uint8_t *data, size_t len);

// Add a peer by MAC address
void cluster_espnow_add_peer(const uint8_t *mac);

// Remove a peer by MAC address
void cluster_espnow_remove_peer(const uint8_t *mac);

// Get the device's own MAC address (STA interface)
void cluster_espnow_get_mac(uint8_t *mac_out);
