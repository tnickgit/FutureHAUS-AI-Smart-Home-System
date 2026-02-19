#include "ws_client.h"
#include "esp_log.h"

static const char *TAG = "WS_CLIENT";
static esp_websocket_client_handle_t client_handle = NULL;

#define WS_SERVER_URL  "ws://192.168.4.2:8765"

// --- CONFIGURATION ---
// Note: For real production WSS, you usually need to provide .cert_pem

// --- INTERNAL EVENT HANDLER ---
static void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
    switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            ESP_LOGI(TAG, "Connected to WebSocket Server");
            break;
        case WEBSOCKET_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "Disconnected from WebSocket Server");
            break;
        case WEBSOCKET_EVENT_DATA:
            // Optional: Print incoming data from server
            ESP_LOGI(TAG, "Received opcode=%d", data->op_code);
            break;
        case WEBSOCKET_EVENT_ERROR:
            ESP_LOGE(TAG, "WebSocket Error");
            break;
    }
}

// --- PUBLIC FUNCTIONS ---
void ws_start(void) {
    if (client_handle != NULL) return;

    ESP_LOGI(TAG, "Starting WebSocket Client...");

    const esp_websocket_client_config_t local_cfg = {
        .uri = "ws://10.0.0.2:8765",
        .network_timeout_ms = 30000,      // Increase to 30s
        .buffer_size = 2048,              // Double the buffer for mesh overhead
        .reconnect_timeout_ms = 10000,
        .task_prio = 15
    };

    client_handle = esp_websocket_client_init(&local_cfg);
    
    if (client_handle == NULL) {
        ESP_LOGE(TAG, "Failed to initialize WebSocket client handle!");
        return;
    }

    esp_websocket_register_events(client_handle, WEBSOCKET_EVENT_ANY, websocket_event_handler, (void *)client_handle);
    esp_websocket_client_start(client_handle);
}

void ws_stop(void) {
    if (client_handle) {
        ESP_LOGI(TAG, "Stopping WS Client...");
        esp_websocket_client_stop(client_handle);
        esp_websocket_client_destroy(client_handle);
        client_handle = NULL;
    }
}

bool ws_send(const char *message) {
    if (client_handle && esp_websocket_client_is_connected(client_handle)) {
        esp_websocket_client_send_text(client_handle, message, strlen(message), portMAX_DELAY);
        ESP_LOGI(TAG, "Sent: %s", message);
        return true;
    }
    ESP_LOGW(TAG, "Cannot send, client not connected");
    return false;
}

bool ws_is_connected(void) {
    if (!client_handle) return false;
    return esp_websocket_client_is_connected(client_handle);
}