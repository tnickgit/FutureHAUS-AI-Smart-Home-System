#ifndef SENSORNODE_H_
#define SENSORNODE_H_

#include "waterSense.h"
#include "temperatureSense.h"

#define PAYLOAD_SIZE 256  // Enough for standard JSON messages

// 1. The Menu of available roles
enum SensorType {
    SENSOR_TYPE_WATER,
    SENSOR_TYPE_MOTION,
    SENSOR_TYPE_TEMP,
    SENSOR_TYPE_POWER
};

// 2. The Universal Container
struct _sensorNode{
    //node properties
    int nodeID;                 // Unique ID (derived from MAC or random)
    enum SensorType type;       // What role am I playing?

    //data properties
    float data;
    char jsonPayload[PAYLOAD_SIZE]; // The final message ready for shipping

    //handling specific sensor types
    bool constructed;
    temperatureSense ts;
    waterSense ws;
};

typedef struct _sensorNode sensorNode;

// Constructor: Sets up the ID and Type
sensorNode sensorNode_construct(enum SensorType type, int nodeID);

void sensorNode_get_data(sensorNode *sn);

// Packager: Takes raw data string (e.g., "75.4 F") and wraps it in JSON
void sensorNode_package_data(sensorNode *sn);

// Accessor: Returns the final buffer pointer (for esp_mesh_send)
char* getSensorNodeMessage(sensorNode *sn);

#endif