#ifndef SENSORNODE_H_
#define SENSORNODE_H_

#include "waterSense.h"
#include "temperatureSense.h"

#define PAYLOAD_SIZE 256  // Enough for standard JSON messages
#define MAC_STR_SIZE 18

//possible sensor types
enum SensorType {
    SENSOR_TYPE_WATER,
    SENSOR_TYPE_MOTION,
    SENSOR_TYPE_TEMP,
    SENSOR_TYPE_POWER
};

// containers truct for sensor node
struct _sensorNode{
    //node properties
    char nodeID[MAC_STR_SIZE];
    enum SensorType type;       // type of sensor
    bool isRoot;

    //data properties
    float data;
    bool polled;
    char jsonPayload[PAYLOAD_SIZE]; // The final message ready for shipping

    //handling specific sensor types
    bool constructed;
    temperatureSense ts;
    waterSense ws;
};

typedef struct _sensorNode sensorNode;

// Constructor: Sets up the ID and Type
sensorNode sensorNode_construct(enum SensorType type, char* nodeID, bool isRoot);

void sensorNode_get_data(sensorNode *sn);

// Packager: Takes raw data string (e.g., "75.4 F") and wraps it in JSON
void sensorNode_package_data(sensorNode *sn);


#endif