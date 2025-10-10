#ifndef WATERSENSE_H_
#define WATERSENSE_H_

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include "esp_system.h"

#define limit 500

// Define the struct with leading underscore
struct _waterSense {
    int sensedNumber;          // random number generated
    char determination[limit];   // message string
};

typedef struct _waterSense waterSense;

waterSense waterSense_construct(void);

void determineWaterLevel(waterSense *ws); // this is where a number is going to be randomly generated

void determineData(waterSense *ws); // this is going to put the random number generated into the string

void combineData(waterSense *ws);

#endif