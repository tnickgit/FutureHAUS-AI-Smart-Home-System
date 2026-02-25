#ifndef LIGHTSENSE_H_
#define LIGHTSENSE_H_

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include "esp_system.h"

#define MAX_LUX 1000

// Define the struct with leading underscore
struct _lightSense {
    float sensedLux;          // random temperature value
    bool isOn;
};

typedef struct _lightSense lightSense;

// Constructor
lightSense lightSense_construct(void);

// Generate random temperature between 23°F and 85°F
void determineLuxLevel(lightSense *ls);

#endif // TEMPERATURESENSE_H_
