// main.c — Minimal ESP-WIFI-MESH (router-backed) for ESP32 / IDF 5/6 friendly
// - Root auto-joins your AP/hotspot (use iPhone "Maximize Compatibility" for 2.4 GHz)
// - Non-root nodes send "hi from <MAC>" to root every 2s
// - Avoids fields/enums missing in some IDF variants (no old_layer, no ROUTER_* events)

#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_mesh.h"
#include "esp_mac.h"   // MACSTR, MAC2STR, esp_read_mac
#include "src/sensors/sensorNode.h"
#include "src/WebSocket/ws_client.h"

/************* EDIT THESE FOR YOUR NETWORK *************/
#define ROUTER_SSID  "FutureHAUS"   // iPhone hotspot name = iPhone device "Name"
#define ROUTER_PASS  "teams26-06"
#define MESH_AP_PASS "mesh-pass"       // 8..63 chars; used by parents for child joins
#define BUFFER_SIZE   1024
#define RX_SIZE       1024
#define MAX_NODES     20
/******************************************************/

/************* EDIT THIS FOR SENSOR TYPE  *************/
#define NODE_TYPE   SENSOR_TYPE_TEMP   //change to whatever needed
/******************************************************/  

//node data
char macStr[18]; //for device ID
sensorNode sensor_node; // global sensor node

bool conn = false;

static uint8_t rx_buf[RX_SIZE]; // rx buffer used to transmit data


//mesh tag
static const char *TAG = "ESP_MESH";

//arbitrary, just need one to function as a mesh, each node must have this okay
static const uint8_t MESH_ID[6] = {0x11,0x22,0x33,0x44,0x55,0x66};

//pretty self explanatory
static bool s_is_root = false;
static bool s_mesh_started = false;
static bool s_has_parent = false;

//completely forgot what this is used for, no clue tho
//static int  s_layer = -1;  // track ourselves since some IDF structs don't give old_layer


//used for node table at root ( will be changed to WSS eventually )
typedef struct {
    char id[MAC_STR_SIZE];
    mesh_addr_t mac_addr; // Store binary MAC for routing
    bool active;
} NodeEntry;

//node table globals
static NodeEntry node_table[MAX_NODES];
static int node_count = 0;

// rx running flag
static bool s_rx_task_running = false;

//prototype for helper function
void process_root_rx(mesh_addr_t *from, uint8_t *payload);


//returns esp error or ESP_OK if the function is working
static esp_err_t TRY(const char *what, esp_err_t err) {
    if (err != ESP_OK) ESP_LOGE(TAG, "%s failed: %s (0x%x)", what, esp_err_to_name(err), err);
    else               ESP_LOGI(TAG, "%s ok", what);
    return err;
}

// for ROOT to have a node table, will be on WSS eventually
void update_node_table(char* id_str, mesh_addr_t *raw_mac) {
    // 1. Check if exists
    for (int i = 0; i < node_count; i++) {
        if (strncmp(node_table[i].id, id_str, MAC_STR_SIZE) == 0) {
            // Already known, update timestamp if you want
            return; 
        }
    }
    
    if (node_count < MAX_NODES) {
        strncpy(node_table[node_count].id, id_str, MAC_STR_SIZE);
        node_table[node_count].mac_addr = *raw_mac;
        node_table[node_count].active = true;
        node_count++;
        ESP_LOGI(TAG, "New Node Discovered! ID: %s (Total: %d)", id_str, node_count);
    }
}

//mimic commands/polling from central server
static void root_polling_task(void *arg) {
    while(1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        
        if (s_is_root && node_count > 0) {
            // No more is_root_sensor logic here — mesh_tx_task handles root's own data
            ESP_LOGI(TAG, "--- Polling %d child nodes ---", node_count);
            
            for (int i = 0; i < node_count; i++) {
                char command[] = "{\"cmd\": \"POLL_DATA\"}";
                mesh_data_t data = {
                    .data = (uint8_t*)command,
                    .size = strlen(command) + 1,
                    .proto = MESH_PROTO_BIN,
                    .tos = MESH_TOS_P2P
                };
                esp_err_t err = esp_mesh_send(&node_table[i].mac_addr, &data, MESH_DATA_P2P, NULL, 0);
                if (err == ESP_OK) ESP_LOGI("ROOT_SEND", "Sent POLL to %s", node_table[i].id);
                else ESP_LOGW("ROOT_SEND", "Failed to poll %s", node_table[i].id);
            }
        }
    }
}

/************* RX/TX tasks *************/
//Root RX task — prints any packets it receives
void mesh_rx_task(void *arg) {
    esp_err_t err;
    mesh_addr_t from;
    mesh_data_t data;
    int flag = 0;
    
    data.data = rx_buf; 
    data.size = RX_SIZE;

    while (1) {
        data.size = RX_SIZE;
        // Wait for data...
        err = esp_mesh_recv(&from, &data, portMAX_DELAY, &flag, NULL, 0);

        if (err == ESP_OK && data.size > 0) {
            // Null-terminate to ensure string functions work safely
            rx_buf[data.size] = '\0'; 

            if (esp_mesh_is_root()) {
                // Root still handles forwarding to WebSocket and node table
                process_root_rx(&from, data.data);
            } 
            else {
                // Child nodes now use the JSON processor
                if (!process_json_data(&sensor_node, (char*)data.data)) {
                    ESP_LOGW("NODE_RECV", "Received unknown or malformed command");
                }
            }
        }
        // Small delay to prevent watchdog issues if the loop runs too fast
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    vTaskDelete(NULL);
}

void process_root_rx(mesh_addr_t *from, uint8_t *payload) {
    // 1. Log that we got something from the mesh
    ESP_LOGI("ROOT_BRIDGE", "Forwarding Child data to Mac: %s", payload);
    
    // 2. IMMEDIATELY send the raw JSON to the MacBook
    ws_send((char*)payload);

    // 3. We still need to update the node table so the Root knows 
    // where the children are for the MacBook's future commands.
    cJSON *root = cJSON_Parse((char *)payload);
    if (root) {
        cJSON *id_item = cJSON_GetObjectItem(root, "src_id");
        if (cJSON_IsString(id_item) && (id_item->valuestring != NULL)) {
            update_node_table(id_item->valuestring, from);
        }
        cJSON_Delete(root);
    } else {
        ESP_LOGE("ROOT_PROC", "Failed to parse Child JSON for Node Table");
    }

    
    // NOTE: All "If temp > 75" logic is removed. 
    // The Mac will now receive the data and send a command back if needed.
}

static void mesh_tx_task(void *arg) {
    TickType_t last_root_poll = 0;

    for (;;) {
        // ---- CHILD NODE PATH ----
        if (s_mesh_started && s_has_parent && !s_is_root) {
            if (sensor_node.polled) {
                sensor_node.polled = false;
                sensorNode_get_data(&sensor_node);
                sensorNode_package_data(&sensor_node);

                mesh_data_t data = {
                    .data  = (uint8_t*)sensor_node.jsonPayload,
                    .size  = (uint16_t)(strlen(sensor_node.jsonPayload) + 1),
                    .proto = MESH_PROTO_BIN,
                    .tos   = MESH_TOS_P2P
                };

                esp_err_t err = esp_mesh_send(NULL, &data, 0, NULL, 0);
                if (err != ESP_OK) {
                    ESP_LOGW("NODE_SEND", "send failed: %s (0x%x)", esp_err_to_name(err), err);
                } else {
                    ESP_LOGI("NODE_SEND", "sent: %s", sensor_node.jsonPayload);
                }
            }
        }
        // ---- ROOT NODE PATH ----
        else if (s_is_root && s_mesh_started) {
            // Self-poll every 10s using a tick timer instead of a blocking delay
            TickType_t now = xTaskGetTickCount();
            if ((now - last_root_poll) >= pdMS_TO_TICKS(10000)) {
                last_root_poll = now;
                sensor_node.polled = true;
            }

            if (sensor_node.polled) {
                sensor_node.polled = false;
                sensorNode_get_data(&sensor_node);
                sensorNode_package_data(&sensor_node);

                ESP_LOGI("ROOT_SENSOR", "Root local data: %s", sensor_node.jsonPayload);

                bool sent = ws_send(sensor_node.jsonPayload);
                if (sent) {
                    ESP_LOGI("ROOT_WS_CLIENT", "Sent: %s", sensor_node.jsonPayload);
                }
            }
        }

        // Single delay at the bottom — keeps the task from spinning
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/************* events *************/
static void mesh_event_handler(void *arg, esp_event_base_t base, int32_t id, void *event_data) {

    // --- 1. IP EVENT ---
    // NOTE: This handler is registered for BOTH MESH_EVENT and IP_EVENT.
    // The IP_EVENT branch MUST come first, before the MESH_EVENT-only guard below.
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
        ESP_LOGI("MESH_EVENT", "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));

        // Promote the STA netif so outgoing TCP traffic routes through it,
        // not through the mesh softAP interface.
        esp_netif_t *sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (sta_netif) {
            esp_netif_set_default_netif(sta_netif);
            ESP_LOGI("ROUTING_FIX", "STA promoted as default netif — TCP will now exit via WiFi.");
        } else {
            ESP_LOGE("ROUTING_FIX", "Could not find WIFI_STA_DEF netif!");
        }

        // Small delay to let the IP stack settle before opening a TCP socket
        vTaskDelay(pdMS_TO_TICKS(500));

        ws_start();
        return; // done with IP event
    }
    // --- 2. MESH EVENTS ---
    if (base != MESH_EVENT) return;

    switch (id) {
        case MESH_EVENT_STARTED:
            s_mesh_started = true;
            ESP_LOGI(TAG, "Mesh started. Waiting for role assignment...");
            break;

        case MESH_EVENT_PARENT_CONNECTED:
            s_has_parent = true;
            s_is_root = esp_mesh_is_root();
            sensor_node.isRoot = s_is_root;
            
            if (s_is_root) {
                ESP_LOGI(TAG, "Root connected to AP. Manually starting DHCP client...");
                
                // This ensures the network stack actually starts looking for an IP
                esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
                if (netif) {
                    esp_netif_dhcpc_stop(netif);
                    esp_netif_dhcpc_start(netif);
                }
                } 
            else {
                ESP_LOGI(TAG, "I am a Child. Connected to Mesh parent.");
            }

            // Start the RX task on every node so they can hear Root commands
            if (!s_rx_task_running) {
                if (xTaskCreate(mesh_rx_task, "mesh_rx", 4096, NULL, 5, NULL) == pdPASS) {
                    s_rx_task_running = true;
                }
            }
            break;

        case MESH_EVENT_PARENT_DISCONNECTED:
            s_has_parent = false;
            s_is_root = false;
            // If the Root loses the AP, or a Child loses the Root, shut down WS
            ws_stop(); 
            ESP_LOGW(TAG, "Parent disconnected.");
            break;

        case MESH_EVENT_CHILD_CONNECTED:
            ESP_LOGI(TAG, "child connected (total=%d)", esp_mesh_get_total_node_num());
            break;

        case MESH_EVENT_CHILD_DISCONNECTED:
            ESP_LOGI(TAG, "child disconnected (total=%d)", esp_mesh_get_total_node_num());
            break;

        case MESH_EVENT_STOPPED:
            s_mesh_started = false;
            ws_stop();
            s_rx_task_running = false;
            break;

        case MESH_EVENT_VOTE_STARTED:
            ESP_LOGI(TAG, "Mesh election in progress...");
            break;

        default:
            break;
    }
}

/************* init *************/
static void wifi_mesh_init(void) {
    TRY("esp_netif_init", esp_netif_init());
    TRY("esp_event_loop_create_default", esp_event_loop_create_default());

    esp_netif_create_default_wifi_mesh_netifs(NULL, NULL); 

    wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
    TRY("esp_wifi_init", esp_wifi_init(&wcfg));
    TRY("esp_wifi_set_storage(RAM)", esp_wifi_set_storage(WIFI_STORAGE_RAM));
    TRY("esp_wifi_start", esp_wifi_start());  // start Wi-Fi before mesh

    TRY("esp_mesh_init", esp_mesh_init());
    TRY("register MESH_EVENT handler",
        esp_event_handler_register(MESH_EVENT, ESP_EVENT_ANY_ID, &mesh_event_handler, NULL));

    // Auto root election + networking (join AP & build mesh)
    TRY("esp_mesh_set_self_organized(true,true)", esp_mesh_set_self_organized(true, true));

    mesh_cfg_t mcfg = MESH_INIT_CONFIG_DEFAULT();
    memcpy(mcfg.mesh_id.addr, MESH_ID, 6);

    // Backhaul (router/AP)
    mcfg.router.ssid_len = strlen(ROUTER_SSID);
    memcpy(mcfg.router.ssid, ROUTER_SSID, mcfg.router.ssid_len);
    memcpy(mcfg.router.password, ROUTER_PASS, strlen(ROUTER_PASS));

    // Follow router channel
    mcfg.channel = 0;
    mcfg.allow_channel_switch = true;

    // AP params for child joins
    mcfg.mesh_ap.max_connection = 6;
    mcfg.mesh_ap.nonmesh_max_connection = 0;
    strcpy((char*)mcfg.mesh_ap.password, MESH_AP_PASS);

    TRY("esp_mesh_set_topology(TREE)", esp_mesh_set_topology(MESH_TOPO_TREE));
    TRY("esp_mesh_set_max_layer(6)", esp_mesh_set_max_layer(6));
    TRY("esp_mesh_set_vote_percentage(0.9)", esp_mesh_set_vote_percentage(0.9f));

    if (TRY("esp_mesh_set_config", esp_mesh_set_config(&mcfg)) != ESP_OK) return;

    TRY("esp_wifi_set_ps(NONE)", esp_wifi_set_ps(WIFI_PS_NONE));

    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &mesh_event_handler, NULL, NULL);


    TRY("esp_mesh_start", esp_mesh_start());
    ESP_LOGI(TAG, "mesh starting... using router SSID=\"%.*s\"", mcfg.router.ssid_len, mcfg.router.ssid);

    // Get the Raw MAC and store it in macStr (for ID uses)
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(macStr, sizeof(macStr), MACSTR, MAC2STR(mac));
}

void app_main(void) {
    esp_err_t nvs = nvs_flash_init();
    if (nvs != ESP_OK) {
        ESP_LOGW(TAG, "nvs_flash_init failed: %s, erasing NVS…", esp_err_to_name(nvs));
        nvs_flash_erase();
        nvs_flash_init();
    }

    esp_log_level_set("esp-tls", ESP_LOG_DEBUG);
    esp_log_level_set("transport_base", ESP_LOG_DEBUG);
    esp_log_level_set("TRANS_TCP", ESP_LOG_DEBUG);

    wifi_mesh_init();
    sensor_node = sensorNode_construct(NODE_TYPE, macStr, esp_mesh_is_root());

    xTaskCreate(mesh_tx_task, "mesh_tx", 4096, NULL, 5, NULL);
    xTaskCreate(root_polling_task, "root_poll", 4096, NULL, 3, NULL);
}
