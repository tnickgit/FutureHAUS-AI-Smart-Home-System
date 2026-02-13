#ifndef WSS_CLIENT_H
#define WSS_CLIENT_H

#include <stdbool.h>
#include "esp_websocket_client.h"

// Initialize and start the WSS client
void wss_start(void);

// Stop and destroy the WSS client (to save memory when not Root)
void wss_stop(void);

// Send a text message to the server
// Returns true if sent, false if client not connected
bool wss_send(const char *message);

// Check if we are currently connected to the server
bool wss_is_connected(void);

#endif