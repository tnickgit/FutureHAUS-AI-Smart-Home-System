#include "sensorNode.h"

// Constructor
sensorNode sensorNode_construct(enum SensorType type, char* nodeID, bool isRoot) {
    sensorNode sn; // initialize from template
    sn.type = type;
    strncpy(sn.nodeID, nodeID, MAC_STR_SIZE);
    sn.isRoot = isRoot;
    sn.data = 0;
    // payload is empty at start
    snprintf(sn.jsonPayload, PAYLOAD_SIZE, "{}");
    snprintf(sn.data_string, PAYLOAD_SIZE/2, " ");
    return sn;
}

void sensorNode_get_data(sensorNode *sn) {
    if(!sn->constructed){
        sn->constructed = true;
        //switch statement used to call sensor constructor
        
        switch(sn->type) {
            case SENSOR_TYPE_WATER:
                sn->ws = waterSense_construct();
                sn->data = sn->ws.sensedNumber;
                break;
            case SENSOR_TYPE_TEMP:
                sn->ts = temperatureSense_construct();
                sn->data = sn->ts.sensedTemperature;
                break;
            case SENSOR_TYPE_LIGHT:
                lightSense_init(&sn->ls);
                determineLuxLevel(&sn->ls);
                sn->data = sn->ls.sensedLux;
                break;
            case SENSOR_TYPE_MOTION:
                sn->ms = motionSense_construct();
                sn->data = sn->ms.sensedMotion;
                break;
            case SENSOR_TYPE_POWER:
            default:
                //ESP_LOGW("DEFAULT_NO_TYPE");
                break;
        }
    }
    else{
        switch(sn->type) {
            case SENSOR_TYPE_WATER:
                determineWaterLevel(&sn->ws);
                sn->data = sn->ws.sensedNumber;
                break;
            case SENSOR_TYPE_TEMP:
                determineTemperatureLevel(&sn->ts);
                sn->data = sn->ts.sensedTemperature;
                break;
            case SENSOR_TYPE_LIGHT:
                determineLuxLevel(&sn->ls);
                sn->data = sn->ls.sensedLux;
                break;
            case SENSOR_TYPE_MOTION:
                determineMotion(&sn->ms);
                sn->data = sn->ms.isMotionSensed;
                break;
            default:
                //ESP_LOGW("DEFAULT_NO_TYPE");
                break;
        }
    }
}

// The "Wrapper" logic
void sensorNode_package_data(sensorNode *sn) {
    // esp_timer_get_time returns microseconds; divide by 1000 for ms
    int64_t uptime_ms = esp_timer_get_time() / 1000;

    switch(sn->type){
        case SENSOR_TYPE_WATER:
            snprintf(sn->data_string,PAYLOAD_SIZE/2, "\"type\":\"water\", \"fixture\":\"%s\", \"curr_usage\":%f", sn->ws.fixture, sn->data); 
            break;
        case SENSOR_TYPE_TEMP:
            snprintf(sn->data_string,PAYLOAD_SIZE/2, "\"type\":\"temp_hum\", \"temp_f\":%f,\"humidity_percent\":%f", sn->data, sn->ts.sensedHumidity); 
            break;
        case SENSOR_TYPE_LIGHT:
            snprintf(sn->data_string,PAYLOAD_SIZE/2, "\"type\":\"lux\", \"lux\":%f, \"isOn\":%s", ((3.3 - sn->data) / 3.3) * 100, sn->ls.isOn ? "true" : "false"); 
            break;
        case SENSOR_TYPE_MOTION:
            snprintf(sn->data_string,PAYLOAD_SIZE/2, "\"type\":\"motion\", \"is_motion\":\"%s\"", sn->ms.isMotionSensed ? "true" : "false"); 
            break;
        default:
            break;
    }


    snprintf(sn->jsonPayload, PAYLOAD_SIZE, 
             "{\"src_id\": \"%s\", \"mode\":\"SEND_DATA\",\"node_type\": \"SENSOR\", \"sensor_type\": %d, \"data\": {%s}, \"isRoot\": %s, \"timestamp\": %lld}", 
             sn->nodeID, 
             sn->type, 
             sn->data_string,
             sn->isRoot ? "true" : "false",
             uptime_ms);
}
//	{"src_id":"__MAC_ADDR__", "node_type":"__NODE_TYPE__", "sensor_type": "__SENSOR TYPE__", "mode":"__MANUAL OR AUTO__, "data":"__data__","isRoot":"__BOOL__", "timestamp":"__TIMESTAMP__"}


bool process_json_data(sensorNode *sn, char* jsonData) {
    if (jsonData == NULL) return false;

    cJSON *root = cJSON_Parse(jsonData);
    if (root == NULL) {
        ESP_LOGE("SENSOR_NODE", "Failed to parse command JSON");
        return false;
    }

    bool handled = false;
    cJSON *cmd_item = cJSON_GetObjectItem(root, "cmd");

    if (cJSON_IsString(cmd_item) && (cmd_item->valuestring != NULL)) {
        const char *command = cmd_item->valuestring;

        // 1. Handle Polling Request
        if (strcmp(command, "POLL_DATA") == 0) {
            sn->polled = true;
            handled = true;
        } 
        // 2. Handle Actuator Commands (Example: Fan)
        else if (strcmp(command, "SET_TEMP") == 0) {
            cJSON *temp_item = cJSON_GetObjectItem(root, "value");
            
            // Check if "value" exists and is a literal number (not a string)
            if (cJSON_IsNumber(temp_item)) {
                int target_temp = temp_item->valueint; // Extract as integer
                // Perform your hardware logic here
                // e.g., update_hvac_threshold(target_temp);
                ESP_LOGI("SENSOR_NODE", "Command received: Setting temp to %d", target_temp);
                handled = true;
            } else {
                ESP_LOGW("SENSOR_NODE", "SET_TEMP failed: 'value' is missing or not a number");
            }
        }
        else if (strcmp(command, "FAN_OFF") == 0) {
            ESP_LOGI("SENSOR_NODE", "Command received: Deactivating Fan");
            handled = true;
        }
    }

    cJSON_Delete(root);
    return handled;
}
