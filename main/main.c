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
#include "waterSense.h"
#include "temperatureSense.h"

/************* EDIT THESE FOR YOUR NETWORK *************/
#define ROUTER_SSID  "esp_test"   // iPhone hotspot name = iPhone device "Name"
#define ROUTER_PASS  "test12345"
#define MESH_AP_PASS "mesh-pass"       // 8..63 chars; used by parents for child joins
/******************************************************/

static const char *TAG = "MESH_MIN";
//arbitrary, just need one to function as a mesh, each node must have this 
static const uint8_t MESH_ID[6] = {0x11,0x22,0x33,0x44,0x55,0x66};

//pretty self explanatory
static bool s_is_root = false;
static bool s_mesh_started = false;
static bool s_has_parent = false;
static int  s_layer = -1;  // track ourselves since some IDF structs don't give old_layer

// NEW: ensure we only spawn one RX task when we become root
static bool s_rx_task_running = false;

//returns esp error or ESP_OK if the function is working
static esp_err_t TRY(const char *what, esp_err_t err) {
    if (err != ESP_OK) ESP_LOGE(TAG, "%s failed: %s (0x%x)", what, esp_err_to_name(err), err);
    else               ESP_LOGI(TAG, "%s ok", what);
    return err;
}

/************* RX/TX tasks *************/
// NEW: Root RX task — prints any packets it receives
static void mesh_rx_task(void *arg) {
    uint8_t buf[1024];
    mesh_addr_t from;
    mesh_data_t data = {
        .data  = buf,
        .size  = sizeof(buf),
        .proto = MESH_PROTO_BIN,
        .tos   = MESH_TOS_P2P
    };
    for (;;) {
        data.size = sizeof(buf);
        if (esp_mesh_recv(&from, &data, portMAX_DELAY, NULL, NULL, 0) == ESP_OK) {
            ESP_LOGI(TAG, "root got %uB from " MACSTR ": %s",
                     data.size, MAC2STR(from.addr), (char*)data.data);
        }
    }
}

static void mesh_tx_task(void *arg) {

    waterSense sensor = waterSense_construct();
    temperatureSense temp_sensor = temperatureSense_construct();

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    for (;;) {
        // water
        determineWaterLevel(&sensor);
        determineWaterData(&sensor);
        combineData(&sensor);

        // temperature
        determineTemperatureLevel(&temp_sensor);
        determineData(&temp_sensor);

        if (s_mesh_started && s_has_parent && !s_is_root) {
            char msg[500];
            snprintf(msg, sizeof(msg), "hi from Kitchen, MAC:" MACSTR, MAC2STR(mac));

            // water
            snprintf(msg, sizeof(msg), sensor.determination);

            // temperature
            snprintf(msg, sizeof(msg), temp_sensor.determination);

            mesh_data_t data = {
                .data  = (uint8_t*)msg,
                .size  = (uint16_t)(strlen(msg) + 1),
                .proto = MESH_PROTO_BIN,
                .tos   = MESH_TOS_P2P
            };
            esp_err_t err = esp_mesh_send(NULL, &data, 0, NULL, 0);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "send failed: %s (0x%x)", esp_err_to_name(err), err);
            }
            else {
                ESP_LOGI(TAG, "sent: %s", msg);
            }
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
        s_rx_task_running = false; // safe to allow re-create next time
        s_layer = -1;
        ESP_LOGI(TAG, "mesh stopped");
        break;

    case MESH_EVENT_PARENT_CONNECTED:
        s_has_parent = true;
        s_is_root = esp_mesh_is_root();
        s_layer = esp_mesh_get_layer();
        ESP_LOGI(TAG, "parent connected, layer=%d, root=%d", s_layer, s_is_root);

        // NEW: if we are root, start RX task once
        if (s_is_root && !s_rx_task_running) {
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
}

void app_main(void) {
    esp_err_t nvs = nvs_flash_init();
    if (nvs != ESP_OK) {
        ESP_LOGW(TAG, "nvs_flash_init failed: %s, erasing NVS…", esp_err_to_name(nvs));
        nvs_flash_erase();
        nvs_flash_init();
    }
    wifi_mesh_init();
    xTaskCreate(mesh_tx_task, "mesh_tx", 4096, NULL, 5, NULL);
}
