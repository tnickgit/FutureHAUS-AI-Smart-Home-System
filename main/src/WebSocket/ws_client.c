#include "ws_client.h"
#include "esp_log.h"
#include "cJSON.h"
#include "src/sensors/sensorNode.h"
#include "esp_mesh.h"

extern char macStr[18];        // Declared in main.c
extern sensorNode sensor_node; // Declared in main.c

static const char *TAG = "WS_CLIENT";
static esp_websocket_client_handle_t client_handle = NULL;


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
            // Guard: only process actual text frames with content
            if (data->op_code != 1) break;  // op_code 1 = text frame
            if (data->data_ptr == NULL || data->data_len == 0) break;
            
            ESP_LOGI(TAG, "Received: %.*s", data->data_len, (char *)data->data_ptr);
            if (data->data_ptr != NULL && data->data_len > 0) {
                cJSON *root = cJSON_ParseWithLength((const char *)data->data_ptr, data->data_len);
                if (root == NULL) break;

                cJSON *target_item = cJSON_GetObjectItem(root, "target");
                if (cJSON_IsString(target_item)) {
                    const char *target_mac_str = target_item->valuestring;

                    // CHECK: Is this for the ROOT?
                    if (strcmp(target_mac_str, macStr) == 0) {
                        process_json_data(&sensor_node, (char *)data->data_ptr);
                    } 
                    // CHECK: Is this for a CHILD?
                    else {
                        mesh_addr_t child_addr;
                        // Manual MAC string to binary conversion
                        int m[6];
                        if (sscanf(target_mac_str, "%x:%x:%x:%x:%x:%x", 
                            &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) == 6) {
                            
                            for(int i=0; i<6; i++) child_addr.addr[i] = (uint8_t)m[i];

                            mesh_data_t mesh_data = {0};
                            mesh_data.data = (uint8_t *)data->data_ptr;
                            mesh_data.size = data->data_len;
                            mesh_data.proto = MESH_PROTO_BIN;
                            mesh_data.tos = MESH_TOS_P2P;

                            esp_mesh_send(&child_addr, &mesh_data, MESH_DATA_P2P, NULL, 0);
                        }
                    }
                }
                cJSON_Delete(root);
            }
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
        //CHANGE URI BASED ON SERVER 
        .uri = "ws://192.168.1.42:8765",
        .network_timeout_ms = 5000,      // Increase to 30s
        .buffer_size = 2048,              // Double the buffer for mesh overhead
        .reconnect_timeout_ms = 10000
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
        esp_websocket_client_send_text(client_handle, message, strlen(message), pdMS_TO_TICKS(1000));
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