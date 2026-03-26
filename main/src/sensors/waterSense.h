#ifndef WATERSENSE_H_
#define WATERSENSE_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "esp_system.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

// --- Pin Definition ---
#define FLOW_SENSOR_PIN     GPIO_NUM_4

// Sensor calibration constants (Adafruit flow sensor formula: Hz = 5.0 * Q L/min)
#define FLOW_CALIBRATION    5.0f        // pulses per litre per minute
#define ML_PER_PULSE        0.00225f    // litres per pulse (2.25 mL)

// Fixture names — randomly assigned at construct time, same as before
static const char * const FIXTURE_NAMES[] = { "shower", "toilet", "sink", "washer" };
#define FIXTURE_COUNT 4

struct _waterSense {
    float   flowRate;       // current flow rate in L/min (replaces sensedNumber)
    float   totalLiters;    // cumulative litres since boot (replaces totalNumber)
    float   sensedNumber;   // alias kept so sensorNode_package_data compiles unchanged
    float   totalNumber;    // alias kept so sensorNode_package_data compiles unchanged
    const char *fixture;    // randomly assigned fixture label
    bool    initialised;    // true after waterSense_init() has been called
};

typedef struct _waterSense waterSense;

// Constructor — zero-initialises and assigns a random fixture label
waterSense waterSense_construct(void);

// Call once after construct — configures GPIO and attaches the ISR
void waterSense_init(waterSense *ws);

// Call from a dedicated 1-second task (or from sensorNode_get_data for polling mode)
// Snapshots the pulse count, computes flow rate and cumulative total
void determineWaterLevel(waterSense *ws);

#endif // WATERSENSE_H_