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
#include "src/WebSocket/wss_client.h"
#include "cJSON.h"

/************* EDIT THESE FOR YOUR NETWORK *************/
#define ROUTER_SSID  "esp_test"   // iPhone hotspot name = iPhone device "Name"
#define ROUTER_PASS  "test12345"
#define MESH_AP_PASS "mesh-pass"       // 8..63 chars; used by parents for child joins
#define BUFFER_SIZE   1024
#define RX_SIZE       1024
#define MAX_NODES     20
/******************************************************/

/************* EDIT THIS FOR SENSOR TYPE  *************/
#define NODE_TYPE   SENSOR_TYPE_WATER   //change to whatever needed
/******************************************************/  

//node data
static char macStr[18]; //for device ID
static sensorNode sensor_node; // global sensor node


static uint8_t rx_buf[RX_SIZE]; // rx buffer used to transmit data


//mesh tag
static const char *TAG = "ESP_MESH";

//arbitrary, just need one to function as a mesh, each node must have this okay
static const uint8_t MESH_ID[6] = {0x11,0x22,0x33,0x44,0x55,0x66};

//pretty self explanatory
static bool s_is_root = false;
static bool s_mesh_started = false;
static bool s_has_parent = false;
static int  s_layer = -1;  // track ourselves since some IDF structs don't give old_layer


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
        vTaskDelay(pdMS_TO_TICKS(10000)); // Poll every 10 seconds
        
        if (s_is_root && node_count > 0) {
            int is_root_sensor = 0;
            if(sensor_node.type >= 0 && sensor_node.type <= 3){
                sensor_node.polled = true; // set own polled flag to true to send data
                is_root_sensor = 1;
            }
            ESP_LOGI(TAG, "--- Polling %d Nodes ---", node_count+is_root_sensor);
            
            for (int i = 0; i < node_count; i++) {
                // Construct a command packet
                char command[] = "{\"cmd\": \"POLL_DATA\"}";
                
                mesh_data_t data = {
                    .data = (uint8_t*)command,
                    .size = strlen(command) + 1,
                    .proto = MESH_PROTO_BIN,
                    .tos = MESH_TOS_P2P
                };

                // Send to specific node using stored binary MAC
                // ROOT MUST USE P2P FLAG 
                esp_err_t err = esp_mesh_send(&node_table[i].mac_addr, &data, MESH_DATA_P2P, NULL, 0);
                
                if (err == ESP_OK) {
                    ESP_LOGI("ROOT_SEND", "Sent POLL to %s", node_table[i].id);
                } else {
                    ESP_LOGW("ROOT_SEND", "Failed to poll %s", node_table[i].id);
                }
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
                // *** CALL THE HELPER FUNCTION ***
                process_root_rx(&from, data.data);
            } 
            else {
                //CHILD LOGIC, NEED TO IMPLEMENT CJSON PARSING DATA AND READING FROM JSON FILE
                // child logic
                if(strstr((char*)data.data, "POLL_DATA")) {
                    ESP_LOGI("NODE_RECV", "Received POLL_DATA command from Root");
                    sensor_node.polled = true;
                } 
                 else
                if (strstr((char*)data.data, "FAN_ON")) {
                    ESP_LOGI("NODE_RECV", "Turning FAN ON");
                } 
                else if (strstr((char*)data.data, "FAN_OFF")) {
                    ESP_LOGI("NODE_RECV", "Turning FAN OFF");
                }
            }
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
    vTaskDelete(NULL);
}

void process_root_rx(mesh_addr_t *from, uint8_t *payload) {
    ESP_LOGI("ROOT_RECV", "Processing data from Child: %s", payload);
    wss_send((char*)payload);
    cJSON *root = cJSON_Parse((char *)payload);
    if (root) {
        cJSON *data_item = cJSON_GetObjectItem(root, "data");
        
        // EXTRACT ID AND UPDATE TABLE
        cJSON *id_item = cJSON_GetObjectItem(root, "id");
        if (cJSON_IsString(id_item) && (id_item->valuestring != NULL)) {
            // Update the table with the ID string and the binary MAC from the header
            update_node_table(id_item->valuestring, from);
        }

        if (cJSON_IsString(data_item)) {
            float temp = atof(data_item->valuestring); // ascii to float
            const char *cmd;

            // Logic: Decide on Feedback
            if (temp > 75.0) {
                cmd = "{\"cmd\": \"FAN_ON\"}"; // Updated to be proper JSON if you like
                ESP_LOGW("ROOT_PROCESS", "High Temp (%.1f). Sending FAN_ON.", temp);
            } else {
                cmd = "{\"cmd\": \"FAN_OFF\"}";
                ESP_LOGI("ROOT_PROCESS", "Temp OK (%.1f). Sending FAN_OFF.", temp);
            }

            // Send Feedback
            mesh_data_t feedback_data;
            feedback_data.data = (uint8_t *)cmd;
            feedback_data.size = strlen(cmd) + 1;
            feedback_data.proto = MESH_PROTO_BIN;
            feedback_data.tos = MESH_TOS_P2P;

            esp_mesh_send(from, &feedback_data, MESH_DATA_P2P, NULL, 0);
        }
        cJSON_Delete(root);
    } else {
        ESP_LOGE("ROOT_PROC", "Failed to parse JSON");
    }
}

static void mesh_tx_task(void *arg) {
    //fix id logic later
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    for (;;) {
        if (s_mesh_started && s_has_parent && !s_is_root) {
            sensorNode_get_data(&sensor_node);
            sensorNode_package_data(&sensor_node);
            mesh_data_t data = {
                .data  = (uint8_t*)sensor_node.jsonPayload,
                .size  = (uint16_t)(strlen(sensor_node.jsonPayload) + 1),
                .proto = MESH_PROTO_BIN,
                .tos   = MESH_TOS_P2P
            };
            if(sensor_node.polled){
                sensor_node.polled = false; // reset poll flag
                esp_err_t err = esp_mesh_send(NULL, &data, 0, NULL, 0);
                if (err != ESP_OK) {
                    ESP_LOGW("NODE_SEND", "send failed: %s (0x%x)", esp_err_to_name(err), err);
                }
                else {
                    ESP_LOGI("NODE_SEND", "sent: %s", sensor_node.jsonPayload);
                }
            }
        
        }
        else if(s_is_root && s_mesh_started){
            if(sensor_node.polled){
                sensor_node.polled = false; // reset poll flag
                sensorNode_get_data(&sensor_node); // read Sensor
                sensorNode_package_data(&sensor_node); //package data

                ESP_LOGI("ROOT_SENSOR", "%s", sensor_node.jsonPayload);
            }

            //ADD LOGIC TO SEND TO SERVER HERE, POSSIBLY ALSO JUST ADD TO RX TASK WHEN RECIEVING
            //SO THAT IT JUST FORWARDS, WILL NEED TO HANDLE DATA TRANSMISSION FROM ROOT SENSOR THOUGH
            
    }
    
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

/************* events *************/
static void mesh_event_handler(void *arg, esp_event_base_t base, int32_t id, void *event_data) {
    if (base != MESH_EVENT) return;

    switch (id) {
    case MESH_EVENT_STARTED:
        s_mesh_started = true;
        s_layer = esp_mesh_get_layer();
        ESP_LOGI(TAG, "mesh started, layer=%d", s_layer);
        break;

    case MESH_EVENT_STOPPED:
        s_mesh_started = false;
        s_has_parent = false;
        s_is_root = false;
        wss_stop();
        s_rx_task_running = false; // safe to allow re-create next time
        s_layer = -1;
        ESP_LOGI(TAG, "mesh stopped");
        break;

    case MESH_EVENT_PARENT_CONNECTED:
        s_has_parent = true;
        s_is_root = esp_mesh_is_root();
        sensor_node.isRoot = s_is_root; // update sensor node's isRoot status
        if (s_is_root) {
            ESP_LOGI(TAG, "Root starting WSS...");
            wss_start();
        }
        s_layer = esp_mesh_get_layer();
        ESP_LOGI(TAG, "parent connected, layer=%d, root=%d", s_layer, s_is_root);

        //start RX task once
        if (!s_rx_task_running) {
            if (xTaskCreate(mesh_rx_task, "mesh_rx", 4096, NULL, 5, NULL) == pdPASS) {
                s_rx_task_running = true;
                ESP_LOGI(TAG, "mesh_rx task started");
            } else {
                ESP_LOGE(TAG, "failed to start mesh_rx task");
            }
        }
        break;

    case MESH_EVENT_PARENT_DISCONNECTED:
        s_has_parent = false;
        s_is_root = false;
        wss_stop();
        ESP_LOGW(TAG, "parent disconnected");
        break;

    case MESH_EVENT_LAYER_CHANGE: {
        mesh_event_layer_change_t *e = (mesh_event_layer_change_t*)event_data;
        int old = s_layer;
        s_layer = e->new_layer;                // some IDF variants only expose new_layer
        ESP_LOGI(TAG, "layer change: %d -> %d", old, s_layer);
        break;
    }

    case MESH_EVENT_CHILD_CONNECTED:
        ESP_LOGI(TAG, "child connected (total=%d)", esp_mesh_get_total_node_num());
        break;

    case MESH_EVENT_CHILD_DISCONNECTED:
        ESP_LOGI(TAG, "child disconnected (total=%d)", esp_mesh_get_total_node_num());
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
    wifi_mesh_init();
    sensor_node = sensorNode_construct(NODE_TYPE, macStr, esp_mesh_is_root());
    xTaskCreate(mesh_tx_task, "mesh_tx", 4096, NULL, 5, NULL);
    xTaskCreate(root_polling_task, "root_poll", 4096, NULL, 5, NULL);
}
