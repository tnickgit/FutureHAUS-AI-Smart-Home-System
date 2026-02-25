#ifndef TEMPERATURESENSE_H_
#define TEMPERATURESENSE_H_

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include "esp_system.h"

#define TEMP_LIMIT 100

// Define the struct with leading underscore
struct _temperatureSense {
    float sensedTemperature;          // random temperature value
    float sensedHumidity;
};

typedef struct _temperatureSense temperatureSense;

// Constructor
temperatureSense temperatureSense_construct(void);

// Generate random temperature between 23°F and 85°F
void determineTemperatureLevel(temperatureSense *ts);


#endif // TEMPERATURESENSE_H_
