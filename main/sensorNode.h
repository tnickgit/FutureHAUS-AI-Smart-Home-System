#ifndef SENSORNODE_H_
#define SENSORNODE_H_

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include "esp_system.h"

#define BUFFERSIZE   1024
enum SensorType {
    WATER_SENSOR,
    MOTION_SENSOR,
    TEMP_SENSOR,
    POWER_SENSOR
};
// Define the struct with leading underscore
struct _sensorNode {
    int sensorReading;          // random number generated for simplicity
    char message[BUFFERSIZE];   // message string
    enum SensorType type;      // type of sensor
};


typedef struct _sensorNode sensorNode;

// constructor for sensor node
sensorNode* sensorNode_construct(void);

// essentially read sensor data, and format it in json file
char* getSensorNodeData(sensorNode *sn);

//send data to root
void sendSensorNodeData(sensorNode *sn);




#endif