#ifndef ws_CLIENT_H
#define ws_CLIENT_H

#include <stdbool.h>
#include "esp_websocket_client.h"

// Initialize and start the WSS client
void ws_start(void);

// Stop and destroy the WSS client (to save memory when not Root)
void ws_stop(void);

// Send a text message to the server
// Returns true if sent, false if client not connected
bool ws_send(const char *message);

// Check if we are currently connected to the server
bool ws_is_connected(void);

#endif