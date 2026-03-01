#ifndef MOTIONSENSE_H_
#define MOTIONSENSE_H_

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include "esp_system.h"

#define MAX_MOTION 1000

// Define the struct with leading underscore
struct _motionSense {
    float sensedMotion;          // random temperature value
    int lastMotion;
    bool isMotionSensed;
};

typedef struct _motionSense motionSense;

// Constructor
motionSense motionSense_construct(void);

// Generate random temperature between 23°F and 85°F
void determineMotion(motionSense *ls);

#endif // TEMPERATURESENSE_H_
