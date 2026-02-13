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
            default:
                //ESP_LOGW("DEFAULT_NO_TYPE");
                break;
        }
    }
    else{
        switch(sn->type) {
            case SENSOR_TYPE_WATER:
                determineWaterLevel(&sn->ws);
                sn->data = sn->ws.totalNumber;
                break;
            case SENSOR_TYPE_TEMP:
                determineTemperatureLevel(&sn->ts);
                sn->data = sn->ts.sensedTemperature;
                break;
            default:
                //ESP_LOGW("DEFAULT_NO_TYPE");
                break;
        }
    }
}

// The "Wrapper" logic
void sensorNode_package_data(sensorNode *sn) {
    // We create a standard JSON format:
    // { "id": 123, "type": 1, "data": "Your raw string here" }
    
    // NOTE: We use snprintf to prevent buffer overflows (crashing)
    snprintf(sn->jsonPayload, PAYLOAD_SIZE, 
             "{\"id\": \"%s\", \"type\": %d, \"data\": \"%f\", \"isRoot\": %s}", 
             sn->nodeID, 
             sn->type, 
             sn->data,
             sn->isRoot ? "true" : "false");
}

